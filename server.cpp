#include<fcntl.h> // fcntl, O_NONBLOCK
#include<netinet/in.h> // sockaddr, sockaddr_in
#include<sys/epoll.h> // epoll_create, epoll_ctl, epoll_wait, epoll_event
#include<sys/socket.h> // socket
#include<unistd.h> // write
#include<vector> // std::vector
#include"common.hpp"
#include"networking.hpp"
#include"networking-impl.hpp"
#include"position/cpp.hpp"
#include"memcpy.hpp"

// server->client message types
// 0: here are the ids of existing players
// 1: a new player joined, here is their id
// 2: a player disconnected, here is their id
// 3: here are the positions of all players you've been told are connected

template<typename... Srcs>
auto serialise(Srcs &&...srcs) {
	StaticArray<char, (0 + ... + sizeof(Srcs))> ret{Tag::defaultInitialise};
	std::size_t offset= 0;
	(... , [&ret, &offset, &srcs]{
		std::memcpy(getData(ret) + offset, &srcs, sizeof srcs);
		offset += sizeof srcs;
	}());
	return ret;
}

struct MutexedPlayers;
struct Player{
	AsyncSocket socket;
	Position position;
	Player(EpollReactor&, signed socketFd, MutexedPlayers &players, Sync::PlayerI);
private:
	// ctor implementation
	Player(signed const socketFd, ReactionHandle const&);
};

struct MutexedPlayers {
	HoleyArray<std::unique_ptr<Player>, Sync::PlayerC> o{5};
	// controls access to the array of players and to the players themselves
	// should each Player have their own mutex?
	std::mutex mutex;
};

struct NewConnectionContext {
	EpollReactor *reactor;
	MutexedPlayers &players;
	signed tcpListenSockFd;
};

struct PlayerSocketReactionContext {
	MutexedPlayers &players;
	Sync::PlayerI playerI;
};

static void handlePlayerSocketReady(
	void *const ctx_,
	U32 const epollEvents,
	ReactionExecutionInfo const execInfo
) {
	auto &ctx= assertExists(static_cast<PlayerSocketReactionContext*>(ctx_));
	auto &player= [&ctx]()->Player& {
		std::lock_guard g{ctx.players.mutex};
		return assertExists(ctx.players.o[ctx.playerI].get());
	}();
	auto const &handleClientDisconnected= [&player, &ctx, execInfo]{
		std::cout << "player " << ctx.playerI << " disconnected, closing socket...\n";
		PERROR_ASSERT(0 == close(player.socket.fd));
		auto &players= ctx.players;
		auto const playerI= ctx.playerI;
		// remove the player from its thread's reaction table (this destroys ctx)
		removeReactionFromThisThread(getThisThread(execInfo), player.socket.reactionHandle.epollReactionI);
		// remove the player from the list of players
		destroy(players.o, playerI);
		// notify all the other players that this one has disconnected
		std::lock_guard g{players.mutex};
		foreach(players.o,
			[playerI, execInfo]
			(auto const filledI, auto const oI, std::unique_ptr<Player> const &player) {
				auto const buf= serialise(MessageType{2}, playerI);
				scheduleSocketWrite(
					player.get()->socket,
					serialise(MessageType{2}, playerI - (oI <= playerI)),
					execInfo.thisReactor
				);
			}
		);
	};
	if(epollEvents & (EPOLLHUP | EPOLLRDHUP)) {
		std::cout << "peer hung up!\n";
		handleClientDisconnected();
		return;
	}
	if(epollEvents & EPOLLIN) handleMessageStreamReadable(
		player.socket.fd,
		player.socket.asyncRead,
		// handle message
		[&ctx, &player]
			(MessageType messageType,
			char const *scanPos,
			auto remainingByteC
		)->FastInteger<MessageBufSize> {
			// assume the message is an UpdatePos for now
			ASSERT(messageType == 0);
			if(remainingByteC < sizeof(UpdatePos))
				return -1;
			std::lock_guard g{ctx.players.mutex};
			memcpyInit(getX(player.position), scanPos + 0*sizeof(Position::El));
			memcpyInit(getY(player.position), scanPos + 1*sizeof(Position::El));
			memcpyInit(getZ(player.position), scanPos + 2*sizeof(Position::El));
/*			std::cout
				<< "updating position: {"
				<< player.x << ","
				<< player.y << ","
				<< player.z
				<< "}\n"; */
			return sizeof(UpdatePos);
		},
		// handle end of stream
		handleClientDisconnected
	);
	if(epollEvents & EPOLLOUT)
		handleMessageStreamWritable(player.socket, execInfo);
}

Player::Player(
	EpollReactor &reactor,
	signed const socketFd,
	MutexedPlayers &players,
	Sync::PlayerI const playerI
):
	socket{
		reactor,
		socketFd,
		defaultSocketEvents,
		{
			handlePlayerSocketReady,
			{Tag::defaultDeleted, *new PlayerSocketReactionContext{
				players, playerI
			}}
		}
	},
	// if a player spawns somewhere other than the origin, here is where that would need to change
	position{{0, 0, 0}}
{}

static void handleNewConnection(void *newConnCtx_, U32 const epollEvent, ReactionExecutionInfo const execInfo) {
	auto const &ctx= assertExists(static_cast<NewConnectionContext*>(newConnCtx_));
	sockaddr_in clientAddr_{};
	socklen_t connectingSockAddrLen= sizeof clientAddr_;
	auto const tcpConnSockFd= [&]{
		for(;;) {
			signed const acceptRet= accept(
				ctx.tcpListenSockFd,
				&reinterpret_cast<sockaddr&>(clientAddr_),
				&connectingSockAddrLen
			);
			if(0 <= acceptRet)
				return acceptRet;
			PERROR_ASSERT(errno == EINTR);
			std::cout << "accept interrupted, retrying...\n";
		}
	}();
	U32 const clientAddr= clientAddr_.sin_addr.s_addr;
	char clientAddrMem[sizeof clientAddr];
	memcpyInspect(clientAddrMem, clientAddr);
	std::cout << "accepted a connection! client addr: "
		<< static_cast<signed>(clientAddrMem[0]) << '.'
		<< static_cast<signed>(clientAddrMem[1]) << '.'
		<< static_cast<signed>(clientAddrMem[2]) << '.'
		<< static_cast<signed>(clientAddrMem[3])
		<< '\n';
	fcntl(tcpConnSockFd, F_SETFL, O_NONBLOCK);
	PERROR_ASSERT(-1 != fcntl(tcpConnSockFd, F_SETFL, O_NONBLOCK));
	std::lock_guard g{ctx.players.mutex};
	auto const playerInfo= emplace(
		ctx.players.o,
		[&ctx, tcpConnSockFd](auto const &cons, auto const playerI_)->auto {
			Sync::PlayerI const playerI= playerI_;
			WATCH(playerI);
			auto playerPtr= std::make_unique<Player>(
				*ctx.reactor,
				tcpConnSockFd,
				ctx.players,
				playerI
			);
			auto &player= *playerPtr;
			cons(std::move(playerPtr));
			return std::forward_as_tuple(player, playerI);
		}
	);
	auto &player= std::get<0>(playerInfo);
	auto const newPlayerI= std::get<1>(playerInfo);
	Sync::PlayerC const playerC= size(ctx.players.o) - 1; // don't include the new player
	std::cout << "preliminary send, sending playerC=" << playerC << "\n";

	// gather indices of existing players, except for the current player
	std::vector<Sync::PlayerI> playerIs;
	foreach(ctx.players.o,
		[newPlayerI, &playerIs]
		(auto const filledI, auto const oI, std::unique_ptr<Player>&) {
			if(oI == newPlayerI)
				return;
			playerIs.push_back(oI);
		}
	);

	// send a message to the new player containing the ids of all the existing players
	MessageType const existingPlayersMessageType= 0;
	auto const bufSize=
		sizeof(existingPlayersMessageType)
		+ sizeof playerC
		+ sizeof(Sync::PlayerI) * playerIs.size();
	auto const mem= std::make_unique<char[]>(bufSize);
	memcpyInspect(mem.get(), existingPlayersMessageType);
	memcpyInspect(mem.get() + sizeof(MessageType), playerC);
	for(U32 i=0; i<playerIs.size(); ++i)
		memcpyInspect(
			mem.get() + sizeof(MessageType) + sizeof playerC + i*sizeof(Sync::PlayerI),
			playerIs[i]
		);
	scheduleSocketWrite(player.socket, {mem.get(), bufSize}, execInfo.thisReactor);
	
	// send a message to all existing players containing the id of the new player
	for(auto const playerI : playerIs)
		scheduleSocketWrite(
			ctx.players.o[playerI]->socket,
			serialise(MessageType{1}, newPlayerI - (playerI <= newPlayerI)),
			execInfo.thisReactor
		);
}

static void broadcastPlayerPositions(void *players_, ReactionExecutionInfo execInfo) {
	auto &players= assertExists(static_cast<MutexedPlayers*>(players_));
	std::cout << "broadcasting player positions...\n";
	std::lock_guard g0{players.mutex};
	if(size(players.o) < 1)
		return;
	auto const bufSize= sizeof(MessageType) + 3*sizeof(Position::El) * (size(players.o) - 1);
	auto const buf= std::make_unique<char[]>(bufSize);
	memcpyInspect(buf.get(), MessageType{3});
	foreach(players.o,
		[&players, &execInfo, buf= buf.get(), bufSize]
		(auto, auto const oI0, auto &player) {
			foreach(
				players.o,
				[buf= buf+sizeof(MessageType), oI0, haveReachedSamePlayer= false]
				(auto const filledI, auto const oI1, auto &player) mutable {
					if(oI0 == oI1) {
						// don't send a player their own position
						haveReachedSamePlayer= true;
						return;
					}
					auto const i= filledI - haveReachedSamePlayer;
					memcpyInspect(buf + (0 + i*3)*sizeof(Position::El), getX(player->position));
					memcpyInspect(buf + (1 + i*3)*sizeof(Position::El), getY(player->position));
					memcpyInspect(buf + (2 + i*3)*sizeof(Position::El), getZ(player->position));
				}
			);
			scheduleSocketWrite(players.o[oI0]->socket, {buf, bufSize}, execInfo.thisReactor);
		}
	);
}

signed main() {
	MutexedPlayers players;
	// https://riptutorial.com/posix/example/16533/tcp-concurrent-echo-server
	signed const tcpListenSockFd= socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	PERROR_ASSERT(0 <= tcpListenSockFd);
	WATCH(tcpListenSockFd);
	signed const shouldEnableReusePort= true;
	setsockopt(
		tcpListenSockFd,
		SOL_SOCKET,
		SO_REUSEPORT,
		&shouldEnableReusePort,
		sizeof shouldEnableReusePort
	);
	sockaddr_in addrToAcceptOn{};
	addrToAcceptOn.sin_family= AF_INET;
	// "The sin_port and sin_addr members shall be in network byte order" ~Posix
	addrToAcceptOn.sin_port= htons(port);
	addrToAcceptOn.sin_addr.s_addr= 0;
	PERROR_ASSERT(0 == bind(
		tcpListenSockFd,
		&reinterpret_cast<sockaddr&>(addrToAcceptOn),
		sizeof addrToAcceptOn
	));
	PERROR_ASSERT(-1 != fcntl(tcpListenSockFd, F_SETFL, O_NONBLOCK));
	PERROR_ASSERT(0 == listen(tcpListenSockFd, tcpListenBacklog));	
	NewConnectionContext newConnCtx{
		getUninitialised<EpollReactor*>(),
		players,
		tcpListenSockFd
	};
	// reactor must be declared after contexts, because its destructor will block
	// on the joining of the internal thread pool
	EpollReactor reactor{4};
	newConnCtx.reactor= &reactor;
	addFdReaction(
		reactor,
		tcpListenSockFd,
		EPOLLIN,
		{
			handleNewConnection,
			{Tag::notDeleted, &newConnCtx}
		}
	);
	addTimerReaction(reactor, positionUpdateInterval, TimerReaction{
		*broadcastPlayerPositions,
		{Tag::notDeleted, static_cast<void*>(&players)}
	});
}
