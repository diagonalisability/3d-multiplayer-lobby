#include<fcntl.h>
#include<netinet/in.h>
#include<netinet/tcp.h>
#include<sys/socket.h>
#include<unistd.h>
#include<functional>
#include"client.hpp"
#include"common.hpp"
#include"networking.hpp"
#include"networking-impl.hpp"
#include"memcpy.hpp"
#include"vulkan.hpp"

static auto const handleMessage= [](
	NetworkingState &ns,
	MessageType const messageType,
	char const *const scanPos,
	auto const remainingByteC
)->FastInteger<MessageBufSize> {
	switch(messageType) {
	case 0: {
			Sync::PlayerC playerC;
			if(remainingByteC < sizeof playerC)
				return -1;
			memcpyInit(playerC, scanPos);
			std::cout << "received player count: " << playerC << "!\n";
			// TODO: this message can be arbitrarily long, so it might not fit
			// inside the stream read buffer, in that case it needs to be read
			// piece by piece, which is as-of-yet unimplemented. it might be
			// easier to limit the length of the message, possibly by sending
			// multiple of these messages.
			if(remainingByteC < sizeof playerC + playerC*sizeof(Sync::PlayerI))
				return -1;
			auto const playerCBufSize= playerC * sizeof(Sync::PlayerI);
			auto const playerIs= std::make_unique<Sync::PlayerI[]>(playerC);
			std::memcpy(playerIs.get(), scanPos + sizeof playerC, playerCBufSize);
			{
				std::lock_guard g{ns.mutex};
				allocate(ns.otherPlayers, 5u);
				for(U16F playerII= 0; playerII < playerC; ++playerII)
					// todo: should the server send the other players' positions in the initial update?
					emplace(ns.otherPlayers, playerIs[playerII], Position{{0, 0, 0}});
			}
			std::cout << "players with these ids are already playing: ";
			bool isInitial= true;
			for(FastInteger<Sync::PlayerC> i=0; i<playerC; ++i) {
				if(isInitial)
					isInitial= false;
				else
					std::cout << ' ';
				std::cout << playerIs[i];
			}
			std::cout << '\n';
			return sizeof playerC + playerCBufSize;
		}
	case 1:
		Sync::PlayerI newPlayerI;
		if(remainingByteC < sizeof newPlayerI)
			return -1;
		memcpyInit(newPlayerI, scanPos);
		{
			std::lock_guard g{ns.mutex};
			emplace(ns.otherPlayers, newPlayerI, Position{{0, 0, 0}});
		}
		std::cout << "new player joined with id " << newPlayerI << "\n";
		return sizeof newPlayerI;
	case 2:
		Sync::PlayerI disconnectedPlayerI;
		if(remainingByteC < sizeof disconnectedPlayerI)
			return -1;
		memcpyInit(disconnectedPlayerI, scanPos);
		{
			std::lock_guard g{ns.mutex};
			destroy(ns.otherPlayers, disconnectedPlayerI);
		}
		std::cout << "player disconnected with id " << disconnectedPlayerI << "\n";
		return sizeof disconnectedPlayerI;
	case 3:
		{
			std::lock_guard g{ns.mutex};
			std::size_t const msgLen= 3*sizeof(getX(OtherPlayer::position)) * size(ns.otherPlayers);
			if(remainingByteC < msgLen)
				return -1;
			foreach(ns.otherPlayers, [scanPos](auto const i, auto, auto &player) {
				memcpyInit(getX(player.position), scanPos + (0 + 3*i)*sizeof(Position::El));
				memcpyInit(getY(player.position), scanPos + (1 + 3*i)*sizeof(Position::El));
				memcpyInit(getZ(player.position), scanPos + (2 + 3*i)*sizeof(Position::El));
			});
			return msgLen;
		}
	default:
		std::cout << "received an unknown message type, can't continue processing messages\n";
		ASSERT(false);
	}
};

auto constexpr &socketFunc= *socket;
NetworkingState::NetworkingState(Program &program): socket{
	program.reactor,
	[]{
		// https://riptutorial.com/posix/example/17612/tcp-daytime-client
		// connect to the server
		signed const tcpConnSockFd= socketFunc(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		PERROR_ASSERT(0 <= tcpConnSockFd);
		signed const noDelayOption= 1;
		setsockopt(tcpConnSockFd, SOL_SOCKET, TCP_NODELAY, &noDelayOption, sizeof noDelayOption);
		sockaddr_in serverAddr{};
		serverAddr.sin_family= AF_INET;
		serverAddr.sin_port= htons(port);
		char constexpr serverAddrMem[] { 127, 0, 0, 1 };
		std::memcpy(&serverAddr.sin_addr.s_addr, serverAddrMem, sizeof serverAddr.sin_addr.s_addr);
		std::cout << "connecting...\n";
		// connect synchronously
		PERROR_ASSERT(0 == connect(
			tcpConnSockFd,
			&reinterpret_cast<sockaddr&>(serverAddr),
			sizeof serverAddr
		));
		// make the socket asynchronous now
		fcntl(tcpConnSockFd, F_SETFL, O_NONBLOCK);
		return tcpConnSockFd;
	}(),
	defaultSocketEvents,
	{
		*[](void *const data, U32 const events, ReactionExecutionInfo const execInfo){
			auto &program= assertExists(static_cast<Program*>(data));
			auto &socket= program.networkingState.socket;
			handleMessageStreamReadable(
				socket.fd, socket.asyncRead,
				// handle message
				[&ns= program.networkingState](
					MessageType const messageType,
					char const *const scanPos,
					auto const remainingByteC
				) { return handleMessage(ns, messageType, scanPos, remainingByteC); },
				// handle end of stream
				[]{
					std::cout << "end of stream, server disconnected!\n";
					UNIMPLEMENTED;
				}
			);
		},
		{ Tag::notDeleted, &program }
	}
} {
	addTimerReaction(program.reactor, positionUpdateInterval, TimerReaction{
		*[](void *data, ReactionExecutionInfo const execInfo){
			auto &program= assertExists(static_cast<Program*>(data));
			// send a position update
			auto const &cam= program.vulkanWindow.statics.camera;
			UpdatePos const update{
				getX(cam.position).o,
				getY(cam.position).o,
				getZ(cam.position).o,
			};
			char buf[sizeof(MessageType) + sizeof update];
			memcpyInspect(buf, MessageType{0});
			memcpyInspect(buf + sizeof(MessageType), update);
			scheduleSocketWrite(program.networkingState.socket, {buf}, execInfo.thisReactor);
		},
		{ Tag::notDeleted, &program }
	});
}
