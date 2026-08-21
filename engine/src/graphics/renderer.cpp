#include "mge/graphics/renderer.h"

#include <cstring>

#include "mge/core/log.h"

#include "shaders/lit_frag.h"
#include "shaders/mesh_vert.h"
#include "shaders/placeholder_frag.h"
#include "shaders/skinned_vert.h"
#include "shaders/ui_frag.h"
#include "shaders/ui_vert.h"

namespace mge {

namespace {

constexpr const char* kTag = "renderer";
constexpr VkFormat kColorFormat = VK_FORMAT_R8G8B8A8_UNORM;
constexpr VkFormat kDepthFormat = VK_FORMAT_D32_SFLOAT;

// Must match the shader-side FrameData block.
struct FrameData {
    float viewProj[16];
    float lightDirIntensity[4];
    float cameraPos[4];
};

// Must match the shader-side push-constant block (112 bytes <= min spec 128).
struct DrawPush {
    float model[16];
    float baseColor[4];
    float params[4];
    // x = roughness fallback, y = AO fallback, z = 1 when a packed map is
    // bound, w = UV scale. The fallbacks are what makes a one-fetch material
    // ordinary rather than a special case (docs/TEXTURING.md §3).
    float material[4];
};
static_assert(sizeof(DrawPush) == 112, "push constant layout is a shader contract");

// One skinning slot as the shader sees it: `mat4 joints[17]` followed by
// `vec4 morphWeights[4]`. The weights ride alongside the palette because they
// are the same kind of thing — the small per-character data that turns the one
// shared mesh into this character (ADR 0009).
constexpr VkDeviceSize kSkinSlotBytes = sizeof(Mat4) * kJointCount + 16 * sizeof(float);
// GLSL cannot import kJointCount, so skinned.vert hardcodes it — and the
// morph weights sit immediately after the palette, so a rig that grew would
// have the shader reading its weights out of the last joint's matrix with no
// error anywhere. ADR 0019 takes the rig 17 -> 19 for the clavicles; when it
// lands, update `kJointCount` in engine/shaders/skinned.vert to match, re-run
// scripts/compile-shaders.sh, and change the number here. Failing the build is
// the point.
static_assert(kJointCount == 17,
              "rig changed: update kJointCount in engine/shaders/skinned.vert and re-run "
              "scripts/compile-shaders.sh, then update this assert");
constexpr VkDeviceSize kSkinSlotWeightOffset = sizeof(Mat4) * kJointCount;
static_assert(kMorphCount <= 16, "morph weights are packed into four vec4s");

// Morph delta buffer layout, mirrored word for word in skinned.vert.
constexpr uint32_t kMorphEntryBaseWord = 0;  // first word of the entry table
constexpr uint32_t kMorphVertexCountWord = 1;
constexpr uint32_t kMorphScaleWord = 2;      // 16 per-target quantization scales
constexpr uint32_t kMorphOffsetWord = 18;    // vertexCount + 1 prefix offsets
constexpr uint32_t kMorphWordsPerDelta = 3;  // 12 bytes, as in the file

}  // namespace

Renderer::Renderer(VulkanDevice& device) : device_(device) {}

Renderer::~Renderer() { shutdown(); }

bool Renderer::init(const RendererConfig& config) {
    if (initialized()) return true;
    config_ = config;

    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(device_.physicalDevice(), &properties);
    const VkDeviceSize align = properties.limits.minUniformBufferOffsetAlignment;
    uniformSlotStride_ = ((sizeof(FrameData) + align - 1) / align) * align;
    if (uniformSlotStride_ < align) uniformSlotStride_ = align;
    paletteSlotStride_ = ((kSkinSlotBytes + align - 1) / align) * align;

    if (!createTarget() || !createDescriptors() || !createPipelines()) {
        shutdown();
        return false;
    }

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = device_.graphicsQueueFamily();
    if (vkCreateCommandPool(device_.device(), &poolInfo, nullptr, &commandPool_) != VK_SUCCESS) {
        shutdown();
        return false;
    }
    VkCommandBufferAllocateInfo cmdInfo{};
    cmdInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdInfo.commandPool = commandPool_;
    cmdInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdInfo.commandBufferCount = 1;
    vkAllocateCommandBuffers(device_.device(), &cmdInfo, &commandBuffer_);

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    vkCreateFence(device_.device(), &fenceInfo, nullptr, &fence_);

    const VkDeviceSize readbackSize =
        static_cast<VkDeviceSize>(config_.width) * config_.height * 4;
    if (!createBuffer(readbackSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT, readbackBuffer_,
                      readbackMemory_, readbackMemorySize_)) {
        shutdown();
        return false;
    }

    // Needs the command buffer and fence above: uploading the 1x1 white
    // texture is a real transfer submit.
    if (!createDefaultMaterial()) {
        shutdown();
        return false;
    }

    MGE_LOGI(kTag, "renderer ready %ux%u (%s texture pack)", config_.width, config_.height,
             texturePackName(device_.preferredTexturePack()));
    return true;
}

void Renderer::shutdown() {
    VkDevice vk = device_.device();
    if (vk == VK_NULL_HANDLE) return;
    vkDeviceWaitIdle(vk);

    auto destroyBuffer = [&](VkBuffer& b, VkDeviceMemory& m, VkDeviceSize& size) {
        if (b != VK_NULL_HANDLE) vkDestroyBuffer(vk, b, nullptr);
        device_.free(m, size);
        b = VK_NULL_HANDLE;
        m = VK_NULL_HANDLE;
        size = 0;
    };

    // UI overlay resources.
    destroyBuffer(uiVertexBuffer_, uiVertexMemory_, uiVertexMemorySize_);
    destroyBuffer(uiIndexBuffer_, uiIndexMemory_, uiIndexMemorySize_);
    if (uiPipeline_ != VK_NULL_HANDLE) vkDestroyPipeline(vk, uiPipeline_, nullptr);
    if (uiPipelineLayout_ != VK_NULL_HANDLE)
        vkDestroyPipelineLayout(vk, uiPipelineLayout_, nullptr);
    if (uiSetLayout_ != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(vk, uiSetLayout_, nullptr);
    if (uiSampler_ != VK_NULL_HANDLE) vkDestroySampler(vk, uiSampler_, nullptr);
    if (uiAtlasView_ != VK_NULL_HANDLE) vkDestroyImageView(vk, uiAtlasView_, nullptr);
    if (uiAtlasImage_ != VK_NULL_HANDLE) vkDestroyImage(vk, uiAtlasImage_, nullptr);
    device_.free(uiAtlasMemory_, uiAtlasMemorySize_);
    uiPipeline_ = VK_NULL_HANDLE;
    uiPipelineLayout_ = VK_NULL_HANDLE;
    uiSetLayout_ = VK_NULL_HANDLE;
    uiDescriptorSet_ = VK_NULL_HANDLE;
    uiSampler_ = VK_NULL_HANDLE;
    uiAtlasView_ = VK_NULL_HANDLE;
    uiAtlasImage_ = VK_NULL_HANDLE;
    uiAtlasMemory_ = VK_NULL_HANDLE;
    uiAtlasMemorySize_ = 0;

    destroyBuffer(readbackBuffer_, readbackMemory_, readbackMemorySize_);
    destroyBuffer(paletteBuffer_, paletteMemory_, paletteMemorySize_);
    if (fence_ != VK_NULL_HANDLE) vkDestroyFence(vk, fence_, nullptr);
    if (commandPool_ != VK_NULL_HANDLE) vkDestroyCommandPool(vk, commandPool_, nullptr);
    fence_ = VK_NULL_HANDLE;
    commandPool_ = VK_NULL_HANDLE;
    commandBuffer_ = VK_NULL_HANDLE;

    if (litPipeline_ != VK_NULL_HANDLE) vkDestroyPipeline(vk, litPipeline_, nullptr);
    if (placeholderPipeline_ != VK_NULL_HANDLE) vkDestroyPipeline(vk, placeholderPipeline_, nullptr);
    if (skinnedPipeline_ != VK_NULL_HANDLE) vkDestroyPipeline(vk, skinnedPipeline_, nullptr);
    if (pipelineLayout_ != VK_NULL_HANDLE) vkDestroyPipelineLayout(vk, pipelineLayout_, nullptr);
    litPipeline_ = placeholderPipeline_ = VK_NULL_HANDLE;
    pipelineLayout_ = VK_NULL_HANDLE;

    destroyMaterial(defaultMaterial_);
    destroyTexture(whiteTexture_);
    if (textureSampler_ != VK_NULL_HANDLE) vkDestroySampler(vk, textureSampler_, nullptr);
    if (materialPool_ != VK_NULL_HANDLE) vkDestroyDescriptorPool(vk, materialPool_, nullptr);
    if (materialSetLayout_ != VK_NULL_HANDLE)
        vkDestroyDescriptorSetLayout(vk, materialSetLayout_, nullptr);
    textureSampler_ = VK_NULL_HANDLE;
    materialPool_ = VK_NULL_HANDLE;
    materialSetLayout_ = VK_NULL_HANDLE;

    destroyBuffer(emptyMorphBuffer_, emptyMorphMemory_, emptyMorphMemorySize_);
    if (morphPool_ != VK_NULL_HANDLE) vkDestroyDescriptorPool(vk, morphPool_, nullptr);
    if (morphSetLayout_ != VK_NULL_HANDLE)
        vkDestroyDescriptorSetLayout(vk, morphSetLayout_, nullptr);
    morphPool_ = VK_NULL_HANDLE;
    morphSetLayout_ = VK_NULL_HANDLE;
    emptyMorphSet_ = VK_NULL_HANDLE;

    if (descriptorPool_ != VK_NULL_HANDLE) vkDestroyDescriptorPool(vk, descriptorPool_, nullptr);
    if (setLayout_ != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(vk, setLayout_, nullptr);
    descriptorPool_ = VK_NULL_HANDLE;
    setLayout_ = VK_NULL_HANDLE;
    descriptorSet_ = VK_NULL_HANDLE;
    destroyBuffer(uniformBuffer_, uniformMemory_, uniformMemorySize_);

    if (framebuffer_ != VK_NULL_HANDLE) vkDestroyFramebuffer(vk, framebuffer_, nullptr);
    if (renderPass_ != VK_NULL_HANDLE) vkDestroyRenderPass(vk, renderPass_, nullptr);
    framebuffer_ = VK_NULL_HANDLE;
    renderPass_ = VK_NULL_HANDLE;
    if (colorView_ != VK_NULL_HANDLE) vkDestroyImageView(vk, colorView_, nullptr);
    if (depthView_ != VK_NULL_HANDLE) vkDestroyImageView(vk, depthView_, nullptr);
    colorView_ = depthView_ = VK_NULL_HANDLE;
    if (colorImage_ != VK_NULL_HANDLE) vkDestroyImage(vk, colorImage_, nullptr);
    if (depthImage_ != VK_NULL_HANDLE) vkDestroyImage(vk, depthImage_, nullptr);
    device_.free(colorMemory_, colorMemorySize_);
    device_.free(depthMemory_, depthMemorySize_);
    colorImage_ = depthImage_ = VK_NULL_HANDLE;
    colorMemory_ = depthMemory_ = VK_NULL_HANDLE;
    colorMemorySize_ = depthMemorySize_ = 0;
}

bool Renderer::createTarget() {
    VkDevice vk = device_.device();

    auto createImage = [&](VkFormat format, VkImageUsageFlags usage, VkImage& image,
                           VkDeviceMemory& memory, VkDeviceSize& memorySize) {
        VkImageCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        info.imageType = VK_IMAGE_TYPE_2D;
        info.format = format;
        info.extent = {config_.width, config_.height, 1};
        info.mipLevels = 1;
        info.arrayLayers = 1;
        info.samples = VK_SAMPLE_COUNT_1_BIT;
        info.tiling = VK_IMAGE_TILING_OPTIMAL;
        info.usage = usage;
        info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (vkCreateImage(vk, &info, nullptr, &image) != VK_SUCCESS) return false;
        VkMemoryRequirements requirements;
        vkGetImageMemoryRequirements(vk, image, &requirements);
        memory = device_.allocate(requirements, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (memory == VK_NULL_HANDLE) return false;
        memorySize = requirements.size;
        vkBindImageMemory(vk, image, memory, 0);
        return true;
    };

    if (!createImage(kColorFormat,
                     VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                     colorImage_, colorMemory_, colorMemorySize_)) {
        return false;
    }
    if (!createImage(kDepthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, depthImage_,
                     depthMemory_, depthMemorySize_)) {
        return false;
    }

    auto createView = [&](VkImage image, VkFormat format, VkImageAspectFlags aspect,
                          VkImageView& view) {
        VkImageViewCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        info.image = image;
        info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        info.format = format;
        info.subresourceRange = {aspect, 0, 1, 0, 1};
        return vkCreateImageView(vk, &info, nullptr, &view) == VK_SUCCESS;
    };
    if (!createView(colorImage_, kColorFormat, VK_IMAGE_ASPECT_COLOR_BIT, colorView_)) return false;
    if (!createView(depthImage_, kDepthFormat, VK_IMAGE_ASPECT_DEPTH_BIT, depthView_)) return false;

    VkAttachmentDescription attachments[2]{};
    attachments[0].format = kColorFormat;
    attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[0].finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;  // ready for readback
    attachments[1].format = kDepthFormat;
    attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference depthRef{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;
    subpass.pDepthStencilAttachment = &depthRef;

    VkRenderPassCreateInfo passInfo{};
    passInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    passInfo.attachmentCount = 2;
    passInfo.pAttachments = attachments;
    passInfo.subpassCount = 1;
    passInfo.pSubpasses = &subpass;
    if (vkCreateRenderPass(vk, &passInfo, nullptr, &renderPass_) != VK_SUCCESS) return false;

    VkImageView views[2] = {colorView_, depthView_};
    VkFramebufferCreateInfo fbInfo{};
    fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbInfo.renderPass = renderPass_;
    fbInfo.attachmentCount = 2;
    fbInfo.pAttachments = views;
    fbInfo.width = config_.width;
    fbInfo.height = config_.height;
    fbInfo.layers = 1;
    return vkCreateFramebuffer(vk, &fbInfo, nullptr, &framebuffer_) == VK_SUCCESS;
}

bool Renderer::createDescriptors() {
    VkDevice vk = device_.device();

    // Dynamic-offset UBO: slot 0 = main camera, slots 1.. = object views.
    if (!createBuffer(uniformSlotStride_ * kMaxUniformSlots,
                      VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, uniformBuffer_, uniformMemory_,
                      uniformMemorySize_)) {
        return false;
    }

    // Skinning palettes (task 8.10): one dynamic-offset slot per skinned draw.
    if (!createBuffer(paletteSlotStride_ * kMaxSkinnedDraws, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                      paletteBuffer_, paletteMemory_, paletteMemorySize_)) {
        return false;
    }

    VkDescriptorSetLayoutBinding bindings[2]{};
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 2;
    layoutInfo.pBindings = bindings;
    if (vkCreateDescriptorSetLayout(vk, &layoutInfo, nullptr, &setLayout_) != VK_SUCCESS)
        return false;

    VkDescriptorPoolSize poolSizes[2] = {
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 2},  // frame + skin palette
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1},  // UI atlas
    };
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = 2;
    poolInfo.poolSizeCount = 2;
    poolInfo.pPoolSizes = poolSizes;
    if (vkCreateDescriptorPool(vk, &poolInfo, nullptr, &descriptorPool_) != VK_SUCCESS)
        return false;

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool_;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &setLayout_;
    if (vkAllocateDescriptorSets(vk, &allocInfo, &descriptorSet_) != VK_SUCCESS) return false;

    VkDescriptorBufferInfo bufferInfo{uniformBuffer_, 0, sizeof(FrameData)};
    VkDescriptorBufferInfo paletteInfo{paletteBuffer_, 0, kSkinSlotBytes};
    VkWriteDescriptorSet writes[2]{};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = descriptorSet_;
    writes[0].dstBinding = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    writes[0].pBufferInfo = &bufferInfo;
    writes[1] = writes[0];
    writes[1].dstBinding = 1;
    writes[1].pBufferInfo = &paletteInfo;
    vkUpdateDescriptorSets(vk, 2, writes, 0, nullptr);

    // Set 1: the per-mesh morph deltas (ADR 0009). One descriptor per skinned
    // mesh, capped — over the cap an upload refuses rather than the pool
    // growing (P1: caps refuse, never grow).
    VkDescriptorSetLayoutBinding morphBinding{};
    morphBinding.binding = 0;
    morphBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    morphBinding.descriptorCount = 1;
    morphBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    VkDescriptorSetLayoutCreateInfo morphLayoutInfo{};
    morphLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    morphLayoutInfo.bindingCount = 1;
    morphLayoutInfo.pBindings = &morphBinding;
    if (vkCreateDescriptorSetLayout(vk, &morphLayoutInfo, nullptr, &morphSetLayout_) !=
        VK_SUCCESS) {
        return false;
    }

    VkDescriptorPoolSize morphPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, kMaxMorphMeshes + 1};
    VkDescriptorPoolCreateInfo morphPoolInfo{};
    morphPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    // Meshes are uploaded and destroyed as content streams, so sets must come
    // back to the pool rather than being spent once.
    morphPoolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    morphPoolInfo.maxSets = kMaxMorphMeshes + 1;  // + the empty set
    morphPoolInfo.poolSizeCount = 1;
    morphPoolInfo.pPoolSizes = &morphPoolSize;
    if (vkCreateDescriptorPool(vk, &morphPoolInfo, nullptr, &morphPool_) != VK_SUCCESS)
        return false;

    // The empty set: what a mesh without morph targets binds. Its header says
    // "no vertices, no entries", and the shape pass is switched off for such a
    // draw anyway — this exists because Vulkan requires every statically-used
    // descriptor to be bound, not because anything reads it.
    const VkDeviceSize emptyBytes = kMorphOffsetWord * sizeof(uint32_t);
    if (!createBuffer(emptyBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, emptyMorphBuffer_,
                      emptyMorphMemory_, emptyMorphMemorySize_)) {
        return false;
    }
    void* emptyMapped = nullptr;
    if (vkMapMemory(vk, emptyMorphMemory_, 0, emptyBytes, 0, &emptyMapped) != VK_SUCCESS)
        return false;
    memset(emptyMapped, 0, emptyBytes);
    vkUnmapMemory(vk, emptyMorphMemory_);

    VkDescriptorSetAllocateInfo emptyAlloc{};
    emptyAlloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    emptyAlloc.descriptorPool = morphPool_;
    emptyAlloc.descriptorSetCount = 1;
    emptyAlloc.pSetLayouts = &morphSetLayout_;
    if (vkAllocateDescriptorSets(vk, &emptyAlloc, &emptyMorphSet_) != VK_SUCCESS) return false;
    VkDescriptorBufferInfo emptyInfo{emptyMorphBuffer_, 0, emptyBytes};
    VkWriteDescriptorSet emptyWrite{};
    emptyWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    emptyWrite.dstSet = emptyMorphSet_;
    emptyWrite.dstBinding = 0;
    emptyWrite.descriptorCount = 1;
    emptyWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    emptyWrite.pBufferInfo = &emptyInfo;
    vkUpdateDescriptorSets(vk, 1, &emptyWrite, 0, nullptr);

    // Set 2: the material — albedo and packed, two combined image samplers.
    // Two fetches is the ceiling the standard sets (TEXTURING §3).
    VkDescriptorSetLayoutBinding materialBindings[2]{};
    for (int i = 0; i < 2; ++i) {
        materialBindings[i].binding = static_cast<uint32_t>(i);
        materialBindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        materialBindings[i].descriptorCount = 1;
        materialBindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    VkDescriptorSetLayoutCreateInfo materialLayoutInfo{};
    materialLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    materialLayoutInfo.bindingCount = 2;
    materialLayoutInfo.pBindings = materialBindings;
    if (vkCreateDescriptorSetLayout(vk, &materialLayoutInfo, nullptr, &materialSetLayout_) !=
        VK_SUCCESS) {
        return false;
    }

    VkDescriptorPoolSize materialPoolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                          (kMaxMaterials + 1) * 2};
    VkDescriptorPoolCreateInfo materialPoolInfo{};
    materialPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    materialPoolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    materialPoolInfo.maxSets = kMaxMaterials + 1;  // + the default material
    materialPoolInfo.poolSizeCount = 1;
    materialPoolInfo.pPoolSizes = &materialPoolSize;
    if (vkCreateDescriptorPool(vk, &materialPoolInfo, nullptr, &materialPool_) != VK_SUCCESS)
        return false;

    // Bilinear + mips is the default: trilinear costs 2x on Mali-class
    // hardware, and aniso is used surgically per material rather than
    // globally (TEXTURING §7). Repeat addressing, because world materials tile.
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.maxLod = VK_LOD_CLAMP_NONE;
    if (vkCreateSampler(vk, &samplerInfo, nullptr, &textureSampler_) != VK_SUCCESS)
        return false;
    return true;
}

VkShaderModule Renderer::createShaderModule(const uint32_t* code, size_t codeSize) {
    VkShaderModuleCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = codeSize;
    info.pCode = code;
    VkShaderModule module = VK_NULL_HANDLE;
    vkCreateShaderModule(device_.device(), &info, nullptr, &module);
    return module;
}

bool Renderer::createPipelines() {
    VkDevice vk = device_.device();

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.size = sizeof(DrawPush);
    // Set 0 is per frame and per character; set 1 is per mesh (morph deltas);
    // set 2 is per material. Pipelines that do not use a set simply never
    // reference it.
    const VkDescriptorSetLayout setLayouts[3] = {setLayout_, morphSetLayout_,
                                                 materialSetLayout_};
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 3;
    layoutInfo.pSetLayouts = setLayouts;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;
    if (vkCreatePipelineLayout(vk, &layoutInfo, nullptr, &pipelineLayout_) != VK_SUCCESS)
        return false;

    VkShaderModule vert = createShaderModule(k_spv_mesh_vert, k_spv_mesh_vert_size);
    VkShaderModule litFrag = createShaderModule(k_spv_lit_frag, k_spv_lit_frag_size);
    VkShaderModule placeholderFrag =
        createShaderModule(k_spv_placeholder_frag, k_spv_placeholder_frag_size);
    if (vert == VK_NULL_HANDLE || litFrag == VK_NULL_HANDLE ||
        placeholderFrag == VK_NULL_HANDLE) {
        return false;
    }

    VkVertexInputBindingDescription vertexBinding{0, sizeof(Vertex),
                                                  VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription vertexAttributes[3]{};
    vertexAttributes[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position)};
    vertexAttributes[1] = {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal)};
    // Float UV, because world geometry tiles past the unit square (mesh_data.h).
    vertexAttributes[2] = {2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv)};

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &vertexBinding;
    vertexInput.vertexAttributeDescriptionCount = 3;
    vertexInput.pVertexAttributeDescriptions = vertexAttributes;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    // Dynamic viewport/scissor: object views re-target mid-pass (task 5.10).
    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;
    const VkDynamicState dynamicStates[2] = {VK_DYNAMIC_STATE_VIEWPORT,
                                             VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    VkPipelineRasterizationStateCreateInfo raster{};
    raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_BACK_BIT;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depth{};
    depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth.depthTestEnable = VK_TRUE;
    depth.depthWriteEnable = VK_TRUE;
    depth.depthCompareOp = VK_COMPARE_OP_LESS;

    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                     VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo blend{};
    blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend.attachmentCount = 1;
    blend.pAttachments = &blendAttachment;

    // Skinned vertex layout (task 8.10): the same lit fragment stage, fed by
    // the template body's 36-byte SkinVertex.
    VkShaderModule skinnedVert = createShaderModule(k_spv_skinned_vert, k_spv_skinned_vert_size);
    if (skinnedVert == VK_NULL_HANDLE) return false;
    VkVertexInputBindingDescription skinnedBinding{0, sizeof(SkinVertex),
                                                   VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription skinnedAttributes[5]{};
    skinnedAttributes[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(SkinVertex, position)};
    skinnedAttributes[1] = {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(SkinVertex, normal)};
    skinnedAttributes[2] = {2, 0, VK_FORMAT_R16G16_UNORM, offsetof(SkinVertex, uv)};  // 0..1 chart
    skinnedAttributes[3] = {3, 0, VK_FORMAT_R8G8B8A8_UINT, offsetof(SkinVertex, joints)};
    skinnedAttributes[4] = {4, 0, VK_FORMAT_R8G8B8A8_UNORM, offsetof(SkinVertex, weights)};
    VkPipelineVertexInputStateCreateInfo skinnedInput{};
    skinnedInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    skinnedInput.vertexBindingDescriptionCount = 1;
    skinnedInput.pVertexBindingDescriptions = &skinnedBinding;
    skinnedInput.vertexAttributeDescriptionCount = 5;
    skinnedInput.pVertexAttributeDescriptions = skinnedAttributes;

    auto makePipeline = [&](VkShaderModule vertModule,
                            const VkPipelineVertexInputStateCreateInfo& inputState,
                            VkShaderModule fragModule, VkPipeline& out) {
        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vertModule;
        stages[0].pName = "main";
        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = fragModule;
        stages[1].pName = "main";

        VkGraphicsPipelineCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        info.stageCount = 2;
        info.pStages = stages;
        info.pVertexInputState = &inputState;
        info.pInputAssemblyState = &inputAssembly;
        info.pViewportState = &viewportState;
        info.pRasterizationState = &raster;
        info.pMultisampleState = &multisample;
        info.pDepthStencilState = &depth;
        info.pColorBlendState = &blend;
        info.pDynamicState = &dynamicState;
        info.layout = pipelineLayout_;
        info.renderPass = renderPass_;
        return vkCreateGraphicsPipelines(vk, VK_NULL_HANDLE, 1, &info, nullptr, &out) ==
               VK_SUCCESS;
    };

    const bool ok = makePipeline(vert, vertexInput, litFrag, litPipeline_) &&
                    makePipeline(vert, vertexInput, placeholderFrag, placeholderPipeline_) &&
                    makePipeline(skinnedVert, skinnedInput, litFrag, skinnedPipeline_);

    vkDestroyShaderModule(vk, vert, nullptr);
    vkDestroyShaderModule(vk, skinnedVert, nullptr);
    vkDestroyShaderModule(vk, litFrag, nullptr);
    vkDestroyShaderModule(vk, placeholderFrag, nullptr);
    return ok;
}

// ------------------------------------------------------------- textures ----

bool Renderer::uploadTexture(const TextureData& data, GpuTexture& out) {
    const char* reason = "";
    if (!validateTexture(data, &reason)) {
        MGE_LOGE(kTag, "refused texture upload: %s", reason);
        return false;
    }
    if (!device_.supportsTextureFormat(data.format)) {
        // Refuse rather than silently substituting a format: a device that
        // cannot sample this pack was shipped the wrong pack, and quietly
        // decoding it at runtime is exactly the cost the format exists to
        // avoid (docs/TEXTURING.md §4).
        MGE_LOGE(kTag, "device cannot sample %s — wrong texture pack for this device",
                 textureFormatName(data.format));
        return false;
    }
    VkDevice vk = device_.device();

    const VkFormat format = toVkFormat(data.format, data.colorSpace);
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = format;
    imageInfo.extent = {data.width, data.height, 1};
    imageInfo.mipLevels = static_cast<uint32_t>(data.mips.size());
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(vk, &imageInfo, nullptr, &out.image) != VK_SUCCESS) return false;

    VkMemoryRequirements requirements;
    vkGetImageMemoryRequirements(vk, out.image, &requirements);
    // The TEXTURE budget, not the general GPU one.
    out.memory = device_.allocateTexture(requirements, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (out.memory == VK_NULL_HANDLE) {
        MGE_LOGW(kTag, "texture budget refused %zu KiB",
                 static_cast<size_t>(requirements.size) / 1024);
        vkDestroyImage(vk, out.image, nullptr);
        out.image = VK_NULL_HANDLE;
        return false;  // caller degrades — caps refuse, they never grow
    }
    out.memorySize = requirements.size;
    vkBindImageMemory(vk, out.image, out.memory, 0);

    // Staging: the whole baked payload goes up in one copy list, one region
    // per mip. The bytes are already in their final GPU format — no decode,
    // no mip generation, nothing but a transfer (TEXTURING §7).
    VkBuffer staging = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
    VkDeviceSize stagingSize = 0;
    if (!createBuffer(data.pixels.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, staging,
                      stagingMemory, stagingSize)) {
        destroyTexture(out);
        return false;
    }
    void* mapped = nullptr;
    vkMapMemory(vk, stagingMemory, 0, data.pixels.size(), 0, &mapped);
    memcpy(mapped, data.pixels.data(), data.pixels.size());
    vkUnmapMemory(vk, stagingMemory);

    std::vector<VkBufferImageCopy> regions(data.mips.size());
    for (size_t i = 0; i < data.mips.size(); ++i) {
        const TextureMip& mip = data.mips[i];
        VkBufferImageCopy& region = regions[i];
        region = VkBufferImageCopy{};
        region.bufferOffset = mip.offset;
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, static_cast<uint32_t>(i), 0, 1};
        region.imageExtent = {mip.width, mip.height, 1};
    }

    vkResetCommandBuffer(commandBuffer_, 0);
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(commandBuffer_, &beginInfo);
    VkImageMemoryBarrier toDst{};
    toDst.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toDst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toDst.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    toDst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toDst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toDst.image = out.image;
    toDst.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0,
                              static_cast<uint32_t>(data.mips.size()), 0, 1};
    vkCmdPipelineBarrier(commandBuffer_, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toDst);
    vkCmdCopyBufferToImage(commandBuffer_, staging, out.image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           static_cast<uint32_t>(regions.size()), regions.data());
    VkImageMemoryBarrier toRead = toDst;
    toRead.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    toRead.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    vkCmdPipelineBarrier(commandBuffer_, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &toRead);
    vkEndCommandBuffer(commandBuffer_);
    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &commandBuffer_;
    vkResetFences(vk, 1, &fence_);
    vkQueueSubmit(device_.graphicsQueue(), 1, &submit, fence_);
    vkWaitForFences(vk, 1, &fence_, VK_TRUE, UINT64_MAX);
    vkDestroyBuffer(vk, staging, nullptr);
    device_.free(stagingMemory, stagingSize);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = out.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0,
                                 static_cast<uint32_t>(data.mips.size()), 0, 1};
    if (vkCreateImageView(vk, &viewInfo, nullptr, &out.view) != VK_SUCCESS) {
        destroyTexture(out);
        return false;
    }

    out.width = data.width;
    out.height = data.height;
    out.mipLevels = static_cast<uint32_t>(data.mips.size());
    out.format = data.format;
    out.colorSpace = data.colorSpace;
    return true;
}

void Renderer::destroyTexture(GpuTexture& texture) {
    VkDevice vk = device_.device();
    if (vk == VK_NULL_HANDLE) return;
    if (texture.view != VK_NULL_HANDLE) vkDestroyImageView(vk, texture.view, nullptr);
    if (texture.image != VK_NULL_HANDLE) vkDestroyImage(vk, texture.image, nullptr);
    device_.freeTexture(texture.memory, texture.memorySize);
    texture = GpuTexture{};
}

bool Renderer::createMaterial(const GpuTexture* albedo, const GpuTexture* packed,
                              GpuMaterial& out) {
    if (materialPool_ == VK_NULL_HANDLE) return false;
    VkDevice vk = device_.device();

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = materialPool_;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &materialSetLayout_;
    if (vkAllocateDescriptorSets(vk, &allocInfo, &out.set) != VK_SUCCESS) {
        MGE_LOGW(kTag, "material pool exhausted (cap %u materials)", kMaxMaterials);
        out.set = VK_NULL_HANDLE;
        return false;
    }

    // Both slots always point at something real: an absent map resolves to the
    // 1x1 white texture, so the shader has one code path and the pipeline has
    // one permutation.
    const GpuTexture* albedoTexture = albedo != nullptr && albedo->valid() ? albedo
                                                                          : &whiteTexture_;
    const GpuTexture* packedTexture = packed != nullptr && packed->valid() ? packed
                                                                          : &whiteTexture_;
    VkDescriptorImageInfo images[2] = {
        {textureSampler_, albedoTexture->view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {textureSampler_, packedTexture->view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
    };
    VkWriteDescriptorSet writes[2]{};
    for (int i = 0; i < 2; ++i) {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = out.set;
        writes[i].dstBinding = static_cast<uint32_t>(i);
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[i].pImageInfo = &images[i];
    }
    vkUpdateDescriptorSets(vk, 2, writes, 0, nullptr);

    out.albedo = albedo;
    out.packed = packed;
    return true;
}

void Renderer::destroyMaterial(GpuMaterial& material) {
    if (material.set != VK_NULL_HANDLE && materialPool_ != VK_NULL_HANDLE) {
        vkFreeDescriptorSets(device_.device(), materialPool_, 1, &material.set);
    }
    material = GpuMaterial{};
}

bool Renderer::createDefaultMaterial() {
    // A 1x1 white texture with a full (single-level) mip chain. Every material
    // slot that has no map points here, which is what keeps "textured" and
    // "untextured" the same pipeline.
    TextureData white;
    white.width = 1;
    white.height = 1;
    white.format = TextureFormat::Rgba8;
    white.colorSpace = ColorSpace::Srgb;
    white.usage = TextureUsage::Albedo;
    white.pixels = {255, 255, 255, 255};
    white.mips.push_back(TextureMip{1, 1, 0, 4});
    if (!uploadTexture(white, whiteTexture_)) return false;
    return createMaterial(nullptr, nullptr, defaultMaterial_);
}

bool Renderer::createUiPipeline() {
    VkDevice vk = device_.device();

    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &binding;
    if (vkCreateDescriptorSetLayout(vk, &layoutInfo, nullptr, &uiSetLayout_) != VK_SUCCESS)
        return false;

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushRange.size = 2 * sizeof(float);  // screen size
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &uiSetLayout_;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushRange;
    if (vkCreatePipelineLayout(vk, &pipelineLayoutInfo, nullptr, &uiPipelineLayout_) !=
        VK_SUCCESS) {
        return false;
    }

    VkShaderModule vert = createShaderModule(k_spv_ui_vert, k_spv_ui_vert_size);
    VkShaderModule frag = createShaderModule(k_spv_ui_frag, k_spv_ui_frag_size);
    if (vert == VK_NULL_HANDLE || frag == VK_NULL_HANDLE) return false;

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag;
    stages[1].pName = "main";

    VkVertexInputBindingDescription vertexBinding{0, sizeof(UiVertex),
                                                  VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attributes[3]{};
    attributes[0] = {0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(UiVertex, x)};
    attributes[1] = {1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(UiVertex, u)};
    attributes[2] = {2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(UiVertex, r)};
    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &vertexBinding;
    vertexInput.vertexAttributeDescriptionCount = 3;
    vertexInput.pVertexAttributeDescriptions = attributes;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;
    const VkDynamicState dynamicStates[2] = {VK_DYNAMIC_STATE_VIEWPORT,
                                             VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    VkPipelineRasterizationStateCreateInfo raster{};
    raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depth{};
    depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth.depthTestEnable = VK_FALSE;
    depth.depthWriteEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.blendEnable = VK_TRUE;
    blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                     VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo blend{};
    blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend.attachmentCount = 1;
    blend.pAttachments = &blendAttachment;

    VkGraphicsPipelineCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    info.stageCount = 2;
    info.pStages = stages;
    info.pVertexInputState = &vertexInput;
    info.pInputAssemblyState = &inputAssembly;
    info.pViewportState = &viewportState;
    info.pRasterizationState = &raster;
    info.pMultisampleState = &multisample;
    info.pDepthStencilState = &depth;
    info.pColorBlendState = &blend;
    info.pDynamicState = &dynamicState;
    info.layout = uiPipelineLayout_;
    info.renderPass = renderPass_;
    const bool ok =
        vkCreateGraphicsPipelines(vk, VK_NULL_HANDLE, 1, &info, nullptr, &uiPipeline_) ==
        VK_SUCCESS;
    vkDestroyShaderModule(vk, vert, nullptr);
    vkDestroyShaderModule(vk, frag, nullptr);
    return ok;
}

bool Renderer::setUiFont(const FontAtlas& font) {
    VkDevice vk = device_.device();
    if (uiPipeline_ == VK_NULL_HANDLE && !createUiPipeline()) return false;

    // Atlas image (R8), uploaded through a staging buffer.
    const uint32_t size = FontAtlas::kAtlasSize;
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R8_UNORM;
    imageInfo.extent = {size, size, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(vk, &imageInfo, nullptr, &uiAtlasImage_) != VK_SUCCESS) return false;
    VkMemoryRequirements requirements;
    vkGetImageMemoryRequirements(vk, uiAtlasImage_, &requirements);
    uiAtlasMemory_ = device_.allocate(requirements, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (uiAtlasMemory_ == VK_NULL_HANDLE) return false;
    uiAtlasMemorySize_ = requirements.size;
    vkBindImageMemory(vk, uiAtlasImage_, uiAtlasMemory_, 0);

    VkBuffer staging = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
    VkDeviceSize stagingSize = 0;
    if (!createBuffer(size * size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, staging, stagingMemory,
                      stagingSize)) {
        return false;
    }
    void* mapped = nullptr;
    vkMapMemory(vk, stagingMemory, 0, size * size, 0, &mapped);
    memcpy(mapped, font.pixels(), size * size);
    vkUnmapMemory(vk, stagingMemory);

    // One-time upload.
    vkResetCommandBuffer(commandBuffer_, 0);
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(commandBuffer_, &beginInfo);
    VkImageMemoryBarrier toDst{};
    toDst.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toDst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toDst.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    toDst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toDst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toDst.image = uiAtlasImage_;
    toDst.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(commandBuffer_, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toDst);
    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {size, size, 1};
    vkCmdCopyBufferToImage(commandBuffer_, staging, uiAtlasImage_,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    VkImageMemoryBarrier toRead = toDst;
    toRead.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    toRead.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    vkCmdPipelineBarrier(commandBuffer_, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                         &toRead);
    vkEndCommandBuffer(commandBuffer_);
    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &commandBuffer_;
    vkResetFences(vk, 1, &fence_);
    vkQueueSubmit(device_.graphicsQueue(), 1, &submit, fence_);
    vkWaitForFences(vk, 1, &fence_, VK_TRUE, UINT64_MAX);
    vkDestroyBuffer(vk, staging, nullptr);
    device_.free(stagingMemory, stagingSize);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = uiAtlasImage_;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8_UNORM;
    viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (vkCreateImageView(vk, &viewInfo, nullptr, &uiAtlasView_) != VK_SUCCESS) return false;

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (vkCreateSampler(vk, &samplerInfo, nullptr, &uiSampler_) != VK_SUCCESS) return false;

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool_;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &uiSetLayout_;
    if (vkAllocateDescriptorSets(vk, &allocInfo, &uiDescriptorSet_) != VK_SUCCESS) return false;
    VkDescriptorImageInfo imageDescriptor{uiSampler_, uiAtlasView_,
                                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = uiDescriptorSet_;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &imageDescriptor;
    vkUpdateDescriptorSets(vk, 1, &write, 0, nullptr);

    // UI geometry buffers: per-frame vertices + a fixed quad index pattern.
    if (!createBuffer(UiDrawList::kMaxQuads * 4 * sizeof(UiVertex),
                      VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, uiVertexBuffer_, uiVertexMemory_,
                      uiVertexMemorySize_)) {
        return false;
    }
    if (!createBuffer(UiDrawList::kMaxQuads * 6 * sizeof(uint32_t),
                      VK_BUFFER_USAGE_INDEX_BUFFER_BIT, uiIndexBuffer_, uiIndexMemory_,
                      uiIndexMemorySize_)) {
        return false;
    }
    uint32_t* indices = nullptr;
    vkMapMemory(vk, uiIndexMemory_, 0, UiDrawList::kMaxQuads * 6 * sizeof(uint32_t), 0,
                reinterpret_cast<void**>(&indices));
    for (uint32_t q = 0; q < UiDrawList::kMaxQuads; ++q) {
        const uint32_t v = q * 4;
        uint32_t* i = indices + q * 6;
        i[0] = v;
        i[1] = v + 1;
        i[2] = v + 2;
        i[3] = v;
        i[4] = v + 2;
        i[5] = v + 3;
    }
    vkUnmapMemory(vk, uiIndexMemory_);

    MGE_LOGI(kTag, "UI overlay ready (atlas %ux%u)", size, size);
    return true;
}

bool Renderer::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkBuffer& buffer,
                            VkDeviceMemory& memory, VkDeviceSize& memorySize) {
    VkDevice vk = device_.device();
    VkBufferCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    info.size = size;
    info.usage = usage;
    if (vkCreateBuffer(vk, &info, nullptr, &buffer) != VK_SUCCESS) return false;
    VkMemoryRequirements requirements;
    vkGetBufferMemoryRequirements(vk, buffer, &requirements);
    // v1: host-visible memory for all buffers (fine on UMA mobile GPUs and
    // llvmpipe; the staging/device-local path arrives with texture streaming).
    memory = device_.allocate(requirements, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (memory == VK_NULL_HANDLE) {
        vkDestroyBuffer(vk, buffer, nullptr);
        buffer = VK_NULL_HANDLE;
        return false;
    }
    memorySize = requirements.size;
    vkBindBufferMemory(vk, buffer, memory, 0);
    return true;
}

bool Renderer::uploadMesh(const MeshData& data, GpuMesh& out) {
    if (data.vertices.empty() || data.indices.empty()) return false;
    VkDevice vk = device_.device();

    const VkDeviceSize vertexBytes = data.vertices.size() * sizeof(Vertex);
    const VkDeviceSize indexBytes = data.indices.size() * sizeof(uint32_t);
    if (!createBuffer(vertexBytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, out.vertexBuffer,
                      out.vertexMemory, out.vertexMemorySize)) {
        return false;  // GPU budget refused — caller degrades
    }
    if (!createBuffer(indexBytes, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, out.indexBuffer,
                      out.indexMemory, out.indexMemorySize)) {
        destroyMesh(out);
        return false;
    }
    void* mapped = nullptr;
    vkMapMemory(vk, out.vertexMemory, 0, vertexBytes, 0, &mapped);
    memcpy(mapped, data.vertices.data(), vertexBytes);
    vkUnmapMemory(vk, out.vertexMemory);
    vkMapMemory(vk, out.indexMemory, 0, indexBytes, 0, &mapped);
    memcpy(mapped, data.indices.data(), indexBytes);
    vkUnmapMemory(vk, out.indexMemory);

    out.indexCount = static_cast<uint32_t>(data.indices.size());
    out.bounds = data.bounds;
    return true;
}

bool Renderer::uploadSkinnedMesh(const SkinnedMeshData& data, GpuSkinnedMesh& out) {
    if (data.vertices.empty() || data.indices.empty()) return false;
    VkDevice vk = device_.device();

    const VkDeviceSize vertexBytes = data.vertices.size() * sizeof(SkinVertex);
    const VkDeviceSize indexBytes = data.indices.size() * sizeof(uint32_t);
    if (!createBuffer(vertexBytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, out.vertexBuffer,
                      out.vertexMemory, out.vertexMemorySize)) {
        return false;  // GPU budget refused — caller degrades
    }
    if (!createBuffer(indexBytes, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, out.indexBuffer,
                      out.indexMemory, out.indexMemorySize)) {
        destroySkinnedMesh(out);
        return false;
    }
    void* mapped = nullptr;
    vkMapMemory(vk, out.vertexMemory, 0, vertexBytes, 0, &mapped);
    memcpy(mapped, data.vertices.data(), vertexBytes);
    vkUnmapMemory(vk, out.vertexMemory);
    vkMapMemory(vk, out.indexMemory, 0, indexBytes, 0, &mapped);
    memcpy(mapped, data.indices.data(), indexBytes);
    vkUnmapMemory(vk, out.indexMemory);

    out.indexCount = static_cast<uint32_t>(data.indices.size());
    out.bounds = data.bounds;
    if (!uploadMorphDeltas(data, out)) {
        destroySkinnedMesh(out);
        return false;
    }
    return true;
}

// Turns the file's per-TARGET delta lists into a per-VERTEX one, so the vertex
// shader can read its own deltas in one contiguous slice instead of searching
// 15 sparse lists. The deltas themselves keep their 12-byte packed form — the
// GPU pays exactly what the file pays (42 KB for the shipped LOD0), once,
// however many characters are drawn with the mesh.
//
// Entry order within a vertex follows the target order in the file, which is
// the order the CPU reference accumulates in: same operands, same sequence,
// same rounding, so the two paths agree bit for bit and `mge_skin_test` can
// demand a 0.000 pixel difference.
bool Renderer::uploadMorphDeltas(const SkinnedMeshData& data, GpuSkinnedMesh& out) {
    const size_t vertexCount = data.vertices.size();
    // A target outside the known set has no weight slot and no scale word, so
    // it is dropped rather than trusted — the same refusal the loader makes.
    auto usable = [vertexCount](const MorphTarget& target, const MorphDelta& delta) {
        return delta.vertex < vertexCount && static_cast<size_t>(target.morph) < kMorphCount;
    };
    size_t deltaCount = 0;
    for (const MorphTarget& target : data.morphs) {
        for (const MorphDelta& delta : target.deltas) {
            if (usable(target, delta)) ++deltaCount;
        }
    }
    if (deltaCount == 0) return true;  // no targets: the empty set serves

    // Prefix offsets, then a write cursor per vertex.
    std::vector<uint32_t> words(kMorphOffsetWord + vertexCount + 1 +
                                deltaCount * kMorphWordsPerDelta);
    uint32_t* offsets = words.data() + kMorphOffsetWord;
    for (const MorphTarget& target : data.morphs) {
        for (const MorphDelta& delta : target.deltas) {
            if (usable(target, delta)) ++offsets[delta.vertex + 1];
        }
    }
    for (size_t v = 0; v < vertexCount; ++v) offsets[v + 1] += offsets[v];

    const uint32_t entryBase =
        static_cast<uint32_t>(kMorphOffsetWord + vertexCount + 1);
    words[kMorphEntryBaseWord] = entryBase;
    words[kMorphVertexCountWord] = static_cast<uint32_t>(vertexCount);
    for (const MorphTarget& target : data.morphs) {
        const size_t index = static_cast<size_t>(target.morph);
        if (index < kMorphCount) {
            memcpy(&words[kMorphScaleWord + index], &target.scale, sizeof(float));
        }
    }

    std::vector<uint32_t> cursor(offsets, offsets + vertexCount);
    for (const MorphTarget& target : data.morphs) {
        const uint32_t morphIndex = static_cast<uint32_t>(target.morph);
        for (const MorphDelta& delta : target.deltas) {
            if (!usable(target, delta)) continue;
            uint32_t* entry = &words[entryBase + cursor[delta.vertex]++ * kMorphWordsPerDelta];
            const uint32_t px = static_cast<uint16_t>(delta.position[0]);
            const uint32_t py = static_cast<uint16_t>(delta.position[1]);
            const uint32_t pz = static_cast<uint16_t>(delta.position[2]);
            const uint32_t nx = static_cast<uint8_t>(delta.normal[0]);
            const uint32_t ny = static_cast<uint8_t>(delta.normal[1]);
            const uint32_t nz = static_cast<uint8_t>(delta.normal[2]);
            entry[0] = px | (py << 16);
            entry[1] = pz | (nx << 16) | (ny << 24);
            entry[2] = nz | (morphIndex << 8);
        }
    }

    const VkDeviceSize bytes = words.size() * sizeof(uint32_t);
    if (!createBuffer(bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, out.morphBuffer,
                      out.morphMemory, out.morphMemorySize)) {
        return false;  // GPU budget refused — caller degrades
    }
    void* mapped = nullptr;
    if (vkMapMemory(device_.device(), out.morphMemory, 0, bytes, 0, &mapped) != VK_SUCCESS)
        return false;
    memcpy(mapped, words.data(), bytes);
    vkUnmapMemory(device_.device(), out.morphMemory);

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = morphPool_;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &morphSetLayout_;
    if (vkAllocateDescriptorSets(device_.device(), &allocInfo, &out.morphSet) != VK_SUCCESS) {
        MGE_LOGW(kTag, "morph descriptor pool exhausted (cap %u meshes)", kMaxMorphMeshes);
        out.morphSet = VK_NULL_HANDLE;
        return false;
    }
    VkDescriptorBufferInfo bufferInfo{out.morphBuffer, 0, bytes};
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = out.morphSet;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    write.pBufferInfo = &bufferInfo;
    vkUpdateDescriptorSets(device_.device(), 1, &write, 0, nullptr);

    out.morphDeltaCount = static_cast<uint32_t>(deltaCount);
    MGE_LOGI(kTag, "morph deltas: %zu across %zu targets, %zu KiB shared", deltaCount,
             data.morphs.size(), static_cast<size_t>(bytes) / 1024);
    return true;
}

void Renderer::destroySkinnedMesh(GpuSkinnedMesh& mesh) {
    VkDevice vk = device_.device();
    if (mesh.vertexBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(vk, mesh.vertexBuffer, nullptr);
        device_.free(mesh.vertexMemory, mesh.vertexMemorySize);
    }
    if (mesh.indexBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(vk, mesh.indexBuffer, nullptr);
        device_.free(mesh.indexMemory, mesh.indexMemorySize);
    }
    // Only if the pool outlives the mesh: shutdown() destroys it, and that
    // frees every set with it.
    if (mesh.morphSet != VK_NULL_HANDLE && morphPool_ != VK_NULL_HANDLE) {
        vkFreeDescriptorSets(vk, morphPool_, 1, &mesh.morphSet);
    }
    if (mesh.morphBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(vk, mesh.morphBuffer, nullptr);
        device_.free(mesh.morphMemory, mesh.morphMemorySize);
    }
    mesh = GpuSkinnedMesh{};
}

bool Renderer::uploadLodMesh(const LodMesh& data, GpuLodMesh& out) {
    out.lods.resize(data.lods.size());
    for (size_t i = 0; i < data.lods.size(); ++i) {
        if (!uploadMesh(data.lods[i], out.lods[i])) {
            destroyLodMesh(out);
            return false;
        }
    }
    out.switchDistances = data.switchDistances;
    out.bounds = data.bounds;
    return true;
}

void Renderer::destroyMesh(GpuMesh& mesh) {
    VkDevice vk = device_.device();
    if (mesh.vertexBuffer != VK_NULL_HANDLE) vkDestroyBuffer(vk, mesh.vertexBuffer, nullptr);
    if (mesh.indexBuffer != VK_NULL_HANDLE) vkDestroyBuffer(vk, mesh.indexBuffer, nullptr);
    device_.free(mesh.vertexMemory, mesh.vertexMemorySize);
    device_.free(mesh.indexMemory, mesh.indexMemorySize);
    mesh = GpuMesh{};
}

void Renderer::destroyLodMesh(GpuLodMesh& mesh) {
    for (auto& lod : mesh.lods) destroyMesh(lod);
    mesh.lods.clear();
}

namespace {
void fillFrameData(void* dest, const Camera& camera, const RendererConfig& config) {
    FrameData frame{};
    const Mat4 vp = camera.viewProj();
    memcpy(frame.viewProj, vp.m, sizeof(frame.viewProj));
    const Vec3 light =
        Vec3{config.lightDir[0], config.lightDir[1], config.lightDir[2]}.normalized();
    frame.lightDirIntensity[0] = light.x;
    frame.lightDirIntensity[1] = light.y;
    frame.lightDirIntensity[2] = light.z;
    frame.lightDirIntensity[3] = config.lightIntensity;
    frame.cameraPos[0] = camera.eye.x;
    frame.cameraPos[1] = camera.eye.y;
    frame.cameraPos[2] = camera.eye.z;
    memcpy(dest, &frame, sizeof(FrameData));
}
}  // namespace

void Renderer::recordDrawItems(const Camera& camera, const DrawItem* items, size_t count,
                               bool cull, RenderStats* stats) {
    const Frustum frustum = Frustum::fromViewProj(camera.viewProj());
    VkPipeline boundPipeline = VK_NULL_HANDLE;
    VkDescriptorSet boundMaterial = VK_NULL_HANDLE;
    for (size_t i = 0; i < count; ++i) {
        const DrawItem& item = items[i];
        if (item.mesh == nullptr || item.mesh->lods.empty()) continue;
        if (cull && !frustum.intersects(item.worldBounds)) {
            if (stats != nullptr) ++stats->culled;
            continue;
        }
        const float distance = (item.lodReference - camera.eye).length();
        const size_t lodIndex =
            selectLod(distance, item.mesh->switchDistances.data(), item.mesh->lods.size());
        const GpuMesh& lod = item.mesh->lods[lodIndex];
        if (!lod.valid()) continue;

        VkPipeline wanted =
            item.material == MaterialKind::Lit ? litPipeline_ : placeholderPipeline_;
        if (wanted != boundPipeline) {
            vkCmdBindPipeline(commandBuffer_, VK_PIPELINE_BIND_POINT_GRAPHICS, wanted);
            boundPipeline = wanted;
        }

        // The material's textures. Every draw binds one: an item without a
        // surface gets the default white, so there is no untextured pipeline.
        const GpuMaterial* surface =
            item.surface != nullptr && item.surface->valid() ? item.surface : &defaultMaterial_;
        if (surface->set != boundMaterial) {
            vkCmdBindDescriptorSets(commandBuffer_, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    pipelineLayout_, 2, 1, &surface->set, 0, nullptr);
            boundMaterial = surface->set;
        }

        DrawPush push{};
        memcpy(push.model, item.model.m, sizeof(push.model));
        memcpy(push.baseColor, item.baseColor, sizeof(push.baseColor));
        memcpy(push.params, item.params, sizeof(push.params));
        push.material[0] = surface->roughness;
        push.material[1] = surface->ao;
        push.material[2] = surface->packed != nullptr && surface->packed->valid() ? 1.0f : 0.0f;
        push.material[3] = surface->uvScale;
        vkCmdPushConstants(commandBuffer_, pipelineLayout_,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                           sizeof(push), &push);

        const VkDeviceSize zero = 0;
        vkCmdBindVertexBuffers(commandBuffer_, 0, 1, &lod.vertexBuffer, &zero);
        vkCmdBindIndexBuffer(commandBuffer_, lod.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(commandBuffer_, lod.indexCount, 1, 0, 0, 0);
        if (stats != nullptr) ++stats->drawn;
    }
}

void Renderer::recordSkinnedItems(const Camera& camera, const SkinnedDrawItem* items,
                                  size_t count, bool cull, RenderStats* stats) {
    if (items == nullptr || count == 0 || skinnedPipeline_ == VK_NULL_HANDLE) return;
    const Frustum frustum = Frustum::fromViewProj(camera.viewProj());
    vkCmdBindPipeline(commandBuffer_, VK_PIPELINE_BIND_POINT_GRAPHICS, skinnedPipeline_);
    VkDescriptorSet boundMorphSet = VK_NULL_HANDLE;
    VkDescriptorSet boundMaterial = VK_NULL_HANDLE;

    for (size_t i = 0; i < count; ++i) {
        const SkinnedDrawItem& item = items[i];
        if (item.mesh == nullptr || !item.mesh->valid()) continue;
        if (stats != nullptr) ++stats->submitted;
        if (cull && !frustum.intersects(item.worldBounds)) {
            if (stats != nullptr) ++stats->culled;
            continue;
        }
        // This character's skin slot — palette + shape weights — is the ONLY
        // per-character GPU data.
        const uint32_t offsets[2] = {
            0, static_cast<uint32_t>(paletteSlotStride_ * skinnedSlots_[i])};
        vkCmdBindDescriptorSets(commandBuffer_, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                pipelineLayout_, 0, 1, &descriptorSet_, 2, offsets);

        // This piece's material — a skin sheet, a garment's cloth, or the
        // default white. Consecutive draws sharing one bind it once.
        const GpuMaterial* surface =
            item.surface != nullptr && item.surface->valid() ? item.surface : &defaultMaterial_;
        if (surface->set != boundMaterial) {
            vkCmdBindDescriptorSets(commandBuffer_, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    pipelineLayout_, 2, 1, &surface->set, 0, nullptr);
            boundMaterial = surface->set;
        }

        // The mesh's shared deltas. A crowd on one mesh binds this once.
        const bool morphed = item.mesh->hasMorphs() && item.morphWeights != nullptr;
        VkDescriptorSet morphSet = morphed ? item.mesh->morphSet : emptyMorphSet_;
        if (morphSet != boundMorphSet) {
            vkCmdBindDescriptorSets(commandBuffer_, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    pipelineLayout_, 1, 1, &morphSet, 0, nullptr);
            boundMorphSet = morphSet;
        }

        DrawPush push{};
        memcpy(push.model, item.model.m, sizeof(push.model));
        memcpy(push.baseColor, item.baseColor, sizeof(push.baseColor));
        push.params[0] = morphed ? 1.0f : 0.0f;  // run the shape pass
        push.material[0] = surface->roughness;
        push.material[1] = surface->ao;
        push.material[2] = surface->packed != nullptr && surface->packed->valid() ? 1.0f : 0.0f;
        push.material[3] = surface->uvScale;
        vkCmdPushConstants(commandBuffer_, pipelineLayout_,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                           sizeof(push), &push);

        const VkDeviceSize zero = 0;
        vkCmdBindVertexBuffers(commandBuffer_, 0, 1, &item.mesh->vertexBuffer, &zero);
        vkCmdBindIndexBuffer(commandBuffer_, item.mesh->indexBuffer, 0, VK_INDEX_TYPE_UINT32);
        const uint32_t indexCount =
            item.indexCount > 0 ? item.indexCount : item.mesh->indexCount;
        vkCmdDrawIndexed(commandBuffer_, indexCount, 1, item.firstIndex, 0, 0);
        if (stats != nullptr) ++stats->drawn;
    }
}

void Renderer::recordObjectView(const ObjectViewDraw& view, uint32_t slotIndex) {
    // Clear the rect (color + depth), retarget the viewport, draw with the
    // view's own camera slot.
    const VkClearRect clearRect{
        {{static_cast<int32_t>(view.rect[0]), static_cast<int32_t>(view.rect[1])},
         {static_cast<uint32_t>(view.rect[2]), static_cast<uint32_t>(view.rect[3])}},
        0,
        1};
    VkClearAttachment clearAttachments[2]{};
    clearAttachments[0].aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    clearAttachments[0].colorAttachment = 0;
    memcpy(clearAttachments[0].clearValue.color.float32, view.clear, sizeof(view.clear));
    clearAttachments[1].aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    clearAttachments[1].clearValue.depthStencil = {1.0f, 0};
    vkCmdClearAttachments(commandBuffer_, 2, clearAttachments, 1, &clearRect);

    const VkViewport viewport{view.rect[0], view.rect[1], view.rect[2], view.rect[3], 0, 1};
    vkCmdSetViewport(commandBuffer_, 0, 1, &viewport);
    vkCmdSetScissor(commandBuffer_, 0, 1, &clearRect.rect);

    const uint32_t offsets[2] = {slotIndex * static_cast<uint32_t>(uniformSlotStride_), 0};
    vkCmdBindDescriptorSets(commandBuffer_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_, 0,
                            1, &descriptorSet_, 2, offsets);
    recordDrawItems(view.camera, view.items, view.count, false, nullptr);
}

bool Renderer::renderFrame(const Camera& camera, const DrawItem* items, size_t count,
                           RenderStats* stats, const UiDrawList* ui,
                           const ObjectViewDraw* objectViews, size_t objectViewCount,
                           const SkinnedDrawItem* skinned, size_t skinnedCount) {
    if (!initialized()) return false;
    VkDevice vk = device_.device();
    if (objectViewCount > kMaxUniformSlots - 1) objectViewCount = kMaxUniformSlots - 1;
    if (skinnedCount > kMaxSkinnedDraws) skinnedCount = kMaxSkinnedDraws;

    // Upload this frame's joint palettes (one dynamic-offset slot each).
    if (skinnedCount > 0 && paletteMemory_ != VK_NULL_HANDLE) {
        void* palettes = nullptr;
        if (vkMapMemory(vk, paletteMemory_, 0, paletteSlotStride_ * kMaxSkinnedDraws, 0,
                        &palettes) == VK_SUCCESS) {
            auto* bytes = static_cast<uint8_t*>(palettes);
            uint32_t slot = 0;
            for (size_t i = 0; i < skinnedCount; ++i) {
                // A character's body regions and garments come through as
                // consecutive draws carrying the same palette AND the same
                // shape, so they cost one slot between them.
                const bool sharesPrevious =
                    i > 0 && skinned[i].palette == skinned[i - 1].palette &&
                    skinned[i].morphWeights == skinned[i - 1].morphWeights;
                if (!sharesPrevious) {
                    slot = static_cast<uint32_t>(i == 0 ? 0 : slot + 1);
                    uint8_t* dest = bytes + paletteSlotStride_ * slot;
                    if (skinned[i].palette != nullptr) {
                        memcpy(dest, skinned[i].palette, sizeof(Mat4) * kJointCount);
                    }
                    float weights[16] = {0};
                    if (skinned[i].morphWeights != nullptr) {
                        memcpy(weights, skinned[i].morphWeights, sizeof(float) * kMorphCount);
                    }
                    memcpy(dest + kSkinSlotWeightOffset, weights, sizeof(weights));
                }
                skinnedSlots_[i] = slot;
            }
            vkUnmapMemory(vk, paletteMemory_);
        }
    }

    // Camera slots: 0 = main, 1.. = object views.
    {
        uint8_t* mapped = nullptr;
        vkMapMemory(vk, uniformMemory_, 0, uniformSlotStride_ * kMaxUniformSlots, 0,
                    reinterpret_cast<void**>(&mapped));
        fillFrameData(mapped, camera, config_);
        for (size_t v = 0; v < objectViewCount; ++v) {
            fillFrameData(mapped + uniformSlotStride_ * (v + 1), objectViews[v].camera, config_);
        }
        vkUnmapMemory(vk, uniformMemory_);
    }

    // UI vertices for this frame.
    if (ui != nullptr && uiVertexBuffer_ != VK_NULL_HANDLE && ui->quadCount() > 0) {
        void* mapped = nullptr;
        vkMapMemory(vk, uiVertexMemory_, 0, ui->vertexCount() * sizeof(UiVertex), 0, &mapped);
        memcpy(mapped, ui->vertices(), ui->vertexCount() * sizeof(UiVertex));
        vkUnmapMemory(vk, uiVertexMemory_);
    }

    RenderStats localStats;
    localStats.submitted = static_cast<uint32_t>(count);

    // Record.
    vkResetCommandBuffer(commandBuffer_, 0);
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(commandBuffer_, &beginInfo);

    VkClearValue clears[2]{};
    memcpy(clears[0].color.float32, config_.clearColor, sizeof(config_.clearColor));
    clears[1].depthStencil = {1.0f, 0};
    VkRenderPassBeginInfo passBegin{};
    passBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    passBegin.renderPass = renderPass_;
    passBegin.framebuffer = framebuffer_;
    passBegin.renderArea = {{0, 0}, {config_.width, config_.height}};
    passBegin.clearValueCount = 2;
    passBegin.pClearValues = clears;
    vkCmdBeginRenderPass(commandBuffer_, &passBegin, VK_SUBPASS_CONTENTS_INLINE);

    const VkViewport fullViewport{0, 0, static_cast<float>(config_.width),
                                  static_cast<float>(config_.height), 0, 1};
    const VkRect2D fullScissor{{0, 0}, {config_.width, config_.height}};
    vkCmdSetViewport(commandBuffer_, 0, 1, &fullViewport);
    vkCmdSetScissor(commandBuffer_, 0, 1, &fullScissor);

    const uint32_t zeroOffsets[2] = {0, 0};
    vkCmdBindDescriptorSets(commandBuffer_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_, 0,
                            1, &descriptorSet_, 2, zeroOffsets);
    recordDrawItems(camera, items, count, true, &localStats);
    recordSkinnedItems(camera, skinned, skinnedCount, true, &localStats);

    // UI overlay pass (P6).
    if (ui != nullptr && uiPipeline_ != VK_NULL_HANDLE && ui->quadCount() > 0) {
        vkCmdSetViewport(commandBuffer_, 0, 1, &fullViewport);
        vkCmdSetScissor(commandBuffer_, 0, 1, &fullScissor);
        vkCmdBindPipeline(commandBuffer_, VK_PIPELINE_BIND_POINT_GRAPHICS, uiPipeline_);
        vkCmdBindDescriptorSets(commandBuffer_, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                uiPipelineLayout_, 0, 1, &uiDescriptorSet_, 0, nullptr);
        const float screen[2] = {static_cast<float>(config_.width),
                                 static_cast<float>(config_.height)};
        vkCmdPushConstants(commandBuffer_, uiPipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT, 0,
                           sizeof(screen), screen);
        const VkDeviceSize zero = 0;
        vkCmdBindVertexBuffers(commandBuffer_, 0, 1, &uiVertexBuffer_, &zero);
        vkCmdBindIndexBuffer(commandBuffer_, uiIndexBuffer_, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(commandBuffer_, static_cast<uint32_t>(ui->quadCount() * 6), 1, 0, 0,
                         0);
        localStats.uiQuads = static_cast<uint32_t>(ui->quadCount());
    }

    // Live-object views (task 5.10) draw on top of the UI: their rects are
    // windows cut into the page (the surrounding chrome stays UI).
    for (size_t v = 0; v < objectViewCount; ++v) {
        recordObjectView(objectViews[v], static_cast<uint32_t>(v + 1));
    }

    vkCmdEndRenderPass(commandBuffer_);

    // Copy the color target into the readback buffer.
    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {config_.width, config_.height, 1};
    vkCmdCopyImageToBuffer(commandBuffer_, colorImage_, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           readbackBuffer_, 1, &region);

    vkEndCommandBuffer(commandBuffer_);

    vkResetFences(vk, 1, &fence_);
    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &commandBuffer_;
    if (vkQueueSubmit(device_.graphicsQueue(), 1, &submit, fence_) != VK_SUCCESS) return false;
    if (vkWaitForFences(vk, 1, &fence_, VK_TRUE, UINT64_MAX) != VK_SUCCESS) return false;

    if (stats != nullptr) *stats = localStats;
    return true;
}

bool Renderer::readback(uint8_t* dest, size_t destSize) {
    const size_t needed = static_cast<size_t>(config_.width) * config_.height * 4;
    if (destSize < needed) return false;
    void* mapped = nullptr;
    if (vkMapMemory(device_.device(), readbackMemory_, 0, needed, 0, &mapped) != VK_SUCCESS)
        return false;
    memcpy(dest, mapped, needed);
    vkUnmapMemory(device_.device(), readbackMemory_);
    return true;
}

}  // namespace mge
