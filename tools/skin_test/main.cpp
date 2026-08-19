// skin_test: proves the GPU skinning path (task 8.10) against the CPU
// reference. body_mesh.h declares skinMesh() to be "the definition GPU
// skinning must match" — so this renders the SAME character, same variant,
// same pose, twice: once CPU-skinned into a plain mesh through the lit
// pipeline, once as the shared template mesh + a 17-matrix palette through
// the skinned pipeline. The two images must agree.
//
// It also demonstrates the P1 claim of the template body: N characters share
// ONE uploaded mesh and differ only by their palette.

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "mge/character/body_mesh.h"
#include "mge/core/memory.h"
#include "mge/graphics/renderer.h"
#include "mge/graphics/vulkan_device.h"

using namespace mge;

namespace {

Camera bodyCamera(uint32_t width, uint32_t height) {
    Camera camera;
    camera.eye = {0.0f, 1.05f, 2.9f};
    camera.target = {0.0f, 0.95f, 0.0f};
    camera.aspect = static_cast<float>(width) / height;
    return camera;
}

double meanAbsDifference(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
    double sum = 0;
    size_t n = 0;
    for (size_t i = 0; i + 3 < a.size(); i += 4) {
        for (int c = 0; c < 3; ++c) {
            sum += std::fabs(static_cast<double>(a[i + c]) - b[i + c]);
            ++n;
        }
    }
    return n > 0 ? sum / n : 0.0;
}

bool writePpm(const std::string& path, const std::vector<uint8_t>& rgba, uint32_t w, uint32_t h) {
    FILE* f = fopen(path.c_str(), "wb");
    if (f == nullptr) return false;
    fprintf(f, "P6\n%u %u\n255\n", w, h);
    for (size_t i = 0; i + 3 < rgba.size(); i += 4) fwrite(&rgba[i], 1, 3, f);
    fclose(f);
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    const std::string outDir = argc > 1 ? argv[1] : ".";

    BudgetRegistry budgets;
    VulkanDevice device(budgets);
    if (!device.init(VulkanDeviceConfig{})) return 1;
    printf("device: %s\n", device.deviceName());

    Renderer renderer(device);
    RendererConfig config;
    config.width = 640;
    config.height = 720;
    if (!renderer.init(config)) return 1;

    // One character: an ordinary variant, mid-stride so joints actually bend
    // (a bind-pose comparison would hide skinning bugs).
    HumanoidVariant variant;
    variant.height = 1.82f;
    variant.bulk = 1.15f;
    variant.shoulderWidth = 0.48f;
    LocomotionAnimator animator;
    for (int i = 0; i < 40; ++i) animator.update(1.0f / 60.0f, 1.6f);
    Pose pose;
    animator.samplePose(pose);

    Mat4 palette[kJointCount];
    buildSkinPalette(variant, pose, palette);

    const std::vector<SkinnedMeshData>& lods = sharedTemplateLods();
    if (lods.empty()) {
        fprintf(stderr, "FAIL: template body has no LODs\n");
        return 1;
    }
    const SkinnedMeshData& body = lods[0];
    printf("template LOD0: %zu vertices, %zu triangles\n", body.vertices.size(),
           body.triangleCount());

    const float skin[4] = {0.80f, 0.62f, 0.48f, 1.0f};
    const size_t pixelBytes = static_cast<size_t>(config.width) * config.height * 4;

    // --- Path A: CPU reference — skin on the CPU, draw as a plain mesh ---
    MeshData cpuSkinned;
    skinMesh(body, palette, cpuSkinned);
    cpuSkinned.computeBounds();
    GpuLodMesh cpuMesh;
    {
        LodMesh lod;
        lod.lods.push_back(cpuSkinned);
        lod.computeBounds();
        if (!renderer.uploadLodMesh(lod, cpuMesh)) return 1;
    }
    std::vector<DrawItem> cpuItems(1);
    cpuItems[0].mesh = &cpuMesh;
    cpuItems[0].worldBounds = Aabb::fromCenterExtents({0, 1, 0}, {2, 2, 2});
    memcpy(cpuItems[0].baseColor, skin, sizeof(skin));
    const Camera camera = bodyCamera(config.width, config.height);
    if (!renderer.renderFrame(camera, cpuItems.data(), cpuItems.size())) return 1;
    std::vector<uint8_t> cpuPixels(pixelBytes);
    if (!renderer.readback(cpuPixels.data(), cpuPixels.size())) return 1;

    // --- Path B: GPU skinning — the shared mesh + this character's palette ---
    GpuSkinnedMesh gpuMesh;
    if (!renderer.uploadSkinnedMesh(body, gpuMesh)) return 1;
    std::vector<SkinnedDrawItem> skinnedItems(1);
    skinnedItems[0].mesh = &gpuMesh;
    skinnedItems[0].palette = palette;
    skinnedItems[0].worldBounds = cpuItems[0].worldBounds;
    memcpy(skinnedItems[0].baseColor, skin, sizeof(skin));
    RenderStats stats;
    if (!renderer.renderFrame(camera, nullptr, 0, &stats, nullptr, nullptr, 0,
                              skinnedItems.data(), skinnedItems.size())) {
        return 1;
    }
    std::vector<uint8_t> gpuPixels(pixelBytes);
    if (!renderer.readback(gpuPixels.data(), gpuPixels.size())) return 1;

    const double difference = meanAbsDifference(cpuPixels, gpuPixels);
    size_t bodyPixels = 0;
    for (size_t i = 0; i + 3 < gpuPixels.size(); i += 4) {
        if (gpuPixels[i] != 135 || gpuPixels[i + 1] != 168 || gpuPixels[i + 2] != 214) {
            ++bodyPixels;
        }
    }
    printf("skinned draw: %u submitted, %u drawn; body covers %.1f%% of the frame\n",
           stats.submitted, stats.drawn,
           100.0 * bodyPixels / (pixelBytes / 4));
    printf("CPU reference vs GPU skinning: mean channel difference %.3f / 255\n", difference);
    writePpm(outDir + "/skin_cpu.ppm", cpuPixels, config.width, config.height);
    writePpm(outDir + "/skin_gpu.ppm", gpuPixels, config.width, config.height);

    // --- Path C: a DRESSED character, exactly as the device draws one:
    //     uncovered body regions as index ranges on the shared mesh, plus a
    //     shared mesh per garment — compared against buildPosedCharacter's
    //     CPU result, which masks the same way.
    const WearableInstance outfit[4] = {
        {WearableKind::Tunic, 1, false, {0.42f, 0.32f, 0.20f, 1}},
        {WearableKind::Armor, 2, false, {0.55f, 0.57f, 0.62f, 1}},
        {WearableKind::Pants, 1, false, {0.25f, 0.22f, 0.18f, 1}},
        {WearableKind::Boots, 1, false, {0.18f, 0.14f, 0.10f, 1}},
    };
    // CPU reference for the dressed character.
    std::vector<CharacterPiece> pieces;
    buildPosedCharacter(variant, outfit, 4, pose, BodyLod::Lod0, pieces);
    std::vector<GpuLodMesh> pieceMeshes(pieces.size());
    std::vector<DrawItem> pieceItems;
    for (size_t i = 0; i < pieces.size(); ++i) {
        LodMesh lod;
        pieces[i].mesh.computeBounds();
        lod.lods.push_back(pieces[i].mesh);
        lod.computeBounds();
        if (!renderer.uploadLodMesh(lod, pieceMeshes[i])) return 1;
        DrawItem item;
        item.mesh = &pieceMeshes[i];
        item.worldBounds = cpuItems[0].worldBounds;
        memcpy(item.baseColor, pieces[i].color, sizeof(item.baseColor));
        pieceItems.push_back(item);
    }
    if (!renderer.renderFrame(camera, pieceItems.data(), pieceItems.size())) return 1;
    std::vector<uint8_t> dressedCpu(pixelBytes);
    renderer.readback(dressedCpu.data(), dressedCpu.size());

    // GPU path: masked body ranges + garment meshes, one palette.
    std::vector<GpuSkinnedMesh> garmentMeshes(4);
    std::vector<SkinnedDrawItem> dressedItems;
    uint32_t visibleRegions = kAllRegions;
    for (const WearableInstance& worn : outfit) visibleRegions &= ~garmentCoverage(worn.kind);
    SkinnedDrawItem bodyItem;
    bodyItem.mesh = &gpuMesh;
    bodyItem.palette = palette;
    bodyItem.worldBounds = cpuItems[0].worldBounds;
    memcpy(bodyItem.baseColor, skin, sizeof(skin));
    for (const MeshPart& part : body.parts) {
        if ((visibleRegions & regionBit(part.region)) == 0) continue;
        bodyItem.firstIndex = part.firstIndex;
        bodyItem.indexCount = part.indexCount;
        dressedItems.push_back(bodyItem);
    }
    const size_t bodyDraws = dressedItems.size();
    for (size_t i = 0; i < 4; ++i) {
        GarmentBuildDesc desc;
        desc.kind = outfit[i].kind;
        desc.layer = outfit[i].layer;
        desc.lod = BodyLod::Lod0;
        SkinnedMeshData garment;
        buildGarmentMesh(desc, garment);
        if (garment.vertices.empty()) continue;
        if (!renderer.uploadSkinnedMesh(garment, garmentMeshes[i])) return 1;
        SkinnedDrawItem worn = bodyItem;
        worn.mesh = &garmentMeshes[i];
        worn.firstIndex = 0;
        worn.indexCount = 0;
        memcpy(worn.baseColor, outfit[i].color, sizeof(worn.baseColor));
        dressedItems.push_back(worn);
    }
    RenderStats dressedStats;
    if (!renderer.renderFrame(camera, nullptr, 0, &dressedStats, nullptr, nullptr, 0,
                              dressedItems.data(), dressedItems.size())) {
        return 1;
    }
    std::vector<uint8_t> dressedGpu(pixelBytes);
    renderer.readback(dressedGpu.data(), dressedGpu.size());
    const double dressedDifference = meanAbsDifference(dressedCpu, dressedGpu);
    writePpm(outDir + "/skin_dressed.ppm", dressedGpu, config.width, config.height);
    printf("dressed character: %zu body ranges + %zu garments = %u draws, one palette;"
           " CPU vs GPU difference %.3f / 255\n",
           bodyDraws, dressedItems.size() - bodyDraws, dressedStats.drawn, dressedDifference);

    // --- The P1 claim: a crowd shares ONE mesh, one palette each ---
    constexpr size_t kCrowd = 12;
    std::vector<Mat4> palettes(kCrowd * kJointCount);
    std::vector<SkinnedDrawItem> crowd(kCrowd);
    for (size_t i = 0; i < kCrowd; ++i) {
        HumanoidVariant person;
        person.height = 1.55f + 0.06f * (i % 7);
        person.bulk = 0.85f + 0.09f * (i % 5);
        person.shoulderWidth = 0.38f + 0.02f * (i % 6);
        LocomotionAnimator walk;
        for (size_t f = 0; f < 30 + i * 3; ++f) walk.update(1.0f / 60.0f, 1.4f);
        Pose walkPose;
        walk.samplePose(walkPose);
        buildSkinPalette(person, walkPose, &palettes[i * kJointCount]);

        const float x = -5.0f + 0.95f * i;
        crowd[i].mesh = &gpuMesh;  // ONE mesh for the whole crowd
        crowd[i].palette = &palettes[i * kJointCount];
        crowd[i].model = Mat4::translation({x, 0, -1.5f});
        crowd[i].worldBounds = Aabb::fromCenterExtents({x, 1, -1.5f}, {1.5f, 1.5f, 1.5f});
        memcpy(crowd[i].baseColor, skin, sizeof(skin));
    }
    Camera crowdCamera;
    crowdCamera.eye = {0.0f, 2.4f, 7.5f};
    crowdCamera.target = {0.0f, 0.9f, -1.5f};
    crowdCamera.aspect = static_cast<float>(config.width) / config.height;
    RenderStats crowdStats;
    if (!renderer.renderFrame(crowdCamera, nullptr, 0, &crowdStats, nullptr, nullptr, 0,
                              crowd.data(), crowd.size())) {
        return 1;
    }
    std::vector<uint8_t> crowdPixels(pixelBytes);
    renderer.readback(crowdPixels.data(), crowdPixels.size());
    writePpm(outDir + "/skin_crowd.ppm", crowdPixels, config.width, config.height);
    const BudgetStats gpu = device.gpuBudgetStats();
    printf("crowd of %zu: %u drawn from ONE uploaded mesh; gpu budget %zu KiB used\n", kCrowd,
           crowdStats.drawn, gpu.usedBytes / 1024);

    for (GpuSkinnedMesh& garment : garmentMeshes) {
        if (garment.valid()) renderer.destroySkinnedMesh(garment);
    }
    for (GpuLodMesh& piece : pieceMeshes) renderer.destroyLodMesh(piece);
    renderer.destroySkinnedMesh(gpuMesh);
    renderer.destroyLodMesh(cpuMesh);
    renderer.shutdown();
    const size_t residual = device.gpuBudgetStats().usedBytes;
    device.shutdown();

    // Rasterizing the same triangles from two vertex paths differs only by
    // float precision; anything above a fraction of a channel is a real bug.
    const bool ok = difference < 1.0 && dressedDifference < 1.0 &&
                    bodyPixels > pixelBytes / 4 / 20 && crowdStats.drawn == kCrowd &&
                    residual == 0;
    if (!ok) {
        fprintf(stderr,
                "FAIL: difference=%.3f dressed=%.3f bodyPixels=%zu crowdDrawn=%u residual=%zu\n",
                difference, dressedDifference, bodyPixels, crowdStats.drawn, residual);
        return 1;
    }
    printf("OK\n");
    return 0;
}
