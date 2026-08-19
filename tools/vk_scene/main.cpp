// vk_scene: renders a composed 3D scene headlessly and verifies it — the
// Phase 2 proof tool. The scene exercises the v1 renderer end to end:
// lit geometry, virtual-model placeholders (P5) through the same path,
// frustum culling, LOD selection, and GPU memory fully inside budgets.
// Writes a PPM capture for the review board (● real engine output).

#include <cstdio>
#include <cstring>
#include <vector>

#include "mge/core/memory.h"
#include "mge/graphics/mesh_io.h"
#include "mge/graphics/primitives.h"
#include "mge/graphics/renderer.h"
#include "mge/graphics/vulkan_device.h"

using namespace mge;

namespace {

DrawItem makeItem(const GpuLodMesh* mesh, const Vec3& position, float yawRadians,
                  MaterialKind material, float r, float g, float b) {
    DrawItem item;
    item.mesh = mesh;
    Transform xf;
    xf.position = position;
    xf.rotation = Quat::fromAxisAngle({0, 1, 0}, yawRadians);
    item.model = xf.toMatrix();
    // Conservative world bounds: local bounds swept by rotation.
    const Vec3 e = mesh->bounds.extents();
    const float radius = e.length();
    item.worldBounds = Aabb::fromCenterExtents(position + mesh->bounds.center(),
                                               {radius, radius, radius});
    item.lodReference = position;
    item.baseColor[0] = r;
    item.baseColor[1] = g;
    item.baseColor[2] = b;
    item.material = material;
    if (material == MaterialKind::Placeholder) {
        item.params[0] = 6.0f;  // hatch stripes per meter
    }
    return item;
}

}  // namespace

int main(int argc, char** argv) {
    const char* ppmPath = argc > 1 ? argv[1] : "vk_scene.ppm";
    const char* bakedMeshPath = argc > 2 ? argv[2] : nullptr;  // .mgemesh from asset_import

    BudgetRegistry budgets;
    VulkanDevice device(budgets);
    if (!device.init(VulkanDeviceConfig{})) return 1;
    printf("device: %s\n", device.deviceName());

    Renderer renderer(device);
    RendererConfig config;
    config.width = 1280;
    config.height = 720;
    // Sun placed to light the camera-facing sides of the hamlet.
    config.lightDir[0] = -0.25f;
    config.lightDir[1] = 0.80f;
    config.lightDir[2] = 0.55f;
    if (!renderer.init(config)) return 1;

    // --- Meshes ---
    GpuLodMesh ground, house, tower, stallPlaceholder, cratePlaceholder, characterPlaceholder;
    {
        LodMesh m;
        m.lods.push_back(makePlane(60, 60));
        m.computeBounds();
        if (!renderer.uploadLodMesh(m, ground)) return 1;
    }
    {
        LodMesh m;
        m.lods.push_back(makeBox({3.2f, 2.6f, 2.8f}));
        m.computeBounds();
        if (!renderer.uploadLodMesh(m, house)) return 1;
    }
    {
        // Two LODs: fine cylinder near, coarse far — LOD selection is live.
        LodMesh m;
        m.lods.push_back(makeCylinder(1.2f, 7.0f, 32));
        m.lods.push_back(makeCylinder(1.2f, 7.0f, 8));
        m.switchDistances = {25.0f};
        m.computeBounds();
        if (!renderer.uploadLodMesh(m, tower)) return 1;
    }
    // Virtual-model placeholders at their declared proportions (P5).
    {
        LodMesh m;
        m.lods.push_back(makeBox({2.4f, 2.1f, 1.8f}));  // "market stall"
        m.computeBounds();
        if (!renderer.uploadLodMesh(m, stallPlaceholder)) return 1;
    }
    {
        LodMesh m;
        m.lods.push_back(makeBox({1.0f, 1.0f, 1.0f}));  // "crate"
        m.computeBounds();
        if (!renderer.uploadLodMesh(m, cratePlaceholder)) return 1;
    }
    {
        LodMesh m;
        m.lods.push_back(makeCapsule(0.35f, 1.8f, 20, 8));  // "villager"
        m.computeBounds();
        if (!renderer.uploadLodMesh(m, characterPlaceholder)) return 1;
    }

    // Imported asset via the real runtime path: baked .mgemesh -> loader -> GPU.
    GpuLodMesh imported;
    bool haveImported = false;
    if (bakedMeshPath != nullptr) {
        LodMesh m;
        if (!readMeshFile(bakedMeshPath, m)) {
            fprintf(stderr, "FAIL: could not read baked mesh %s\n", bakedMeshPath);
            return 1;
        }
        if (!renderer.uploadLodMesh(m, imported)) return 1;
        haveImported = true;
        printf("imported mesh: %zu vertices\n", m.lods[0].vertices.size());
    }

    // --- Scene ---
    std::vector<DrawItem> items;
    items.push_back(makeItem(&ground, {0, 0, 0}, 0, MaterialKind::Lit, 0.42f, 0.47f, 0.36f));
    // A hamlet of lit houses.
    items.push_back(makeItem(&house, {-5.0f, 1.3f, -2.0f}, 0.3f, MaterialKind::Lit, 0.62f, 0.55f, 0.45f));
    items.push_back(makeItem(&house, {4.5f, 1.3f, -5.5f}, -0.5f, MaterialKind::Lit, 0.58f, 0.50f, 0.42f));
    items.push_back(makeItem(&house, {1.0f, 1.3f, -11.0f}, 1.1f, MaterialKind::Lit, 0.55f, 0.52f, 0.47f));
    // Tower: near instance (LOD0) and far instance (LOD1).
    items.push_back(makeItem(&tower, {8.5f, 3.5f, -1.0f}, 0, MaterialKind::Lit, 0.52f, 0.50f, 0.55f));
    items.push_back(makeItem(&tower, {-14.0f, 3.5f, -34.0f}, 0, MaterialKind::Lit, 0.52f, 0.50f, 0.55f));
    // Virtual models as placeholders — amber hatched volumes (P5).
    items.push_back(makeItem(&stallPlaceholder, {-1.5f, 1.05f, 1.5f}, 0.4f,
                             MaterialKind::Placeholder, 0.85f, 0.55f, 0.18f));
    items.push_back(makeItem(&cratePlaceholder, {2.2f, 0.5f, 2.8f}, 0.1f,
                             MaterialKind::Placeholder, 0.85f, 0.55f, 0.18f));
    items.push_back(makeItem(&characterPlaceholder, {0.8f, 0.9f, 4.0f}, 0,
                             MaterialKind::Placeholder, 0.80f, 0.45f, 0.25f));
    // The imported glTF asset (a cube), stacked into a small well — real
    // imported geometry rendered next to placeholders awaiting theirs.
    if (haveImported) {
        items.push_back(makeItem(&imported, {4.6f, 0.5f, 3.6f}, 0.2f, MaterialKind::Lit,
                                 0.72f, 0.68f, 0.60f));
        items.push_back(makeItem(&imported, {4.6f, 1.35f, 3.6f}, 0.6f, MaterialKind::Lit,
                                 0.66f, 0.62f, 0.55f));
    }

    // Behind-the-camera geometry: must be culled, not drawn.
    items.push_back(makeItem(&house, {0, 1.3f, 40.0f}, 0, MaterialKind::Lit, 1, 0, 0));
    items.push_back(makeItem(&house, {10, 1.3f, 45.0f}, 0, MaterialKind::Lit, 1, 0, 0));

    Camera camera;
    camera.eye = {6.0f, 4.2f, 10.5f};
    camera.target = {-1.0f, 1.0f, -4.0f};
    camera.aspect = static_cast<float>(config.width) / config.height;

    RenderStats stats;
    if (!renderer.renderFrame(camera, items.data(), items.size(), &stats)) {
        fprintf(stderr, "FAIL: renderFrame\n");
        return 1;
    }
    printf("submitted %u, drawn %u, culled %u\n", stats.submitted, stats.drawn, stats.culled);

    // --- Verify on the CPU ---
    const size_t pixelBytes = static_cast<size_t>(config.width) * config.height * 4;
    std::vector<uint8_t> pixels(pixelBytes);
    if (!renderer.readback(pixels.data(), pixels.size())) return 1;

    const uint8_t skyR = static_cast<uint8_t>(config.clearColor[0] * 255.0f + 0.5f);
    const uint8_t skyG = static_cast<uint8_t>(config.clearColor[1] * 255.0f + 0.5f);
    const uint8_t skyB = static_cast<uint8_t>(config.clearColor[2] * 255.0f + 0.5f);
    // Top-left corner must be sky.
    const bool cornerIsSky =
        pixels[0] == skyR && pixels[1] == skyG && pixels[2] == skyB;
    // A meaningful share of the frame must NOT be sky (geometry was drawn).
    size_t nonSky = 0;
    for (size_t i = 0; i < pixelBytes; i += 4) {
        if (pixels[i] != skyR || pixels[i + 1] != skyG || pixels[i + 2] != skyB) ++nonSky;
    }
    const double nonSkyShare = static_cast<double>(nonSky) / (pixelBytes / 4);
    printf("non-sky pixel share: %.1f%%\n", nonSkyShare * 100.0);

    // --- Capture ---
    if (FILE* f = fopen(ppmPath, "wb")) {
        fprintf(f, "P6\n%u %u\n255\n", config.width, config.height);
        for (size_t i = 0; i < pixelBytes; i += 4) fwrite(&pixels[i], 1, 3, f);
        fclose(f);
        printf("wrote %s\n", ppmPath);
    }

    const BudgetStats gpu = device.gpuBudgetStats();
    printf("gpu budget: used %zu KiB / cap %zu KiB (peak %zu KiB)\n", gpu.usedBytes / 1024,
           gpu.capBytes / 1024, gpu.peakBytes / 1024);

    // --- Cleanup: budget must return to zero ---
    renderer.destroyLodMesh(ground);
    renderer.destroyLodMesh(house);
    renderer.destroyLodMesh(tower);
    renderer.destroyLodMesh(stallPlaceholder);
    renderer.destroyLodMesh(cratePlaceholder);
    renderer.destroyLodMesh(characterPlaceholder);
    if (haveImported) renderer.destroyLodMesh(imported);
    renderer.shutdown();
    const size_t residual = device.gpuBudgetStats().usedBytes;
    device.shutdown();

    const bool ok = cornerIsSky && nonSkyShare > 0.15 && stats.culled >= 2 &&
                    stats.drawn == stats.submitted - stats.culled && residual == 0;
    if (!ok) {
        fprintf(stderr, "FAIL: cornerIsSky=%d nonSky=%.3f culled=%u residual=%zu\n",
                cornerIsSky, nonSkyShare, stats.culled, residual);
        return 1;
    }
    printf("OK\n");
    return 0;
}
