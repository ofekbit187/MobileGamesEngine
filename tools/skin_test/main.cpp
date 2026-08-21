// skin_test: proves the GPU skinning path (task 8.10) against the CPU
// reference. body_mesh.h declares skinMesh() to be "the definition GPU
// skinning must match" — so this renders the SAME character, same variant,
// same pose, twice: once CPU-skinned into a plain mesh through the lit
// pipeline, once as the shared template mesh + a 17-matrix palette + 15 morph
// weights through the skinned pipeline. The two images must agree.
//
// The character is deliberately NOT at the template shape: its 15 morph
// weights are well off zero, so the comparison exercises the shape pass that
// ADR 0009 requires to run in bind space BEFORE the palette. A shader that
// skipped morphs entirely used to pass this test — that is what
// `morphedAgainstTemplate` below now makes impossible.
//
// It also demonstrates the P1 claim of the template body: N characters share
// ONE uploaded mesh and ONE delta buffer, and differ only by their palette
// and their weights.

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "mge/character/body_mesh.h"
#include "mge/core/memory.h"
#include "mge/graphics/renderer.h"
#include "mge/graphics/texture_io.h"
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

// A test sheet whose colour is a known function of the UV it sits at: a
// coloured gradient with a fine grid ruled over it. It is not skin — the
// authored sheet is the textures session's — it is a PROBE. Two properties
// earn it that role:
//   * every texel differs from its neighbours, so a body that sampled ONE
//     texel (the bug this job closes: no UV reached the fragment stage) comes
//     out flat and unmissable;
//   * colour is a function of position on the chart, so CPU and GPU agreeing
//     pixel-for-pixel means they agree about the CHART, not merely about
//     geometry.
TextureData makeChartProbe(uint32_t size) {
    TextureData texture;
    texture.width = size;
    texture.height = size;
    texture.format = TextureFormat::Rgba8;
    texture.colorSpace = ColorSpace::Srgb;
    texture.usage = TextureUsage::Albedo;

    std::vector<uint8_t> level(static_cast<size_t>(size) * size * 4);
    for (uint32_t y = 0; y < size; ++y) {
        for (uint32_t x = 0; x < size; ++x) {
            uint8_t* p = &level[(static_cast<size_t>(y) * size + x) * 4];
            const bool rule = (x % 32 == 0) || (y % 32 == 0);
            p[0] = static_cast<uint8_t>(rule ? 20 : 60 + (x * 180) / size);
            p[1] = static_cast<uint8_t>(rule ? 20 : 60 + (y * 180) / size);
            p[2] = static_cast<uint8_t>(rule ? 20 : 140);
            p[3] = 255;
        }
    }
    // A full chain to 1x1, as the standard requires of every texture; the
    // levels below mip 0 are a box filter of it, which is enough for a probe.
    uint32_t w = size, h = size;
    std::vector<uint8_t> current = level;
    for (uint32_t i = 0; i < fullMipCount(size, size); ++i) {
        TextureMip mip;
        mip.width = w;
        mip.height = h;
        mip.offset = texture.pixels.size();
        mip.size = mipByteSize(TextureFormat::Rgba8, w, h);
        texture.pixels.insert(texture.pixels.end(), current.begin(), current.end());
        texture.mips.push_back(mip);
        const uint32_t nw = w > 1 ? w / 2 : 1, nh = h > 1 ? h / 2 : 1;
        std::vector<uint8_t> next(static_cast<size_t>(nw) * nh * 4);
        for (uint32_t yy = 0; yy < nh; ++yy) {
            for (uint32_t xx = 0; xx < nw; ++xx) {
                for (int c = 0; c < 4; ++c) {
                    uint32_t sum = 0;
                    for (int j = 0; j < 2; ++j) {
                        for (int k = 0; k < 2; ++k) {
                            const uint32_t sx = w > 1 ? xx * 2 + k : 0;
                            const uint32_t sy = h > 1 ? yy * 2 + j : 0;
                            sum += current[(static_cast<size_t>(sy) * w + sx) * 4 + c];
                        }
                    }
                    next[(static_cast<size_t>(yy) * nw + xx) * 4 + c] =
                        static_cast<uint8_t>(sum / 4);
                }
            }
        }
        current.swap(next);
        w = nw;
        h = nh;
    }
    return texture;
}

// Spread of CHROMATICITY — R/(R+G+B) — across the silhouette, x1000.
//
// Deliberately not brightness spread: lighting already varies brightness a
// great deal across a lit body, so a bright-and-dark untextured body would
// score higher than a textured one and prove nothing. Chromaticity divides
// the lighting out. A body sampling a single texel has ONE chromaticity
// everywhere, whatever the shading does to it; a body reading its chart takes
// its hue from where each point sits on the sheet. So this separates "the UV
// reached the fragment stage" from "the body is lit", which brightness cannot.
double chromaticitySpread(const std::vector<uint8_t>& rgba) {
    double sum = 0, sumSq = 0;
    size_t n = 0;
    for (size_t i = 0; i + 3 < rgba.size(); i += 4) {
        if (rgba[i] == 135 && rgba[i + 1] == 168 && rgba[i + 2] == 214) continue;  // sky
        const double total = static_cast<double>(rgba[i]) + rgba[i + 1] + rgba[i + 2];
        if (total < 12.0) continue;  // near-black: chromaticity is pure noise there
        const double chroma = rgba[i] / total;
        sum += chroma;
        sumSq += chroma * chroma;
        ++n;
    }
    if (n == 0) return 0;
    const double mean = sum / n;
    const double variance = sumSq / n - mean * mean;
    return (variance > 0 ? std::sqrt(variance) : 0) * 1000.0;
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
    // (a bind-pose comparison would hide skinning bugs), and carrying a strong
    // shape on every one of the 15 morph parameters (a zero-weight comparison
    // would hide morph bugs the same way).
    HumanoidVariant variant;
    variant.height = 1.82f;
    variant.bulk = 1.15f;
    variant.shoulderWidth = 0.48f;
    variant.chest = 0.70f;
    variant.belly = 0.90f;
    variant.seat = 0.60f;
    variant.muscle = -0.50f;
    variant.neck = 0.80f;
    variant.face.skull = -0.80f;
    variant.face.brow = 0.90f;
    variant.face.cheeks = -0.70f;
    variant.face.jawWidth = 1.00f;
    variant.face.chin = 0.60f;
    variant.face.noseLength = 0.80f;
    variant.face.noseWidth = -0.60f;
    variant.face.mouth = 0.70f;
    variant.face.eyes = -0.50f;
    variant.face.ears = 1.00f;
    float shape[kMorphCount];
    morphWeights(variant, shape);
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

    // --- Path A: CPU reference — morph + skin on the CPU, draw as a plain
    //     mesh. This is the definition; everything below is measured on it.
    MeshData cpuSkinned;
    skinMesh(body, palette, shape, cpuSkinned);
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
    skinnedItems[0].morphWeights = shape;
    skinnedItems[0].worldBounds = cpuItems[0].worldBounds;
    memcpy(skinnedItems[0].baseColor, skin, sizeof(skin));
    RenderStats stats;
    if (!renderer.renderFrame(camera, nullptr, 0, &stats, nullptr, nullptr, 0,
                              skinnedItems.data(), skinnedItems.size())) {
        return 1;
    }
    std::vector<uint8_t> gpuPixels(pixelBytes);
    if (!renderer.readback(gpuPixels.data(), gpuPixels.size())) return 1;

    // --- The morph pass must actually DO something. Same character, same
    //     palette, weights dropped: if this comes out identical to the frame
    //     above, the shape pass never ran and the 0.000 agreement below would
    //     be the agreement of two identically-unmorphed bodies.
    skinnedItems[0].morphWeights = nullptr;
    if (!renderer.renderFrame(camera, nullptr, 0, nullptr, nullptr, nullptr, 0,
                              skinnedItems.data(), skinnedItems.size())) {
        return 1;
    }
    std::vector<uint8_t> templatePixels(pixelBytes);
    if (!renderer.readback(templatePixels.data(), templatePixels.size())) return 1;
    skinnedItems[0].morphWeights = shape;
    const double morphedAgainstTemplate = meanAbsDifference(gpuPixels, templatePixels);
    writePpm(outDir + "/skin_template_shape.ppm", templatePixels, config.width, config.height);

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
    printf("CPU reference vs GPU skinning (15 morph weights on): mean channel difference"
           " %.3f / 255\n",
           difference);
    printf("morphed vs template shape, same palette: %.3f / 255 — the shape pass ran\n",
           morphedAgainstTemplate);
    writePpm(outDir + "/skin_cpu.ppm", cpuPixels, config.width, config.height);
    writePpm(outDir + "/skin_gpu.ppm", gpuPixels, config.width, config.height);

    // --- Path D: the SAME comparison with a texture on. Until this job the
    //     skinned path could not sample one at all: skinned.vert read inUv and
    //     never output it, and SkinnedDrawItem carried no material, so every
    //     character in the engine was untexturable while every wall was not.
    //
    //     The CPU reference textures through the lit pipeline from the UVs
    //     skinMesh() now writes; the GPU path textures from the body's own
    //     chart in the vertex stream. Agreeing to 0.000 means both sample the
    //     SAME texel for the same point on the body.
    TextureData probeData = makeChartProbe(512);
    GpuTexture probeTexture;
    if (!renderer.uploadTexture(probeData, probeTexture)) {
        fprintf(stderr, "FAIL: could not upload the chart probe\n");
        return 1;
    }
    GpuMaterial probeMaterial;
    if (!renderer.createMaterial(&probeTexture, nullptr, probeMaterial)) {
        fprintf(stderr, "FAIL: could not create the probe material\n");
        return 1;
    }

    // CPU reference, textured.
    std::vector<DrawItem> texturedCpuItems = cpuItems;
    texturedCpuItems[0].surface = &probeMaterial;
    for (int c = 0; c < 4; ++c) texturedCpuItems[0].baseColor[c] = 1.0f;
    if (!renderer.renderFrame(camera, texturedCpuItems.data(), texturedCpuItems.size())) return 1;
    std::vector<uint8_t> texturedCpu(pixelBytes);
    if (!renderer.readback(texturedCpu.data(), texturedCpu.size())) return 1;

    // GPU skinning, textured — the thing that did not exist this morning.
    std::vector<SkinnedDrawItem> texturedGpuItems = skinnedItems;
    texturedGpuItems[0].surface = &probeMaterial;
    for (int c = 0; c < 4; ++c) texturedGpuItems[0].baseColor[c] = 1.0f;
    if (!renderer.renderFrame(camera, nullptr, 0, nullptr, nullptr, nullptr, 0,
                              texturedGpuItems.data(), texturedGpuItems.size())) {
        return 1;
    }
    std::vector<uint8_t> texturedGpu(pixelBytes);
    if (!renderer.readback(texturedGpu.data(), texturedGpu.size())) return 1;

    const double texturedDifference = meanAbsDifference(texturedCpu, texturedGpu);
    const double texturedSpread = chromaticitySpread(texturedGpu);
    const double untexturedSpread = chromaticitySpread(gpuPixels);
    writePpm(outDir + "/skin_textured_cpu.ppm", texturedCpu, config.width, config.height);
    writePpm(outDir + "/skin_textured_gpu.ppm", texturedGpu, config.width, config.height);
    printf("TEXTURED CPU vs GPU: mean channel difference %.3f / 255\n", texturedDifference);
    printf("  chromaticity spread across the body: %.1f textured vs %.1f flat-shaded"
           " (x1000) — the chart reaches the fragment stage\n",
           texturedSpread, untexturedSpread);

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
    // Every draw of this character carries its shape — including the garments,
    // which have no morph targets of their own and so are unaffected, exactly
    // as buildPosedCharacter leaves them (ADR 0009: garments do not yet follow
    // the shape half). Passing it uniformly keeps the whole character in ONE
    // uniform slot.
    bodyItem.morphWeights = shape;
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

    // The same guard as the bare character: with the weights dropped, the
    // uncovered skin must visibly change, or the dressed path is agreeing with
    // the reference only because neither of them morphed.
    for (SkinnedDrawItem& item : dressedItems) item.morphWeights = nullptr;
    if (!renderer.renderFrame(camera, nullptr, 0, nullptr, nullptr, nullptr, 0,
                              dressedItems.data(), dressedItems.size())) {
        return 1;
    }
    std::vector<uint8_t> dressedTemplate(pixelBytes);
    renderer.readback(dressedTemplate.data(), dressedTemplate.size());
    const double dressedMorphEffect = meanAbsDifference(dressedGpu, dressedTemplate);
    printf("dressed character: %zu body ranges + %zu garments = %u draws, one palette;"
           " CPU vs GPU difference %.3f / 255 (shape on; %.3f against the template shape)\n",
           bodyDraws, dressedItems.size() - bodyDraws, dressedStats.drawn, dressedDifference,
           dressedMorphEffect);

    // --- The P1 claim: a crowd shares ONE mesh, one palette each ---
    constexpr size_t kCrowd = 12;
    std::vector<Mat4> palettes(kCrowd * kJointCount);
    std::vector<float> shapes(kCrowd * kMorphCount);
    std::vector<SkinnedDrawItem> crowd(kCrowd);
    for (size_t i = 0; i < kCrowd; ++i) {
        HumanoidVariant person;
        person.height = 1.55f + 0.06f * (i % 7);
        person.bulk = 0.85f + 0.09f * (i % 5);
        person.shoulderWidth = 0.38f + 0.02f * (i % 6);
        // Proportions differ, and so does shape: 12 faces, no two alike, off
        // one mesh and one delta buffer.
        person.belly = -0.8f + 0.15f * i;
        person.chest = 0.9f - 0.14f * i;
        person.neck = -0.6f + 0.11f * i;
        person.face.skull = 0.8f - 0.13f * i;
        person.face.jawWidth = -0.9f + 0.16f * i;
        person.face.noseLength = 0.7f - 0.12f * i;
        person.face.ears = -0.7f + 0.12f * i;
        morphWeights(person, &shapes[i * kMorphCount]);
        LocomotionAnimator walk;
        for (size_t f = 0; f < 30 + i * 3; ++f) walk.update(1.0f / 60.0f, 1.4f);
        Pose walkPose;
        walk.samplePose(walkPose);
        buildSkinPalette(person, walkPose, &palettes[i * kJointCount]);

        const float x = -5.0f + 0.95f * i;
        crowd[i].mesh = &gpuMesh;  // ONE mesh for the whole crowd
        crowd[i].palette = &palettes[i * kJointCount];
        crowd[i].morphWeights = &shapes[i * kMorphCount];
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
    printf("crowd of %zu: %u drawn from ONE uploaded mesh + ONE delta buffer (%u deltas);"
           " per character %zu B palette + %zu B shape; gpu budget %zu KiB used\n",
           kCrowd, crowdStats.drawn, gpuMesh.morphDeltaCount, sizeof(Mat4) * kJointCount,
           sizeof(float) * kMorphCount, gpu.usedBytes / 1024);

    for (GpuSkinnedMesh& garment : garmentMeshes) {
        if (garment.valid()) renderer.destroySkinnedMesh(garment);
    }
    for (GpuLodMesh& piece : pieceMeshes) renderer.destroyLodMesh(piece);
    renderer.destroyMaterial(probeMaterial);
    renderer.destroyTexture(probeTexture);
    renderer.destroySkinnedMesh(gpuMesh);
    renderer.destroyLodMesh(cpuMesh);
    renderer.shutdown();
    const size_t residual = device.gpuBudgetStats().usedBytes;
    device.shutdown();

    // Rasterizing the same triangles from two vertex paths differs only by
    // float precision; anything above a fraction of a channel is a real bug.
    // ...and the morph must have moved the surface enough to be worth having:
    // a whole body's worth of shape that shifted fewer pixels than a rounding
    // error means the deltas never reached the shader.
    // The textured comparison holds to the same bar as the untextured one —
    // and the spread check is what makes it meaningful: a body sampling one
    // texel for its whole surface would still agree with itself, so agreement
    // alone proves nothing without evidence the chart actually varied.
    const bool ok = difference < 1.0 && dressedDifference < 1.0 &&
                    texturedDifference < 1.0 && texturedSpread > 4.0 * untexturedSpread &&
                    morphedAgainstTemplate > 0.5 && dressedMorphEffect > 0.1 &&
                    bodyPixels > pixelBytes / 4 / 20 && crowdStats.drawn == kCrowd &&
                    residual == 0;
    if (!ok) {
        fprintf(stderr,
                "FAIL: difference=%.3f dressed=%.3f textured=%.3f spread=%.1f/%.1f"
                " morphEffect=%.3f dressedMorphEffect=%.3f bodyPixels=%zu crowdDrawn=%u"
                " residual=%zu\n",
                difference, dressedDifference, texturedDifference, texturedSpread,
                untexturedSpread, morphedAgainstTemplate, dressedMorphEffect, bodyPixels,
                crowdStats.drawn, residual);
        return 1;
    }
    printf("OK\n");
    return 0;
}
