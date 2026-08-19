#pragma once

// Presentation (task 2.1, the on-device half). The renderer draws into its
// offscreen color target; this class hands that image to the display:
// acquire a swapchain image, blit (scaling and format-converting in the
// driver), present. No CPU readback, no per-frame copy through main memory —
// the ANativeWindow_lock blit it replaces cost a full framebuffer round trip
// every frame.
//
// The VkSurfaceKHR is created by platform code (P3: vkCreateAndroidSurfaceKHR
// lives in the app module) and handed here; the engine stays windowless.
// Surface loss and resizes are normal answers: present() reports when the
// swapchain went stale so the caller recreates it.

#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

#include "mge/graphics/renderer.h"
#include "mge/graphics/vulkan_device.h"

namespace mge {

class Swapchain {
public:
    ~Swapchain();

    Swapchain(const Swapchain&) = delete;
    Swapchain& operator=(const Swapchain&) = delete;
    Swapchain() = default;

    // Takes ownership of nothing: the surface outlives this object and is
    // destroyed by whoever created it.
    bool init(VulkanDevice& device, VkSurfaceKHR surface);
    // Rebuilds against the surface's current size (rotation, resize, or a
    // stale swapchain). Safe to call repeatedly.
    bool recreate();
    void shutdown();
    bool valid() const { return swapchain_ != VK_NULL_HANDLE; }

    // Blits the renderer's finished color image to the next display image and
    // presents it. Returns false when the swapchain is out of date — the
    // caller should recreate() and try the next frame (never a crash path).
    bool present(const Renderer& renderer);

    uint32_t width() const { return extent_.width; }
    uint32_t height() const { return extent_.height; }
    uint32_t imageCount() const { return static_cast<uint32_t>(images_.size()); }

private:
    bool build();
    void destroySwapchainObjects();

    VulkanDevice* device_ = nullptr;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    std::vector<VkImage> images_;
    VkExtent2D extent_{0, 0};
    VkFormat format_ = VK_FORMAT_UNDEFINED;
    VkSurfaceTransformFlagBitsKHR transform_ = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;

    // One in-flight blit: the v1 renderer already fences per frame, so a
    // single command buffer + semaphore pair is the honest match.
    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer_ = VK_NULL_HANDLE;
    VkSemaphore acquired_ = VK_NULL_HANDLE;
    VkSemaphore blitDone_ = VK_NULL_HANDLE;
    VkFence inFlight_ = VK_NULL_HANDLE;
    bool fenceArmed_ = false;
};

}  // namespace mge
