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
    textureBudget_ = budgets_.registerBudget("gpu.textures", config.textureBudgetBytes);

    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = config.appName;
    appInfo.pEngineName = "MobileGamesEngine";
    appInfo.apiVersion = VK_API_VERSION_1_1;

    VkInstanceCreateInfo instanceInfo{};
    instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instanceInfo.pApplicationInfo = &appInfo;
    // Surface extensions come from the platform layer (P3): the engine never
    // names an Android/X11/Win32 extension itself.
    instanceInfo.enabledExtensionCount = config.instanceExtensionCount;
    instanceInfo.ppEnabledExtensionNames = config.instanceExtensions;

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

    const char* const kSwapchainExtension = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    VkDeviceCreateInfo deviceInfo{};
    deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceInfo.queueCreateInfoCount = 1;
    deviceInfo.pQueueCreateInfos = &queueInfo;
    if (config.enableSwapchain) {
        deviceInfo.enabledExtensionCount = 1;
        deviceInfo.ppEnabledExtensionNames = &kSwapchainExtension;
        swapchainEnabled_ = true;
    }

    if (vkCreateDevice(physicalDevice_, &deviceInfo, nullptr, &device_) != VK_SUCCESS) {
        MGE_LOGE(kTag, "vkCreateDevice failed");
        shutdown();
        return false;
    }
    vkGetDeviceQueue(device_, graphicsQueueFamily_, 0, &graphicsQueue_);

    // Pick the texture pack this device can sample, best first. ASTC is the
    // primary shipping pack (>80% of Play devices), ETC2 the safe fallback
    // (>95%); BC covers desktop; Uncompressed always works and is what
    // headless verification runs on.
    const TexturePack order[] = {TexturePack::Astc, TexturePack::Etc2, TexturePack::Bc,
                                 TexturePack::Uncompressed};
    texturePack_ = TexturePack::Uncompressed;
    for (TexturePack candidate : order) {
        if (supportsPack(candidate)) {
            texturePack_ = candidate;
            break;
        }
    }

    MGE_LOGI(kTag, "device ready: %s (gpu budget %zu MiB, texture budget %zu MiB, %s pack)",
             deviceName_, config.gpuBudgetBytes / (1024 * 1024),
             config.textureBudgetBytes / (1024 * 1024), texturePackName(texturePack_));
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

VkFormat toVkFormat(TextureFormat format, ColorSpace space) {
    const bool srgb = space == ColorSpace::Srgb;
    switch (format) {
        case TextureFormat::Rgba8:
            return srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
        case TextureFormat::Bc1Rgb:
            return srgb ? VK_FORMAT_BC1_RGB_SRGB_BLOCK : VK_FORMAT_BC1_RGB_UNORM_BLOCK;
        case TextureFormat::Bc3Rgba:
            return srgb ? VK_FORMAT_BC3_SRGB_BLOCK : VK_FORMAT_BC3_UNORM_BLOCK;
        case TextureFormat::Bc7Rgba:
            return srgb ? VK_FORMAT_BC7_SRGB_BLOCK : VK_FORMAT_BC7_UNORM_BLOCK;
        case TextureFormat::Astc4x4:
            return srgb ? VK_FORMAT_ASTC_4x4_SRGB_BLOCK : VK_FORMAT_ASTC_4x4_UNORM_BLOCK;
        case TextureFormat::Astc5x5:
            return srgb ? VK_FORMAT_ASTC_5x5_SRGB_BLOCK : VK_FORMAT_ASTC_5x5_UNORM_BLOCK;
        case TextureFormat::Astc6x6:
            return srgb ? VK_FORMAT_ASTC_6x6_SRGB_BLOCK : VK_FORMAT_ASTC_6x6_UNORM_BLOCK;
        case TextureFormat::Astc8x8:
            return srgb ? VK_FORMAT_ASTC_8x8_SRGB_BLOCK : VK_FORMAT_ASTC_8x8_UNORM_BLOCK;
        case TextureFormat::Etc2Rgb8:
            return srgb ? VK_FORMAT_ETC2_R8G8B8_SRGB_BLOCK : VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK;
        case TextureFormat::Etc2Rgba8:
            return srgb ? VK_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK
                        : VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK;
        case TextureFormat::EacRg11:
            // A two-channel normal map; there is no sRGB variant and there
            // should not be one.
            return VK_FORMAT_EAC_R11G11_UNORM_BLOCK;
        default:
            return VK_FORMAT_UNDEFINED;
    }
}

VkDeviceMemory VulkanDevice::allocateFrom(BudgetId budget,
                                          const VkMemoryRequirements& requirements,
                                          VkMemoryPropertyFlags properties) {
    const uint32_t typeIndex = findMemoryType(requirements.memoryTypeBits, properties);
    if (typeIndex == UINT32_MAX) return VK_NULL_HANDLE;

    // Budget first: an allocation the cap refuses never reaches the driver.
    if (!budgets_.charge(budget, static_cast<size_t>(requirements.size))) {
        return VK_NULL_HANDLE;
    }

    VkMemoryAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocateInfo.allocationSize = requirements.size;
    allocateInfo.memoryTypeIndex = typeIndex;

    VkDeviceMemory memory = VK_NULL_HANDLE;
    if (vkAllocateMemory(device_, &allocateInfo, nullptr, &memory) != VK_SUCCESS) {
        budgets_.release(budget, static_cast<size_t>(requirements.size));
        return VK_NULL_HANDLE;
    }
    return memory;
}

VkDeviceMemory VulkanDevice::allocate(const VkMemoryRequirements& requirements,
                                      VkMemoryPropertyFlags properties) {
    return allocateFrom(gpuBudget_, requirements, properties);
}

void VulkanDevice::free(VkDeviceMemory memory, VkDeviceSize size) {
    if (memory == VK_NULL_HANDLE) return;
    vkFreeMemory(device_, memory, nullptr);
    budgets_.release(gpuBudget_, static_cast<size_t>(size));
}

VkDeviceMemory VulkanDevice::allocateTexture(const VkMemoryRequirements& requirements,
                                             VkMemoryPropertyFlags properties) {
    return allocateFrom(textureBudget_, requirements, properties);
}

void VulkanDevice::freeTexture(VkDeviceMemory memory, VkDeviceSize size) {
    if (memory == VK_NULL_HANDLE) return;
    vkFreeMemory(device_, memory, nullptr);
    budgets_.release(textureBudget_, static_cast<size_t>(size));
}

bool VulkanDevice::supportsTextureFormat(TextureFormat format) const {
    if (physicalDevice_ == VK_NULL_HANDLE) return false;
    const VkFormat vk = toVkFormat(format, ColorSpace::Linear);
    if (vk == VK_FORMAT_UNDEFINED) return false;
    VkFormatProperties properties{};
    vkGetPhysicalDeviceFormatProperties(physicalDevice_, vk, &properties);
    // Sampling with a filter is the whole requirement: the engine never
    // renders into a compressed image, it only reads one.
    constexpr VkFormatFeatureFlags needed =
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    return (properties.optimalTilingFeatures & needed) == needed;
}

bool VulkanDevice::supportsPack(TexturePack pack) const {
    switch (pack) {
        case TexturePack::Uncompressed: return supportsTextureFormat(TextureFormat::Rgba8);
        case TexturePack::Bc: return supportsTextureFormat(TextureFormat::Bc7Rgba);
        case TexturePack::Astc: return supportsTextureFormat(TextureFormat::Astc6x6);
        case TexturePack::Etc2: return supportsTextureFormat(TextureFormat::Etc2Rgb8);
        default: return false;
    }
}

}  // namespace mge
