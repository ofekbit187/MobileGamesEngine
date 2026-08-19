#pragma once

// Vulkan bring-up (task 2.1): instance, physical device selection, logical
// device + graphics queue, and budgeted device-memory allocation. GPU memory
// is a BudgetRegistry budget like everything else (P1): allocations that
// would exceed the cap are refused before they reach the driver.
//
// The device is windowless by default so it runs headless (CI, remote dev,
// llvmpipe) and on-device alike; presentation is opt-in through the config
// (see Swapchain), keeping the surface itself in platform code (P3).

#include <vulkan/vulkan.h>

#include "mge/core/memory.h"

namespace mge {

struct VulkanDeviceConfig {
    const char* appName = "MobileGamesEngine";
    size_t gpuBudgetBytes = 256u * 1024u * 1024u;
    // Presentation (task 2.1): the platform layer names the surface
    // extensions it needs (e.g. VK_KHR_surface + VK_KHR_android_surface) and
    // asks for the swapchain device extension. Headless stays the default —
    // the same device code runs in CI with none of this.
    const char* const* instanceExtensions = nullptr;
    uint32_t instanceExtensionCount = 0;
    bool enableSwapchain = false;
};

class VulkanDevice {
public:
    explicit VulkanDevice(BudgetRegistry& budgets);
    ~VulkanDevice();

    VulkanDevice(const VulkanDevice&) = delete;
    VulkanDevice& operator=(const VulkanDevice&) = delete;

    bool init(const VulkanDeviceConfig& config);
    void shutdown();
    bool initialized() const { return device_ != VK_NULL_HANDLE; }

    VkInstance instance() const { return instance_; }
    VkPhysicalDevice physicalDevice() const { return physicalDevice_; }
    VkDevice device() const { return device_; }
    VkQueue graphicsQueue() const { return graphicsQueue_; }
    uint32_t graphicsQueueFamily() const { return graphicsQueueFamily_; }
    const char* deviceName() const { return deviceName_; }
    bool swapchainEnabled() const { return swapchainEnabled_; }

    // Budgeted device-memory allocation: charges the GPU budget first and
    // returns VK_NULL_HANDLE (allocating nothing) if the cap refuses.
    VkDeviceMemory allocate(const VkMemoryRequirements& requirements,
                            VkMemoryPropertyFlags properties);
    // `size` must be the requirements.size the allocation was made with.
    void free(VkDeviceMemory memory, VkDeviceSize size);

    BudgetStats gpuBudgetStats() const { return budgets_.stats(gpuBudget_); }

private:
    uint32_t findMemoryType(uint32_t typeBits, VkMemoryPropertyFlags properties) const;

    BudgetRegistry& budgets_;
    BudgetId gpuBudget_ = kInvalidBudget;

    VkInstance instance_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue graphicsQueue_ = VK_NULL_HANDLE;
    uint32_t graphicsQueueFamily_ = 0;
    bool swapchainEnabled_ = false;
    char deviceName_[VK_MAX_PHYSICAL_DEVICE_NAME_SIZE] = {};
};

}  // namespace mge
