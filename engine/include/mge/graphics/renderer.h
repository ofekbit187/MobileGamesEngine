#pragma once

// Renderer v1 (tasks 2.2/2.3/2.6/2.7/2.8): forward pass into an offscreen
// color+depth target, budgeted GPU mesh upload, frustum culling and
// distance LOD, two materials (basic lit opaque, virtual-model placeholder),
// and CPU readback for headless verification and captures.
//
// The offscreen target doubles as the swapchain stand-in: on Android the
// same pass renders into swapchain images once surface integration lands
// (the remaining half of task 2.1).

#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

#include "mge/graphics/camera.h"
#include "mge/graphics/mesh_data.h"
#include "mge/graphics/vulkan_device.h"
#include "mge/ui/draw_list.h"

namespace mge {

// One LOD level resident on the GPU. Memory comes from the device's budgeted
// allocator; creation fails cleanly when the GPU budget refuses (P1).
struct GpuMesh {
    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    VkBuffer indexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory vertexMemory = VK_NULL_HANDLE;
    VkDeviceMemory indexMemory = VK_NULL_HANDLE;
    VkDeviceSize vertexMemorySize = 0;
    VkDeviceSize indexMemorySize = 0;
    uint32_t indexCount = 0;
    Aabb bounds{};
    bool valid() const { return vertexBuffer != VK_NULL_HANDLE; }
};

struct GpuLodMesh {
    std::vector<GpuMesh> lods;
    std::vector<float> switchDistances;
    Aabb bounds{};
};

enum class MaterialKind : uint8_t {
    Lit = 0,      // basic lit opaque
    Placeholder,  // virtual-model treatment (P5)
};

struct DrawItem {
    const GpuLodMesh* mesh = nullptr;
    Mat4 model;
    Aabb worldBounds{};        // world-space bounds for culling
    Vec3 lodReference{};       // world position measured for LOD distance
    float baseColor[4] = {1, 1, 1, 1};
    float params[4] = {0, 0, 0, 0};  // placeholder: x = hatch scale
    MaterialKind material = MaterialKind::Lit;
};

struct RenderStats {
    uint32_t submitted = 0;
    uint32_t drawn = 0;
    uint32_t culled = 0;
    uint32_t uiQuads = 0;
};

// A live-object view (task 5.10): 3D content rendered into a UI rectangle —
// e.g. the player's character inside the inventory screen. Drawn after the
// world pass with its own camera, clipped to the rect.
struct ObjectViewDraw {
    Camera camera;
    const DrawItem* items = nullptr;
    size_t count = 0;
    float rect[4] = {0, 0, 100, 100};   // x, y, w, h in pixels
    float clear[4] = {0.1f, 0.1f, 0.1f, 1.0f};
};

struct RendererConfig {
    uint32_t width = 1280;
    uint32_t height = 720;
    float clearColor[4] = {0.53f, 0.66f, 0.84f, 1.0f};  // sky
    float lightDir[3] = {0.35f, 0.85f, 0.40f};          // toward the light
    float lightIntensity = 0.9f;
};

class Renderer {
public:
    explicit Renderer(VulkanDevice& device);
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    bool init(const RendererConfig& config);
    void shutdown();
    bool initialized() const { return renderPass_ != VK_NULL_HANDLE; }

    // Upload/destroy meshes against the GPU budget.
    bool uploadMesh(const MeshData& data, GpuMesh& out);
    bool uploadLodMesh(const LodMesh& data, GpuLodMesh& out);
    void destroyMesh(GpuMesh& mesh);
    void destroyLodMesh(GpuLodMesh& mesh);

    // Uploads the UI font atlas and enables the overlay pipeline (task 5.2).
    bool setUiFont(const FontAtlas& font);

    // Culls, LOD-selects, records, submits, and waits (v1 sync model).
    // Optional: object views (rendered into their rects after the world) and
    // a UI draw list composited on top (the overlay pass, P6).
    bool renderFrame(const Camera& camera, const DrawItem* items, size_t count,
                     RenderStats* stats = nullptr, const UiDrawList* ui = nullptr,
                     const ObjectViewDraw* objectViews = nullptr, size_t objectViewCount = 0);

    // Copies the last rendered frame (RGBA8, width*height*4 bytes) to dest.
    bool readback(uint8_t* dest, size_t destSize);

    uint32_t width() const { return config_.width; }
    uint32_t height() const { return config_.height; }

private:
    bool createTarget();
    bool createDescriptors();
    bool createPipelines();
    bool createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkBuffer& buffer,
                      VkDeviceMemory& memory, VkDeviceSize& memorySize);
    VkShaderModule createShaderModule(const uint32_t* code, size_t codeSize);

    VulkanDevice& device_;
    RendererConfig config_{};

    // Offscreen target
    VkImage colorImage_ = VK_NULL_HANDLE;
    VkDeviceMemory colorMemory_ = VK_NULL_HANDLE;
    VkDeviceSize colorMemorySize_ = 0;
    VkImageView colorView_ = VK_NULL_HANDLE;
    VkImage depthImage_ = VK_NULL_HANDLE;
    VkDeviceMemory depthMemory_ = VK_NULL_HANDLE;
    VkDeviceSize depthMemorySize_ = 0;
    VkImageView depthView_ = VK_NULL_HANDLE;
    VkRenderPass renderPass_ = VK_NULL_HANDLE;
    VkFramebuffer framebuffer_ = VK_NULL_HANDLE;

    // Frame uniforms + descriptors
    VkBuffer uniformBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory uniformMemory_ = VK_NULL_HANDLE;
    VkDeviceSize uniformMemorySize_ = 0;
    VkDescriptorSetLayout setLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet_ = VK_NULL_HANDLE;

    // Pipelines
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline litPipeline_ = VK_NULL_HANDLE;
    VkPipeline placeholderPipeline_ = VK_NULL_HANDLE;

    // UI overlay (task 5.2)
    bool createUiPipeline();
    void recordObjectView(const ObjectViewDraw& view, uint32_t slotIndex);
    void recordDrawItems(const Camera& camera, const DrawItem* items, size_t count,
                         bool cull, RenderStats* stats);
    VkImage uiAtlasImage_ = VK_NULL_HANDLE;
    VkDeviceMemory uiAtlasMemory_ = VK_NULL_HANDLE;
    VkDeviceSize uiAtlasMemorySize_ = 0;
    VkImageView uiAtlasView_ = VK_NULL_HANDLE;
    VkSampler uiSampler_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout uiSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorSet uiDescriptorSet_ = VK_NULL_HANDLE;
    VkPipelineLayout uiPipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline uiPipeline_ = VK_NULL_HANDLE;
    VkBuffer uiVertexBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory uiVertexMemory_ = VK_NULL_HANDLE;
    VkDeviceSize uiVertexMemorySize_ = 0;
    VkBuffer uiIndexBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory uiIndexMemory_ = VK_NULL_HANDLE;
    VkDeviceSize uiIndexMemorySize_ = 0;

    // Dynamic-uniform slots: 0 = main camera, 1.. = object views.
    static constexpr uint32_t kMaxUniformSlots = 9;
    VkDeviceSize uniformSlotStride_ = 256;

    // Commands + readback
    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer_ = VK_NULL_HANDLE;
    VkFence fence_ = VK_NULL_HANDLE;
    VkBuffer readbackBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory readbackMemory_ = VK_NULL_HANDLE;
    VkDeviceSize readbackMemorySize_ = 0;
};

}  // namespace mge
