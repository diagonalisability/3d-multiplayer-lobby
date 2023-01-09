#pragma once
#include <string>
#include <unordered_map>
#include <vulkan/vulkan.h>
// https://www.khronos.org/registry/vulkan/specs/1.2-extensions/man/html/VkPhysicalDeviceType.html
extern std::unordered_map<VkPhysicalDeviceType, std::string> const mapVulkanPhysicalDeviceTypeToString;
// https://www.khronos.org/registry/vulkan/specs/1.2-extensions/man/html/VkSurfaceFormatKHR.html
extern std::unordered_map<VkFormat, std::string> const mapVulkanFormatToString;
// https://www.khronos.org/registry/vulkan/specs/1.2-extensions/man/html/VkColorSpaceKHR.html
extern std::unordered_map<VkColorSpaceKHR, std::string> const mapVulkanColourspaceToString;
extern std::unordered_map<VkPresentModeKHR, std::string> const mapVulkanPresentModeToString;
extern std::unordered_map<VkSurfaceTransformFlagBitsKHR, std::string> const mapVulkanSurfaceTransformToString;
extern std::unordered_map<VkResult, std::string> const mapVulkanResultToString;
