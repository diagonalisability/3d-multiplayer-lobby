#pragma once
#include<queue>
#include"array.hpp"
#include"networking.hpp"
#include"position/cpp.hpp"

struct PosUpdate {
	U32 playerI;
	float x,y,z;
};
struct RenderUpdateQueue {
	std::queue<PosUpdate> queue;
	std::mutex mutex;
};
struct OtherPlayer {
	Position position;
};
struct Program;
struct NetworkingState {
	ReplicaHoleyArray<OtherPlayer, U32L> otherPlayers{Tag::empty};
	std::mutex mutex;
	AsyncSocket socket;
	NetworkingState(Program&);
};
