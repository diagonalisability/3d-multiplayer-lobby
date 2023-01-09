#pragma once
#include<atomic> // std::atomic<U8F>
#include<condition_variable> // std::condition_variable
#include<functional> // std::reference_wrapper
#include<mutex> // std::mutex
#include<thread> // std::thread
#include<queue> // std::queue
#include<utility> // std::make_index_sequence
#include<vector> // std::vector
#include<sys/epoll.h> // EPOLLIN
#include"array.hpp"
#include"common.hpp"

struct JoiningThread {
	std::thread o;
	JoiningThread()= default;
	template<typename F, typename ...Args>
	JoiningThread(F&&, Args&&...);
	~JoiningThread();
};

template<typename F, typename ...Args>
JoiningThread::JoiningThread(F &&f, Args &&...args):
	o{std::forward<F>(f), std::forward<Args>(args)...}
{}
template<typename F, typename ...Args>
void start(JoiningThread &jt, F &&f, Args &&...args) {
	jt.o= std::thread{std::forward<F>(f), std::forward<Args>(args)...};
}

namespace Tag {
	struct DefaultDeleted {} constexpr defaultDeleted;
	struct NotDeleted {} constexpr notDeleted;
}
struct GenericUniquePointer {
	void *o;
	void (&deleter)(void*);
	GenericUniquePointer(GenericUniquePointer&&) noexcept;
	template<typename O>
	GenericUniquePointer(Tag::DefaultDeleted, O&) noexcept;
	GenericUniquePointer(Tag::NotDeleted, void*) noexcept;
	~GenericUniquePointer() noexcept;
};
template<typename O>
GenericUniquePointer::GenericUniquePointer(Tag::DefaultDeleted, O &o) noexcept:
	o{&o},
	deleter{*[](void *const o) {
		delete static_cast<O*>(o);
	}}
{}

struct EpollReactor;
struct EpollThread;
struct ReactionExecutionInfo {
	EpollReactor &thisReactor;
	U32 thisThreadI;
};
EpollThread &getThisThread(ReactionExecutionInfo);

typedef void FdReactionFunc(void *data, U32 events, ReactionExecutionInfo);
struct FdReaction {
	std::reference_wrapper<FdReactionFunc> func;
	GenericUniquePointer data;
};
typedef void TimerReactionFunc(void *data, ReactionExecutionInfo);
struct TimerReaction {
	std::reference_wrapper<TimerReactionFunc> func;
	GenericUniquePointer data;
	TimerReaction(TimerReactionFunc &func, GenericUniquePointer data):
		func{std::ref(func)},
		data{std::move(data)}
	{}
};
typedef U16L SmallReactionI;
typedef U16F FastReactionI;
struct PendingTimer {
	std::chrono::steady_clock::time_point time;
	std::chrono::steady_clock::duration interval;
	SmallReactionI indexInTable;
};
struct PendingTimerCmp {
	bool operator()(PendingTimer const&, PendingTimer const&);
};
struct EpollReactor;
struct EpollThread {
	U32L i;
	signed epollFd;
	signed wakeupForNewTimerFd;
	HoleyArray<FdReaction, FastReactionI> fdReactionTable;
	std::mutex reactionTableMutex;
	HoleyArray<TimerReaction, FastReactionI> timerReactionTable;
	std::priority_queue<
		PendingTimer,
		std::vector<PendingTimer>,
		PendingTimerCmp
	> pendingTimers;
	// this is last because it must be destroyed (by joining) before reactionTable and such
	JoiningThread o;
	EpollThread(EpollReactor &reactor, U32L i);
};
// each thread "epoll_wait"s on a different epoll instance's FD
// epoll instances' event FD sets are disjoint, to improve cache locality
// TODO: implement rebalancing FDs between threads
struct EpollReactor {
	std::atomic<U8F> nextRoundRobinI= 0;
	std::mutex nextRoundRobinIMutex;
	signed stopEventFd;
	SizedArray<EpollThread, U8F> reactorThreads;
	EpollReactor(U8F threadC);
};
struct ReactionHandle {
	U32 epollReactionI;
	U32 reactionThreadI;
};
ReactionHandle addFdReaction(EpollReactor&, signed fd, U32 events, FdReaction&&);
void removeReactionFromThisThread(EpollThread &thisThread, U32 const thisReactionI);
void addTimerReaction(
	EpollReactor&,
	std::chrono::steady_clock::duration interval,
	TimerReaction&&
);
