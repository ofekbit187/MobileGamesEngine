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
#include "mge/graphics/texture_data.h"

namespace mge {

// The Vulkan format a baked texture maps to. Colour space is carried by the
// TEXTURE (docs/TEXTURING.md §4), so an sRGB albedo asks for an sRGB-typed
// format and the hardware linearises on every fetch for free — no shader-side
// pow(), and no roughness map accidentally read through a gamma curve.
VkFormat toVkFormat(TextureFormat format, ColorSpace space);

struct VulkanDeviceConfig {
    const char* appName = "MobileGamesEngine";
    size_t gpuBudgetBytes = 256u * 1024u * 1024u;
    // Texture memory is its OWN registered budget, refused at cap like every
    // other one (docs/TEXTURING.md §5). 64 MiB is the mid-tier proposal; the
    // number is the owner's per-device-tier call, the mechanism is not
    // optional. Kept separate from the mesh budget so a texture leak cannot
    // hide inside geometry headroom.
    size_t textureBudgetBytes = 64u * 1024u * 1024u;
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

    // The compressed-texture pack this device can actually sample, chosen at
    // init from the Vulkan feature bits. Play's App Bundle texture targeting
    // ships one pack per device; this is the runtime half of that choice.
    // Falls back to Uncompressed, which every device supports — that is the
    // pack headless verification runs on, since llvmpipe has neither ASTC nor
    // ETC2 (it does have BC).
    TexturePack preferredTexturePack() const { return texturePack_; }
    bool supportsPack(TexturePack pack) const;
    // True when the format can be sampled with mips and filtering here.
    bool supportsTextureFormat(TextureFormat format) const;

    // Budgeted device-memory allocation: charges the GPU budget first and
    // returns VK_NULL_HANDLE (allocating nothing) if the cap refuses.
    VkDeviceMemory allocate(const VkMemoryRequirements& requirements,
                            VkMemoryPropertyFlags properties);
    // `size` must be the requirements.size the allocation was made with.
    void free(VkDeviceMemory memory, VkDeviceSize size);

    // Texture image memory charges the texture budget; everything else
    // charges the general GPU budget.
    VkDeviceMemory allocateTexture(const VkMemoryRequirements& requirements,
                                   VkMemoryPropertyFlags properties);
    void freeTexture(VkDeviceMemory memory, VkDeviceSize size);

    BudgetStats gpuBudgetStats() const { return budgets_.stats(gpuBudget_); }
    BudgetStats textureBudgetStats() const { return budgets_.stats(textureBudget_); }

private:
    uint32_t findMemoryType(uint32_t typeBits, VkMemoryPropertyFlags properties) const;
    VkDeviceMemory allocateFrom(BudgetId budget, const VkMemoryRequirements& requirements,
                                VkMemoryPropertyFlags properties);

    BudgetRegistry& budgets_;
    BudgetId gpuBudget_ = kInvalidBudget;
    BudgetId textureBudget_ = kInvalidBudget;
    TexturePack texturePack_ = TexturePack::Uncompressed;

    VkInstance instance_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue graphicsQueue_ = VK_NULL_HANDLE;
    uint32_t graphicsQueueFamily_ = 0;
    bool swapchainEnabled_ = false;
    char deviceName_[VK_MAX_PHYSICAL_DEVICE_NAME_SIZE] = {};
};

}  // namespace mge
