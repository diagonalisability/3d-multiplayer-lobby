#pragma once
#include"vulkan.hpp"
#include"client-networking.hpp"
struct Dummy{};
struct Program {
	VulkanInstance vulkanInstance;
	VulkanWindow vulkanWindow;
	EpollReactor reactor;
	NetworkingState networkingState;
	Program();
	~Program();
};
