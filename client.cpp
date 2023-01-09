#include"client.hpp"
#include"client-networking.hpp"
#include"vulkan.hpp"

Program::Program():
	vulkanWindow{vulkanInstance},
	reactor{3},
	networkingState{*this}
{}
Program::~Program() {
	destroy(vulkanWindow, vulkanInstance);
}

signed main() {
	initGlfw();
	Program program;
	drawFrames(program);
}
