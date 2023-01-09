#pragma once
#include<bit>
#include<cstdint>
#include<cstring>
#include<iomanip>
#include<iostream>
#include<filesystem>
#include<memory>
#include<optional>
#include<tuple>
#include<unordered_map>
#include<unordered_set>
#include<vector>
#include<sys/stat.h>
#include<filesystem>
#include<fstream>
#include<vulkan/vulkan.h>
#define GLFW_INCLUDE_VULKAN
#include<GLFW/glfw3.h>
#include<glm/glm.hpp>
#include"wayland-protocol.h"
#include"vulkan-memory-allocator.hpp"
//#include <vulkan/vulkan_wayland.h>
#include<wayland-client.h>
#include"array.hpp"
#include"common.hpp"
#include"position/cpp.hpp"
#include"vulkan-enum-name-maps.hpp"

#define ASSERT_VK_SUCCESS(X)\
	do if(VkResult const result1073467922= (X); result1073467922 != VK_SUCCESS) {\
		std::cout\
			<< "assertion failed: "\
			<< #X\
			<< " evaluated to "\
			<< mapVulkanResultToString.at(result1073467922)\
			<< " instead of VK_SUCCESS (at "\
			<< __FILE__\
			<< ":"\
			<< STR(__LINE__)\
			<< ")\n";\
		std::exit(1);\
	} while(0)
#define OUTPUT_VERSION(VERSION)\
	VK_VERSION_MAJOR((VERSION)) << "."\
	<< VK_VERSION_MINOR((VERSION)) << "."\
	<< VK_VERSION_PATCH((VERSION))

typedef StringView<U16F> IdentifierStringView;
typedef StringView<U16F> PathStringView;

VkDeviceSize constexpr sharedStagingBufferSize= 1u << 20; // MiB
auto constexpr maxFrameInFlightC= tightenSizeType<2>;
typedef std::remove_const_t<decltype(maxFrameInFlightC)> FramesSize;
typedef FramesSize FrameIndex;

// === struct declarations ===
struct Dynamics;
struct VulkanWindow;

// === struct definitions ===

// note that this class has a "proper" destructor, unlike the other Vulkan
// classes, which need to be manually destroyed because they don't store the
// VkDevice that was used to create them.
struct VulkanInstance {
	VkInstance o;
	std::vector<std::string> layerNames;
	VulkanInstance();
	VulkanInstance(VkInstance, std::vector<std::string>&&);
	VulkanInstance(VulkanInstance const&)= delete;
	~VulkanInstance();
};

namespace Tag {
	struct Null {} constexpr null;
}
struct VulkanCommandBuffer {
	VkCommandBuffer o;
	VulkanCommandBuffer(VulkanCommandBuffer const&)= delete;
	VulkanCommandBuffer(Tag::Null);
	VulkanCommandBuffer(VkCommandPool, VkDevice);
	~VulkanCommandBuffer();
	VulkanCommandBuffer &operator=(VulkanCommandBuffer&&);
};

struct VulkanFence {
	VkFence o;
	VulkanFence(VulkanFence const&)= delete;
	VulkanFence(Tag::Null);
	VulkanFence(VkDevice, VkFenceCreateFlagBits= static_cast<VkFenceCreateFlagBits>(0));
	~VulkanFence();
	VulkanFence &operator=(VulkanFence&&);
};

#ifdef __cpp_concepts
template<typename WindowEventHandler>
concept ResizeHandler = requires(
	WindowEventHandler weh,
	signed const width,
	signed const height,
	signed const key,
	signed const scancode,
	signed const action,
	signed const mods,
	double const xpos,
	double const ypos
) {
	handleResize(weh, width, height);
	handleKey(weh, key, scancode, action, mods);
	handleCursorPosition(weh, xpos, ypos);
};
#endif

template<typename T>
struct Extent {
	T width, height;
};

template<>
struct Extent<U32> {
	U32 width, height;
	Extent(U32 width, U32 height);
	Extent(VkExtent2D);
};

struct GlfwWindow {
	GLFWwindow &o;
#ifndef __cpp_concepts
	template<typename WindowEventHandler>
#endif
	GlfwWindow(
		VulkanInstance const &instance,
		WindowEventHandler
#ifdef __cpp_concepts
			auto
#endif
			&windowEventHandler,
		Extent<U32>
	);
	~GlfwWindow();
};

struct VulkanSurface {
	VkSurfaceKHR o;
	VulkanSurface(VkSurfaceKHR o);
	VulkanSurface(VulkanSurface const&)= delete;
	VulkanSurface(VulkanSurface&&);
	~VulkanSurface();
};

struct VulkanQueue {
	VkQueue o;
	U32 index;
};

struct VulkanDevice {
	VkPhysicalDevice physical;
	VkDevice logical;
	VulkanQueue graphicsQueue;
	VulkanQueue presentQueue;
	VkPhysicalDeviceProperties deviceProperties;
	VkPhysicalDeviceMemoryProperties memoryProperties;
	VkPhysicalDeviceFeatures featureSupport;
	VulkanDevice(VulkanDevice&&);
	VulkanDevice(VulkanDevice const&)= delete;
	VulkanDevice(VulkanInstance const &vkInstance, VulkanSurface const &surface);
	VulkanDevice(
		VkPhysicalDevice,
		VkDevice,
		VulkanQueue const&,
		VulkanQueue const&,
		VkPhysicalDeviceProperties const&,
		VkPhysicalDeviceMemoryProperties const&,
		VkPhysicalDeviceFeatures const&
	);
	~VulkanDevice();
};

struct VulkanShaderModule {
	VkShaderModule o;
	VulkanShaderModule(VulkanShaderModule const&)= delete;
	VulkanShaderModule(VulkanShaderModule&&);
	VulkanShaderModule(VulkanDevice const &device, char const &filePath);
	~VulkanShaderModule();
};

struct VulkanBuffer {
	VkBuffer o;
	VmaAllocation allocation;
	VulkanBuffer(VulkanBuffer&&);
	VulkanBuffer(
		VkBufferUsageFlags,
		VmaMemoryUsage,
		VmaAllocationCreateFlags,
		VkMemoryPropertyFlags requiredMemoryPropertyFlags,
		VkMemoryPropertyFlags preferredMemoryPropertyFlags,
		VkDeviceSize size,
		VkDeviceSize minAlignment,
		VmaAllocator,
		VmaAllocationInfo *allocInfo // out-parameter (optional, can be null)
	);
	~VulkanBuffer();
	VulkanBuffer &operator=(VulkanBuffer&&);
};

struct VulkanImageParamDataDeleter {
	void operator()(char unsigned*);
};
typedef std::unique_ptr<char unsigned, VulkanImageParamDataDeleter> VulkanImageParamData;
struct VulkanImageParams {
	VulkanImageParamData imageData;
	Extent<U32> extent;
};

struct VulkanImage {
	VkImage o;
	VmaAllocation allocation;
	VulkanImage(VulkanImage&&);
	VulkanImage(
		Extent<U32>,
		VkFormat format,
		VkImageTiling tiling,
		VkImageUsageFlags usage,
		VmaAllocator
	);
	VulkanImage(
		PathStringView const imagePath,
		VmaAllocator const allocator,
		VkCommandBuffer const cmdBufForLoading,
		VkFence const fenceForLoading,
		VulkanDevice const &logicalDevice
	);
	~VulkanImage();
	VulkanImage &operator=(VulkanImage &&other);
private:
	// ctor implementation
	VulkanImage(
		VulkanImageParams const &params,
		VmaAllocator const allocator,
		VkCommandBuffer const cmdBufForLoading,
		VkFence const fenceForLoading,
		VulkanDevice const &logicalDevice
	);
};

struct DrawingSyncObjects {
	VkSemaphore *imageAvailableSemaphores; // x maxFrameInFlightC
	VkSemaphore *renderFinishedSemaphores; // x maxFrameInFlightC
	VkFence *frameInFlightFences;          // x maxFrameInFlightC
	DrawingSyncObjects(DrawingSyncObjects&&);
	DrawingSyncObjects(VulkanDevice const &device);
	~DrawingSyncObjects();
	DrawingSyncObjects &operator=(DrawingSyncObjects&&);
};

namespace Tag {
	struct Plain {} constexpr plain;
}

struct VulkanImageView {
	VkImageView o;
	VulkanImageView(Tag::Null);
	VulkanImageView(VkImage, VkFormat, VkImageAspectFlags, VkDevice);
	~VulkanImageView();
	VulkanImageView &operator=(VulkanImageView&&);
};

struct VulkanImageSampler {
	VkSampler o;
	VulkanImageSampler(VulkanDevice const&);
	~VulkanImageSampler();
};

struct PlainVertex {
	glm::vec3 pos;
	glm::vec2 texPos;
};
struct PlainModelInstance {
	Position position;
	glm::vec4 orient;
};

// "static usage" here simply means that a default usage is associated with a
// specialisation of GrowableHostVisibleBuffer. it does not prevent a different
// usage from being passed as a parameter to functions accepting a
// GrowableHostVisibleBuffer.
struct GrowableHostVisibleBufferDynamicUsage;
template<VkBufferUsageFlags o_>
struct GrowableHostVisibleBufferStaticUsage {
	static VkBufferUsageFlags constexpr o= o_;
};
template<typename>
bool constexpr isSpecialisationOfGrowableHostVisibleBufferStaticUsage= false;
template<VkBufferUsageFlags const usage>
bool constexpr isSpecialisationOfGrowableHostVisibleBufferStaticUsage<
	GrowableHostVisibleBufferStaticUsage<usage>
> = true;
template<typename T, typename Usage0, typename Size>
struct GrowableHostVisibleBuffer {
	static_assert(
		std::is_same_v<Usage0, GrowableHostVisibleBufferDynamicUsage>
		|| isSpecialisationOfGrowableHostVisibleBufferStaticUsage<Usage0>
	);
	static Size constexpr initialCap= 20;
	VulkanBuffer o;
	char *mem;
	VkDeviceSize capacityInBytes;
	Size size;
	GrowableHostVisibleBuffer(VkBufferUsageFlags, VmaAllocator);
	// https://stackoverflow.com/a/53996631
	template<
		typename Usage1= Usage0,
		typename= std::enable_if_t<isSpecialisationOfGrowableHostVisibleBufferStaticUsage<Usage1>>
	> GrowableHostVisibleBuffer(VmaAllocator);
	GrowableHostVisibleBuffer &operator=(GrowableHostVisibleBuffer&&)= delete;
	template<typename Index, typename= EnableIfIntegral<Index>>
	T const &operator[](Index) const;
	template<typename Index, typename= EnableIfIntegral<Index>>
	T &operator[](Index);
private:
	GrowableHostVisibleBuffer(VkBufferUsageFlags, VmaAllocator, VmaAllocationInfo&&);
};

struct VulkanViewableImage {
	VulkanImage o;
	VulkanImageView view;
	VulkanViewableImage(
		PathStringView const pathToLoad,
		VmaAllocator const allocator,
		VkCommandBuffer const cmdBufForLoading,
		VkFence const fenceForLoading,
		VulkanDevice const &device
	);
	~VulkanViewableImage();
};

struct VulkanDescriptorPool {
	VkDescriptorPool o;
	template<std::size_t size>
	VulkanDescriptorPool(VkDescriptorType const (&descriptorTypes)[size], VkDevice);
	~VulkanDescriptorPool();
};

// Plain Models
// the vertex shader transforms vertices with an instance-specific position and
// orientiation, and a uniform world->NDCS matrix. the fragment shader simply
// samples the given texture to determine fragment colour.
struct PlainGeometry {
	std::vector<PlainVertex> vertices;
	std::vector<U32> indices;
};
struct PlainModelParams {
	std::string const &name;
	VkDescriptorSetLayout descriptorSetLayout;
	StaticArray<VulkanBuffer, maxFrameInFlightC> const &perspectiveTransformationMatrixUniformBuffers;
	VkSampler sampler;
	VmaAllocator allocator;
	VkCommandBuffer commandBufferForLoading;
	VkFence fenceForLoading;
	VulkanDevice const &device;
};
struct PlainModel {
	U32 triangleC;
	VulkanDescriptorPool descriptorPool;
	VulkanViewableImage texture;
	StaticArray<VkDescriptorSet, maxFrameInFlightC> descriptorSets;
	VulkanBuffer vertexBuffer;
	VulkanBuffer indexBuffer;
	// instance buffer (technically a per-instance vertex buffer)
	StaticArray<
		GrowableHostVisibleBuffer<
			PlainModelInstance,
			GrowableHostVisibleBufferStaticUsage<VK_BUFFER_USAGE_VERTEX_BUFFER_BIT>,
			U32F
		>,
		maxFrameInFlightC
	> poses;
	~PlainModel();
	PlainModel(
		IdentifierStringView const name,
		VkDescriptorSetLayout,
		StaticArray<VulkanBuffer, maxFrameInFlightC> const &perspectiveTransformationMatrixUniformBuffers,
		VkSampler,
		VmaAllocator,
		VkCommandBuffer const cmdBufForLoading,
		VkFence const fenceForLoading,
		VulkanDevice const &device
	);
private:
	// ctor implementation
	PlainModel(
		PlainModelParams const&,
		PlainGeometry const&
	);
};

typedef U16F VertexInputBindingDescriptionsSize;
typedef U16F VertexInputAttributeDescriptionsSize;
struct VulkanPipeline {
	VulkanShaderModule
		vertexShaderModule,
		fragmentShaderModule;
	VkPipelineLayout layout;
	VkPipeline o;
	VulkanPipeline(
		VkRenderPass,
		char const &vertexShaderPath,
		char const &fragmentShaderPath,
		ArrayView<VkVertexInputBindingDescription, true, VertexInputBindingDescriptionsSize>,
		ArrayView<VkVertexInputAttributeDescription, true, VertexInputAttributeDescriptionsSize>,
		VkPrimitiveTopology,
		VkDescriptorSetLayout const,
		VulkanDevice const&
	);
	~VulkanPipeline();
};

struct Camera {
	Position position;
	float yaw, pitch;
};

typedef U16F DescriptorSetLayoutBindingsSize;
struct VulkanDescriptorSetLayout {
	VkDescriptorSetLayout o;
	VulkanDescriptorSetLayout(
		ArrayView<VkDescriptorSetLayoutBinding, true, DescriptorSetLayoutBindingsSize> bindings,
		VkDevice
	);
	~VulkanDescriptorSetLayout();
};

struct Statics {
	std::chrono::time_point<std::chrono::high_resolution_clock> startTime, lastFrameEndTime;
	std::unordered_set<signed> heldKeys, justPressedKeys;
	Camera camera{{{0, 0, 1}}, 0.f, 0.f};
	U32 renderedFrameC= 0;
	VkExtent2D extent;
	GlfwWindow glfwWindow;
	VulkanSurface surface;
	VulkanDevice device;
	VmaAllocator vmaAllocator;
	VkFormat depthFormat;
	VkCommandPool commandPool;
	VulkanImageSampler plainImageSampler;
	VulkanDescriptorSetLayout
		plainPipelineDescriptorSetLayout,
		groundPipelineDescriptorSetLayout;
	StaticArray<VulkanBuffer, maxFrameInFlightC> perspectiveTransformationMatrixUniformBuffers;
	VulkanDescriptorPool groundDescriptorPool;
	StaticArray<VkDescriptorSet, maxFrameInFlightC> groundDescriptorSets;
	PlainModel
		houseModel,
		cubeModel,
		dietCokeModel;
	StaticArray<VkCommandBuffer, maxFrameInFlightC> commandBuffers;
	DrawingSyncObjects drawingSync;
	~Statics();
	Statics(VulkanInstance const&, VulkanWindow&);
private:
	// ctor implementation
	Statics(
		VulkanInstance const &vulkanInstance,
		VulkanWindow &vw,
		VulkanCommandBuffer cmdBufForLoading,
		VulkanFence fenceForLoading
	);
};

typedef U8L ImagesSize;
typedef FastInteger<ImagesSize> FastImagesSize;
typedef ImagesSize ImageIndex;
typedef FastImagesSize FastImageIndex;

struct VulkanSwapchain {
	VkSwapchainKHR o;
	VkSurfaceFormatKHR surfaceFormat;
	FastImagesSize imageC;
	HeapArray<VkImage> images;
	VulkanSwapchain(Statics const&);
	VulkanSwapchain(VulkanSwapchain&&);
	VulkanSwapchain(VkSwapchainKHR, VkSurfaceFormatKHR, FastInteger<ImagesSize> imageC, HeapArray<VkImage>);
	~VulkanSwapchain();
};

struct FrameObjects {
	VulkanImageView colourImageView;
	VkFramebuffer framebuffer;
	FrameObjects(ImageIndex, Statics const&, Dynamics const&);
};

struct DepthResources {
	VulkanImage image;
	VulkanImageView imageView;
	DepthResources(VkFormat, Extent<U32>, VkDevice, VmaAllocator);
	~DepthResources();
};

// these vulkan objects depend on the extent of the swapchain
// (so they get recreated when the swapchain gets recreated)
struct Dynamics {
	VulkanSwapchain swapchain;
	HeapArray<DepthResources> depthResources;
	VkRenderPass renderPass;
	// parallel arrays, each element is associated with a swapchain image
	HeapArray<VkFence> mapImageFence;
	HeapArray<FrameObjects> framesObjects;
	VulkanPipeline
		plainPipeline,
		groundPipeline;
	~Dynamics();
	Dynamics(Statics const&);
};

struct VulkanWindow {
	Statics statics;
	Dynamics dynamics;
	VulkanWindow(VulkanInstance const&);
	~VulkanWindow();
};

// === function declarations ===
struct Program;
void drawFrames(Program&);
void initGlfw();
void destroy(VulkanWindow&, VulkanInstance const&);
