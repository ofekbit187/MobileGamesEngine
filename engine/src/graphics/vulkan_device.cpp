#include "mge/graphics/vulkan_device.h"

#include <cstring>
#include <vector>

#include "mge/core/log.h"

namespace mge {

namespace {
constexpr const char* kTag = "vulkan";
}

VulkanDevice::VulkanDevice(BudgetRegistry& budgets) : budgets_(budgets) {}

VulkanDevice::~VulkanDevice() { shutdown(); }

bool VulkanDevice::init(const VulkanDeviceConfig& config) {
    if (initialized()) return true;

    gpuBudget_ = budgets_.registerBudget("gpu", config.gpuBudgetBytes);

    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = config.appName;
    appInfo.pEngineName = "MobileGamesEngine";
    appInfo.apiVersion = VK_API_VERSION_1_1;

    VkInstanceCreateInfo instanceInfo{};
    instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instanceInfo.pApplicationInfo = &appInfo;

    if (vkCreateInstance(&instanceInfo, nullptr, &instance_) != VK_SUCCESS) {
        MGE_LOGE(kTag, "vkCreateInstance failed");
        return false;
    }

    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance_, &deviceCount, nullptr);
    if (deviceCount == 0) {
        MGE_LOGE(kTag, "no Vulkan physical devices");
        shutdown();
        return false;
    }
    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance_, &deviceCount, devices.data());

    // Prefer a discrete GPU, then integrated (the common Android case), then
    // anything that has a graphics queue (llvmpipe in headless environments).
    int bestScore = -1;
    for (VkPhysicalDevice candidate : devices) {
        VkPhysicalDeviceProperties properties;
        vkGetPhysicalDeviceProperties(candidate, &properties);

        uint32_t familyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, nullptr);
        std::vector<VkQueueFamilyProperties> families(familyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, families.data());

        int graphicsFamily = -1;
        for (uint32_t i = 0; i < familyCount; ++i) {
            if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                graphicsFamily = static_cast<int>(i);
                break;
            }
        }
        if (graphicsFamily < 0) continue;

        int score = 1;
        if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) score = 2;
        if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) score = 3;
        if (score > bestScore) {
            bestScore = score;
            physicalDevice_ = candidate;
            graphicsQueueFamily_ = static_cast<uint32_t>(graphicsFamily);
            strncpy(deviceName_, properties.deviceName, sizeof(deviceName_) - 1);
        }
    }
    if (physicalDevice_ == VK_NULL_HANDLE) {
        MGE_LOGE(kTag, "no device with a graphics queue");
        shutdown();
        return false;
    }

    const float priority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{};
    queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueInfo.queueFamilyIndex = graphicsQueueFamily_;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &priority;

    VkDeviceCreateInfo deviceInfo{};
    deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceInfo.queueCreateInfoCount = 1;
    deviceInfo.pQueueCreateInfos = &queueInfo;

    if (vkCreateDevice(physicalDevice_, &deviceInfo, nullptr, &device_) != VK_SUCCESS) {
        MGE_LOGE(kTag, "vkCreateDevice failed");
        shutdown();
        return false;
    }
    vkGetDeviceQueue(device_, graphicsQueueFamily_, 0, &graphicsQueue_);

    MGE_LOGI(kTag, "device ready: %s (gpu budget %zu MiB)", deviceName_,
             config.gpuBudgetBytes / (1024 * 1024));
    return true;
}

void VulkanDevice::shutdown() {
    if (device_ != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device_);
        vkDestroyDevice(device_, nullptr);
        device_ = VK_NULL_HANDLE;
    }
    if (instance_ != VK_NULL_HANDLE) {
        vkDestroyInstance(instance_, nullptr);
        instance_ = VK_NULL_HANDLE;
    }
    physicalDevice_ = VK_NULL_HANDLE;
    graphicsQueue_ = VK_NULL_HANDLE;
}

uint32_t VulkanDevice::findMemoryType(uint32_t typeBits, VkMemoryPropertyFlags properties) const {
    VkPhysicalDeviceMemoryProperties memoryProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &memoryProperties);
    for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i) {
        if ((typeBits & (1u << i)) &&
            (memoryProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    return UINT32_MAX;
}

VkDeviceMemory VulkanDevice::allocate(const VkMemoryRequirements& requirements,
                                      VkMemoryPropertyFlags properties) {
    const uint32_t typeIndex = findMemoryType(requirements.memoryTypeBits, properties);
    if (typeIndex == UINT32_MAX) return VK_NULL_HANDLE;

    // Budget first: an allocation the cap refuses never reaches the driver.
    if (!budgets_.charge(gpuBudget_, static_cast<size_t>(requirements.size))) {
        return VK_NULL_HANDLE;
    }

    VkMemoryAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocateInfo.allocationSize = requirements.size;
    allocateInfo.memoryTypeIndex = typeIndex;

    VkDeviceMemory memory = VK_NULL_HANDLE;
    if (vkAllocateMemory(device_, &allocateInfo, nullptr, &memory) != VK_SUCCESS) {
        budgets_.release(gpuBudget_, static_cast<size_t>(requirements.size));
        return VK_NULL_HANDLE;
    }
    return memory;
}

void VulkanDevice::free(VkDeviceMemory memory, VkDeviceSize size) {
    if (memory == VK_NULL_HANDLE) return;
    vkFreeMemory(device_, memory, nullptr);
    budgets_.release(gpuBudget_, static_cast<size_t>(size));
}

}  // namespace mge
