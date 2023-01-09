#include<cstring> // std::memmove
#include<unistd.h>
#include"networking.hpp"

AsyncSocket::AsyncSocket(EpollReactor &reactor, signed fd, U32 epollEvents, FdReaction &&reaction):
	fd{fd},
	reactionHandle{addFdReaction(
		reactor,
		fd,
		epollEvents,
		std::move(reaction)
	)}
{}

// returns the count of written bytes
static U32 writeAsMuchAsPossible(
	signed const fd,
	StringView<FastInteger<MessageBufSize>> const src
) {
	for(U32F pos=0;;) {
		auto const leftByteC= src.size - pos;
		if(!leftByteC)
			return src.size;
		auto const writeRet= write(fd, src.o + pos, leftByteC);
		if(0 < writeRet) {
			pos+= writeRet;
			continue;
		}
		// TODO: this fails when the peer disconnects, which should be handled
		if(writeRet == -1 && errno == EAGAIN)
			UNIMPLEMENTED;
		return pos;
	}
	unreachable();
}

void scheduleSocketWrite(
	AsyncSocket &socket,
	StringView<FastInteger<MessageBufSize>> const src,
	EpollReactor &reactor
) {
	std::lock_guard g0{socket.asyncWrite.bufMutex};
	U32 const writtenByteC= socket.asyncWrite.buf.empty()
		? writeAsMuchAsPossible(socket.fd, src)
		: 0;
	if(writtenByteC == src.size)
		// there's nothing to schedule, we were able to immediately write the whole message
		return;
	auto &dst= socket.asyncWrite.buf;
	dst.insert(end(dst), begin(src) + writtenByteC, end(src));
	if(!socket.asyncWrite.willNotifyOnWritable) {
		epoll_event event;
		event.events= defaultSocketEvents | EPOLLOUT;
		event.data.u32= socket.reactionHandle.epollReactionI;
		epoll_ctl(
			reactor.reactorThreads[socket.reactionHandle.reactionThreadI].epollFd,
			EPOLL_CTL_MOD,
			socket.fd,
			&event
		);
		socket.asyncWrite.willNotifyOnWritable= true;
	}
}

void handleMessageStreamWritable(
	AsyncSocket &socket,
	ReactionExecutionInfo const execInfo
) {
	std::cout << "start handleMessageStreamWritable\n";
	auto &asyncWrite= socket.asyncWrite;
	auto &buf= asyncWrite.buf;
	std::lock_guard g{asyncWrite.bufMutex};
	auto const writtenByteC= writeAsMuchAsPossible(socket.fd, {buf.data(), buf.size()});
	U32 const leftByteC= buf.size() - writtenByteC;
	std::memmove(buf.data(), buf.data() + writtenByteC, leftByteC);
	buf.resize(leftByteC);
	epoll_event event;
	event.events= defaultSocketEvents; // deregister notification for writability
	event.data.u32= socket.reactionHandle.epollReactionI;
	epoll_ctl(
		execInfo.thisReactor.reactorThreads[execInfo.thisThreadI].epollFd,
		EPOLL_CTL_MOD,
		socket.fd,
		&event
	);
}
