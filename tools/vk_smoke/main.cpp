// Vulkan smoke test: brings up the device, records and submits real GPU
// commands (image clear + copy to a readback buffer), then verifies every
// pixel on the CPU and writes a PPM proof image. Runs headless on any Vulkan
// implementation, including llvmpipe — this is how rendering work is
// validated without a device.

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "mge/core/log.h"
#include "mge/core/memory.h"
#include "mge/graphics/vulkan_device.h"

using namespace mge;

namespace {

constexpr uint32_t kWidth = 256;
constexpr uint32_t kHeight = 256;
// Engine clear color for the smoke test: a recognizable orange.
constexpr float kClearColor[4] = {1.0f, 0.5f, 0.1f, 1.0f};

bool check(bool condition, const char* what) {
    if (!condition) fprintf(stderr, "FAIL: %s\n", what);
    return condition;
}

}  // namespace

int main(int argc, char** argv) {
    const char* ppmPath = argc > 1 ? argv[1] : "vk_smoke.ppm";

    BudgetRegistry budgets;
    VulkanDevice device(budgets);
    VulkanDeviceConfig config;
    if (!device.init(config)) {
        fprintf(stderr, "FAIL: Vulkan device init\n");
        return 1;
    }
    printf("vulkan device: %s\n", device.deviceName());

    VkDevice vk = device.device();

    // -- Offscreen color image (optimal tiling, device-local path) --
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.extent = {kWidth, kHeight, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VkImage image = VK_NULL_HANDLE;
    if (!check(vkCreateImage(vk, &imageInfo, nullptr, &image) == VK_SUCCESS, "create image"))
        return 1;

    VkMemoryRequirements imageRequirements;
    vkGetImageMemoryRequirements(vk, image, &imageRequirements);
    VkDeviceMemory imageMemory =
        device.allocate(imageRequirements, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (!check(imageMemory != VK_NULL_HANDLE, "budgeted image memory")) return 1;
    vkBindImageMemory(vk, image, imageMemory, 0);

    // -- Host-visible readback buffer --
    const VkDeviceSize readbackSize = kWidth * kHeight * 4;
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = readbackSize;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;

    VkBuffer readback = VK_NULL_HANDLE;
    if (!check(vkCreateBuffer(vk, &bufferInfo, nullptr, &readback) == VK_SUCCESS, "create buffer"))
        return 1;
    VkMemoryRequirements bufferRequirements;
    vkGetBufferMemoryRequirements(vk, readback, &bufferRequirements);
    VkDeviceMemory readbackMemory = device.allocate(
        bufferRequirements,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (!check(readbackMemory != VK_NULL_HANDLE, "budgeted readback memory")) return 1;
    vkBindBufferMemory(vk, readback, readbackMemory, 0);

    // -- Record: clear image, copy to buffer --
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = device.graphicsQueueFamily();
    VkCommandPool pool = VK_NULL_HANDLE;
    vkCreateCommandPool(vk, &poolInfo, nullptr, &pool);

    VkCommandBufferAllocateInfo commandInfo{};
    commandInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    commandInfo.commandPool = pool;
    commandInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    commandInfo.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(vk, &commandInfo, &cmd);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(cmd, &beginInfo);

    VkImageSubresourceRange colorRange{};
    colorRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    colorRange.levelCount = 1;
    colorRange.layerCount = 1;

    VkImageMemoryBarrier toClear{};
    toClear.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toClear.srcAccessMask = 0;
    toClear.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toClear.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    toClear.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toClear.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toClear.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toClear.image = image;
    toClear.subresourceRange = colorRange;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                         0, nullptr, 0, nullptr, 1, &toClear);

    VkClearColorValue clearValue;
    memcpy(clearValue.float32, kClearColor, sizeof(kClearColor));
    vkCmdClearColorImage(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearValue, 1,
                         &colorRange);

    VkImageMemoryBarrier toCopy = toClear;
    toCopy.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toCopy.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    toCopy.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toCopy.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0,
                         nullptr, 0, nullptr, 1, &toCopy);

    VkBufferImageCopy region{};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = {kWidth, kHeight, 1};
    vkCmdCopyImageToBuffer(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback, 1, &region);

    vkEndCommandBuffer(cmd);

    // -- Submit and wait --
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VkFence fence = VK_NULL_HANDLE;
    vkCreateFence(vk, &fenceInfo, nullptr, &fence);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    if (!check(vkQueueSubmit(device.graphicsQueue(), 1, &submitInfo, fence) == VK_SUCCESS,
               "queue submit"))
        return 1;
    if (!check(vkWaitForFences(vk, 1, &fence, VK_TRUE, UINT64_MAX) == VK_SUCCESS, "fence wait"))
        return 1;

    // -- Verify on the CPU --
    void* mapped = nullptr;
    vkMapMemory(vk, readbackMemory, 0, readbackSize, 0, &mapped);
    const uint8_t* pixels = static_cast<const uint8_t*>(mapped);
    const uint8_t expected[4] = {
        static_cast<uint8_t>(kClearColor[0] * 255.0f + 0.5f),
        static_cast<uint8_t>(kClearColor[1] * 255.0f + 0.5f),
        static_cast<uint8_t>(kClearColor[2] * 255.0f + 0.5f),
        static_cast<uint8_t>(kClearColor[3] * 255.0f + 0.5f),
    };
    size_t wrongPixels = 0;
    for (size_t i = 0; i < kWidth * kHeight; ++i) {
        if (memcmp(pixels + i * 4, expected, 4) != 0) ++wrongPixels;
    }

    // -- PPM proof image --
    if (FILE* f = fopen(ppmPath, "wb")) {
        fprintf(f, "P6\n%u %u\n255\n", kWidth, kHeight);
        for (size_t i = 0; i < kWidth * kHeight; ++i) fwrite(pixels + i * 4, 1, 3, f);
        fclose(f);
        printf("wrote %s\n", ppmPath);
    }
    vkUnmapMemory(vk, readbackMemory);

    const BudgetStats gpu = device.gpuBudgetStats();
    printf("gpu budget: used %zu KiB / cap %zu KiB (peak %zu KiB)\n", gpu.usedBytes / 1024,
           gpu.capBytes / 1024, gpu.peakBytes / 1024);
    printf("pixels verified: %u, wrong: %zu\n", kWidth * kHeight, wrongPixels);

    // -- Cleanup (budget must return to zero) --
    vkDestroyFence(vk, fence, nullptr);
    vkDestroyCommandPool(vk, pool, nullptr);
    vkDestroyBuffer(vk, readback, nullptr);
    device.free(readbackMemory, bufferRequirements.size);
    vkDestroyImage(vk, image, nullptr);
    device.free(imageMemory, imageRequirements.size);

    const size_t residual = device.gpuBudgetStats().usedBytes;
    device.shutdown();

    if (wrongPixels != 0 || residual != 0) {
        fprintf(stderr, "FAIL: wrongPixels=%zu residualGpuBytes=%zu\n", wrongPixels, residual);
        return 1;
    }
    printf("OK\n");
    return 0;
}
