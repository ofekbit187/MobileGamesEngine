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

#include "mge/character/body_mesh.h"
#include "mge/graphics/camera.h"
#include "mge/graphics/mesh_data.h"
#include "mge/graphics/texture_data.h"
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

// A skinned mesh on the GPU (task 8.10): the template body / a garment,
// shared by every character that wears it. Deformation is per-draw palette
// data plus per-draw morph weights, never a per-character copy of the
// geometry (P1).
//
// The mesh's morph deltas (ADR 0009) live here too — once, shared by every
// character drawn with it, in the same sparse 12-bytes-per-moved-vertex form
// the `.mgeskin` file stores. A character adds 15 floats, not a mesh.
struct GpuSkinnedMesh {
    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    VkBuffer indexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory vertexMemory = VK_NULL_HANDLE;
    VkDeviceMemory indexMemory = VK_NULL_HANDLE;
    VkDeviceSize vertexMemorySize = 0;
    VkDeviceSize indexMemorySize = 0;
    uint32_t indexCount = 0;
    Aabb bounds{};

    // Morph deltas. `morphSet` is null when the mesh carries no targets (every
    // garment today) — such a draw binds the renderer's empty set and skips
    // the shape pass entirely.
    VkBuffer morphBuffer = VK_NULL_HANDLE;
    VkDeviceMemory morphMemory = VK_NULL_HANDLE;
    VkDeviceSize morphMemorySize = 0;
    VkDescriptorSet morphSet = VK_NULL_HANDLE;
    uint32_t morphDeltaCount = 0;

    bool valid() const { return vertexBuffer != VK_NULL_HANDLE; }
    bool hasMorphs() const { return morphSet != VK_NULL_HANDLE; }
};

// A texture resident on the GPU: one image, all its mip levels, sampled by
// every material that references it. Memory comes from the device's TEXTURE
// budget, separate from the mesh budget so a texture leak cannot hide inside
// geometry headroom (docs/TEXTURING.md §5).
struct GpuTexture {
    VkImage image = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceSize memorySize = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t mipLevels = 0;
    TextureFormat format = TextureFormat::Rgba8;
    ColorSpace colorSpace = ColorSpace::Linear;
    bool valid() const { return image != VK_NULL_HANDLE; }
};

// What a surface is made of. At most TWO texture fetches, because mobile
// tile-based GPUs are filtering-bound long before they are bandwidth-bound
// and every extra sampler is a permanent per-pixel tax (TEXTURING §3).
//
// A material with no `packed` map is normal, not lazy: roughness and AO fall
// back to the constants below, and most props never need the second fetch.
struct GpuMaterial {
    VkDescriptorSet set = VK_NULL_HANDLE;
    const GpuTexture* albedo = nullptr;
    const GpuTexture* packed = nullptr;  // R=AO G=roughness B=mask, linear
    float baseColor[4] = {1, 1, 1, 1};
    float roughness = 0.8f;  // used when `packed` is absent
    float ao = 1.0f;         // used when `packed` is absent
    float uvScale = 1.0f;    // tiles the mesh's metre-based UVs (mesh_data.h)
    bool valid() const { return set != VK_NULL_HANDLE; }
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
    // Null draws untextured — the renderer binds its 1x1 white default, so
    // there is one lit pipeline rather than a textured and an untextured one.
    const GpuMaterial* surface = nullptr;
};

// One skinned draw: a shared mesh + this character's joint palette and shape.
// `indexCount == 0` draws the whole mesh; a sub-range draws one region
// (masking a covered body part is a draw-range decision).
//
// `palette` and `morphWeights` are the whole of a character's per-draw GPU
// cost: 17 matrices and 15 floats. Consecutive draws that pass the SAME two
// pointers (one character's body regions plus its garments) share one uniform
// slot, so a dressed character still uploads its palette once.
struct SkinnedDrawItem {
    const GpuSkinnedMesh* mesh = nullptr;
    const Mat4* palette = nullptr;         // kJointCount matrices
    const float* morphWeights = nullptr;   // kMorphCount weights in [-1,+1];
                                           // null = the template shape
    Mat4 model;
    Aabb worldBounds{};
    Vec3 lodReference{};
    float baseColor[4] = {1, 1, 1, 1};
    uint32_t firstIndex = 0;
    uint32_t indexCount = 0;
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

    // Skinned geometry (task 8.10): upload once, draw for every character.
    // Morph targets on the mesh are uploaded with it and shared the same way.
    bool uploadSkinnedMesh(const SkinnedMeshData& data, GpuSkinnedMesh& out);
    void destroySkinnedMesh(GpuSkinnedMesh& mesh);
    static constexpr uint32_t kMaxSkinnedDraws = 48;   // palettes per frame
    static constexpr uint32_t kMaxMorphMeshes = 32;    // distinct delta sets

    // Textures and materials (task 2.3/2.6, docs/TEXTURING.md). Uploads are
    // load-path work: every mip comes from the baked container, the runtime
    // neither decodes nor generates any of them.
    bool uploadTexture(const TextureData& data, GpuTexture& out);
    void destroyTexture(GpuTexture& texture);
    // Binds textures into a descriptor set. `albedo` may be null, in which
    // case the default white 1x1 stands in.
    bool createMaterial(const GpuTexture* albedo, const GpuTexture* packed,
                        GpuMaterial& out);
    void destroyMaterial(GpuMaterial& material);
    static constexpr uint32_t kMaxMaterials = 64;

    // Uploads the UI font atlas and enables the overlay pipeline (task 5.2).
    bool setUiFont(const FontAtlas& font);

    // Culls, LOD-selects, records, submits, and waits (v1 sync model).
    // Optional: object views (rendered into their rects after the world) and
    // a UI draw list composited on top (the overlay pass, P6).
    bool renderFrame(const Camera& camera, const DrawItem* items, size_t count,
                     RenderStats* stats = nullptr, const UiDrawList* ui = nullptr,
                     const ObjectViewDraw* objectViews = nullptr, size_t objectViewCount = 0,
                     const SkinnedDrawItem* skinned = nullptr, size_t skinnedCount = 0);

    // Copies the last rendered frame (RGBA8, width*height*4 bytes) to dest.
    bool readback(uint8_t* dest, size_t destSize);

    uint32_t width() const { return config_.width; }
    uint32_t height() const { return config_.height; }

    // The finished frame's color image, left in TRANSFER_SRC_OPTIMAL by the
    // pass — what Swapchain blits to the display (and readback copies).
    VkImage colorImage() const { return colorImage_; }

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
    VkPipeline skinnedPipeline_ = VK_NULL_HANDLE;

    // Skinning palettes: one dynamic-offset slot per skinned draw. A slot is
    // the 17 matrices followed by this character's 15 morph weights.
    VkBuffer paletteBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory paletteMemory_ = VK_NULL_HANDLE;
    VkDeviceSize paletteMemorySize_ = 0;
    VkDeviceSize paletteSlotStride_ = 0;

    // Materials: set 2, one combined-image-sampler pair per material.
    bool createDefaultMaterial();
    VkDescriptorSetLayout materialSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool materialPool_ = VK_NULL_HANDLE;
    VkSampler textureSampler_ = VK_NULL_HANDLE;
    GpuTexture whiteTexture_{};
    GpuMaterial defaultMaterial_{};

    // Morph deltas: set 1, one storage-buffer descriptor per skinned mesh.
    // Meshes without targets bind `emptyMorphSet_` — the shader never reads
    // it, but a statically-used descriptor must still be bound.
    bool uploadMorphDeltas(const SkinnedMeshData& data, GpuSkinnedMesh& out);
    VkDescriptorSetLayout morphSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool morphPool_ = VK_NULL_HANDLE;
    VkDescriptorSet emptyMorphSet_ = VK_NULL_HANDLE;
    VkBuffer emptyMorphBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory emptyMorphMemory_ = VK_NULL_HANDLE;
    VkDeviceSize emptyMorphMemorySize_ = 0;
    // Palette slot each skinned draw reads: consecutive draws sharing a
    // palette (one character's body regions + garments) share one slot.
    uint32_t skinnedSlots_[kMaxSkinnedDraws] = {};
    void recordSkinnedItems(const Camera& camera, const SkinnedDrawItem* items, size_t count,
                            bool cull, RenderStats* stats);

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
