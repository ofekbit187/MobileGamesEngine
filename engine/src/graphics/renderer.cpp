#include "mge/graphics/renderer.h"

#include <cstring>

#include "mge/core/log.h"

#include "shaders/lit_frag.h"
#include "shaders/mesh_vert.h"
#include "shaders/placeholder_frag.h"
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

// Must match the shader-side push-constant block (96 bytes <= min spec 128).
struct DrawPush {
    float model[16];
    float baseColor[4];
    float params[4];
};
static_assert(sizeof(DrawPush) == 96, "push constant layout is a shader contract");

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

    MGE_LOGI(kTag, "renderer ready %ux%u", config_.width, config_.height);
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
    if (fence_ != VK_NULL_HANDLE) vkDestroyFence(vk, fence_, nullptr);
    if (commandPool_ != VK_NULL_HANDLE) vkDestroyCommandPool(vk, commandPool_, nullptr);
    fence_ = VK_NULL_HANDLE;
    commandPool_ = VK_NULL_HANDLE;
    commandBuffer_ = VK_NULL_HANDLE;

    if (litPipeline_ != VK_NULL_HANDLE) vkDestroyPipeline(vk, litPipeline_, nullptr);
    if (placeholderPipeline_ != VK_NULL_HANDLE) vkDestroyPipeline(vk, placeholderPipeline_, nullptr);
    if (pipelineLayout_ != VK_NULL_HANDLE) vkDestroyPipelineLayout(vk, pipelineLayout_, nullptr);
    litPipeline_ = placeholderPipeline_ = VK_NULL_HANDLE;
    pipelineLayout_ = VK_NULL_HANDLE;

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

    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &binding;
    if (vkCreateDescriptorSetLayout(vk, &layoutInfo, nullptr, &setLayout_) != VK_SUCCESS)
        return false;

    VkDescriptorPoolSize poolSizes[2] = {
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1},
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
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = descriptorSet_;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    write.pBufferInfo = &bufferInfo;
    vkUpdateDescriptorSets(vk, 1, &write, 0, nullptr);
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
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &setLayout_;
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
    VkVertexInputAttributeDescription vertexAttributes[2]{};
    vertexAttributes[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position)};
    vertexAttributes[1] = {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal)};

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &vertexBinding;
    vertexInput.vertexAttributeDescriptionCount = 2;
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

    auto makePipeline = [&](VkShaderModule fragModule, VkPipeline& out) {
        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vert;
        stages[0].pName = "main";
        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = fragModule;
        stages[1].pName = "main";

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
        info.layout = pipelineLayout_;
        info.renderPass = renderPass_;
        return vkCreateGraphicsPipelines(vk, VK_NULL_HANDLE, 1, &info, nullptr, &out) ==
               VK_SUCCESS;
    };

    const bool ok = makePipeline(litFrag, litPipeline_) &&
                    makePipeline(placeholderFrag, placeholderPipeline_);

    vkDestroyShaderModule(vk, vert, nullptr);
    vkDestroyShaderModule(vk, litFrag, nullptr);
    vkDestroyShaderModule(vk, placeholderFrag, nullptr);
    return ok;
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

        DrawPush push{};
        memcpy(push.model, item.model.m, sizeof(push.model));
        memcpy(push.baseColor, item.baseColor, sizeof(push.baseColor));
        memcpy(push.params, item.params, sizeof(push.params));
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

    const uint32_t offset = slotIndex * static_cast<uint32_t>(uniformSlotStride_);
    vkCmdBindDescriptorSets(commandBuffer_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_, 0,
                            1, &descriptorSet_, 1, &offset);
    recordDrawItems(view.camera, view.items, view.count, false, nullptr);
}

bool Renderer::renderFrame(const Camera& camera, const DrawItem* items, size_t count,
                           RenderStats* stats, const UiDrawList* ui,
                           const ObjectViewDraw* objectViews, size_t objectViewCount) {
    if (!initialized()) return false;
    VkDevice vk = device_.device();
    if (objectViewCount > kMaxUniformSlots - 1) objectViewCount = kMaxUniformSlots - 1;

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

    const uint32_t zeroOffset = 0;
    vkCmdBindDescriptorSets(commandBuffer_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_, 0,
                            1, &descriptorSet_, 1, &zeroOffset);
    recordDrawItems(camera, items, count, true, &localStats);

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
