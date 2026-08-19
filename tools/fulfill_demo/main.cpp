// Fulfillment round-trip (task 7.7, P5's proof):
//   1. author a world against virtual models — placeholders render
//   2. export the manifest for external agents
//   3. an "agent" (simulated here) delivers baked models per the manifest
//   4. fulfillFromDirectory swaps them in under the same ids
//   5. the SAME placements render again — real models, zero scene edits
// Captures before/after (● real engine output) and asserts the invariants.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "mge/framework/virtual_models.h"
#include "mge/graphics/mesh_io.h"
#include "mge/graphics/primitives.h"
#include "mge/graphics/renderer.h"
#include "mge/graphics/vulkan_device.h"

using namespace mge;

namespace {

constexpr uint32_t kW = 1280, kH = 720;

std::string tmpDir() {
    std::string dir = "/tmp";
    if (const char* t = getenv("TMPDIR")) dir = t;
    return dir;
}

bool savePpm(const char* path, const std::vector<uint8_t>& rgba) {
    FILE* f = fopen(path, "wb");
    if (f == nullptr) return false;
    fprintf(f, "P6\n%u %u\n255\n", kW, kH);
    for (size_t i = 0; i < rgba.size(); i += 4) fwrite(&rgba[i], 1, 3, f);
    fclose(f);
    return true;
}

// One source of truth for the scene: placements never change across the
// fulfillment — that IS the point.
struct ScenePlacement {
    AssetId asset;
    Vec3 pos;
    float yaw;
    float color[4];
};

// --- the simulated external agent -----------------------------------------
// Composes a delivered model from primitives, honoring the manifest's
// declared proportions. A real agent would ship glTF through
// mge_asset_import; the baked .mgemesh delivery is identical either way.

MeshData buildStall(const Vec3& p) {
    MeshData m = makeBox({p.x, p.y * 0.45f, p.z});                       // counter
    appendMesh(m, makeBox({0.08f, p.y, 0.08f}), {-p.x * 0.45f, p.y * 0.28f, -p.z * 0.45f});
    appendMesh(m, makeBox({0.08f, p.y, 0.08f}), {p.x * 0.45f, p.y * 0.28f, -p.z * 0.45f});
    appendMesh(m, makeBox({0.08f, p.y, 0.08f}), {-p.x * 0.45f, p.y * 0.28f, p.z * 0.45f});
    appendMesh(m, makeBox({0.08f, p.y, 0.08f}), {p.x * 0.45f, p.y * 0.28f, p.z * 0.45f});
    appendMesh(m, makeBox({p.x * 1.15f, 0.08f, p.z * 1.15f}), {0, p.y * 0.78f, 0});  // roof
    return m;
}

MeshData buildWell(const Vec3& p) {
    MeshData m = makeCylinder(p.x * 0.5f, p.y * 0.55f, 20);              // stone ring
    appendMesh(m, makeBox({0.09f, p.y, 0.09f}), {-p.x * 0.4f, p.y * 0.25f, 0});
    appendMesh(m, makeBox({0.09f, p.y, 0.09f}), {p.x * 0.4f, p.y * 0.25f, 0});
    appendMesh(m, makeBox({p.x * 1.1f, 0.07f, p.z * 0.6f}), {0, p.y * 0.72f, 0});  // roof
    return m;
}

MeshData buildStatue(const Vec3& p) {
    MeshData m = makeBox({p.x, p.y * 0.25f, p.z});                        // pedestal
    appendMesh(m, makeCapsule(p.x * 0.28f, p.y * 0.7f, 16, 6), {0, p.y * 0.55f, 0});
    return m;
}

}  // namespace

int main(int argc, char** argv) {
    const std::string outPrefix = argc > 1 ? argv[1] : "fulfill";

    BudgetRegistry budgets;
    VulkanDevice device(budgets);
    if (!device.init(VulkanDeviceConfig{})) return 1;
    Renderer renderer(device);
    RendererConfig config;
    config.width = kW;
    config.height = kH;
    config.lightDir[0] = -0.25f;
    config.lightDir[1] = 0.80f;
    config.lightDir[2] = 0.55f;
    if (!renderer.init(config)) return 1;

    // --- 1) author the world: real basics + three virtual models ---
    AssetRegistry assets;
    auto meshOf = [](MeshData data) {
        LodMesh m;
        m.lods.push_back(std::move(data));
        m.computeBounds();
        return m;
    };
    const AssetId groundId = assets.registerMesh("world/ground", meshOf(makePlane(60, 60)));
    const AssetId houseId = assets.registerMesh("prop/house", meshOf(makeBox({3.2f, 2.6f, 2.8f})));

    VirtualModelDesc stallDesc;
    stallDesc.proportions = {2.4f, 2.1f, 1.8f};
    stallDesc.description = "wooden market stall with an open front counter";
    stallDesc.style = "rustic medieval, hand-hewn";
    stallDesc.materials = "oak, canvas";
    stallDesc.features = "striped canvas roof, worn planks";
    VirtualModelDesc wellDesc;
    wellDesc.proportions = {1.8f, 1.6f, 1.8f};
    wellDesc.shape = PlaceholderShape::Cylinder;
    wellDesc.description = "stone village well with a small wooden roof";
    wellDesc.materials = "fieldstone, oak";
    VirtualModelDesc statueDesc;
    statueDesc.proportions = {0.9f, 2.4f, 0.9f};
    statueDesc.shape = PlaceholderShape::Capsule;
    statueDesc.description = "weathered statue of a robed founder on a pedestal";
    statueDesc.materials = "granite";

    std::string error;
    if (!validateVirtualModelDesc(stallDesc, &error) ||
        !validateVirtualModelDesc(wellDesc, &error) ||
        !validateVirtualModelDesc(statueDesc, &error)) {
        fprintf(stderr, "FAIL: %s\n", error.c_str());
        return 1;
    }
    const AssetId stallId = assets.registerVirtualModel("prop/market_stall", stallDesc);
    const AssetId wellId = assets.registerVirtualModel("prop/well", wellDesc);
    const AssetId statueId = assets.registerVirtualModel("prop/founder_statue", statueDesc);

    const std::vector<ScenePlacement> placements = {
        {groundId, {0, 0, 0}, 0, {0.42f, 0.47f, 0.36f, 1}},
        {houseId, {-6.5f, 1.3f, -8.0f}, 0.3f, {0.62f, 0.55f, 0.45f, 1}},
        {houseId, {6.5f, 1.3f, -9.0f}, -0.4f, {0.58f, 0.50f, 0.42f, 1}},
        {stallId, {-2.2f, 1.05f, -4.5f}, 0.35f, {0.85f, 0.55f, 0.18f, 1}},
        {wellId, {2.6f, 0.8f, -3.5f}, 0, {0.85f, 0.55f, 0.18f, 1}},
        {statueId, {0.2f, 1.2f, -7.5f}, 0, {0.82f, 0.50f, 0.22f, 1}},
    };

    Camera camera;
    camera.eye = {0.5f, 3.6f, 3.5f};
    camera.target = {0, 1.0f, -6.0f};
    camera.aspect = static_cast<float>(kW) / kH;

    std::vector<uint8_t> before(static_cast<size_t>(kW) * kH * 4);
    std::vector<uint8_t> after(before.size());

    // Renders the placements exactly as they are, resolving look from the
    // registry — placeholder treatment for virtuals, lit for real meshes.
    auto renderScene = [&](std::vector<uint8_t>& pixels, const char* name) {
        std::vector<std::pair<AssetId, GpuLodMesh>> gpu;
        std::vector<DrawItem> items;
        for (const ScenePlacement& placement : placements) {
            const AssetRecord* record = assets.find(placement.asset);
            if (record == nullptr || record->mesh.lods.empty()) continue;
            GpuLodMesh mesh;
            if (!renderer.uploadLodMesh(record->mesh, mesh)) return false;
            gpu.emplace_back(placement.asset, std::move(mesh));
            DrawItem item;
            item.mesh = &gpu.back().second;
            Transform xf;
            xf.position = placement.pos;
            xf.rotation = Quat::fromAxisAngle({0, 1, 0}, placement.yaw);
            item.model = xf.toMatrix();
            const float radius = gpu.back().second.bounds.extents().length();
            item.worldBounds = Aabb::fromCenterExtents(placement.pos, {radius, radius, radius});
            item.lodReference = placement.pos;
            memcpy(item.baseColor, placement.color, sizeof(item.baseColor));
            item.material = record->kind == AssetKind::VirtualModel ? MaterialKind::Placeholder
                                                                    : MaterialKind::Lit;
            if (item.material == MaterialKind::Placeholder) item.params[0] = 6.0f;
            items.push_back(item);
        }
        // gpu vector may reallocate: fix mesh pointers.
        for (size_t i = 0; i < items.size(); ++i) items[i].mesh = &gpu[i].second;

        RenderStats stats;
        const bool ok = renderer.renderFrame(camera, items.data(), items.size(), &stats);
        renderer.readback(pixels.data(), pixels.size());
        char path[512];
        snprintf(path, sizeof(path), "%s_%s.ppm", outPrefix.c_str(), name);
        savePpm(path, pixels);
        printf("render %-7s: %u drawn\n", name, stats.drawn);
        for (auto& [id, mesh] : gpu) renderer.destroyLodMesh(mesh);
        return ok;
    };

    if (!renderScene(before, "before")) return 1;

    // --- 2) manifest for the agents ---
    const std::string manifestPath = tmpDir() + "/world_manifest.json";
    const size_t pendingCount = exportManifest(assets, manifestPath.c_str());
    printf("manifest exported: %zu unfulfilled models -> %s\n", pendingCount,
           manifestPath.c_str());

    // --- 3) the "agent" builds and delivers per the manifest ---
    const std::string deliveryDir = tmpDir();
    writeMeshFile((deliveryDir + "/" + fulfillmentFileName(stallId)).c_str(),
                  meshOf(buildStall(stallDesc.proportions)));
    writeMeshFile((deliveryDir + "/" + fulfillmentFileName(wellId)).c_str(),
                  meshOf(buildWell(wellDesc.proportions)));
    writeMeshFile((deliveryDir + "/" + fulfillmentFileName(statueId)).c_str(),
                  meshOf(buildStatue(statueDesc.proportions)));

    // --- 4) fulfillment: same ids, zero scene edits ---
    const size_t fulfilled = fulfillFromDirectory(assets, deliveryDir.c_str());
    printf("fulfilled: %zu models\n", fulfilled);
    std::vector<const AssetRecord*> stillPending;
    assets.unfulfilled(stillPending);

    // --- 5) render the SAME placements again ---
    if (!renderScene(after, "after")) return 1;

    // Cleanup deliveries.
    remove((deliveryDir + "/" + fulfillmentFileName(stallId)).c_str());
    remove((deliveryDir + "/" + fulfillmentFileName(wellId)).c_str());
    remove((deliveryDir + "/" + fulfillmentFileName(statueId)).c_str());

    size_t changedPixels = 0;
    for (size_t i = 0; i < before.size(); i += 4) {
        if (before[i] != after[i] || before[i + 1] != after[i + 1] ||
            before[i + 2] != after[i + 2]) {
            ++changedPixels;
        }
    }
    const double changedShare = static_cast<double>(changedPixels) / (before.size() / 4);
    printf("pixels changed by fulfillment: %.1f%%\n", changedShare * 100.0);

    renderer.shutdown();
    const size_t residual = device.gpuBudgetStats().usedBytes;
    device.shutdown();

    const bool ok = pendingCount == 3 && fulfilled == 3 && stillPending.empty() &&
                    changedShare > 0.005 && changedShare < 0.5 && residual == 0;
    if (!ok) {
        fprintf(stderr, "FAIL: pending=%zu fulfilled=%zu still=%zu changed=%.3f residual=%zu\n",
                pendingCount, fulfilled, stillPending.size(), changedShare, residual);
        return 1;
    }
    printf("OK\n");
    return 0;
}
