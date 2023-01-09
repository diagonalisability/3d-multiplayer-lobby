#include<array>
#include<chrono>
#include<optional>
#include<unordered_map>
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include<glm/glm.hpp>
#include<glm/gtc/matrix_transform.hpp>
#include<stb/stb_image.h>
#include<tiny_obj_loader.h>
#include"client.hpp"
#include"vulkan.hpp"

// === struct definitions ===

struct ResizeCallback {
	virtual void operator()(U32 width, U32 height)= 0;
};

struct UniformBufferObject {
	glm::mat4 proj;
	Position cameraPos;
};

struct SwapchainAndFormat {
	VkSwapchainKHR swapchain;
	VkSurfaceFormatKHR format;
};

// === constants / variables ===

static bool constexpr shouldPrintVerboseVulkanInfo = false;
static bool constexpr shouldPrintCameraInfo = false;
static VkFormat constexpr loadedImageFormat= VK_FORMAT_R8G8B8A8_SRGB;
// Vulkan 1.1 is needed for vkGetPhysicalDeviceProperties2
static U32 constexpr minVulkanAPIVersion= shouldPrintVerboseVulkanInfo ? VK_API_VERSION_1_1 : VK_API_VERSION_1_0;

// === function definitions ===

template<typename F>
static void forEachLayer(VulkanInstance const &vkInstance, F &&f) {
	f(nullptr);
	for(std::string const &layerName : vkInstance.layerNames)
		f(layerName.c_str());
}

VulkanFence::VulkanFence(Tag::Null):
	o{VK_NULL_HANDLE}
{}
VulkanFence::VulkanFence(VkDevice const logicalDevice, VkFenceCreateFlagBits flags) {
	VkFenceCreateInfo createInfo{
		VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, // sType
		nullptr, // pNext
		flags, // flags
	};
	vkCreateFence(logicalDevice, &createInfo, nullptr, &o);
}
static bool isDestroyed(VulkanFence const &fence) {
	return fence.o == VK_NULL_HANDLE;
}
static void destroy(VulkanFence &fence, VkDevice const logicalDevice) {
	vkDestroyFence(logicalDevice, fence.o, nullptr);
	// mark as destroyed
	fence.o= VK_NULL_HANDLE;
}
VulkanFence::~VulkanFence() {
	ASSERT(isDestroyed(*this));
}
VulkanFence &VulkanFence::operator=(VulkanFence &&other) {
	ASSERT(isDestroyed(*this));
	o= other.o;
	other.o= VK_NULL_HANDLE;
	return *this;
}

VulkanCommandBuffer::VulkanCommandBuffer(Tag::Null):
	o{VK_NULL_HANDLE}
{}
VulkanCommandBuffer::VulkanCommandBuffer(
	VkCommandPool const commandPool,
	VkDevice const logicalDevice
):
	o{[=]{
		VkCommandBufferAllocateInfo const bufferAllocInfo{
			VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
			nullptr,
			commandPool, // commandPool
			VK_COMMAND_BUFFER_LEVEL_PRIMARY, // level
			1, // commandBufferCount
		};
		auto const ret= initWithDefaulted<VkCommandBuffer>([&](auto &ret) {
			vkAllocateCommandBuffers(logicalDevice, &bufferAllocInfo, &ret);
		});
		return ret;
	}()}
{}

static bool isDestroyed(VulkanCommandBuffer const &cmdBuf) {
	return cmdBuf.o == VK_NULL_HANDLE;
}
static void begin(VkCommandBuffer const cmdBuf) {
	VkCommandBufferBeginInfo const beginInfo{
		VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		nullptr,
		0, //VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, // flags
		nullptr, // pInheritanceInfo
	};
	vkBeginCommandBuffer(cmdBuf, &beginInfo);
}
static void submitAndWait(
	VkCommandBuffer const cmdBuf,
	VkFence const execFence,
	VulkanDevice const &device
) {
	vkEndCommandBuffer(cmdBuf);
	VkSubmitInfo const submitInfo{
		VK_STRUCTURE_TYPE_SUBMIT_INFO,
		nullptr,
		0, // waitSemaphoreCount
		nullptr, // pWaitSemaphores
		nullptr, // pWaitDstStageMask
		1, // commandBufferCount
		&cmdBuf, // pCommandBuffers
		0, // signalSemaphoreCount
		nullptr, // pSignalSemaphores
	};
	vkResetFences(device.logical, 1, &execFence);
	ASSERT_VK_SUCCESS(vkQueueSubmit(device.graphicsQueue.o, 1, &submitInfo, execFence));
	ASSERT_VK_SUCCESS(vkWaitForFences(device.logical, 1, &execFence, VK_FALSE, UINT64_MAX));
}
static void destroy(
	VulkanCommandBuffer &cmdBuf,
	VkCommandPool const commandPool,
	VkDevice const logicalDevice
) {
	vkFreeCommandBuffers(logicalDevice, commandPool, 1, &cmdBuf.o);
	cmdBuf.o= VK_NULL_HANDLE; // mark as destroyed
}
VulkanCommandBuffer::~VulkanCommandBuffer() {
	ASSERT(isDestroyed(*this));
}

VulkanCommandBuffer &VulkanCommandBuffer::operator=(VulkanCommandBuffer &&other) {
	ASSERT(isDestroyed(*this));
	o= other.o;
	other.o= VK_NULL_HANDLE;
	return *this;
}

template<typename F>
static void withImmediatelyExecutedCommandBuffer(
	F &&f,
	VulkanDevice const &device,
	VkCommandBuffer const cmdBuf,
	VkFence const execFence
) {
	begin(cmdBuf);
	f();
	submitAndWait(cmdBuf, execFence, device);
}

template<typename F>
static void withImmediatelyExecutedCommandBufferAutoFence(
	VkDevice const logicalDevice,
	VkCommandBuffer const cmdBuf,
	F &&f
) {
	VulkanFence fence{logicalDevice};
	ScopeExitGuard guard{[&]{ destroy(fence, logicalDevice); }};
	withImmediatelyExecutedCommandBuffer(logicalDevice, cmdBuf, fence, f);
}


static VkBool32 debugMessengerCallback(
	VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
	VkDebugUtilsMessageTypeFlagsEXT messageTypes,
	VkDebugUtilsMessengerCallbackDataEXT const *pCallbackData,
	void *pUserData
) {
	switch(messageSeverity) {
		case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT:
			std::cout << "VERB";
			break;
		case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:
			std::cout << "INFO";
			break;
		case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
			std::cout << "WARN";
			break;
		case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
			std::cout << "ERROR";
			break;
		default:
			std::cout << "OTHER";
	}
	std::cout << " [";
	bool isInitialIter= true;
	static std::tuple<VkDebugUtilsMessageTypeFlagsEXT, std::string> const messageTypesAndNames[] {
		{VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT, "general"},
		{VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT, "validation"},
		{VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT, "performance"}
	};
	for(auto const &[type, str] : messageTypesAndNames) if(messageTypes & type) {
		if(isInitialIter)
			isInitialIter= false;
		else
			std::cout << ", ";
		std::cout << str;
	}
	std::cout << "] " << pCallbackData->pMessageIdName << ": " << pCallbackData->pMessage << "\n" << std::flush;
	return VK_FALSE; // required by Vulkan
}

static void showInstanceExtensions(char const *layerName, char const *leading) {
	U32 extensionC;
	ASSERT_VK_SUCCESS(vkEnumerateInstanceExtensionProperties( // determine extension count
		layerName,
		&extensionC,
		nullptr
	));
	SizedArray<VkExtensionProperties, U16F> extensions{Tag::defaultInitialise, extensionC};
	ASSERT_VK_SUCCESS(vkEnumerateInstanceExtensionProperties( // actually fetch extension props
		layerName,
		&extensionC,
		getData(extensions)
	));
	if(extensions.size == 0)
		std::cout << leading << "<none>\n";
	for(U32 extensionI= 0; extensionI<extensions.size; ++extensionI) {
		VkExtensionProperties const &extension= extensions[extensionI];
		std::cout
			<< leading << extension.extensionName << " (v"
			<< extension.specVersion << ")\n";
	}
}

VulkanInstance::VulkanInstance(VkInstance const o, std::vector<std::string> &&layerNames):
	o{o},
	layerNames{layerNames}
{}

VulkanInstance::~VulkanInstance() {
	// https://www.khronos.org/registry/vulkan/specs/1.2-extensions/man/html/vkDestroyInstance.html
	// returns void (cannot fail)
	// "o" can be null
	vkDestroyInstance(o, nullptr);
}

VulkanInstance::VulkanInstance() {
	U32 apiVersion;
	ASSERT_VK_SUCCESS(vkEnumerateInstanceVersion(&apiVersion));
	std::cout
		<< "Vulkan API version: "
		<< OUTPUT_VERSION(apiVersion) << "\n";
	if constexpr(shouldPrintVerboseVulkanInfo) {
		std::cout << "Vulkan extensions:\n";
		showInstanceExtensions(nullptr, "\t");
	}
	U32 layerC;
	// determine layer count
	ASSERT_VK_SUCCESS(vkEnumerateInstanceLayerProperties(&layerC, nullptr));
	HeapArray<VkLayerProperties> layers{Tag::defaultInitialise, layerC};
	// actually fetch layer props
	ASSERT_VK_SUCCESS(vkEnumerateInstanceLayerProperties(&layerC, getData(layers)));
	for(U32 layerI=0; layerI<layerC; ++layerI) {
		auto const &layer= layers[layerI];
		layerNames.push_back(layer.layerName);
		if constexpr(shouldPrintVerboseVulkanInfo) {
			std::cout
				<< "layer " << layer.layerName << "\n"
				<< "\tspecification version: " << OUTPUT_VERSION(layer.specVersion) << "\n"
				<< "\timplementation version: " << layer.implementationVersion << "\n"
				<< "\tdescription: " << layer.description << "\n"
				<< "\textensions:\n";
			showInstanceExtensions(layer.layerName, "\t\t");
		}
	}
	VkValidationFeatureEnableEXT const enabledValidationFeatures[]= {
		VK_VALIDATION_FEATURE_ENABLE_BEST_PRACTICES_EXT,
		VK_VALIDATION_FEATURE_ENABLE_DEBUG_PRINTF_EXT,
		VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT
	};
	VkValidationFeaturesEXT const validationFeatures{
		VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT, // sType
		nullptr, // pNext
		// enabledValidationFeatureCount
		sizeof enabledValidationFeatures / sizeof *enabledValidationFeatures,
		enabledValidationFeatures, // pEnabledValidationFeatures
		0, // disabledValidationFeatureCount
		nullptr // pDisabledValidationFeatures
	};
	VkDebugUtilsMessengerCreateInfoEXT const debugMessengerCreateInfo{
		VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT, // sType
		&validationFeatures, // pNext
		0, // flags (required by Vulkan to be 0)
//		VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT
//		| VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT
		VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT
		| VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT, // messageSeverity
		VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT
		| VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
		| VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT, // messageType
		debugMessengerCallback, // pfnUserCallback
		nullptr // pUserData
	};
	char const *const enabledLayers[]= {
		"VK_LAYER_KHRONOS_validation",
	};
	U32 glfwExtensionC;
	char const *const *const glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionC);
	if constexpr(shouldPrintVerboseVulkanInfo) {
		std::cout << "GLFW extensions (" << glfwExtensionC << "):\n";
		for(U32 i = 0; i < glfwExtensionC; ++i)
			std::cout << "\t" << glfwExtensions[i] << "\n";
	}
	char const *const myEnabledExtensions[]= {
		VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME,
	};
	std::vector<char const*> enabledExtensions;
	enabledExtensions.reserve(glfwExtensionC + sizeof myEnabledExtensions / sizeof *myEnabledExtensions);
	for(U32 i = 0; i < glfwExtensionC; ++i)
		enabledExtensions.push_back(glfwExtensions[i]);
	for(U32 i = 0; i < sizeof myEnabledExtensions / sizeof *myEnabledExtensions; ++i)
		enabledExtensions.push_back(myEnabledExtensions[i]);
	VkApplicationInfo const appInfo{
		VK_STRUCTURE_TYPE_APPLICATION_INFO,
		nullptr, // pNext (required to be null by Vulkan)
		"my Vulkan application", // pApplicationName (maybe this shows up somewhere, who knows)
		0, // applicationVersion
		"my Vulkan engine", // pEngineName
		0, // engineVersion
		minVulkanAPIVersion, // apiVersion
	};
	// the VK_EXT_debug_report extension was deprecated by the
	// VK_EXT_debug_utils extension, so I use that instead
	VkInstanceCreateInfo const createInfo{
		VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, // sType
		&debugMessengerCreateInfo, // pNext
		0, // flags
		&appInfo, // pApplicationInfo
		// enabledLayerCount
		sizeof enabledLayers / sizeof *enabledLayers,
		enabledLayers, // ppEnabledLayerNames
		// enabledExtensionCount
		static_cast<U32>(enabledExtensions.size()),
		enabledExtensions.data(), // ppEnabledExtensionNames
	};
	ASSERT_VK_SUCCESS(vkCreateInstance(&createInfo, nullptr, &o));
}

static bool isDestroyed(VulkanSurface const &surface) {
	return surface.o == VK_NULL_HANDLE;
}

VulkanSurface::VulkanSurface(VkSurfaceKHR const o): o{o} {}

VulkanSurface::VulkanSurface(VulkanSurface &&other) {
	o= other.o;
	// mark as destroyed
	other.o= VK_NULL_HANDLE;
}

VulkanSurface::~VulkanSurface() {
	ASSERT(isDestroyed(*this));
}

static void destroy(VulkanSurface &surface, VulkanInstance const &instance) {
	vkDestroySurfaceKHR(instance.o, surface.o, nullptr);
	// mark as destroyed
	surface.o= VK_NULL_HANDLE;
}

VulkanDevice::VulkanDevice(
	VkPhysicalDevice const physical,
	VkDevice const logical,
	VulkanQueue const &graphicsQueue,
	VulkanQueue const &presentQueue,
	VkPhysicalDeviceProperties const &deviceProperties,
	VkPhysicalDeviceMemoryProperties const &memoryProperties,
	VkPhysicalDeviceFeatures const &featureSupport
):
	physical{physical},
	logical{logical},
	graphicsQueue{graphicsQueue},
	presentQueue{presentQueue},
	deviceProperties{deviceProperties},
	memoryProperties{memoryProperties},
	featureSupport{featureSupport}
{}

VulkanDevice::VulkanDevice(VulkanDevice &&other):
	physical{other.physical},
	logical{other.logical},
	graphicsQueue{other.graphicsQueue},
	presentQueue{other.presentQueue},
	deviceProperties{other.deviceProperties},
	memoryProperties{other.memoryProperties},
	featureSupport{other.featureSupport}
{
	other.logical= VK_NULL_HANDLE; // mark as destroyed
}

VulkanDevice::~VulkanDevice() {
	if(logical != VK_NULL_HANDLE)
		vkDestroyDevice(logical, nullptr);
}

VulkanDevice::VulkanDevice(VulkanInstance const &vkInstance, VulkanSurface const &surface): VulkanDevice{[&]{
	U32 pDeviceC;
	// determine device count
	ASSERT_VK_SUCCESS(vkEnumeratePhysicalDevices(vkInstance.o, &pDeviceC, nullptr));
	HeapArray<VkPhysicalDevice> pDevices{Tag::defaultInitialise, pDeviceC};
	// actually fetch devices
	ASSERT_VK_SUCCESS(vkEnumeratePhysicalDevices(vkInstance.o, &pDeviceC, getData(pDevices)));
	struct SelectedPhysicalDevice {
		VkPhysicalDevice o;
		U32 graphicsQueueFamilyI;
		U32 presentQueueFamilyI;
		VkPhysicalDeviceProperties deviceProperties;
		VkPhysicalDeviceFeatures featureSupport;
	};
	std::optional<SelectedPhysicalDevice> selectedPDevice;
	for(U32 pDeviceI=0; pDeviceI<pDeviceC; ++pDeviceI) {
		auto const pDevice= pDevices[pDeviceI];
		auto const featureSupport= initWithDefaulted<VkPhysicalDeviceFeatures>([pDevice](auto &features) {
			vkGetPhysicalDeviceFeatures(pDevice, &features);
		});
		VkPhysicalDeviceProperties const deviceProps= [pDevice]{
			if constexpr(!shouldPrintVerboseVulkanInfo) {
				VkPhysicalDeviceProperties ret;
				vkGetPhysicalDeviceProperties(pDevice, &ret);
				return ret;
			}
			VkPhysicalDeviceDriverProperties pDeviceDriverProps;
			pDeviceDriverProps.sType= VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES;
			pDeviceDriverProps.pNext= nullptr;
			VkPhysicalDeviceProperties2 deviceProps2;
			deviceProps2.sType= VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2; // sType
			deviceProps2.pNext= &pDeviceDriverProps;
			vkGetPhysicalDeviceProperties2(pDevice, &deviceProps2); // returns void
			std::cout
				<< "device:\n"
				<< "\tAPI version: " << OUTPUT_VERSION(deviceProps2.properties.apiVersion) << "\n"
				<< "\tdriver version: " << deviceProps2.properties.driverVersion << "\n"
				<< "\ttype: " << mapVulkanPhysicalDeviceTypeToString.at(deviceProps2.properties.deviceType) << "\n"
				<< "\tdriver name: " << pDeviceDriverProps.driverName << "\n"
				<< "\tdriver info: " << pDeviceDriverProps.driverInfo << "\n"
				<< "\tqueue families:\n";
			return deviceProps2.properties;
		}();
		U32 queueFamilyC;
		// determine queue family count
		vkGetPhysicalDeviceQueueFamilyProperties(pDevice, &queueFamilyC, nullptr);
		HeapArray<VkQueueFamilyProperties> queueFamilies{Tag::defaultInitialise, queueFamilyC};
		// actually fetch queue families
		vkGetPhysicalDeviceQueueFamilyProperties(pDevice, &queueFamilyC, getData(queueFamilies));
		U32
			graphicsQueueFamilyI= ~0u,
			presentQueueFamilyI= ~0u;
		for(U32 queueFamilyI= 0; queueFamilyI<queueFamilyC; ++queueFamilyI) {
			// TODO: check if one of the queue families "supports presentation to a Wayland compositor"
			// https://web.archive.org/web/20211225022950/https://www.khronos.org/registry/vulkan/specs/1.2-extensions/html/chap33.html#vkGetPhysicalDeviceWaylandPresentationSupportKHR
			VkQueueFamilyProperties const queueFamily= queueFamilies[queueFamilyI];
			if(VK_QUEUE_GRAPHICS_BIT & queueFamily.queueFlags)
				graphicsQueueFamilyI= queueFamilyI;
			VkBool32 isPresentationSupported= false;
			vkGetPhysicalDeviceSurfaceSupportKHR(pDevice, queueFamilyI, surface.o, &isPresentationSupported);
			if(isPresentationSupported)
				presentQueueFamilyI= queueFamilyI;
			bool isInitial= true;
			if constexpr(shouldPrintVerboseVulkanInfo) {
				std::cout << "\t\tfamily with " << queueFamily.queueCount << " queues, flags: [";
				static std::tuple<VkQueueFlagBits, std::string> const queueFlagsAndNames[] {
					{VK_QUEUE_GRAPHICS_BIT, "graphics"},
					{VK_QUEUE_COMPUTE_BIT, "compute"},
					{VK_QUEUE_TRANSFER_BIT, "transfer"},
					{VK_QUEUE_SPARSE_BINDING_BIT, "sparse binding"},
					{VK_QUEUE_PROTECTED_BIT, "protected"}
				};
				for(auto const &[flag, str] : queueFlagsAndNames) if(queueFamily.queueFlags & flag) {
					if(isInitial)
						isInitial= false;
					else
						std::cout << ", ";
					std::cout << str;
				}
				std::cout << "]\n";
			}
		}
		if(graphicsQueueFamilyI != ~0u && presentQueueFamilyI != ~0u)
			selectedPDevice= SelectedPhysicalDevice{
				pDevice,
				graphicsQueueFamilyI,
				presentQueueFamilyI,
				deviceProps,
				featureSupport,
			};
		if constexpr(shouldPrintVerboseVulkanInfo)
			forEachLayer(vkInstance, [pDevice](char const *layerName) {
				U32 extensionC;
				// determine extension count
				vkEnumerateDeviceExtensionProperties(pDevice, layerName, &extensionC, nullptr);
				HeapArray<VkExtensionProperties> extensions{Tag::defaultInitialise, extensionC};
				// actually fetch extensions
				vkEnumerateDeviceExtensionProperties(pDevice, layerName, &extensionC, getData(extensions));
				std::cout << "\textensions for " << (layerName ? layerName : "Vulkan implementation") << ":\n";
				if(!extensionC)
					std::cout << "\t\t<none>\n";
				for(U32 i=0; i<extensionC; ++i) std::cout
					<< "\t\t" << extensions[i].extensionName
					<< " (v" << extensions[i].specVersion << ")\n";
			});
	}
	ASSERT(selectedPDevice.has_value());
	auto const &selectedPDeviceValue = *selectedPDevice;
	VkDevice lDevice;
	std::unordered_set<U32> createdQueues; // stores queue family indices
	std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
	float const priority= 1.0;
	U32 const queueFamilyIs[] {
		selectedPDeviceValue.graphicsQueueFamilyI,
		selectedPDeviceValue.presentQueueFamilyI
	};
	for(U32 queueFamilyI : queueFamilyIs) {
		if(contains(createdQueues, queueFamilyI))
			continue;
		createdQueues.insert(queueFamilyI);
		queueCreateInfos.push_back({
			VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, // sType
			nullptr, // pNext
			0, // flags
			queueFamilyI, // queueFamilyIndex
			1, // queueCount
			&priority // pQueuePriorities
		});
	}
	char const *const enabledExtensions[]= {
		VK_KHR_SWAPCHAIN_EXTENSION_NAME
	};
	VkBool32 const shouldEnableAnisotropicFiltering = selectedPDeviceValue.featureSupport.samplerAnisotropy;
	// only enable anisotropic filtering, if it's available
	VkPhysicalDeviceFeatures const enabledFeatures{
		VK_FALSE, VK_FALSE, VK_FALSE, VK_FALSE, VK_FALSE, VK_FALSE, VK_FALSE, VK_FALSE, VK_FALSE, VK_FALSE, VK_FALSE, VK_FALSE, VK_FALSE, VK_FALSE, VK_FALSE, VK_FALSE, VK_FALSE, VK_FALSE, VK_FALSE,
		shouldEnableAnisotropicFiltering,
		VK_FALSE, VK_FALSE, VK_FALSE, VK_FALSE, VK_FALSE, VK_FALSE, VK_FALSE, VK_FALSE, VK_FALSE, VK_FALSE, VK_FALSE, VK_FALSE, VK_FALSE, VK_FALSE, VK_FALSE, VK_FALSE, VK_FALSE, VK_FALSE, VK_FALSE, VK_FALSE, VK_FALSE, VK_FALSE, VK_FALSE, VK_FALSE, VK_FALSE, VK_FALSE, VK_FALSE, VK_FALSE, VK_FALSE, VK_FALSE, VK_FALSE, VK_FALSE, VK_FALSE, VK_FALSE, VK_FALSE
	};
	VkDeviceCreateInfo const lDeviceCreateInfo{
		VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, // sType
		nullptr, // pNext
		0,  // flags (must be 0 for Vulkan)
		static_cast<U32>(queueCreateInfos.size()), // queueCreateInfoCount
		&queueCreateInfos[0], // pQueueCreateInfos
		0, // enabledLayerCount (ignored by Vulkan)
		nullptr, // ppEnabledLayerNames (ignored by Vulkan)
		sizeof enabledExtensions / sizeof *enabledExtensions, // enabledExtensionCount
		enabledExtensions, // ppEnabledExtensionNames
		&enabledFeatures // pEnabledFeatures
	};
	ASSERT_VK_SUCCESS(vkCreateDevice(selectedPDeviceValue.o, &lDeviceCreateInfo, nullptr, &lDevice));
	VkQueue graphicsQueue;
	VkQueue presentQueue;
	vkGetDeviceQueue(
		lDevice,
		selectedPDeviceValue.graphicsQueueFamilyI,
		0, // queueIndex
		&graphicsQueue
	);
	vkGetDeviceQueue(
		lDevice,
		selectedPDeviceValue.presentQueueFamilyI,
		0, // queueIndex
		&presentQueue
	);
	
	return VulkanDevice {
		selectedPDeviceValue.o,
		lDevice,
		{ graphicsQueue, selectedPDevice->graphicsQueueFamilyI },
		{ presentQueue, selectedPDevice->presentQueueFamilyI },
		selectedPDeviceValue.deviceProperties,
		initWithDefaulted<VkPhysicalDeviceMemoryProperties>([&](auto &memProps) {
			vkGetPhysicalDeviceMemoryProperties(selectedPDeviceValue.o, &memProps);
		}),
		selectedPDeviceValue.featureSupport,
	};
}()} {}

static U32 findSuitableMemoryTypeI(
	U32 const possibleMemoryTypeBits,
	VkMemoryPropertyFlags const requiredMemoryPropertyFlags,
	VkPhysicalDeviceMemoryProperties const &memoryProperties
) {
	U32 ret=0;
	for(; ret < memoryProperties.memoryTypeCount; ++ret) if(
		possibleMemoryTypeBits & (1<<ret)
		&& requiredMemoryPropertyFlags == (
			requiredMemoryPropertyFlags
			& memoryProperties.memoryTypes[ret].propertyFlags
		)
	)
		return ret;
	ASSERT(false); // we expect to find a suitable memory type
}

static bool isDestroyed(VulkanBuffer const &buf) {
	return buf.o == VK_NULL_HANDLE;
}
static void destroy(VulkanBuffer &buf, VmaAllocator const allocator) {
	vmaDestroyBuffer(allocator, buf.o, buf.allocation);
	// mark as destroyed
	buf.o= VK_NULL_HANDLE;
}
VulkanBuffer::~VulkanBuffer() {
	ASSERT(isDestroyed(*this));
}

// note that graphics queues are also implicitly transfer queues, so if we have a graphics queue already,
// we don't need to explicitly search for a transfer queue (and we don't need to worry about sharing
// resources between device queues)
static void copy(
	VulkanBuffer const &src,
	VulkanBuffer const &dest,
	std::size_t size,
	VkCommandBuffer const cmdBufToUse,
	VulkanDevice const &device,
	VkFence const fence
) {
	withImmediatelyExecutedCommandBuffer([&]{
		VkBufferCopy const copyRegion{
			0, // srcOffset
			0, // dstOffset
			size, // size
		};
		vkCmdCopyBuffer(cmdBufToUse, src.o, dest.o, 1, &copyRegion);	
	}, device, cmdBufToUse, fence);
}

static void recordCopy(
	VulkanBuffer const &src,
	VkImage const &dst,
	U32 width,
	U32 height,
	VkCommandBuffer const &commandBuffer
) {
	VkBufferImageCopy const region{
		0, // bufferOffset
		0, // bufferRowLength
		0, // bufferImageHeight
		{ // imageSubresource
			VK_IMAGE_ASPECT_COLOR_BIT, // aspectMask
			0, // mipLevel
			0, // baseArrayLayer
			1, // layerCount
		},
		{0, 0, 0}, // imageOffset
		{width, height, /* depth */ 1}, // imageExtent
	};
	vkCmdCopyBufferToImage(
		commandBuffer,
		src.o,
		dst,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		1,
		&region
	);
}

static VulkanBuffer createStagingBuffer(std::size_t const size, VmaAllocator const allocator, void *&mem) {
	VmaAllocationInfo allocInfo;
	VulkanBuffer ret{
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
		VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, 0,
		size,
		1,
		allocator,
		&allocInfo,
	};
	mem= allocInfo.pMappedData;
	return ret;
}

template<typename T, typename Size>
static VulkanBuffer createVertexOrIndexBufferH(
	ArrayView<T, true, Size> const data,
	VkBufferUsageFlags const bufferType,
	VmaAllocator const allocator,
	VkCommandBuffer const stagingCopyCmdBuf,
	VkFence const stagingCopyFence,
	VulkanDevice const &device
) {
	VkDeviceSize const bufferSize= data.size * sizeof(T);
	void *stagingMem;
	VulkanBuffer stagingBuffer{ createStagingBuffer(bufferSize, allocator, stagingMem) };
	ScopeExitGuard g0{[&stagingBuffer,allocator]{ destroy(stagingBuffer, allocator); }};
	VulkanBuffer finalBuffer{
		static_cast<VkBufferUsageFlags>(VK_BUFFER_USAGE_TRANSFER_DST_BIT) | bufferType,
		VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
		0,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
		0,
		bufferSize,
		0,
		allocator,
		nullptr
	};
	std::memcpy(stagingMem, getData(data), bufferSize);
	copy(
		stagingBuffer,
		finalBuffer,
		bufferSize,
		stagingCopyCmdBuf,
		device,
		stagingCopyFence
	);
	return finalBuffer;	
}

typedef U32F VerticesSize;
template<typename Vertex>
using VerticesView=  ArrayView<Vertex, true, VerticesSize>;
template<typename Vertex>
static VulkanBuffer createVertexBuffer(
	VerticesView<Vertex> const vertices,
	VmaAllocator const allocator,
	VkCommandBuffer const stagingCopyCmdBuf,
	VkFence const stagingCopyFence,
	VulkanDevice const &device
) {
	return createVertexOrIndexBufferH(
		vertices,
		VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
		allocator,
		stagingCopyCmdBuf,
		stagingCopyFence,
		device
	);
}
typedef U32F IndicesSize;
typedef ArrayView<U32, true, IndicesSize> IndicesView;
static VulkanBuffer createIndexBuffer(
	IndicesView const indices,
	VmaAllocator const allocator,
	VkCommandBuffer const stagingCopyCmdBuf,
	VkFence const stagingCopyFence,
	VulkanDevice const &device
) {
	return createVertexOrIndexBufferH(
		indices,
		VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
		allocator,
		stagingCopyCmdBuf,
		stagingCopyFence,
		device
	);
}

VulkanBuffer::VulkanBuffer(VulkanBuffer &&other):
	o{other.o},
	allocation{other.allocation}
{
	other.o= VK_NULL_HANDLE; // mark other as destroyed
}

static void createVulkanBuffer(
	VkBuffer &buf, // out
	VmaAllocation &alloc, // out
	VmaAllocationInfo *const allocInfo, // out (optional, can set to null)
	VkBufferUsageFlags const bufferUsageFlags,
	VmaMemoryUsage const memoryUsageFlags,
	VmaAllocationCreateFlags const allocationFlags,
	VkMemoryPropertyFlags const requiredMemoryPropertyFlags,
	VkMemoryPropertyFlags const preferredMemoryPropertyFlags,
	VkDeviceSize const size,
	VkDeviceSize const minAlignment,
	VmaAllocator const allocator
) {
	VkBufferCreateInfo createInfo;
	createInfo.sType= VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	createInfo.pNext= nullptr;
	createInfo.flags= 0;
	createInfo.size= size;
	createInfo.usage= bufferUsageFlags;
	createInfo.sharingMode= VK_SHARING_MODE_EXCLUSIVE;
	// createInfo.queueFamilyIndexCount and createInfo.pQueueFamilyIndices uninitialised
	VmaAllocationCreateInfo allocCreateInfo;
	allocCreateInfo.flags= allocationFlags;
	allocCreateInfo.usage= memoryUsageFlags;
	allocCreateInfo.requiredFlags= requiredMemoryPropertyFlags;
	allocCreateInfo.preferredFlags= preferredMemoryPropertyFlags;
	allocCreateInfo.memoryTypeBits= 0;
	allocCreateInfo.pool= VK_NULL_HANDLE;
	// allocInfo.pUserData and allocInfo.priority uninitialised
	vmaCreateBufferWithAlignment(
		allocator,
		&createInfo,
		&allocCreateInfo,
		minAlignment,
		&buf,
		&alloc,
		allocInfo
	);
}
VulkanBuffer::VulkanBuffer(
	VkBufferUsageFlags const bufferUsageFlags,
	VmaMemoryUsage const memoryUsageFlags,
	VmaAllocationCreateFlags const allocationFlags,
	VkMemoryPropertyFlags const requiredMemoryPropertyFlags,
	VkMemoryPropertyFlags const preferredMemoryPropertyFlags,
	VkDeviceSize const size,
	VkDeviceSize const minAlignment,
	VmaAllocator const allocator,
	VmaAllocationInfo *const allocInfo
) { createVulkanBuffer(
	o,
	allocation,
	allocInfo,
	bufferUsageFlags,
	memoryUsageFlags,
	allocationFlags,
	requiredMemoryPropertyFlags,
	preferredMemoryPropertyFlags,
	size,
	minAlignment,
	allocator
); }

static void recreate(
	VulkanBuffer &buf,
	VmaAllocation &alloc,
	VkBufferUsageFlags const bufferUsageFlags,
	VmaMemoryUsage const memoryUsageFlags,
	VmaAllocationCreateFlags const allocationFlags,
	VkMemoryPropertyFlags const requiredMemoryPropertyFlags,
	VkMemoryPropertyFlags const preferredMemoryPropertyFlags,
	VkDeviceSize const size,
	VkDeviceSize const minAlignment,
	VmaAllocationInfo *const allocInfo,
	VmaAllocator const allocator
) {
	vmaDestroyBuffer(allocator, buf.o, alloc);
	createVulkanBuffer(
		buf.o,
		alloc,
		allocInfo,
		bufferUsageFlags,
		memoryUsageFlags,
		allocationFlags,
		requiredMemoryPropertyFlags,
		preferredMemoryPropertyFlags,
		size,
		minAlignment,
		allocator
	);
}

VulkanBuffer &VulkanBuffer::operator=(VulkanBuffer &&other) {
	// this object can't destroy itself, so it must already be destroyed to be
	// assigned-to
	ASSERT(isDestroyed(*this));
	o= other.o;
	allocation= other.allocation;
	other.o= VK_NULL_HANDLE; // mark other as destroyed
	return *this;
}

static bool isDestroyed(VulkanImage const &image) {
	return VK_NULL_HANDLE == image.o;
}
static void destroy(VulkanImage &image, VmaAllocator const allocator) {
	vmaDestroyImage(allocator, image.o, image.allocation);
	image.o= VK_NULL_HANDLE; // mark as destroyed
}
VulkanImage::~VulkanImage() {
	ASSERT(isDestroyed(*this));
}

VulkanImage::VulkanImage(VulkanImage &&other):
	o{other.o},
	allocation{other.allocation}
{
	other.o= VK_NULL_HANDLE; // mark as destroyed
}

VulkanImage &VulkanImage::operator=(VulkanImage &&other) {
	ASSERT(isDestroyed(*this));
	o= other.o;
	allocation= other.allocation;
	other.o= VK_NULL_HANDLE;
	return *this;
}

VulkanImage::VulkanImage(
	Extent<U32> const extent,
	VkFormat const format,
	VkImageTiling const tiling,
	VkImageUsageFlags const usage,
	VmaAllocator const allocator
) {
	auto const createInfo = initWithDefaulted<VkImageCreateInfo>([extent,format,usage](auto &ret){
		ret.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		ret.pNext = nullptr;
		ret.flags = 0;
		ret.imageType = VK_IMAGE_TYPE_2D;
		ret.format = format;
		ret.extent = {
			extent.width,
			extent.height,
			/* depth */ 1
		};
		ret.mipLevels = 1;
		ret.arrayLayers = 1;
		ret.samples = VK_SAMPLE_COUNT_1_BIT;
		ret.tiling = VK_IMAGE_TILING_OPTIMAL;
		ret.usage = usage; // usage
		ret.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		// queueFamilyIndexCount and pQueueFamilyIndices omitted
		ret.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	});
	VmaAllocationCreateInfo allocInfo;
	allocInfo.flags= VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
	allocInfo.usage= VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE; // another "usage"? weird naming
	allocInfo.requiredFlags= 0;
	allocInfo.preferredFlags= 0;
	allocInfo.memoryTypeBits= 0;
	allocInfo.pool= VK_NULL_HANDLE;
	// allocInfo.pUserData and allocInfo.priority uninitialised
	ASSERT_VK_SUCCESS(vmaCreateImage(
		allocator, &createInfo, &allocInfo,
		&o, &allocation, nullptr
	));
}

static void recordImageLayoutTransition(
	VkCommandBuffer const commandBuffer,
	VkImage const image,
	VkImageLayout const oldLayout, VkImageLayout const newLayout,
	VkPipelineStageFlags const srcStages, VkAccessFlags const srcAccesses,
	VkPipelineStageFlags const dstStages, VkPipelineStageFlags const dstAccesses
) {
	VkImageMemoryBarrier const imageMemoryBarrier{
		VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		nullptr,
		srcAccesses, dstAccesses, // srcAccessMask, dstAccessMask
		oldLayout, newLayout,
		VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, // not transferring family ownership
		image,
		{ // subresourceRange
			VK_IMAGE_ASPECT_COLOR_BIT, // aspectMask
			0, // baseMipLevel
			1, // levelCount
			0, // baseArrayLayer
			1, // layerCount
		},
	};
	vkCmdPipelineBarrier(
		commandBuffer,
		srcStages, dstStages, // srcStageMask, dstStageMask
		0, // dependencyFlags
		0, // memoryBarrierCount
		nullptr, // pMemoryBarriers
		0, // bufferMemoryBarrierCount
		nullptr, // pBufferMemoryBarriers
		1, // imageMemoryBarrierCount
		&imageMemoryBarrier // pImageMemoryBarriers
	);
}

VulkanImage::VulkanImage(
	VulkanImageParams const &params,
	VmaAllocator const allocator,
	VkCommandBuffer const cmdBufForLoading,
	VkFence const fenceForLoading,
	VulkanDevice const &device
):
	VulkanImage{
		params.extent,
		loadedImageFormat,
		VK_IMAGE_TILING_OPTIMAL,
		VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
		allocator
	}
{
	VkDeviceSize const size = params.extent.height * params.extent.width * 4;		
	void *stagingMem;
	VulkanBuffer stagingBuffer{ createStagingBuffer(size, allocator, stagingMem) };
	ScopeExitGuard g{[&stagingBuffer,allocator]{ destroy(stagingBuffer, allocator); }};
	std::memcpy(stagingMem, params.imageData.get(), size);
	withImmediatelyExecutedCommandBuffer([&](){
		recordImageLayoutTransition(
			cmdBufForLoading,
			o,
			VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0,
			VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT
		);
		recordCopy(stagingBuffer, o, params.extent.width, params.extent.height, cmdBufForLoading);
		recordImageLayoutTransition(
			cmdBufForLoading,
			o,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT
		);
	}, device, cmdBufForLoading, fenceForLoading);
}

void VulkanImageParamDataDeleter::operator()(char unsigned *const imageData) {
	stbi_image_free(imageData);
}

VulkanImage::VulkanImage(
	PathStringView const imagePath,
	VmaAllocator const allocator,
	VkCommandBuffer const cmdBufForLoading,
	VkFence const fenceForLoading,
	VulkanDevice const &device
):
	VulkanImage{[path=getData(imagePath)]{
		signed width, height, channelC;
		VulkanImageParamData imageData {stbi_load(path, &width, &height, &channelC, STBI_rgb_alpha)};
		ASSERT(imageData);
		ASSERT(0 <= width && 0 <= height && 0 <= channelC);
		return VulkanImageParams{
			std::move(imageData),
			{ static_cast<U32>(width), static_cast<U32>(height) } // extent
		};
	}(), allocator, cmdBufForLoading, fenceForLoading, device}
{
//	std::cout << "image loaded from path " << getData(imagePath) << "\n";
}

VulkanImageView::VulkanImageView(Tag::Null):
	o{VK_NULL_HANDLE}
{}

VulkanImageView::VulkanImageView(
	VkImage const image,
	VkFormat const format,
	VkImageAspectFlags const aspect,
	VkDevice const logicalDevice
):
	o{initWithDefaulted<VkImageView>([&,format](auto &view) {
		VkImageViewCreateInfo const createInfo {
			VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
			nullptr,
			0,
			image,
			VK_IMAGE_VIEW_TYPE_2D,
			format,
			{ // componentMapping
				VK_COMPONENT_SWIZZLE_IDENTITY, // r
				VK_COMPONENT_SWIZZLE_IDENTITY, // g
				VK_COMPONENT_SWIZZLE_IDENTITY, // b
				VK_COMPONENT_SWIZZLE_IDENTITY, // a
			},
			{ // subresourceRange
				aspect,
				0,
				1,
				0,
				1,
			}
		};
		ASSERT_VK_SUCCESS(vkCreateImageView(logicalDevice, &createInfo, nullptr, &view));
	})}
{}

static bool isDestroyed(VulkanImageView const &image) {
	return image.o == VK_NULL_HANDLE;
}
static void destroy(VulkanImageView &view, VkDevice const logicalDevice) {
	vkDestroyImageView(logicalDevice, view.o, nullptr);
	view.o= VK_NULL_HANDLE; // mark as destroyed
}
VulkanImageView::~VulkanImageView() {
	ASSERT(isDestroyed(*this));
}

VulkanImageView &VulkanImageView::operator=(VulkanImageView &&other) {
	ASSERT(isDestroyed(*this));
	o= other.o;
	other.o= VK_NULL_HANDLE;
	return *this;
}

static SwapchainAndFormat createSwapchain(Statics const &statics, VkSwapchainKHR const oldSwapchain) {
	VkSurfaceCapabilitiesKHR surfaceCapabilities;
	ASSERT_VK_SUCCESS(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
		statics.device.physical,
		statics.surface.o,
		&surfaceCapabilities
	));
	if constexpr(shouldPrintVerboseVulkanInfo)
		std::cout
			<< "surface capabilities:"
			<< "\n\tminImageCount: " << surfaceCapabilities.minImageCount
			<< "\tmaxImageCount: " << surfaceCapabilities.maxImageCount
			<< "\n\tcurrentExtent: 0x" << std::setbase(16)
				<< surfaceCapabilities.currentExtent.width << " x 0x"
				<< surfaceCapabilities.currentExtent.height
			<< "\n\tminImageExtent: " << std::setbase(10)
				<< surfaceCapabilities.minImageExtent.width << " x "
				<< surfaceCapabilities.minImageExtent.height
			<< "\n\tmaxImageExtent: " << std::setbase(10)
				<< surfaceCapabilities.maxImageExtent.width << " x "
				<< surfaceCapabilities.maxImageExtent.height
			<< "\n";
	if constexpr(shouldPrintVerboseVulkanInfo) {
		std::cout << "\tcurrentTransform:\n";
		for(auto const &[value, str] : mapVulkanSurfaceTransformToString)
			if(value & surfaceCapabilities.currentTransform)
				std::cout << "\t\t" << str << "\n";
	}
	U32 formatC;
	// determine number of formats
	ASSERT_VK_SUCCESS(vkGetPhysicalDeviceSurfaceFormatsKHR(
		statics.device.physical,
		statics.surface.o,
		&formatC,
		nullptr
	));
	ASSERT(formatC);
	HeapArray<VkSurfaceFormatKHR> formats{Tag::defaultInitialise, formatC};
	// actually fetch formats
	ASSERT_VK_SUCCESS(vkGetPhysicalDeviceSurfaceFormatsKHR(
		statics.device.physical,
		statics.surface.o,
		&formatC,
		getData(formats)
	));
	if constexpr(shouldPrintVerboseVulkanInfo) {
		std::cout << "colourspace formats supported by the surface (<VkFormat> : <VkColorSpace>):\n";
		for(U32 i=0; i<formatC; ++i) std::cout
			<< "\t"
			<< (contains(mapVulkanFormatToString, formats[i].format)
				? mapVulkanFormatToString.at(formats[i].format)
				: "unknown format"
			)
			<< " : "
			<< (contains(mapVulkanColourspaceToString, formats[i].colorSpace)
				? mapVulkanColourspaceToString.at(formats[i].colorSpace)
				: "unknown colourspace"
			)
			<< "\n";
	}
	VkSurfaceFormatKHR const chosenFormat= [&formats,formatC]{
		// look for preferred format and colourspace
		for(U32 formatI=0; formatI<formatC; ++formatI) {
			auto const format= formats[formatI];
			if(format.format == VK_FORMAT_R8G8B8A8_SRGB && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
				if constexpr(shouldPrintVerboseVulkanInfo)
					std::cout << "got preferred swapchain format\n";
				return format;
			}
		}
		// look for only preferred colourspace
		for(U32 formatI=0; formatI<formatC; ++formatI) {
			auto const format= formats[formatI];
			if(format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
				return format;
		}
		// just use whatever format is given
		return formats[0];
	}();
	U32 presentModeC;
	ASSERT_VK_SUCCESS(vkGetPhysicalDeviceSurfacePresentModesKHR(
		statics.device.physical,
		statics.surface.o,
		&presentModeC,
		nullptr
	));
	SizedArray presentModes{
		Tag::defaultInitialise,
		static_cast<U8F>(presentModeC),
		Tag::elementTypeHint<VkPresentModeKHR>
	};
	ASSERT_VK_SUCCESS(vkGetPhysicalDeviceSurfacePresentModesKHR(
		statics.device.physical,
		statics.surface.o,
		&presentModeC,
		getData(presentModes)
	));
	if constexpr(shouldPrintVerboseVulkanInfo) {
		std::cout << "present modes supported by the surface:\n";
		for(VkPresentModeKHR const presentMode : presentModes) std::cout
			<< "\t"
			<< (contains(mapVulkanPresentModeToString, presentMode)
				? mapVulkanPresentModeToString.at(presentMode)
				: "unknown present mode"
			)
			<< "\n";
	}
	VkSwapchainCreateInfoKHR const createInfo{
		VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR, // sType
		nullptr, // pNext
		0, // flags
		statics.surface.o, // surface
		std::max(2u, surfaceCapabilities.minImageCount), // minImageCount
		chosenFormat.format, // imageFormat
		chosenFormat.colorSpace, // imageColorSpace
		statics.extent, // imageExtent
		1, // imageArrayLayers (not a stereoscopic-3D)
		VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, // imageUsage
		VK_SHARING_MODE_EXCLUSIVE, // imageSharingMode
		0, // queueFamilyIndexCount
		nullptr, // pQueueFamilyIndices
		surfaceCapabilities.currentTransform, // preTransform
		VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR, // compositeAlpha
		VK_PRESENT_MODE_FIFO_KHR, // presentMode
		false, // clipped
		oldSwapchain, // oldSwapchain
	};
	VkSwapchainKHR swapchain;
	vkCreateSwapchainKHR(
		statics.device.logical,
		&createInfo,
		nullptr, // pAllocator
		&swapchain
	);
	if(oldSwapchain != VK_NULL_HANDLE)
		// destroy the retired swapchain, because that's not done automatically
		vkDestroySwapchainKHR(
			statics.device.logical,
			oldSwapchain,
			nullptr // pAllocator
		);
	return {
		swapchain,
		chosenFormat
	};
}

VulkanSwapchain::VulkanSwapchain(
	VkSwapchainKHR const o,
	VkSurfaceFormatKHR const surfaceFormat,
	FastImagesSize imageC,
	HeapArray<VkImage> images
):
	o{o},
	surfaceFormat{surfaceFormat},
	imageC{imageC},
	images{std::move(images)}
{}
VulkanSwapchain::VulkanSwapchain(VulkanSwapchain &&other):
	o{other.o},
	surfaceFormat{other.surfaceFormat},
	imageC{other.imageC},
	images{std::move(other.images)}
{
	other.o= VK_NULL_HANDLE; // mark as destroyed
}
VulkanSwapchain::VulkanSwapchain(Statics const &statics): VulkanSwapchain{[&statics]{
	SwapchainAndFormat saf= createSwapchain(statics, VK_NULL_HANDLE);
	U32 imageC;
	vkGetSwapchainImagesKHR(statics.device.logical, saf.swapchain, &imageC, nullptr);
	HeapArray<VkImage> images{Tag::defaultInitialise, imageC};
	vkGetSwapchainImagesKHR(statics.device.logical, saf.swapchain, &imageC, getData(images));
	return VulkanSwapchain{
		saf.swapchain,
		saf.format,
		static_cast<FastImagesSize>(imageC),
		std::move(images)
	};
}()} {}
static bool isDestroyed(VulkanSwapchain const &swapchain) {
	return swapchain.o == VK_NULL_HANDLE;
}
static void destroy(VulkanSwapchain &swapchain, VulkanDevice const &device) {
	vkDestroySwapchainKHR(device.logical, swapchain.o, nullptr);
	swapchain.o= VK_NULL_HANDLE; // mark as destroyed
}
VulkanSwapchain::~VulkanSwapchain() {
	ASSERT(isDestroyed(*this));
}

static void recreate(VulkanSwapchain &swapchain, Statics const &statics) {
	SwapchainAndFormat const saf= createSwapchain(statics, swapchain.o);
	swapchain.o= saf.swapchain;
	swapchain.surfaceFormat= saf.format;
	U32 imageC;
	vkGetSwapchainImagesKHR(statics.device.logical, swapchain.o, &imageC, nullptr);
	if(imageC != swapchain.imageC)
		recreateDefault(swapchain.images, imageC);
	vkGetSwapchainImagesKHR(statics.device.logical, swapchain.o, &imageC, getData(swapchain.images));
	swapchain.imageC= imageC;
}

VulkanShaderModule::VulkanShaderModule(VulkanShaderModule &&other): o{other.o} {
	other.o = VK_NULL_HANDLE; // mark as destroyed
}

VulkanShaderModule::VulkanShaderModule(VulkanDevice const &device, char const &filePath) {
	ASSERT(std::filesystem::is_regular_file(&filePath));
	auto const size= std::filesystem::file_size(&filePath);
	ASSERT(0 == size % sizeof(U32));
	char *charBuf= new char[size];
	ScopeExitGuard const g0= [&]{ delete[] charBuf; };
	std::fstream stream{&filePath};
	stream.read(charBuf, size);
	ASSERT(size == static_cast<std::uintmax_t>(stream.gcount()));
	U32 *intBuf= new U32[size/sizeof(U32)];
	ScopeExitGuard const g1= [&]{ delete[] intBuf; };
	// assume that the SPIRV code was stored in the same endianness as
	// the layout of U32s in memory of this program
	std::memcpy(intBuf, charBuf, size);
	VkShaderModuleCreateInfo const createInfo{
		VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, // sType
		nullptr, // pNext
		0, // flags (required by Vulkan to be 0)
		static_cast<std::size_t>(size), // codeSize
		intBuf // pCode
	};
	ASSERT_VK_SUCCESS(vkCreateShaderModule(
		device.logical,
		&createInfo,
		nullptr, // allocator
		&o
	));
}

static bool isDestroyed(VulkanShaderModule const &shaderModule) {
	return shaderModule.o == VK_NULL_HANDLE;
}

VulkanShaderModule::~VulkanShaderModule() {
	ASSERT(isDestroyed(*this));
}

static void destroy(VulkanShaderModule &shaderModule, VulkanDevice const &device) {
	vkDestroyShaderModule(device.logical, shaderModule.o, nullptr);
	// mark as destroyed
	shaderModule.o= VK_NULL_HANDLE;
}

DrawingSyncObjects::DrawingSyncObjects(DrawingSyncObjects &&other):
	imageAvailableSemaphores{std::move(other.imageAvailableSemaphores)},
	renderFinishedSemaphores{std::move(other.renderFinishedSemaphores)},
	frameInFlightFences{std::move(other.frameInFlightFences)}
{
	// mark as destroyed
	other.imageAvailableSemaphores= nullptr;
}

static bool isDestroyed(DrawingSyncObjects const &drawingSync) {
	return drawingSync.imageAvailableSemaphores == nullptr;
}

static void destroy(
	DrawingSyncObjects &drawingSync,
	VulkanDevice const &device
) {
	if(isDestroyed(drawingSync))
		return;
	for(U32 frameI=0; frameI<maxFrameInFlightC; ++frameI) {
		vkDestroySemaphore(
			device.logical,
			drawingSync.imageAvailableSemaphores[frameI],
			nullptr // pAllocator
		);
		vkDestroySemaphore(
			device.logical,
			drawingSync.renderFinishedSemaphores[frameI],
			nullptr // pAllocator
		);
		vkDestroyFence(
			device.logical,
			drawingSync.frameInFlightFences[frameI],
			nullptr // pAllocator
		);
	}
	delete[] drawingSync.imageAvailableSemaphores;
	delete[] drawingSync.renderFinishedSemaphores;
	delete[] drawingSync.frameInFlightFences;
	// mark as destroyed
	drawingSync.imageAvailableSemaphores= nullptr;
}

DrawingSyncObjects::~DrawingSyncObjects() {
	// needs to be destroyed manually
	ASSERT(isDestroyed(*this));
}

DrawingSyncObjects &DrawingSyncObjects::operator=(DrawingSyncObjects &&other) {
	// this object can't destroy itself, so it needs to be destroyed before it is assigned to
	ASSERT(isDestroyed(*this));
	imageAvailableSemaphores= std::move(other.imageAvailableSemaphores);
	renderFinishedSemaphores= std::move(other.renderFinishedSemaphores);
	frameInFlightFences= std::move(other.frameInFlightFences);
	// mark as destroyed
	other.imageAvailableSemaphores= nullptr;
	return *this;
}

DrawingSyncObjects::DrawingSyncObjects(VulkanDevice const &device):
	imageAvailableSemaphores{new VkSemaphore[maxFrameInFlightC]},
	renderFinishedSemaphores{new VkSemaphore[maxFrameInFlightC]},
	frameInFlightFences{new VkFence[maxFrameInFlightC]}
{
	VkSemaphore *const semaphoreArrays[]= {
		imageAvailableSemaphores,
		renderFinishedSemaphores
	};
	for(auto const semaphores : semaphoreArrays) for(
		U32 semaphoreI=0; semaphoreI<maxFrameInFlightC; ++semaphoreI
	) {
		VkSemaphoreCreateInfo const createInfo{
			VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, // sType
			nullptr, // pNext
			0, // flags (required by Vulkan to be 0)
		};
		ASSERT_VK_SUCCESS(vkCreateSemaphore(
			device.logical,
			&createInfo,
			nullptr, // pAllocator
			semaphores + semaphoreI
		));
	}
	VkFenceCreateInfo fenceCreateInfo{
		VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, // sType
		nullptr, // pNext
		VK_FENCE_CREATE_SIGNALED_BIT, // flags
	};
	for(U32 fenceI=0; fenceI<maxFrameInFlightC; ++fenceI)
		ASSERT_VK_SUCCESS(vkCreateFence(
			device.logical,
			&fenceCreateInfo,
			nullptr, // pAllocator
			frameInFlightFences + fenceI
		));
}

static VkFormat getDepthFormat(VulkanDevice const &device) {
	VkFormat const depthFormats[] { VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT };
	for(VkFormat const depthFormat : depthFormats) {
		VkFormatProperties props;
		vkGetPhysicalDeviceFormatProperties(device.physical, depthFormat, &props);
		if constexpr(shouldPrintVerboseVulkanInfo)
			std::cout << "using format for depth buffer: " << mapVulkanFormatToString.at(depthFormat) << "\n";
		if(props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
			return depthFormat;
	}
	ASSERT(0); // couldn't find a depth format
}
DepthResources::DepthResources(
	VkFormat const format,
	Extent<U32> const extent,
	VkDevice const logicalDevice,
	VmaAllocator const allocator
):
	image{
		extent,
		format, 
		VK_IMAGE_TILING_OPTIMAL,
		VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
		allocator
	},
	imageView{image.o, format, VK_IMAGE_ASPECT_DEPTH_BIT, logicalDevice}
{}
static bool isDestroyed(DepthResources const &ds) {
	return isDestroyed(ds.image);
}
static void destroy(
	DepthResources &ds,
	VkDevice const logicalDevice,
	VmaAllocator const allocator
) {
	destroy(ds.image, allocator);
	destroy(ds.imageView, logicalDevice);
}
DepthResources::~DepthResources() {
	ASSERT(isDestroyed(*this));
}

static bool isDestroyed(Dynamics const &dyns) {
	return isDestroyed(dyns.swapchain);
}

static VkRenderPass createRenderPass(
	VkFormat const colourFormat,
	Statics const &statics
) {
	VkAttachmentReference const colourAttachmentReference{
		0, // attachment (an index into VkRenderPassCreateInfo::pAttachments)
		VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL // layout
	};
	VkAttachmentReference const depthAttachmentReference{
		1,
		VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
	};
	VkAttachmentDescription const attachmentDescriptions[] {
		{ // colour attachment
			0, // flags
			colourFormat, // format
			VK_SAMPLE_COUNT_1_BIT, // samples
			VK_ATTACHMENT_LOAD_OP_CLEAR, // loadOp
			VK_ATTACHMENT_STORE_OP_STORE, // storeOp
			VK_ATTACHMENT_LOAD_OP_DONT_CARE, // stencilLoadOp
			VK_ATTACHMENT_STORE_OP_DONT_CARE, // stencilStoreOp
			VK_IMAGE_LAYOUT_UNDEFINED, // initialLayout
			VK_IMAGE_LAYOUT_PRESENT_SRC_KHR // finalLayout
		}, { // depth attachment
			0,
			statics.depthFormat,
			VK_SAMPLE_COUNT_1_BIT,
			VK_ATTACHMENT_LOAD_OP_CLEAR,
			VK_ATTACHMENT_STORE_OP_DONT_CARE,
			VK_ATTACHMENT_LOAD_OP_DONT_CARE,
			VK_ATTACHMENT_STORE_OP_DONT_CARE,
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
		}
	};
	VkSubpassDescription const subpassDescription{
		0, // flags
		VK_PIPELINE_BIND_POINT_GRAPHICS, // pipelineBindPoint
		0, // inputAttachmentCount
		nullptr, // pInputAttachments
		1, // colorAttachmentCount
		&colourAttachmentReference, // pColorAttachments
		0, // pResolveAttachments
		&depthAttachmentReference, // pDepthStencilAttachment
		0, // preserveAttachmentCount
		nullptr, // pPreserveAttachments
	};
	VkSubpassDependency const subpassDependencies[] {
		{ // colour attachment dependency
			VK_SUBPASS_EXTERNAL, // srcSubpass
			0, // dstSubpass
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, // srcStageMask
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, // dstStageMask
			0, // srcAccessMask
			VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, // dstAccessMask
			0, // dependencyFlags
		},
		// TODO: try removing this
		{ // depth attachment dependency
			VK_SUBPASS_EXTERNAL, // srcSubpass
			0, // dstSubpass
			VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, // srcStageMask
			VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, // dstStageMask
			0, // srcAccessMask
			VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, // dstAccessMask
			0, // dependencyFlags
		}
	};
	VkRenderPassCreateInfo const renderPassCreateInfo{
		VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO, // sType
		nullptr, // pNext
		0, // flags
		length(attachmentDescriptions), // attachmentCount
		attachmentDescriptions, // pAttachments
		1, // subpassCount
		&subpassDescription, // pSubpasses
		length(subpassDependencies), // dependencyCount
		subpassDependencies, // pDependencies
	};
	VkRenderPass ret;
	ASSERT_VK_SUCCESS(vkCreateRenderPass(
		statics.device.logical,
		&renderPassCreateInfo,
		nullptr, // pAllocator
		&ret
	));
	return ret;
}

static VkPipeline createGraphicsPipeline(
	VkRenderPass const renderPass,
	ArrayView<VkVertexInputBindingDescription, true, VertexInputBindingDescriptionsSize> const
		vertexInputBindingDescriptions,
	ArrayView<VkVertexInputAttributeDescription, true, VertexInputBindingDescriptionsSize> const
		vertexInputAttributeDescriptions,
	VkPrimitiveTopology const inputAssemblyTopology,
	VkDescriptorSetLayout const descriptorSetLayout,
	VkPipelineLayout const layout,
	VulkanShaderModule const &vertexShaderModule,
	VulkanShaderModule const &fragmentShaderModule,
	VulkanDevice const &device
) {
	VkPipelineShaderStageCreateInfo const shaderStageCreateInfos[]= {
		{
			VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, // sType
			nullptr, // pNext
			0, // flags
			VK_SHADER_STAGE_VERTEX_BIT, // stage
			vertexShaderModule.o, // module
			reinterpret_cast<char const*>(u8"main"), // name (entrypoint to the shader)
			nullptr, // pSpecializationInfo
		}, {
			VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, // sType
			nullptr, // pNext
			0, // flags
			VK_SHADER_STAGE_FRAGMENT_BIT, // stage
			fragmentShaderModule.o, // module
			reinterpret_cast<char const*>(u8"main"), // name (entrypoint to the shader)
			nullptr, // pSpecializationInfo
		},
	};
	VkPipelineVertexInputStateCreateInfo const vertexInputShaderState{
		VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO, // sType
		nullptr, // pNext
		0, // flags (required by Vulkan to be 0)
		static_cast<U32>(vertexInputBindingDescriptions.size), // vertexBindingDescriptionCount
		vertexInputBindingDescriptions.o, // pVertexBindingDescriptions
		static_cast<U32>(vertexInputAttributeDescriptions.size), // vertexAttributeDescriptionCount
		vertexInputAttributeDescriptions.o, // pVertexAttributeDescriptions
	};
	VkPipelineInputAssemblyStateCreateInfo const inputAssemblyState{
		VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, // sType
		nullptr, // pNext
		0, // flags (required by Vulkan to be 0)
		inputAssemblyTopology, // topology
		VK_FALSE, // primitiveRestartEnable
	};
	auto const viewportState= initWithDefaulted<VkPipelineViewportStateCreateInfo>([](auto &vs) {
		vs.sType= VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
		vs.pNext= nullptr;
		vs.flags= 0;
		vs.viewportCount= 1;
		vs.scissorCount= 1;
		// pViewports and pScissors unspecified
	});
	VkPipelineRasterizationStateCreateInfo constexpr rasterisationState{
		VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO, // sType
		nullptr, // pNext
		0, // flags
		VK_FALSE, // depthClampEnable
		VK_FALSE, // rasterizerDiscardEnable
		VK_POLYGON_MODE_FILL, // polygonMode
		VK_CULL_MODE_BACK_BIT, // cullMode
//		VK_FRONT_FACE_CLOCKWISE, // frontFace
		VK_FRONT_FACE_COUNTER_CLOCKWISE, // frontFace
		VK_FALSE, // depthBiasEnable
		0.f, // depthBiasConstantFactor
		0.f, // depthBiasClamp
		1.f, // depthBiasSlopeFactor
		1.f, // lineWidth
	};
	VkPipelineMultisampleStateCreateInfo constexpr multisampleState{
		VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, // sType
		nullptr, // pNext
		0, // flags
		VK_SAMPLE_COUNT_1_BIT, // rasterizationSamples
		VK_FALSE, // sampleShadingEnable
		0.f, // minSampleShading
		nullptr, // pSampleMask;
		VK_FALSE, // alphaToCoverageEnable
		VK_FALSE, // alphaToOneEnable
	};
	VkPipelineColorBlendAttachmentState constexpr colourBlendAttachmentState{
		VK_FALSE, // blendEnable
		VK_BLEND_FACTOR_ZERO, // srcColorBlendFactor
		VK_BLEND_FACTOR_ZERO, // dstColorBlendFactor
		VK_BLEND_OP_ADD, // colorBlendOp
		VK_BLEND_FACTOR_ZERO, // srcAlphaBlendFactor
		VK_BLEND_FACTOR_ZERO, // dstAlphaBlendFactor
		VK_BLEND_OP_ADD, // alphaBlendOp
		VK_COLOR_COMPONENT_R_BIT
		| VK_COLOR_COMPONENT_G_BIT
		| VK_COLOR_COMPONENT_B_BIT
		| VK_COLOR_COMPONENT_A_BIT, // colorWriteMask
	};

	auto depthStencilState= initWithDefaulted<VkPipelineDepthStencilStateCreateInfo>([](auto &dss) {
		dss.sType= VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
		dss.pNext= nullptr;
		dss.flags= 0;
		dss.depthTestEnable= VK_TRUE;
		dss.depthWriteEnable= VK_TRUE;
		dss.depthCompareOp= VK_COMPARE_OP_LESS;
		dss.depthBoundsTestEnable= VK_FALSE;
		dss.stencilTestEnable= VK_FALSE;
		// these must be valid, even though they're not used
		dss.front.failOp= VK_STENCIL_OP_KEEP;
		dss.front.passOp= VK_STENCIL_OP_KEEP;
		dss.front.depthFailOp= VK_STENCIL_OP_KEEP;
		dss.front.compareOp= VK_COMPARE_OP_NEVER;
		dss.back.failOp= VK_STENCIL_OP_KEEP;
		dss.back.passOp= VK_STENCIL_OP_KEEP;
		dss.back.depthFailOp= VK_STENCIL_OP_KEEP;
		dss.back.compareOp= VK_COMPARE_OP_NEVER;
		// front, back, minDepthBounds, maxDepthBounds unspecified because ignored
	});
	VkPipelineColorBlendStateCreateInfo colourBlendState{
		VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, // sType
		nullptr, // pNext
		0, // flags
		VK_FALSE, // logicOpEnable
		VK_LOGIC_OP_COPY, // logicOp
		1, // attachmentCount
		&colourBlendAttachmentState, // pAttachments
		{0.f, 0.f, 0.f, 0.f}, // blendConstants
	};
	VkDynamicState constexpr dynamicStates[] {
		VK_DYNAMIC_STATE_VIEWPORT,
		VK_DYNAMIC_STATE_SCISSOR,
	};
	VkPipelineDynamicStateCreateInfo dynamicState {
		VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
		nullptr,
		0,
		length(dynamicStates),
		dynamicStates
	};
	VkGraphicsPipelineCreateInfo const graphicsPipelineCreateInfo{
		VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, // sType
		nullptr, // pNext
		0, // flags
		length(shaderStageCreateInfos), // stageCount
		shaderStageCreateInfos, // pStages
		&vertexInputShaderState, // pVertexInputState
		&inputAssemblyState, // pInputAssemblyState
		nullptr, // pTessellationState
		&viewportState, // pViewportState
		&rasterisationState, // pRasterizationState
		&multisampleState, // pMultisampleState
		&depthStencilState, // pDepthStencilState
		&colourBlendState, // pColorBlendState
		&dynamicState, // pDynamicState
		layout, // layout
		renderPass, // renderPass
		// (the index of the subpass in the renderpass wherein this pipeline will be used)
		0, // subpass
		VK_NULL_HANDLE, // basePipelineHandle
		0, // basePipelineIndex
	};
	VkPipeline ret;
	ASSERT_VK_SUCCESS(vkCreateGraphicsPipelines(
		device.logical,
		VK_NULL_HANDLE, // pipelineCache
		1, // createInfoCount
		&graphicsPipelineCreateInfo,
		nullptr, // pAllocator
		&ret
	));
	return ret;
}

static VkFramebuffer createFramebuffer(ImagesSize const imageI, Statics const &statics, Dynamics const &dyns) {
	VkImageView const attachments[] {
		dyns.framesObjects[imageI].colourImageView.o,
		dyns.depthResources[imageI].imageView.o,
	};
	VkFramebufferCreateInfo const createInfo{
		VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO, // sType
		nullptr, // pNext
		0, // flags
		dyns.renderPass, // renderPass
		length(attachments), // attachmentCount
		attachments, // pAttachments
		statics.extent.width, // width
		statics.extent.height, // height
		1, // layers
	};
	VkFramebuffer fb;
	ASSERT_VK_SUCCESS(vkCreateFramebuffer(
		statics.device.logical,
		&createInfo,
		nullptr, // pAllocator
		&fb
	));
	return fb;	
}

FrameObjects::FrameObjects(ImagesSize const imageI, Statics const &statics, Dynamics const &dyns):
	colourImageView{
		dyns.swapchain.images[imageI],
		dyns.swapchain.surfaceFormat.format,
		VK_IMAGE_ASPECT_COLOR_BIT,
		statics.device.logical
	},
	framebuffer{createFramebuffer(imageI, statics, dyns)}
{}
static void destroy(FrameObjects &fo, VkDevice const logicalDevice) {
	destroy(fo.colourImageView, logicalDevice);
	vkDestroyFramebuffer(logicalDevice, fo.framebuffer, nullptr);
}

VulkanPipeline::VulkanPipeline(
	VkRenderPass const renderPass,
	char const &vertexShaderPath,
	char const &fragmentShaderPath,
	ArrayView<VkVertexInputBindingDescription, true, VertexInputBindingDescriptionsSize> const
		vertexInputBindingDescriptions,
	ArrayView<VkVertexInputAttributeDescription, true, VertexInputAttributeDescriptionsSize> const
		vertexInputAttributeDescriptions,
	VkPrimitiveTopology const inputAssemblyTopology,
	VkDescriptorSetLayout const descriptorSetLayout,
	VulkanDevice const &device
):
	vertexShaderModule{device, vertexShaderPath},
	fragmentShaderModule{device, fragmentShaderPath},
	layout{[descriptorSetLayout,logicalDevice=device.logical]{
		VkPipelineLayoutCreateInfo const pipelineLayoutCreateInfo{
			VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, // sType
			nullptr, // pNext
			0, // flags (required by Vulkan to be 0)
			1, // setLayoutCount
			&descriptorSetLayout, // pSetLayouts
			0, // pushConstantRangeCount
			nullptr, // pPushConstantRanges
		};
		VkPipelineLayout ret;
		ASSERT_VK_SUCCESS(vkCreatePipelineLayout(
			logicalDevice,
			&pipelineLayoutCreateInfo,
			nullptr, // pAllocator
			&ret
		));
		return ret;
	}()},
	o{createGraphicsPipeline(
		renderPass,
		vertexInputBindingDescriptions,
		vertexInputAttributeDescriptions,
		inputAssemblyTopology,
		descriptorSetLayout,
		layout,
		vertexShaderModule,
		fragmentShaderModule,
		device
	)}
{}

static void recreate(
	VulkanPipeline &pipeline,
	VkRenderPass const renderPass,
	ArrayView<VkVertexInputBindingDescription, true, VertexInputBindingDescriptionsSize> const
		vertexInputBindingDescriptions,
	ArrayView<VkVertexInputAttributeDescription, true, VertexInputAttributeDescriptionsSize> const
		vertexInputAttributeDescriptions,
	VkPrimitiveTopology const inputAssemblyTopology,
	VkDescriptorSetLayout const descriptorSetLayout,
	VulkanDevice const &device
) {
	vkDestroyPipeline(device.logical, pipeline.o, nullptr);
	pipeline.o= createGraphicsPipeline(
		renderPass,
		vertexInputBindingDescriptions,
		vertexInputAttributeDescriptions,
		inputAssemblyTopology,
		descriptorSetLayout,
		pipeline.layout,
		pipeline.vertexShaderModule,
		pipeline.fragmentShaderModule,
		device
	);
}

static bool isDestroyed(VulkanPipeline const &pipeline) {
	return pipeline.o == VK_NULL_HANDLE;
}
static void destroy(VulkanPipeline &pipeline, VulkanDevice const &device) {
	destroy(pipeline.fragmentShaderModule, device);
	destroy(pipeline.vertexShaderModule, device);
	vkDestroyPipelineLayout(device.logical, pipeline.layout, nullptr);
	vkDestroyPipeline(device.logical, pipeline.o, nullptr);
	pipeline.o= VK_NULL_HANDLE; // mark as destroyed
}
VulkanPipeline::~VulkanPipeline() {
	ASSERT(isDestroyed(*this));
}

// plain vertices description
static VkVertexInputBindingDescription constexpr plainVertexInputBindingDescriptions[] {
	{
		0, // binding
		sizeof(PlainVertex), // stride
		VK_VERTEX_INPUT_RATE_VERTEX, // inputRate
	},{
		1,
		sizeof(PlainModelInstance),
		VK_VERTEX_INPUT_RATE_INSTANCE, // inputRate
	}
};
static VkVertexInputAttributeDescription constexpr plainVertexInputAttributeDescriptions[] {
	{ // vertex position
		0, // location
		0, // binding
		VK_FORMAT_R32G32B32_SFLOAT, // format
		offsetof(PlainVertex, pos), // offset
	}, { // vertex texture position
		1,
		0,
		VK_FORMAT_R32G32_SFLOAT,
		offsetof(PlainVertex, texPos),
	}, { // instance position
		2,
		1,
		VK_FORMAT_R32G32B32_SINT,
		offsetof(PlainModelInstance, position),
	}, { // instance orientation
		3,
		1,
		VK_FORMAT_R32G32B32A32_SFLOAT,
		offsetof(PlainModelInstance, orient),
	}
};

Dynamics::Dynamics(Statics const &statics):
	swapchain{statics},
	depthResources{Tag::constructWithUniformArgs, swapchain.imageC, statics.depthFormat, statics.extent, statics.device.logical, statics.vmaAllocator},
	renderPass{createRenderPass(swapchain.surfaceFormat.format, statics)},
	mapImageFence{Tag::constructWithUniformArgs, swapchain.imageC, VK_NULL_HANDLE},
	framesObjects{Tag::constructWithGeneratedArgs, swapchain.imageC,
		[&](auto const &cons, auto const imageI) { cons(
			imageI, statics, *this
		); }
	},
	plainPipeline{
		renderPass,
		*"shaders/plain.vert.spv",
		*"shaders/plain.frag.spv",
		plainVertexInputBindingDescriptions,
		plainVertexInputAttributeDescriptions,
		VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
		statics.plainPipelineDescriptorSetLayout.o,
		statics.device
	},
	groundPipeline{
		renderPass,
		*"shaders/ground.vert.spv",
		*"shaders/ground.frag.spv",
		{{}},
		{{}},
		VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
		statics.groundPipelineDescriptorSetLayout.o,
		statics.device
	}
{}

static void recreate(Dynamics &dyns, Statics const &statics) {
	std::cout << "recreating dynamics\n";
	FastImagesSize const oldImageC= dyns.swapchain.imageC;
	VkFormat const oldFormat= dyns.swapchain.surfaceFormat.format;
	auto const logicalDevice= statics.device.logical;
	recreate(dyns.swapchain, statics);
	destroyAndRecreateElementwise(
		dyns.depthResources,
		oldImageC,
		std::forward_as_tuple(logicalDevice, statics.vmaAllocator),
		dyns.swapchain.imageC,
		std::forward_as_tuple(statics.depthFormat, statics.extent, logicalDevice, statics.vmaAllocator)
	);
	// it's only necessary to recreate the renderPass and graphicsPipeline if the swapchain format changes
	// and the swapchain format is currently constant
	if(oldFormat != dyns.swapchain.surfaceFormat.format) {
		vkDestroyRenderPass(logicalDevice, dyns.renderPass, nullptr);
		vkDestroyPipeline(logicalDevice, dyns.plainPipeline.o, nullptr);
		dyns.renderPass= createRenderPass(dyns.swapchain.surfaceFormat.format, statics);
		recreate(
			dyns.plainPipeline,
			dyns.renderPass,
			plainVertexInputBindingDescriptions,
			plainVertexInputAttributeDescriptions,
			VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
			statics.plainPipelineDescriptorSetLayout.o,
			statics.device
		);
	}
	recreateElementwise(dyns.mapImageFence, oldImageC, dyns.swapchain.imageC, VK_NULL_HANDLE);
	destroyAndRecreateByCallingWithIndex(
		dyns.framesObjects,
		oldImageC, std::forward_as_tuple(logicalDevice),
		dyns.swapchain.imageC, [&](auto const &cons, auto const imageI) { cons(
			imageI, statics, dyns
		); }
	);
}

static void destroy(Dynamics &dyns, Statics const &statics) {
	if(isDestroyed(dyns))
		return;
	destroy(dyns.plainPipeline, statics.device);
	destroy(dyns.groundPipeline, statics.device);
	vkDestroyRenderPass(statics.device.logical, dyns.renderPass, nullptr);
	destroy(dyns.depthResources, dyns.swapchain.imageC, false, statics.device.logical, statics.vmaAllocator);
	destroy(dyns.swapchain, statics.device); // marks as destroyed
	destroy(dyns.framesObjects, dyns.swapchain.imageC, false, statics.device.logical);
}
Dynamics::~Dynamics() {
	// needs to be destroyed manually
	ASSERT(isDestroyed(*this));
}

void initGlfw() {
	glfwInit();
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
}

Extent<U32>::Extent(U32 const width, U32 const height):
	width{width},
	height{height}
{}

Extent<U32>::Extent(VkExtent2D const o):
	width{o.width},
	height{o.height}
{}

#ifndef __cpp_concepts
template<typename WindowEventHandler>
#endif
GlfwWindow::GlfwWindow(
	VulkanInstance const &instance,
	WindowEventHandler
#ifdef __cpp_concepts
		auto
#endif
		&resizeHandler,
	Extent<U32> const extent
):
	o{[extent]()->GLFWwindow& {
		auto const ret = glfwCreateWindow(extent.width, extent.height, "window title", nullptr, nullptr);
		ASSERT(ret);
		return *ret;
	}()}
{
	glfwSetWindowUserPointer(&o, static_cast<void*>(&resizeHandler));
	glfwSetInputMode(&o, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
	glfwSetFramebufferSizeCallback(&o, [](GLFWwindow *const window, signed const width, signed const height) {
		ASSERT(0<=width && 0<=height);
		handleResize(
			*static_cast<WindowEventHandler*>(glfwGetWindowUserPointer(window)),
			width, height
		);
	});
	glfwSetKeyCallback(&o, [](
		GLFWwindow *const window,
		signed const key,
		signed const scancode,
		signed const action,
		signed const mods
	) {
		handleKey(
			*static_cast<WindowEventHandler*>(glfwGetWindowUserPointer(window)),
			key, scancode, action, mods
		);
	});
	glfwSetCursorPosCallback(&o, [](
		GLFWwindow *const window,
		double const xpos,
		double const ypos
	) {
		glfwSetCursorPos(window, 0., 0.);
		handleCursorPosition(
			*static_cast<WindowEventHandler*>(glfwGetWindowUserPointer(window)),
			xpos, ypos
		);
	});
	glfwSetCursorPos(&o, 0., 0.);
}

GlfwWindow::~GlfwWindow() {
	glfwDestroyWindow(&o);
}

static void handleKey(
	VulkanWindow &vw,
	signed const key,
	signed const scancode,
	signed const action,
	signed const mods
) {
	if(action == GLFW_PRESS) {
		vw.statics.justPressedKeys.insert(key);
		vw.statics.heldKeys.insert(key);
	} else if(action == GLFW_RELEASE)
		vw.statics.heldKeys.erase(key);
}

static void handleCursorPosition(
	VulkanWindow &vw,
	double const xpos,
	double const ypos
) {
	static float constexpr movementScale = 0.001f;
	vw.statics.camera.pitch = std::min(tau/4, std::max<float>(-tau/4,
		vw.statics.camera.pitch + movementScale*ypos
	));
	vw.statics.camera.yaw = vw.statics.camera.yaw - movementScale*xpos;
	if constexpr(shouldPrintCameraInfo) {
		WATCH(xpos);
		WATCH(ypos);
		WATCH(vw.statics.camera.pitch / tau);
		WATCH(vw.statics.camera.yaw / tau);
	}
}

static VulkanSurface createSurfaceFrom(VulkanInstance const &instance, GlfwWindow const &glfwWindow) {
	VkSurfaceKHR ret;
	ASSERT_VK_SUCCESS(glfwCreateWindowSurface(instance.o, &glfwWindow.o, nullptr, &ret));
	return VulkanSurface{ret};
}

VulkanImageSampler::VulkanImageSampler(VulkanDevice const &device):
	o{initWithDefaulted<VkSampler>([&](auto &sampler) {
		auto const createInfo= initWithDefaulted<VkSamplerCreateInfo>([&](auto &ci){
			ci.sType= VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
			ci.pNext= nullptr;
			ci.flags= 0;
			ci.magFilter= VK_FILTER_LINEAR;
			ci.minFilter= VK_FILTER_LINEAR;
			ci.mipmapMode= VK_SAMPLER_MIPMAP_MODE_LINEAR;
			ci.addressModeU= VK_SAMPLER_ADDRESS_MODE_REPEAT;
			ci.addressModeV= VK_SAMPLER_ADDRESS_MODE_REPEAT;
			ci.addressModeW= VK_SAMPLER_ADDRESS_MODE_REPEAT;
			ci.mipLodBias= 0.f;
			if((ci.anisotropyEnable= device.featureSupport.samplerAnisotropy))
				ci.maxAnisotropy= device.deviceProperties.limits.maxSamplerAnisotropy;
			ci.compareEnable= VK_FALSE;
			// ci.compareOp uninitialised
			ci.minLod= 0.f;
			ci.maxLod= 0.f; // maxLod must be greater than or equal to minLod
			ci.borderColor= VK_BORDER_COLOR_INT_OPAQUE_BLACK;
			ci.unnormalizedCoordinates= VK_FALSE;
		});
		ASSERT_VK_SUCCESS(vkCreateSampler(device.logical, &createInfo, nullptr, &sampler));
	})}
{}

static bool isDestroyed(VulkanImageSampler const &sampler) {
	return sampler.o == VK_NULL_HANDLE;
}
VulkanImageSampler::~VulkanImageSampler() {
	ASSERT(isDestroyed(*this));
}
static void destroy(VulkanImageSampler &sampler, VkDevice const logicalDevice) {
	vkDestroySampler(logicalDevice, sampler.o, nullptr);
	sampler.o= VK_NULL_HANDLE; // mark as destroyed
}

static void recreateSwapchain(VulkanWindow &vw) {
	recreate(vw.dynamics, vw.statics);
}

static void handleResize(VulkanWindow &vw, U32 width, U32 height) {
	vkDeviceWaitIdle(vw.statics.device.logical);
	std::cout << "resizing to extent (" << width << "," << height << ")\n";
	vw.statics.extent.width = width;
	vw.statics.extent.height = height;
	recreateSwapchain(vw);
}

static bool operator==(PlainVertex const &a, PlainVertex const &b) {
	return a.pos == b.pos
		&& a.texPos == b.texPos;
}
// https://stackoverflow.com/questions/2590677/how-do-i-combine-hash-values-in-c0x
template<typename... Args>
static auto hashCombine(Args &&...args) {
	std::size_t seed= 0;
	[[maybe_unused]] signed expansion[]{([&seed,&args]{
		seed ^=
			std::hash<std::remove_cv_t<std::remove_reference_t<Args>>>{}(args)
			+ 0x9e3779b9 + (seed << 6) + (seed >> 2);
	}(), 0)...};
	return seed;
}
struct VertexHasher{
	auto operator()(PlainVertex const &vertex) const {
		return hashCombine(
			vertex.pos.x,
			vertex.pos.y,
			vertex.pos.z,
			vertex.texPos.x,
			vertex.texPos.y
		);
	}
};

// static usage
template<typename El, typename Usage, typename Size>
template<typename, typename>
GrowableHostVisibleBuffer<El, Usage, Size>::GrowableHostVisibleBuffer(
	VmaAllocator const allocator
): GrowableHostVisibleBuffer{
	Usage::o,
	allocator,
	getUninitialised<VmaAllocationInfo>()
} {}
// dynamic usage
template<typename El, typename Usage, typename Size>
GrowableHostVisibleBuffer<El, Usage, Size>::GrowableHostVisibleBuffer(
	VkBufferUsageFlags const usage,
	VmaAllocator const allocator
): GrowableHostVisibleBuffer{
	usage,
	allocator,
	getUninitialised<VmaAllocationInfo>()
} {}
template<typename El, typename Usage, typename Size>
GrowableHostVisibleBuffer<El, Usage, Size>::GrowableHostVisibleBuffer(
	VkBufferUsageFlags const usage,
	VmaAllocator const allocator,
	VmaAllocationInfo &&allocInfo
):
	o{
		usage,
		VMA_MEMORY_USAGE_AUTO,
		VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, 0,
		initialCap * sizeof(El),
		1,
		allocator,
		&allocInfo
	},
	mem{reinterpret_cast<char*>(allocInfo.pMappedData)},
	capacityInBytes{allocInfo.size},
	size{}
{}

template<typename El, typename Usage, typename Size>
static void destroy(GrowableHostVisibleBuffer<El, Usage, Size> &buf, VmaAllocator const allocator) {
	destroy(buf.o, allocator);
}

template<typename El, typename Usage, typename Size, typename ...CreateArgs>
static void createBackNoRealloc(
	GrowableHostVisibleBuffer<El, Usage, Size> &buf,
	CreateArgs &&...createArgs
) {
	ASSERT((buf.size + 1) * sizeof(El) <= buf.capacityInBytes);
	new(buf.mem + buf.size*sizeof(El)) El{std::forward<CreateArgs>(createArgs)...};
	++buf.size;
	return;
}

template<typename El, typename Usage, typename Size>
static void growToCapacity(
	GrowableHostVisibleBuffer<El, Usage, Size> &buf,
	VkBufferUsageFlags const usage,
	VmaAllocator const allocator,
	Size const capacity,
	bool const shouldPreserveContent
) {
	VmaAllocationInfo allocInfo;
	VulkanBuffer newBuf{
		usage,
		VMA_MEMORY_USAGE_AUTO,
		VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, 0,
		capacity,
		1,
		allocator,
		&allocInfo
	};
	char *newMem= reinterpret_cast<char*>(allocInfo.pMappedData);
	if(shouldPreserveContent)
		for(unsigned i=0; i<buf.size; ++i)
			new(newMem + i*sizeof(El)) El{std::move(buf[i])};
	for(unsigned i=0; i<buf.size; ++i)
		buf[i].~El();
	destroy(buf.o, allocator);
	buf.o= std::move(newBuf);
	buf.mem= newMem;
	buf.capacityInBytes= allocInfo.size;
}
template<typename El, VkBufferUsageFlags usage, typename Size>
static void growToCapacity(
	GrowableHostVisibleBuffer<El, GrowableHostVisibleBufferStaticUsage<usage>, Size> &buf,
	VmaAllocator const allocator,
	Size const capacity,
	bool const shouldPreserveContent
) {
	growToCapacity(buf, usage, allocator, capacity, shouldPreserveContent);
}

template<typename El, typename Usage, typename Size>
static void grow(
	GrowableHostVisibleBuffer<El, Usage, Size> &buf,
	VkBufferUsageFlags const usage,
	VmaAllocator const allocator
) {
	growToCapacity(buf, usage, allocator, 2*buf.capacityInBytes, true);
}
template<typename El, VkBufferUsageFlags usage, typename Size>
static void grow(
	GrowableHostVisibleBuffer<El, GrowableHostVisibleBufferStaticUsage<usage>, Size> &buf,
	VmaAllocator const allocator
) {
	growToCapacity(buf, usage, allocator, 2*buf.capacityInBytes, true);
}

template<typename El, typename Usage, typename Size, typename ...CreateArgs>
static void createBack(
	GrowableHostVisibleBuffer<El, Usage, Size> &buf,
	VkBufferUsageFlags const usage,
	VmaAllocator const allocator,
	CreateArgs &&...createArgs
) {
	if(buf.capacityInBytes < (buf.size + 1) * sizeof(El))
		grow(buf, usage, allocator);
	createBackNoRealloc(buf, std::forward<CreateArgs>(createArgs)...);
}
template<typename El, VkBufferUsageFlags usage, typename Size, typename ...CreateArgs>
static void createBack(
	GrowableHostVisibleBuffer<El, GrowableHostVisibleBufferStaticUsage<usage>, Size> &buf,
	VmaAllocator const allocator,
	CreateArgs &&...createArgs
) {
	createBack(buf, usage, allocator, std::forward<CreateArgs>(createArgs)...);
}

template<typename El, typename Usage, typename Size>
template<typename Index, typename>
El const &GrowableHostVisibleBuffer<El, Usage, Size>::operator[](Index const i) const {
	return *getElementPointer<El>(mem, i);
}
template<typename El, typename Usage, typename Size>
template<typename Index, typename>
El &GrowableHostVisibleBuffer<El, Usage, Size>::operator[](Index const i) {
	return *getElementPointer<El>(mem, i);
}

template<typename T, typename Usage, typename Size>
void copy(
	GrowableHostVisibleBuffer<T, Usage, Size> &dst,
	GrowableHostVisibleBuffer<T, Usage, Size> const &src,
	VkBufferUsageFlags const usage,
	VmaAllocator const allocator
) {
	if(dst.capacityInBytes < src.size*sizeof(T))
		growToCapacity(dst, usage, allocator, src.size*sizeof(T), false);
	for(Size i=0; i<src.size; ++i)
		new(dst.mem + i*sizeof(T)) T{src[i]};
	dst.size= src.size;
}

template<typename T, VkBufferUsageFlags usage, typename Size>
void copy(
	GrowableHostVisibleBuffer<T, GrowableHostVisibleBufferStaticUsage<usage>, Size> &dst,
	GrowableHostVisibleBuffer<T, GrowableHostVisibleBufferStaticUsage<usage>, Size> const &src,
	VmaAllocator const allocator
) {
	copy(dst, src, usage, allocator);
}

VulkanViewableImage::VulkanViewableImage(
	PathStringView const pathToLoad,
	VmaAllocator const allocator,
	VkCommandBuffer const cmdBufForLoading,
	VkFence const fenceForLoading,
	VulkanDevice const &device
):
	o{pathToLoad, allocator, cmdBufForLoading, fenceForLoading, device},
	view{o.o, loadedImageFormat, VK_IMAGE_ASPECT_COLOR_BIT, device.logical}
{}

static bool isDestroyed(VulkanViewableImage const &img) {
	return isDestroyed(img.o);
}
static void destroy(
	VulkanViewableImage &img,
	VmaAllocator const allocator,
	VkDevice const logicalDevice
) {
	destroy(img.o, allocator);
	destroy(img.view, logicalDevice);
}
VulkanViewableImage::~VulkanViewableImage() {
	ASSERT(isDestroyed(*this));
}

typedef U16F DescriptorWritesSize;
static void writeDescriptorSet(
	VkDevice const logicalDevice,
	ArrayView<VkWriteDescriptorSet, true, DescriptorWritesSize> const descriptorWrites
) {
	vkUpdateDescriptorSets(
		logicalDevice,
		descriptorWrites.size,
		getData(descriptorWrites),
		0, nullptr
	);
}
template<typename WriteDescriptorSet>
static StaticArray<VkDescriptorSet, maxFrameInFlightC> createDescriptorSets(
	WriteDescriptorSet &&writeDescriptorSet,
	VkDevice const logicalDevice,
	VkDescriptorSetLayout const descriptorSetLayout,
	VkDescriptorPool const descriptorPool
) {
	StaticArray<VkDescriptorSetLayout, maxFrameInFlightC> layouts{
		Tag::constructWithUniformArgs, descriptorSetLayout
	};
	VkDescriptorSetAllocateInfo const allocInfo{
		VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		nullptr,
		descriptorPool,
		maxFrameInFlightC,
		getData(layouts),
	};
	StaticArray<VkDescriptorSet, maxFrameInFlightC> descriptorSets{Tag::defaultInitialise};
	ASSERT_VK_SUCCESS(vkAllocateDescriptorSets(
		logicalDevice,
		&allocInfo,
		getData(descriptorSets)
	));
	for(FrameIndex frameI=0; frameI<maxFrameInFlightC; ++frameI)
		writeDescriptorSet(descriptorSets[frameI], frameI);
	return descriptorSets;
}

PlainModel::PlainModel(
	IdentifierStringView const name,
	VkDescriptorSetLayout const descriptorSetLayout,
	StaticArray<VulkanBuffer, maxFrameInFlightC> const &perspectiveTransformationMatrixUniformBuffers,
	VkSampler const sampler,
	VmaAllocator const allocator,
	VkCommandBuffer const cmdBufForLoading,
	VkFence const fenceForLoading,
	VulkanDevice const &device
): PlainModel{
	PlainModelParams{
		"models/" + std::string{begin(name), end(name)} + "/texture.png",
		descriptorSetLayout,
		perspectiveTransformationMatrixUniformBuffers,
		sampler,
		allocator,
		cmdBufForLoading,
		fenceForLoading,
		device,
	}, {[&]{
		std::string const modelFile= "models/" + std::string{begin(name), end(name)} + "/model.obj";
		std::cout << "loading model " << modelFile << "\n";
		tinyobj::attrib_t attrib;
		std::vector<tinyobj::shape_t> shapes;
		std::vector<tinyobj::material_t> materials;
		std::string warning, error;
		if(!tinyobj::LoadObj(&attrib, &shapes, &materials, &warning, &error, modelFile.c_str())) {
			std::cout << "tinyobjloader couldn't load model\n";
			WATCH(warning);
			WATCH(error);
			std::exit(1);
		}
		std::vector<PlainVertex> vertices;
		std::vector<U32> indices;
		std::unordered_map<PlainVertex, U32, VertexHasher> mapVertexToIndex{};
		U32F vertexReuseInstanceC= 0;
		for(auto const &shape : shapes)
			for(auto const &index : shape.mesh.indices) {
				PlainVertex const vertex {
					{
						attrib.vertices[3*index.vertex_index + 0],
						attrib.vertices[3*index.vertex_index + 1],
						attrib.vertices[3*index.vertex_index + 2],
					}, {
						attrib.texcoords[2*index.texcoord_index + 0],
						1.f - attrib.texcoords[2*index.texcoord_index + 1],
					}
				};
				auto const it=mapVertexToIndex.find(vertex);
				if(it == end(mapVertexToIndex)) {
					U32 const newIndex= vertices.size();
					vertices.push_back(vertex);
					indices.push_back(newIndex);
					mapVertexToIndex.insert({vertex, newIndex});
				} else {
					++vertexReuseInstanceC;
					indices.push_back(it->second);
				}
			}
//		WATCH(vertices.size());
//		WATCH(indices.size());
//		WATCH(vertexReuseInstanceC);
		return PlainGeometry{
			std::move(vertices),
			std::move(indices)
		};
	}()}
} {}

template<std::size_t size>
VulkanDescriptorPool::VulkanDescriptorPool(
	VkDescriptorType const (&descriptorTypes)[size],
	VkDevice const logicalDevice
): o{[&descriptorTypes,logicalDevice]{
	StaticArray<VkDescriptorPoolSize, size> poolSizes{Tag::constructWithGeneratedArgs,
		[&](auto const &cons, auto const i) { cons(
			descriptorTypes[i], maxFrameInFlightC
		); }
	};
	VkDescriptorPoolCreateInfo const poolCreateInfo{
		VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		nullptr,
		0, // flags
		maxFrameInFlightC, // maxSets
		size, // poolSizeCount 
		getData(poolSizes), // pPoolSizes
	};
	VkDescriptorPool ret;
	ASSERT_VK_SUCCESS(vkCreateDescriptorPool(
		logicalDevice,
		&poolCreateInfo,
		nullptr,
		&ret
	));
	return ret;
}()} {}

static bool isDestroyed(VulkanDescriptorPool const &dp) {
	return dp.o == VK_NULL_HANDLE;
}
static void destroy(VulkanDescriptorPool &dp, VkDevice const logicalDevice) {
	vkDestroyDescriptorPool(logicalDevice, dp.o, nullptr);
	dp.o= VK_NULL_HANDLE; // mark as destroyed
}
VulkanDescriptorPool::~VulkanDescriptorPool() {
	ASSERT(isDestroyed(*this));
}

PlainModel::PlainModel(
	PlainModelParams const &params,
	PlainGeometry const &geometry
):
	triangleC{static_cast<U32>(geometry.indices.size() / 3)},
	descriptorPool{
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER },
		params.device.logical
	},
	texture{
		params.name,
		params.allocator,
		params.commandBufferForLoading,
		params.fenceForLoading,
		params.device
	},
	descriptorSets{[&]{
		VkDescriptorImageInfo const imageInfo {
			params.sampler,
			texture.view.o,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		};
		return createDescriptorSets([&](VkDescriptorSet const descriptorSet, auto const frameI) {
			VkDescriptorBufferInfo const bufferInfo {
				params.perspectiveTransformationMatrixUniformBuffers[frameI].o,
				0,
				sizeof(UniformBufferObject)
			};
			StaticArray descriptorWrites{Tag::listInitialise<VkWriteDescriptorSet>, {
				{
					VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
					nullptr,
					descriptorSets[frameI], // dstSet
					0, // dstBinding
					0, // dstArrayElement
					1, // descriptorCount
					VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, // descriptorType
					nullptr, // pImageInfo
					&bufferInfo, // pBufferInfo
					nullptr, // pTexelBufferView
				}, {
					VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
					nullptr,
					descriptorSet,
					1,
					0,
					1,
					VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
					&imageInfo,
					nullptr,
					nullptr,
				}
			}};
			writeDescriptorSet(params.device.logical, descriptorWrites);
		}, params.device.logical, params.descriptorSetLayout, descriptorPool.o);
	}()},
	vertexBuffer{createVertexBuffer(
		VerticesView<PlainVertex>{geometry.vertices},
		params.allocator,
		params.commandBufferForLoading,
		params.fenceForLoading,
		params.device
	)},
	indexBuffer{createIndexBuffer(
		geometry.indices,
		params.allocator,
		params.commandBufferForLoading,
		params.fenceForLoading,
		params.device
	)},
	poses{Tag::constructWithUniformArgs, params.allocator}
{}

static bool isDestroyed(PlainModel const &model) {
	return isDestroyed(model.texture.o);
}
PlainModel::~PlainModel() {
	ASSERT(isDestroyed(*this));
}
static void destroy(PlainModel &model, VulkanDevice const &device, VmaAllocator const allocator) {
	destroy(model.texture, allocator, device.logical);
	destroy(model.poses, allocator);
	destroy(model.indexBuffer, allocator);
	destroy(model.vertexBuffer, allocator);
	destroy(model.descriptorPool, device.logical);
}

VulkanDescriptorSetLayout::VulkanDescriptorSetLayout(
	ArrayView<VkDescriptorSetLayoutBinding, true, DescriptorSetLayoutBindingsSize> const bindings,
	VkDevice const logicalDevice
): o{[=]{
	VkDescriptorSetLayoutCreateInfo const descriptorSetLayoutCreateInfo {
		VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		nullptr, // pNext
		0, // flags
		static_cast<U32>(bindings.size), // bindingCount
		getData(bindings), // pBindings
	};
	VkDescriptorSetLayout ret;
	ASSERT_VK_SUCCESS(vkCreateDescriptorSetLayout(
		logicalDevice,
		&descriptorSetLayoutCreateInfo,
		nullptr, // pAllocator
		&ret // pSetLayout
	));
	return ret;
}()} {}

static bool isDestroyed(VulkanDescriptorSetLayout const &dsl) {
	return dsl.o == VK_NULL_HANDLE;
}
static void destroy(VulkanDescriptorSetLayout &dsl, VkDevice const logicalDevice) {
	vkDestroyDescriptorSetLayout(logicalDevice, dsl.o, nullptr);
	dsl.o= VK_NULL_HANDLE; // mark as destroyed
}
VulkanDescriptorSetLayout::~VulkanDescriptorSetLayout() {
	ASSERT(isDestroyed(*this));
}

static VkDescriptorSetLayoutBinding constexpr
	plainPipelineDescriptorSetLayoutBindings[] {
		{
			0, // binding
			VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, // descriptorType
			1, // descriptorCount
			VK_SHADER_STAGE_VERTEX_BIT, // stageFlags
			nullptr, // pImmutableSamplers
		},
		{
			1,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			1,
			VK_SHADER_STAGE_FRAGMENT_BIT,
			nullptr,
		}
	},
	textPipelineDescriptorSetLayoutBindings[] {{
		0,
		VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		1,
		VK_SHADER_STAGE_FRAGMENT_BIT,
		nullptr,
	}},
	groundPipelineDescriptorSetLayoutBindings[] {{
		0,
		VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		1,
		VK_SHADER_STAGE_VERTEX_BIT,
		nullptr,
	}};
Statics::Statics(VulkanInstance const &vulkanInstance, VulkanWindow &vw): Statics{
	vulkanInstance, vw,
	{Tag::null},
	{Tag::null}
} {}
Statics::Statics(
	VulkanInstance const &vulkanInstance,
	VulkanWindow &vw,
	VulkanCommandBuffer cmdBufForLoading,
	VulkanFence fenceForLoading
):
	startTime{std::chrono::high_resolution_clock::now()},
	lastFrameEndTime{startTime},
	extent{500, 500},
	glfwWindow{vulkanInstance, vw, extent},
	surface{createSurfaceFrom(vulkanInstance, glfwWindow)},
	device{vulkanInstance, surface},
	vmaAllocator{([&]{
		fenceForLoading= {device.logical};
	}(), initWithDefaulted<VmaAllocator>([this,vi=vulkanInstance.o](auto &allocator) {
		VmaAllocatorCreateInfo const createInfo {
			0, // flags
			device.physical, // physicalDevice
			device.logical, // device
			0, // preferredLargeHeapBlockSize
			nullptr, // pAllocationCallbacks
			nullptr, // pDeviceMemoryCallbacks
			nullptr, // pHeapSizeLimit
			nullptr, // pVulkanFunctions
			vi, // instance
			minVulkanAPIVersion, // vulkanApiVersion
			nullptr, // pTypeExternalMemoryHandleTypes
		};
		vmaCreateAllocator(&createInfo, &allocator);
	}))},
	depthFormat{getDepthFormat(device)},
	commandPool{[&,this]{
		VkCommandPoolCreateInfo const commandPoolCreateInfo{
			VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, // sType
			nullptr, // pNext (must be null for Vulkan)
			VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, // flags
			device.graphicsQueue.index // queueFamilyIndex
		};
		VkCommandPool ret;
		ASSERT_VK_SUCCESS(vkCreateCommandPool(
			device.logical, // device
			&commandPoolCreateInfo, // pCreateInfo
			nullptr, // pAllocator
			&ret // pCommandPool
		));
		cmdBufForLoading= {ret, device.logical};
		return ret;
	}()},
	plainImageSampler{device},
	plainPipelineDescriptorSetLayout{plainPipelineDescriptorSetLayoutBindings, device.logical},
	groundPipelineDescriptorSetLayout{groundPipelineDescriptorSetLayoutBindings, device.logical},
	perspectiveTransformationMatrixUniformBuffers{
		Tag::constructWithUniformArgs,
		VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
		VMA_MEMORY_USAGE_AUTO,
		VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, VkMemoryPropertyFlags{},
		sizeof(UniformBufferObject), VkDeviceSize{1},
		vmaAllocator,
		nullptr
	},
	groundDescriptorPool{{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER}, device.logical},
	groundDescriptorSets{createDescriptorSets([&](
		VkDescriptorSet const descriptorSet,
		auto const frameI
	) {
		VkDescriptorBufferInfo const uniformBufferInfo {
			perspectiveTransformationMatrixUniformBuffers[frameI].o,
			0,
			sizeof(UniformBufferObject)
		};
		StaticArray descriptorWrites{Tag::listInitialise<VkWriteDescriptorSet>, {{
			VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			nullptr,
			descriptorSet, // dstSet
			0, // dstBinding
			0, // dstArrayElement
			1, // descriptorCount
			VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, // descriptorType
			nullptr, // pImageInfo
			&uniformBufferInfo, // pBufferInfo
			nullptr, // pTextBufferView
		}}};
		writeDescriptorSet(device.logical, descriptorWrites);
	}, device.logical, groundPipelineDescriptorSetLayout.o, groundDescriptorPool.o)},
	houseModel{
		{Tag::ignoreTrailingNull, "viking_room"},
		plainPipelineDescriptorSetLayout.o,
		perspectiveTransformationMatrixUniformBuffers,
		plainImageSampler.o,
		vmaAllocator,
		cmdBufForLoading.o, fenceForLoading.o, device
	},
	cubeModel{
		{Tag::ignoreTrailingNull, "cube"},
		plainPipelineDescriptorSetLayout.o,
		perspectiveTransformationMatrixUniformBuffers,
		plainImageSampler.o,
		vmaAllocator,
		cmdBufForLoading.o, fenceForLoading.o, device
	},
	dietCokeModel{
		{Tag::ignoreTrailingNull, "diet-coke"},
		plainPipelineDescriptorSetLayout.o,
		perspectiveTransformationMatrixUniformBuffers,
		plainImageSampler.o,
		vmaAllocator,
		cmdBufForLoading.o, fenceForLoading.o, device
	},
	commandBuffers{[this]{
		StaticArray<VkCommandBuffer, maxFrameInFlightC> ret{Tag::defaultInitialise};
		VkCommandBufferAllocateInfo const allocateInfo{
			VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, // sType
			nullptr, // pNext (required by Vulkan to be null)
			commandPool, // commandPool
			VK_COMMAND_BUFFER_LEVEL_PRIMARY, // level
			maxFrameInFlightC, // commandBufferCount
		};
		ASSERT_VK_SUCCESS(vkAllocateCommandBuffers(
			device.logical,
			&allocateInfo,
			getData(ret)
		));
		return ret;
	}()},
	drawingSync{device}
{
	destroy(cmdBufForLoading, commandPool, device.logical);
	destroy(fenceForLoading, device.logical);
}

VulkanWindow::VulkanWindow(VulkanInstance const &vulkanInstance):
	statics{vulkanInstance, *this},
	dynamics{statics}
{
/* instantiate a lattice of diet cokes for testing
	float const spacing= 1.3f;
	signed const halfWidth= 13;
	for(signed i=-halfWidth; i<=halfWidth; ++i)
		for(signed j=-halfWidth; j<=halfWidth; ++j)
			for(signed k=-2*halfWidth; k<=0; ++k)
				createBack(
					statics.dietCokeModel.poses[0],
					statics.vmaAllocator,
					PlainModelInstance{ {spacing*i, spacing*j, spacing*k}, {} }
				);
*/
}

static bool isDestroyed(VulkanWindow &vw) {
	return isDestroyed(vw.statics.surface);
}

static void destroy(Statics &statics, VulkanInstance const &vulkanInstance) {
	destroy(statics.drawingSync, statics.device);
	destroy(statics.groundDescriptorPool, statics.device.logical);
	destroy(statics.plainPipelineDescriptorSetLayout, statics.device.logical);
	destroy(statics.groundPipelineDescriptorSetLayout, statics.device.logical);
	vkDestroyCommandPool(statics.device.logical, statics.commandPool, nullptr);
	destroy(statics.plainImageSampler, statics.device.logical);
	destroy(statics.surface, vulkanInstance);
	destroy(statics.cubeModel, statics.device, statics.vmaAllocator);
	destroy(statics.houseModel, statics.device, statics.vmaAllocator);
	destroy(statics.dietCokeModel, statics.device, statics.vmaAllocator);
	for(VulkanBuffer &buffer : statics.perspectiveTransformationMatrixUniformBuffers)
		destroy(buffer, statics.vmaAllocator);
}

static bool isDestroyed(Statics const &statics) {
	return isDestroyed(statics.surface);
}

Statics::~Statics() {
	vmaDestroyAllocator(vmaAllocator);
	ASSERT(isDestroyed(*this));
}

void destroy(VulkanWindow &vw, VulkanInstance const &vulkanInstance) {
	vkDeviceWaitIdle(vw.statics.device.logical);
	destroy(vw.dynamics, vw.statics);
	destroy(vw.statics, vulkanInstance);
}

VulkanWindow::~VulkanWindow() {
	ASSERT(isDestroyed(*this));
}

static void recordDrawPlainModels(
	VkCommandBuffer const cmdBuf,
	Program &program,
	unsigned const currentFrameI
) {
	auto &vw= program.vulkanWindow;
	vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, vw.dynamics.plainPipeline.o);
	auto const &recordDraw= [&vw, currentFrameI, cmdBuf](PlainModel &plainModel, auto &&modifyPoses){
		auto &poses = plainModel.poses[currentFrameI];
		modifyPoses(poses);
		VkBuffer const vertexBuffers[] {
			plainModel.vertexBuffer.o,
			poses.o.o,
		};
		VkDeviceSize constexpr offsets[] {0, 0};
		vkCmdBindVertexBuffers(cmdBuf, 0, length(vertexBuffers), vertexBuffers, offsets);
		vkCmdBindIndexBuffer(cmdBuf, plainModel.indexBuffer.o, 0, VK_INDEX_TYPE_UINT32);
		vkCmdBindDescriptorSets(
			cmdBuf,
			VK_PIPELINE_BIND_POINT_GRAPHICS,
			vw.dynamics.plainPipeline.layout,
			0,
			1, &plainModel.descriptorSets[currentFrameI],
			0, nullptr
		);
		vkCmdDrawIndexed(cmdBuf, 3 * plainModel.triangleC, poses.size, 0, 0, 0);
	};
	std::reference_wrapper<PlainModel> const plainModels[] {
		vw.statics.houseModel,
		vw.statics.cubeModel,
	};
	for(PlainModel &plainModel : plainModels)
		recordDraw(plainModel, [](auto const&){});
	recordDraw(vw.statics.dietCokeModel, [&ns= program.networkingState, &vma= vw.statics.vmaAllocator](auto &poses) {
		std::lock_guard g{ns.mutex};
		WATCH(size(ns.otherPlayers));
		for(; poses.size != size(ns.otherPlayers);)
			createBack(poses, vma, PlainModelInstance{
				{{0, 0, 0}},
				{0.f, 0.f, 0.f, 0.f},
			});
		foreach(ns.otherPlayers, [&poses](auto const filledI, auto, auto const &player) {
//			WATCH(filledI);
//			std::cout << "player at (" << getX(player.position) << ", " << getY(player.position) << ", " << getZ(player.position) << ")\n";
			poses[filledI].position= player.position;
		});
//		WATCH(poses.size);
	});
}

static void recordDrawGround(
	VkCommandBuffer const cmdBuf,
	VulkanWindow &vw,
	unsigned const currentFrameI
) {
	auto const &[statics, dyns] = vw;
	vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, dyns.groundPipeline.o);
	vkCmdBindDescriptorSets(
		cmdBuf,
		VK_PIPELINE_BIND_POINT_GRAPHICS,
		dyns.groundPipeline.layout,
		0,
		1, &statics.groundDescriptorSets[currentFrameI],
		0, nullptr
	);
	vkCmdDraw(cmdBuf, 4, 1, 0, 0);
}

static void recordDraws(
	VkCommandBuffer const cmdBuf,
	Program &program,
	unsigned const currentFrameI
) {
	recordDrawPlainModels(cmdBuf, program, currentFrameI);
	recordDrawGround(cmdBuf, program.vulkanWindow, currentFrameI);
}

static void recordRender(
	VkCommandBuffer const cmdBuf,
	Program &program,
	unsigned const currentFrameI,
	unsigned const imageI
) {
	auto const &[statics, dyns] = program.vulkanWindow;
	VkCommandBufferBeginInfo const beginInfo{
		VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, // sType
		nullptr, // pNext
		0, // flags
		nullptr, // pInheritanceInfo
	};
	ASSERT_VK_SUCCESS(vkBeginCommandBuffer(cmdBuf, &beginInfo));
	VkClearValue clearValues[2];
	float const colourClearValue[] {0.f, 0.f, 0.f, 1.f};
	std::memcpy(clearValues[0].color.float32, colourClearValue, sizeof colourClearValue);
	clearValues[1].depthStencil= {1.f, 0};
	VkRenderPassBeginInfo const renderPassBeginInfo{
		VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO, // sType
		nullptr, // pNext
		dyns.renderPass, // renderPass
		dyns.framesObjects[imageI].framebuffer, // framebuffer
		{ // renderArea
			{0, 0}, // offset
			statics.extent, // extent
		},
		length(clearValues), // clearValueCount
		clearValues, // pClearValues
	};
	vkCmdBeginRenderPass(cmdBuf, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
	VkViewport const viewport {
		0.f, 0.f,
		static_cast<float>(statics.extent.width), static_cast<float>(statics.extent.height),
		0.f, 1.f,
	};
	vkCmdSetViewport(cmdBuf, 0, 1, &viewport);
	VkRect2D const scissor {
		{0, 0},
		{statics.extent.width, statics.extent.height},
	};
	vkCmdSetScissor(cmdBuf, 0, 1, &scissor);
	recordDraws(cmdBuf, program, currentFrameI);
	vkCmdEndRenderPass(cmdBuf);
	ASSERT_VK_SUCCESS(vkEndCommandBuffer(cmdBuf));
}

// partly taken from GLM's perspective functions
template<typename T>
glm::mat<4, 4, T> perspectiveMatrix(T const fovy, T const aspect, T const zNear, T const zFar) {
	T const tanHalfFovy = tan(fovy / static_cast<T>(2));
	assert(abs(aspect - std::numeric_limits<T>::epsilon()) > static_cast<T>(0));
	T const zero{0};
	return {
		static_cast<T>(1) / (aspect * tanHalfFovy), zero, zero, zero,
		zero, static_cast<T>(1) / tanHalfFovy, zero, zero,
		zero, zero, zFar / (zFar - zNear), static_cast<T>(1.f),
		zero, zero, (zNear * zFar) / (zNear - zFar), zero,
	};
}

static void updateTransformMatrixBuffer(VulkanBuffer &uniformBuffer, Statics const &statics) {
	UniformBufferObject const ubo{
		glm::rotate(
			glm::rotate(
				perspectiveMatrix(
					tau * 110.f/360.f,
					static_cast<float>(statics.extent.width) / static_cast<float>(statics.extent.height),
					.1f, 100.f
				) * glm::mat4{
					1.f, 0.f, 0.f, 0.f,
					0.f, 0.f, 1.f, 0.f,
					0.f, -1.f, 0.f, 0.f,
					0.f, 0.f, 0.f, 1.f,
				},
				statics.camera.pitch,
				glm::vec3{1.f, 0.f, 0.f}
			),
			-statics.camera.yaw,
			glm::vec3{0.f, 0.f, 1.f}
		),
		statics.camera.position,
	};
	void *bufferMemory;
	ASSERT_VK_SUCCESS(vmaMapMemory(statics.vmaAllocator, uniformBuffer.allocation, &bufferMemory));
	std::memcpy(bufferMemory, &ubo, sizeof ubo);
	vmaUnmapMemory(statics.vmaAllocator, uniformBuffer.allocation);
}

static U32 getNextImageI(VulkanWindow &vw, FrameIndex const currentFrameI) {
	auto&[statics, dyns] = vw;
	DrawingSyncObjects &sync = statics.drawingSync;
	VulkanDevice const &device = statics.device;
	U32 imageI;
	while(true) {
		VkResult const acquireResult= vkAcquireNextImageKHR(
			vw.statics.device.logical,
			dyns.swapchain.o,
			UINT64_MAX,
			sync.imageAvailableSemaphores[currentFrameI],
			VK_NULL_HANDLE,
			&imageI
		);
		if(acquireResult == VK_ERROR_OUT_OF_DATE_KHR) {
			std::cout << "able to acquire a swapchain image, but Vulkan says the swapchain is out of date. recreating it...\n";
			recreateSwapchain(vw);
			continue;
		}
		ASSERT(acquireResult == VK_SUCCESS || acquireResult == VK_SUBOPTIMAL_KHR);
		break;
	}
	if(dyns.mapImageFence[imageI] != VK_NULL_HANDLE)
		ASSERT_VK_SUCCESS(vkWaitForFences(device.logical, 1, getData(dyns.mapImageFence)+imageI, VK_TRUE, UINT64_MAX));
	return imageI;
}

static void renderFrame(Program &program, FramesSize const currentFrameI, U32 const imageI) {
	auto &vw= program.vulkanWindow;
	auto&[statics, dyns] = vw;
	DrawingSyncObjects &sync = statics.drawingSync;
	VulkanDevice const &device = statics.device;
	updateTransformMatrixBuffer(
		statics.perspectiveTransformationMatrixUniformBuffers[currentFrameI],
		statics
	);
	dyns.mapImageFence[imageI]= sync.frameInFlightFences[currentFrameI];
	VkPipelineStageFlags const waitStage= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	VkCommandBuffer const cmdBuf = statics.commandBuffers[currentFrameI];
	VkSubmitInfo const submitInfo{
		VK_STRUCTURE_TYPE_SUBMIT_INFO, // sType
		nullptr, // pNext
		1, // waitSemaphoreCount
		&sync.imageAvailableSemaphores[currentFrameI], // pWaitSemaphores
		&waitStage, // pWaitDstStageMask
		1, // commandBufferCount
		&cmdBuf, // pCommandBuffers
		1, // signalSemaphoreCount
		&sync.renderFinishedSemaphores[currentFrameI] // pSignalSemaphores
	};
	ASSERT_VK_SUCCESS(vkWaitForFences(device.logical, 1, sync.frameInFlightFences+currentFrameI, VK_TRUE, UINT64_MAX));
	ASSERT_VK_SUCCESS(vkResetFences(device.logical, 1, sync.frameInFlightFences+currentFrameI));
	ASSERT_VK_SUCCESS(vkResetCommandBuffer(cmdBuf, /*VkCommandBufferResetFlagBits*/ 0));
	recordRender(cmdBuf, program, currentFrameI, imageI);
	ASSERT_VK_SUCCESS(vkQueueSubmit(
		device.graphicsQueue.o,
		1,
		&submitInfo,
		sync.frameInFlightFences[currentFrameI] // fence
	));
	VkResult result;
	VkPresentInfoKHR const presentInfo{
		VK_STRUCTURE_TYPE_PRESENT_INFO_KHR, // sType
		nullptr, // pNext
		1, // waitSemaphoreCount
		&sync.renderFinishedSemaphores[currentFrameI], // pWaitSemaphores
		1, // swapchainCount
		&dyns.swapchain.o, // pSwapchains
		&imageI, // pImageIndices
		&result, // pResults
	};
	if(VkResult const presentResult = vkQueuePresentKHR(
		device.presentQueue.o,
		&presentInfo
	); presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR)
		recreateSwapchain(vw);
	else
		ASSERT_VK_SUCCESS(result);

	++statics.renderedFrameC;
	auto const currentTime= std::chrono::high_resolution_clock::now();
	auto const prevElapsed= statics.lastFrameEndTime - statics.startTime;
	auto const currElapsed= currentTime - statics.startTime;
	if(
		std::chrono::duration_cast<std::chrono::seconds>(prevElapsed).count()
		< std::chrono::duration_cast<std::chrono::seconds>(currElapsed).count()
	) {
//		WATCH(statics.renderedFrameC);
		statics.renderedFrameC= 0;
	}
	vw.statics.lastFrameEndTime= currentTime;
}

static void handleKeys(VulkanWindow &vw, U32 const frameI) {
	auto constexpr speed = 0.03f;
	// forward is +Y
	auto const fwdX= -speed * std::sin(vw.statics.camera.yaw);
	auto const fwdY= speed * std::cos(vw.statics.camera.yaw);
	auto const endP= end(vw.statics.justPressedKeys);
	auto const endH= end(vw.statics.heldKeys);
	auto const &heldKeys= vw.statics.heldKeys;
	auto const &justPressedKeys= vw.statics.justPressedKeys;
	if(heldKeys.find(GLFW_KEY_W) != endH) {
		getX(vw.statics.camera.position) += fwdX;
		getY(vw.statics.camera.position) += fwdY;
	}
	if(heldKeys.find(GLFW_KEY_S) != endH) {
		getX(vw.statics.camera.position) -= fwdX;
		getY(vw.statics.camera.position) -= fwdY;
	}
	if(heldKeys.find(GLFW_KEY_A) != endH) {
		getX(vw.statics.camera.position) -= fwdY;
		getY(vw.statics.camera.position) += fwdX;
	}
	if(heldKeys.find(GLFW_KEY_D) != endH) {
		getX(vw.statics.camera.position) += fwdY;
		getY(vw.statics.camera.position) -= fwdX;
	}
	if(heldKeys.find(GLFW_KEY_SPACE) != endH)
		getZ(vw.statics.camera.position) += speed;
	if(heldKeys.find(GLFW_KEY_LEFT_CONTROL) != endH || heldKeys.find(GLFW_KEY_RIGHT_CONTROL) != endH)
		getZ(vw.statics.camera.position) -= speed;
	std::tuple<unsigned, std::reference_wrapper<PlainModel>, char const*> modelKeybinds[] {
//		{ GLFW_KEY_E, vw.statics.dietCokeModel, "cans of diet coke" },
//		{ GLFW_KEY_F, vw.statics.houseModel, "houses" },
//		{ GLFW_KEY_C, vw.statics.cubeModel, "cubes" },
	};
	for(auto const &[key, model, description] : modelKeybinds)
		if(justPressedKeys.find(key) != endP) {
			// add a new pose to the array of poses, possibly regrowing the array
			createBack(
				model.get().poses[frameI],
				vw.statics.vmaAllocator,
				PlainModelInstance{
					vw.statics.camera.position,
					{}
				}
			);
			std::cout << "there are now " << model.get().poses[frameI].size << ' ' << description << '\n';
		}
	if constexpr(shouldPrintCameraInfo) {
		WATCH(getX(vw.statics.camera.position));
		WATCH(getY(vw.statics.camera.position));
		WATCH(getZ(vw.statics.camera.position));
	}
}

void drawFrames(Program &program) {
	auto &vw= program.vulkanWindow;
	for(
		FrameIndex frameI=0;
		!glfwWindowShouldClose(&vw.statics.glfwWindow.o);
		frameI= (frameI+1) % maxFrameInFlightC
	) {
		vw.statics.justPressedKeys.clear();
		glfwPollEvents();
		U32 const nextImageI = getNextImageI(vw, frameI);
		std::reference_wrapper<PlainModel> models[]= {
			vw.statics.dietCokeModel,
			vw.statics.houseModel,
			vw.statics.cubeModel
		};
		handleKeys(vw, frameI);
		renderFrame(program, frameI, nextImageI);
		for(PlainModel &model : models)
			copy(
				model.poses[(frameI+1)%maxFrameInFlightC],
				model.poses[frameI],
				vw.statics.vmaAllocator
			);
	}
}
