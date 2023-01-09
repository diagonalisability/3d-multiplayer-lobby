#include<atomic> // std::compare_exchange_weak
#include<functional> // std::reference_wrapper
#include<optional> // std::optional
#include<sys/epoll.h> // epoll_create, epoll_ctl, epoll_wait, epoll_event
#include<sys/eventfd.h> // eventfd
#include<tuple>
#include<unistd.h> // write
#include"common.hpp"
#include"concurrency.hpp"

unsigned constexpr epollCreateHint= 10;
unsigned constexpr maxEventC= 64;

GenericUniquePointer::GenericUniquePointer(GenericUniquePointer &&other) noexcept:
	o{other.o},
	deleter{other.deleter}
{
	other.o= nullptr;
}
static void noopDelete(void*) {}
GenericUniquePointer::GenericUniquePointer(Tag::NotDeleted, void *const o) noexcept:
	o{o},
	deleter{*noopDelete}
{}
GenericUniquePointer::~GenericUniquePointer() noexcept {
	deleter(o);
}

JoiningThread::~JoiningThread() {
	std::cout << "~JoiningThread\n";
	o.join();
}

EpollReactor::EpollReactor(U8F const threadC): reactorThreads{
	Tag::constructWithGeneratedArgs,
	threadC,
	[this](auto const &cons, auto const i) { cons(
		*this, i
	); }
} {}

bool PendingTimerCmp::operator()(
	PendingTimer const &a,
	PendingTimer const &b
) {
	return a.time < b.time;
}

static void executeEpollEvents(ReactionExecutionInfo const execInfo) {
	auto &epollThread= execInfo.thisReactor.reactorThreads[execInfo.thisThreadI];
	for(;;) {
		epoll_event events[maxEventC];
		auto &timers= epollThread.pendingTimers;
		auto const timeout= [&epollThread,&timers]{
			std::lock_guard g{epollThread.reactionTableMutex};
			return timers.empty() ? -1 :
				std::chrono::duration_cast<std::chrono::milliseconds>(
					timers.top().time - std::chrono::steady_clock::now()
				).count();
		}();
		signed const epollRet= epoll_wait(
			epollThread.epollFd,
			events,
			maxEventC,
			timeout
		);
		auto const &checkTimers= [&epollThread, &timers, execInfo]{
			auto const now= std::chrono::ceil<std::chrono::milliseconds>(std::chrono::steady_clock::now());
			for(; !timers.empty() && std::chrono::floor<std::chrono::milliseconds>(timers.top().time) <= now; ) {
				auto timer= timers.top();
				timers.pop();
				auto const &reaction= epollThread.timerReactionTable[timer.indexInTable];
				reaction.func(reaction.data.o, execInfo);
				timer.time += timer.interval;
				timers.push(timer);
			}
		};
		std::lock_guard g{epollThread.reactionTableMutex};
		checkTimers();
		if(epollRet == -1 && errno == EINTR)
			continue;
		PERROR_ASSERT(0 <= epollRet);
		for(U32F i=0; i<static_cast<U32F>(epollRet); ++i) {
			auto const &reaction= epollThread.fdReactionTable[events[i].data.u32];
			reaction.func(reaction.data.o, static_cast<U32>(events[i].events), execInfo);
		}
		checkTimers();
	}
}
EpollThread::EpollThread(EpollReactor &reactor, U32L const i):
	i{i},
	epollFd{epoll_create(epollCreateHint)},
	wakeupForNewTimerFd{[]{
		signed const eventFdRet= eventfd(0, EFD_NONBLOCK);
		PERROR_ASSERT(-1 != eventFdRet);
		return eventFdRet;
	}()},
	fdReactionTable{5},
	timerReactionTable{5},
	o{executeEpollEvents, ReactionExecutionInfo{reactor, i}}
{
	addFdReaction(reactor, wakeupForNewTimerFd, EPOLLIN, {
		*[](void *data, U32 events, ReactionExecutionInfo) {
//			std::cout << "woken up for new timer\n";
			char eventFdBuf[8];
			PERROR_ASSERT(0 <= read(*static_cast<signed*>(data), &eventFdBuf, sizeof eventFdBuf));
		},
		{Tag::notDeleted, &wakeupForNewTimerFd}
	});
}

struct AddReactionLock {
	std::optional<std::lock_guard<std::mutex>> lock;
	U32 targetThreadI;
};
// don't acquire the lock if this thread's epoll executor already acquired it
// (in the case where we want to add a reaction to this thread's executor)
AddReactionLock lockForAddReaction(EpollReactor &reactor) {
	U8F nextRoundRobinI;
	for(;;) {
		nextRoundRobinI= reactor.nextRoundRobinI.load();
		U8F const nextNextRoundRobinI= (1+nextRoundRobinI) % reactor.reactorThreads.size;
		if(reactor.nextRoundRobinI.compare_exchange_weak(
			nextRoundRobinI,
			nextNextRoundRobinI,
			std::memory_order_relaxed,
			std::memory_order_relaxed
		))
			break;
	}
	auto &targetThread= reactor.reactorThreads[nextRoundRobinI];
	return {
		std::this_thread::get_id() == targetThread.o.o.get_id()
			? std::nullopt
			: std::optional<std::lock_guard<std::mutex>>{std::in_place, targetThread.reactionTableMutex},
		nextRoundRobinI
	};
}

ReactionHandle addFdReaction(
	EpollReactor &reactor,
	signed const fd,
	U32 const events,
	FdReaction &&reaction
) {
//	std::cout << "start addFdReaction\n";
	auto const lock= lockForAddReaction(reactor);
	auto &target= reactor.reactorThreads[lock.targetThreadI];
	auto const allocI= emplace(target.fdReactionTable,
		[&reaction, &target, events, fd]
		(auto const &create, auto const allocI) {
			create(std::move(reaction));
			epoll_event event;
			event.events= events;
			event.data.u32= allocI;
			epoll_ctl(target.epollFd, EPOLL_CTL_ADD, fd, &event);
			return allocI;
		}
	);
	return {static_cast<U32>(allocI), lock.targetThreadI};
//	std::cout << "end addFdReaction\n";
}

void removeReactionFromThisThread(EpollThread &thread, U32 const reactionI) {
	destroy(thread.fdReactionTable, reactionI);
}

void addTimerReaction(
	EpollReactor &reactor,
	std::chrono::steady_clock::duration const interval,
	TimerReaction &&reaction
) {
//	std::cout << "start addTimerReaction\n";
	auto const &[lock, targetThreadI]= lockForAddReaction(reactor);
	auto &targetThread= reactor.reactorThreads[targetThreadI];
	auto const i= emplace(
		targetThread.timerReactionTable,
		[&reaction](auto const &cons, auto const i) {
			cons(std::move(reaction));
			return i;
		}
	);
	targetThread.pendingTimers.push({
		std::chrono::steady_clock::now() + interval,
		interval,
		static_cast<SmallReactionI>(i)
	});
	U64 const eventFdBuf= 1;
	static_assert(8 == sizeof eventFdBuf);
	PERROR_ASSERT(8 == write(targetThread.wakeupForNewTimerFd, &eventFdBuf, sizeof eventFdBuf));
//	std::cout << "end addTimerReaction\n";
}

EpollThread &getThisThread(ReactionExecutionInfo const execInfo) {
	return execInfo.thisReactor.reactorThreads[execInfo.thisThreadI];
}
