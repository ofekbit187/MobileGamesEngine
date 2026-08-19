#include "mge/graphics/swapchain.h"

#include "mge/core/log.h"

namespace mge {

namespace {
constexpr const char* kTag = "swapchain";
}

Swapchain::~Swapchain() { shutdown(); }

bool Swapchain::init(VulkanDevice& device, VkSurfaceKHR surface) {
    if (!device.initialized() || surface == VK_NULL_HANDLE) return false;
    if (!device.swapchainEnabled()) {
        MGE_LOGE(kTag, "device was created without the swapchain extension");
        return false;
    }
    shutdown();
    device_ = &device;
    surface_ = surface;

    VkBool32 presentSupported = VK_FALSE;
    vkGetPhysicalDeviceSurfaceSupportKHR(device.physicalDevice(), device.graphicsQueueFamily(),
                                         surface_, &presentSupported);
    if (presentSupported != VK_TRUE) {
        MGE_LOGE(kTag, "graphics queue cannot present to this surface");
        device_ = nullptr;
        return false;
    }

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = device.graphicsQueueFamily();
    if (vkCreateCommandPool(device.device(), &poolInfo, nullptr, &commandPool_) != VK_SUCCESS) {
        return false;
    }
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool_;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(device.device(), &allocInfo, &commandBuffer_) != VK_SUCCESS) {
        return false;
    }
    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    if (vkCreateSemaphore(device.device(), &semaphoreInfo, nullptr, &acquired_) != VK_SUCCESS ||
        vkCreateSemaphore(device.device(), &semaphoreInfo, nullptr, &blitDone_) != VK_SUCCESS ||
        vkCreateFence(device.device(), &fenceInfo, nullptr, &inFlight_) != VK_SUCCESS) {
        return false;
    }
    return build();
}

bool Swapchain::build() {
    VulkanDevice& device = *device_;
    VkSurfaceCapabilitiesKHR caps{};
    if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device.physicalDevice(), surface_, &caps) !=
        VK_SUCCESS) {
        return false;
    }
    extent_ = caps.currentExtent;
    if (extent_.width == UINT32_MAX) {  // surface defers to us
        extent_ = caps.minImageExtent;
    }
    if (extent_.width == 0 || extent_.height == 0) {
        MGE_LOGW(kTag, "surface has zero extent — not building yet");
        return false;
    }
    // The blit target must accept a transfer; every sane driver offers it,
    // but refuse loudly rather than render into nothing.
    if ((caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT) == 0) {
        MGE_LOGE(kTag, "surface images cannot be transfer destinations");
        return false;
    }

    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device.physicalDevice(), surface_, &formatCount, nullptr);
    if (formatCount == 0) return false;
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(device.physicalDevice(), surface_, &formatCount,
                                         formats.data());
    VkSurfaceFormatKHR chosen = formats[0];
    for (const VkSurfaceFormatKHR& candidate : formats) {
        // Blit converts formats, and matches components by name — so either
        // 8-bit UNORM order shows correct colors from our RGBA target.
        if ((candidate.format == VK_FORMAT_R8G8B8A8_UNORM ||
             candidate.format == VK_FORMAT_B8G8R8A8_UNORM) &&
            candidate.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            chosen = candidate;
            break;
        }
    }
    format_ = chosen.format;
    transform_ = (caps.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR) != 0
                     ? VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR
                     : caps.currentTransform;

    uint32_t imageCount = caps.minImageCount + 1;  // one spare: acquire never starves
    if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount) imageCount = caps.maxImageCount;

    VkSwapchainCreateInfoKHR info{};
    info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    info.surface = surface_;
    info.minImageCount = imageCount;
    info.imageFormat = format_;
    info.imageColorSpace = chosen.colorSpace;
    info.imageExtent = extent_;
    info.imageArrayLayers = 1;
    info.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.preTransform = transform_;
    info.compositeAlpha = (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR) != 0
                              ? VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR
                              : VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
    info.presentMode = VK_PRESENT_MODE_FIFO_KHR;  // always supported; vsync-paced
    info.clipped = VK_TRUE;
    info.oldSwapchain = swapchain_;

    VkSwapchainKHR created = VK_NULL_HANDLE;
    const VkResult result = vkCreateSwapchainKHR(device.device(), &info, nullptr, &created);
    if (swapchain_ != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(device.device(), swapchain_, nullptr);
        swapchain_ = VK_NULL_HANDLE;
    }
    if (result != VK_SUCCESS) {
        MGE_LOGE(kTag, "vkCreateSwapchainKHR failed (%d)", result);
        return false;
    }
    swapchain_ = created;

    uint32_t count = 0;
    vkGetSwapchainImagesKHR(device.device(), swapchain_, &count, nullptr);
    images_.resize(count);
    vkGetSwapchainImagesKHR(device.device(), swapchain_, &count, images_.data());
    MGE_LOGI(kTag, "swapchain %ux%u, %u images, format %d", extent_.width, extent_.height, count,
             static_cast<int>(format_));
    return true;
}

bool Swapchain::recreate() {
    if (device_ == nullptr) return false;
    vkDeviceWaitIdle(device_->device());
    fenceArmed_ = false;
    return build();
}

void Swapchain::destroySwapchainObjects() {
    if (device_ == nullptr) return;
    if (swapchain_ != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(device_->device(), swapchain_, nullptr);
        swapchain_ = VK_NULL_HANDLE;
    }
    images_.clear();
}

void Swapchain::shutdown() {
    if (device_ == nullptr) return;
    VkDevice vk = device_->device();
    vkDeviceWaitIdle(vk);
    destroySwapchainObjects();
    if (inFlight_ != VK_NULL_HANDLE) vkDestroyFence(vk, inFlight_, nullptr);
    if (blitDone_ != VK_NULL_HANDLE) vkDestroySemaphore(vk, blitDone_, nullptr);
    if (acquired_ != VK_NULL_HANDLE) vkDestroySemaphore(vk, acquired_, nullptr);
    if (commandPool_ != VK_NULL_HANDLE) vkDestroyCommandPool(vk, commandPool_, nullptr);
    inFlight_ = VK_NULL_HANDLE;
    blitDone_ = VK_NULL_HANDLE;
    acquired_ = VK_NULL_HANDLE;
    commandPool_ = VK_NULL_HANDLE;
    commandBuffer_ = VK_NULL_HANDLE;
    fenceArmed_ = false;
    device_ = nullptr;
    surface_ = VK_NULL_HANDLE;
}

bool Swapchain::present(const Renderer& renderer) {
    if (!valid() || renderer.colorImage() == VK_NULL_HANDLE) return false;
    VkDevice vk = device_->device();

    // Wait for the previous blit before reusing the command buffer/semaphores.
    if (fenceArmed_) {
        vkWaitForFences(vk, 1, &inFlight_, VK_TRUE, UINT64_MAX);
    }
    vkResetFences(vk, 1, &inFlight_);
    fenceArmed_ = false;

    uint32_t index = 0;
    VkResult acquire =
        vkAcquireNextImageKHR(vk, swapchain_, UINT64_MAX, acquired_, VK_NULL_HANDLE, &index);
    if (acquire == VK_ERROR_OUT_OF_DATE_KHR || acquire == VK_SUBOPTIMAL_KHR) return false;
    if (acquire != VK_SUCCESS) {
        MGE_LOGE(kTag, "vkAcquireNextImageKHR failed (%d)", acquire);
        return false;
    }

    vkResetCommandBuffer(commandBuffer_, 0);
    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(commandBuffer_, &begin);

    VkImageMemoryBarrier toDst{};
    toDst.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toDst.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;  // contents are ours to overwrite
    toDst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toDst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toDst.image = images_[index];
    toDst.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    toDst.srcAccessMask = 0;
    toDst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(commandBuffer_, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toDst);

    // The render pass leaves the color target in TRANSFER_SRC_OPTIMAL; the
    // driver scales render resolution to display resolution here for free.
    VkImageBlit blit{};
    blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blit.srcOffsets[1] = {static_cast<int32_t>(renderer.width()),
                          static_cast<int32_t>(renderer.height()), 1};
    blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blit.dstOffsets[1] = {static_cast<int32_t>(extent_.width),
                          static_cast<int32_t>(extent_.height), 1};
    vkCmdBlitImage(commandBuffer_, renderer.colorImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   images_[index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit,
                   VK_FILTER_LINEAR);

    VkImageMemoryBarrier toPresent = toDst;
    toPresent.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toPresent.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    toPresent.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toPresent.dstAccessMask = 0;
    vkCmdPipelineBarrier(commandBuffer_, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1,
                         &toPresent);
    vkEndCommandBuffer(commandBuffer_);

    const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.waitSemaphoreCount = 1;
    submit.pWaitSemaphores = &acquired_;
    submit.pWaitDstStageMask = &waitStage;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &commandBuffer_;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &blitDone_;
    if (vkQueueSubmit(device_->graphicsQueue(), 1, &submit, inFlight_) != VK_SUCCESS) {
        return false;
    }
    fenceArmed_ = true;

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &blitDone_;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapchain_;
    presentInfo.pImageIndices = &index;
    const VkResult presented = vkQueuePresentKHR(device_->graphicsQueue(), &presentInfo);
    if (presented == VK_ERROR_OUT_OF_DATE_KHR || presented == VK_SUBOPTIMAL_KHR) return false;
    return presented == VK_SUCCESS;
}

}  // namespace mge
