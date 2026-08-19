#pragma once

// Vulkan bring-up (task 2.1): instance, physical device selection, logical
// device + graphics queue, and budgeted device-memory allocation. GPU memory
// is a BudgetRegistry budget like everything else (P1): allocations that
// would exceed the cap are refused before they reach the driver.
//
// Surface/swapchain integration is the Android half of 2.1 and lands with the
// app wiring; this class is windowless on purpose so it runs headless (CI,
// remote dev, llvmpipe) and on-device alike.

#include <vulkan/vulkan.h>

#include "mge/core/memory.h"

namespace mge {

struct VulkanDeviceConfig {
    const char* appName = "MobileGamesEngine";
    size_t gpuBudgetBytes = 256u * 1024u * 1024u;
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
    char deviceName_[VK_MAX_PHYSICAL_DEVICE_NAME_SIZE] = {};
};

}  // namespace mge
