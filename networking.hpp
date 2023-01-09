#pragma once
#include"common.hpp"
#include"concurrency.hpp"

typedef char MessageType;
// server->client message type 0
struct UpdatePos {
	S32 x,y,z;
};
#define UPDATE_POS_FOREACH(A, B)\
	A(x) B A(y) B A(z)
#define UPDATE_POS_SIZEOF(x) sizeof UpdatePos::x
U32 constexpr UpdatePosMessageLength= UPDATE_POS_FOREACH(UPDATE_POS_SIZEOF, +);

U32 constexpr maxMessageLength= sizeof(MessageType) + UpdatePosMessageLength;

struct AsyncRead {
	U32L incompleteMessageLength;
	char incompleteMessage[maxMessageLength-1];
};

struct AsyncWrite {
	std::vector<char> buf;
	std::mutex bufMutex;
	bool willNotifyOnWritable;
};

inline void noopFdReaction(void *data, U32 events, ReactionExecutionInfo) {}
struct AsyncSocket {
	signed fd;
	AsyncRead asyncRead{};
	AsyncWrite asyncWrite;
	ReactionHandle reactionHandle;
	AsyncSocket(EpollReactor&, signed fd, U32 events, FdReaction&&={
		*noopFdReaction,
		{Tag::notDeleted, nullptr}
	});
};

void handleMessageStreamWritable(
	AsyncSocket &socket,
	ReactionExecutionInfo const execInfo
);

void scheduleSocketWrite(
	AsyncSocket &socket,
	StringView<std::size_t> const src,
	EpollReactor &reactor
);

unsigned constexpr tcpListenBacklog= 5;
unsigned constexpr port= 9333;
unsigned constexpr epollReceivedEventBufSize= 10;
unsigned constexpr maxMessagesToReceiveAtOnce= 10;
auto constexpr positionUpdateInterval= std::chrono::milliseconds{10};
U32 const defaultSocketEvents= EPOLLIN | EPOLLRDHUP;

namespace Sync {
	typedef U32 PlayerC;
	typedef PlayerC PlayerI;
}
typedef U32L MessageBufSize;
