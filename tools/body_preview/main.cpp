// body_preview: the modelling turntable for the humanoid template body
// (task 8.12). Everything it writes is REAL engine output — the template mesh
// built by the engine, skinned by the engine's palette, rendered by the
// engine's Vulkan renderer.
//
//   body_sheet.ppm     — front / three-quarter / side / back of the template
//   body_variants.ppm  — one mesh, many bodies: the palette does the variety
//   body_walk.ppm      — the shipped walk cycle deforming the template
//   body_dressed.ppm   — garments fitted and layered on the same template
//   body_lods.ppm      — LOD0/1/2 side by side (silhouette must hold)
//
// Usage: mge_body_preview [outputDir]

#include <cstdio>
#include <deque>
#include <cstring>
#include <string>
#include <vector>

#include "mge/character/body_mesh.h"
#include "mge/character/humanoid.h"
#include "mge/core/memory.h"
#include "mge/graphics/primitives.h"
#include "mge/graphics/renderer.h"
#include "mge/graphics/vulkan_device.h"

using namespace mge;

namespace {

struct Skin {
    float color[4] = {0.80f, 0.62f, 0.48f, 1.0f};
};

// One posed, skinned piece on the GPU. Held in a deque: DrawItems point at
// these, so their addresses must stay put as more characters are added. CPU skinning here is deliberate: this
// tool renders stills, and the CPU path is the reference GPU skinning is
// checked against.
struct Piece {
    GpuLodMesh gpu;
    float color[4] = {1, 1, 1, 1};
};

using PieceList = std::deque<Piece>;

bool uploadSkinned(Renderer& renderer, const SkinnedMeshData& mesh,
                   const Mat4 palette[kJointCount], const float color[4],
                   PieceList& out) {
    MeshData posed;
    skinMesh(mesh, palette, posed);
    LodMesh lod;
    lod.lods.push_back(posed);
    lod.computeBounds();
    Piece piece;
    if (!renderer.uploadLodMesh(lod, piece.gpu)) return false;
    std::memcpy(piece.color, color, sizeof piece.color);
    out.push_back(piece);
    return true;
}

void emit(std::vector<DrawItem>& items, const PieceList& pieces, size_t first,
          const Vec3& position, float yaw) {
    for (size_t i = first; i < pieces.size(); ++i) {
        DrawItem item;
        item.mesh = &pieces[i].gpu;
        item.model = Mat4::translation(position) * Mat4::rotation(Quat::fromAxisAngle({0, 1, 0},
                                                                                      yaw));
        item.worldBounds = Aabb::fromCenterExtents(position + Vec3{0, 1.0f, 0}, {2, 2, 2});
        item.lodReference = position;
        std::memcpy(item.baseColor, pieces[i].color, sizeof item.baseColor);
        items.push_back(item);
    }
}

DrawItem prop(const GpuLodMesh* mesh, const Vec3& position, float r, float g, float b) {
    DrawItem item;
    item.mesh = mesh;
    item.model = Mat4::translation(position);
    const Vec3 e = mesh->bounds.extents();
    const float radius = e.length();
    item.worldBounds =
        Aabb::fromCenterExtents(position + mesh->bounds.center(), {radius, radius, radius});
    item.lodReference = position;
    item.baseColor[0] = r;
    item.baseColor[1] = g;
    item.baseColor[2] = b;
    return item;
}

bool capture(Renderer& renderer, const Camera& camera, const std::vector<DrawItem>& items,
             const std::string& path, double minNonSky) {
    RenderStats stats;
    if (!renderer.renderFrame(camera, items.data(), items.size(), &stats)) {
        fprintf(stderr, "FAIL: renderFrame for %s\n", path.c_str());
        return false;
    }
    const size_t pixelBytes = static_cast<size_t>(renderer.width()) * renderer.height() * 4;
    std::vector<uint8_t> pixels(pixelBytes);
    if (!renderer.readback(pixels.data(), pixels.size())) return false;
    size_t nonSky = 0;
    const uint8_t skyR = 135, skyG = 168, skyB = 214;
    for (size_t i = 0; i < pixelBytes; i += 4) {
        if (pixels[i] != skyR || pixels[i + 1] != skyG || pixels[i + 2] != skyB) ++nonSky;
    }
    const double share = static_cast<double>(nonSky) / (pixelBytes / 4);
    FILE* f = fopen(path.c_str(), "wb");
    if (f == nullptr) return false;
    fprintf(f, "P6\n%u %u\n255\n", renderer.width(), renderer.height());
    for (size_t i = 0; i < pixelBytes; i += 4) fwrite(&pixels[i], 1, 3, f);
    fclose(f);
    printf("%s: drawn %u, coverage %.1f%%\n", path.c_str(), stats.drawn, share * 100.0);
    return share > minNonSky;
}

Pose idlePose() {
    LocomotionAnimator anim;
    for (int i = 0; i < 90; ++i) anim.update(1.0f / 60.0f, 0.0f);
    Pose pose;
    anim.samplePose(pose);
    return pose;
}

Pose walkPose(float fraction) {
    LocomotionAnimator anim;
    for (int f = 0; f < 180; ++f) anim.update(1.0f / 60.0f, 1.5f);  // settle the blend
    const int extra = static_cast<int>(60.0f * 0.75f * fraction);
    for (int f = 0; f < extra; ++f) anim.update(1.0f / 60.0f, 1.5f);
    Pose pose;
    anim.samplePose(pose);
    return pose;
}

struct Outfit {
    WearableKind kind;
    uint8_t layer;
    float color[4];
};

// Builds one dressed character: body with covered regions masked away, plus
// each garment fitted to the same variant.
bool addCharacter(Renderer& renderer, const SkinnedMeshData& bodyMesh,
                  const std::vector<SkinnedMeshData>& garments, const Outfit* outfits,
                  size_t outfitCount, const HumanoidVariant& variant, const Pose& pose,
                  const Skin& skin, PieceList& pieces, size_t& firstPiece) {
    firstPiece = pieces.size();
    Mat4 palette[kJointCount];
    buildSkinPalette(variant, pose, palette);
    if (!uploadSkinned(renderer, bodyMesh, palette, skin.color, pieces)) return false;
    for (size_t i = 0; i < outfitCount; ++i) {
        if (!uploadSkinned(renderer, garments[i], palette, outfits[i].color, pieces)) return false;
    }
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
    config.width = 1280;
    config.height = 720;
    config.lightDir[0] = -0.35f;
    config.lightDir[1] = 0.80f;
    config.lightDir[2] = 0.50f;
    if (!renderer.init(config)) return 1;

    // The template body: built once, reused by every character below — the
    // whole point of the design (P1).
    std::vector<SkinnedMeshData> lods;
    buildTemplateBodyLods(kAllRegions, lods);
    printf("template body: LOD0 %zu tris / %zu verts, LOD1 %zu, LOD2 %zu\n",
           lods[0].triangleCount(), lods[0].vertices.size(), lods[1].triangleCount(),
           lods[2].triangleCount());
    printf("template mesh bytes: %zu (one copy for every character on screen)\n",
           lods[0].vertices.size() * sizeof(SkinVertex) + lods[0].indices.size() * 4);

    GpuLodMesh ground;
    {
        LodMesh m;
        m.lods.push_back(makePlane(60, 60));
        m.computeBounds();
        if (!renderer.uploadLodMesh(m, ground)) return 1;
    }

    const Skin skin;
    bool ok = true;

    // ---- 1. Turntable sheet: the template body from four angles ------------
    {
        PieceList pieces;
        size_t first = 0;
        if (!addCharacter(renderer, lods[0], {}, nullptr, 0, templateVariant(), idlePose(), skin,
                          pieces, first)) {
            return 1;
        }
        std::vector<DrawItem> items;
        items.push_back(prop(&ground, {0, 0, 0}, 0.44f, 0.48f, 0.37f));
        const float yaws[4] = {kPi, kPi * 0.75f, kPi * 0.5f, 0.0f};  // front, 3/4, side, back
        for (int i = 0; i < 4; ++i) {
            emit(items, pieces, first, {-1.65f + 1.1f * i, 0, 0}, yaws[i]);
        }
        Camera camera;
        camera.eye = {0.0f, 0.95f, 3.0f};
        camera.target = {0.0f, 0.95f, 0.0f};
        camera.aspect = static_cast<float>(config.width) / config.height;
        ok = capture(renderer, camera, items, outDir + "/body_sheet.ppm", 0.10) && ok;
        for (Piece& p : pieces) renderer.destroyLodMesh(p.gpu);
    }

    // ---- 2. Variants: one mesh, twelve bodies ------------------------------
    {
        const float heights[12] = {1.75f, 2.02f, 1.55f, 2.20f, 1.62f, 1.88f,
                                   1.70f, 2.10f, 1.50f, 1.95f, 1.66f, 1.80f};
        const float bulks[12] = {1.0f, 0.85f, 1.35f, 1.55f, 0.80f, 1.15f,
                                 0.90f, 1.40f, 1.10f, 0.78f, 1.25f, 0.95f};
        const float shoulders[12] = {0.42f, 0.46f, 0.46f, 0.60f, 0.36f, 0.50f,
                                     0.40f, 0.54f, 0.38f, 0.44f, 0.48f, 0.41f};
        const float skins[12][3] = {
            {0.80f, 0.62f, 0.48f}, {0.55f, 0.40f, 0.29f}, {0.70f, 0.52f, 0.38f},
            {0.62f, 0.58f, 0.50f}, {0.86f, 0.68f, 0.55f}, {0.48f, 0.34f, 0.25f},
            {0.75f, 0.58f, 0.44f}, {0.66f, 0.48f, 0.34f}, {0.82f, 0.64f, 0.52f},
            {0.58f, 0.44f, 0.32f}, {0.72f, 0.55f, 0.40f}, {0.63f, 0.47f, 0.35f}};

        PieceList pieces;
        std::vector<DrawItem> items;
        items.push_back(prop(&ground, {0, 0, 0}, 0.44f, 0.48f, 0.37f));
        const Pose pose = idlePose();
        for (int i = 0; i < 12; ++i) {
            HumanoidVariant v;
            v.height = heights[i];
            v.bulk = bulks[i];
            v.shoulderWidth = shoulders[i];
            v.hipWidth = 0.24f + 0.09f * bulks[i];
            v.legRatio = 0.47f + 0.01f * (i % 5);
            v.headScale = 0.94f + 0.03f * (i % 5);
            Skin s;
            for (int c = 0; c < 3; ++c) s.color[c] = skins[i][c];
            size_t first = 0;
            if (!addCharacter(renderer, lods[0], {}, nullptr, 0, v, pose, s, pieces, first)) {
                return 1;
            }
            const float x = -5.5f + 2.2f * (i % 6) + (i < 6 ? 0.0f : 1.1f);
            const float z = i < 6 ? 0.0f : -3.4f;
            emit(items, pieces, first, {x, 0, z}, kPi);
        }
        Camera camera;
        camera.eye = {0.0f, 2.0f, 8.0f};
        camera.target = {0.0f, 1.05f, -1.2f};
        camera.aspect = static_cast<float>(config.width) / config.height;
        ok = capture(renderer, camera, items, outDir + "/body_variants.ppm", 0.15) && ok;
        for (Piece& p : pieces) renderer.destroyLodMesh(p.gpu);
    }

    // ---- 3. The shipped walk cycle on the template -------------------------
    {
        PieceList pieces;
        std::vector<DrawItem> items;
        items.push_back(prop(&ground, {0, 0, 0}, 0.44f, 0.48f, 0.37f));
        for (int i = 0; i < 6; ++i) {
            size_t first = 0;
            if (!addCharacter(renderer, lods[0], {}, nullptr, 0, templateVariant(),
                              walkPose(static_cast<float>(i) / 6.0f), skin, pieces, first)) {
                return 1;
            }
            emit(items, pieces, first, {-5.0f + 2.0f * i, 0, 0}, kPi * 0.5f);
        }
        Camera camera;
        camera.eye = {0.0f, 1.35f, 5.8f};
        camera.target = {0.0f, 0.95f, 0.0f};
        camera.aspect = static_cast<float>(config.width) / config.height;
        ok = capture(renderer, camera, items, outDir + "/body_walk.ppm", 0.10) && ok;
        for (Piece& p : pieces) renderer.destroyLodMesh(p.gpu);
    }

    // ---- 4. Garments: fitting, layering, hair ------------------------------
    {
        const Outfit villager[] = {
            {WearableKind::Tunic, 1, {0.55f, 0.42f, 0.26f, 1}},
            {WearableKind::Pants, 1, {0.30f, 0.24f, 0.18f, 1}},
            {WearableKind::Boots, 1, {0.24f, 0.17f, 0.11f, 1}},
            {WearableKind::HairShort, 2, {0.22f, 0.14f, 0.08f, 1}},
        };
        const Outfit noble[] = {
            {WearableKind::Tunic, 1, {0.56f, 0.18f, 0.17f, 1}},
            {WearableKind::Pants, 1, {0.16f, 0.14f, 0.20f, 1}},
            {WearableKind::Boots, 1, {0.20f, 0.15f, 0.10f, 1}},
            {WearableKind::HairLong, 2, {0.62f, 0.50f, 0.27f, 1}},
        };
        const Outfit guard[] = {
            {WearableKind::Tunic, 1, {0.42f, 0.32f, 0.20f, 1}},
            {WearableKind::Armor, 2, {0.55f, 0.57f, 0.62f, 1}},
            {WearableKind::Pants, 1, {0.25f, 0.22f, 0.18f, 1}},
            {WearableKind::Boots, 1, {0.18f, 0.14f, 0.10f, 1}},
            {WearableKind::HairShort, 2, {0.14f, 0.10f, 0.07f, 1}},
        };

        struct Cast {
            const Outfit* outfits;
            size_t count;
            HumanoidVariant variant;
            float x;
        };
        HumanoidVariant base;
        HumanoidVariant tall;
        tall.height = 1.95f;
        tall.bulk = 0.88f;
        HumanoidVariant broad;
        broad.bulk = 1.40f;
        broad.shoulderWidth = 0.52f;
        broad.height = 1.68f;
        const Cast cast[] = {
            {villager, 4, base, -2.6f},
            {noble, 4, tall, -0.9f},
            {guard, 5, broad, 0.9f},
            {villager, 4, base, 2.6f},  // undressed comparison below
        };

        PieceList pieces;
        std::vector<DrawItem> items;
        items.push_back(prop(&ground, {0, 0, 0}, 0.44f, 0.48f, 0.37f));
        const Pose pose = idlePose();
        for (int c = 0; c < 4; ++c) {
            const bool naked = c == 3;
            // Masking: the body is built WITHOUT the regions the outfit covers.
            uint32_t regions = kAllRegions;
            std::vector<SkinnedMeshData> garments;
            std::vector<Outfit> worn;
            if (!naked) {
                std::vector<WearableInstance> stack(cast[c].count);
                for (size_t i = 0; i < cast[c].count; ++i) {
                    stack[i].kind = cast[c].outfits[i].kind;
                    stack[i].layer = cast[c].outfits[i].layer;
                }
                for (size_t i = 0; i < cast[c].count; ++i) {
                    regions &= ~garmentCoverage(cast[c].outfits[i].kind);
                    // A tunic sealed under armour is never seen: not skinned,
                    // not drawn (CHARACTERS.md §5.4).
                    if (wearableHidden(stack.data(), stack.size(), i)) continue;
                    GarmentBuildDesc desc;
                    desc.kind = cast[c].outfits[i].kind;
                    desc.layer = cast[c].outfits[i].layer;
                    garments.emplace_back();
                    buildGarmentMesh(desc, garments.back());
                    worn.push_back(cast[c].outfits[i]);
                }
            }
            BodyBuildDesc bodyDesc;
            bodyDesc.regions = regions;
            SkinnedMeshData bodyMesh;
            buildTemplateBody(bodyDesc, bodyMesh);
            size_t first = 0;
            if (!addCharacter(renderer, bodyMesh, garments, worn.empty() ? nullptr : worn.data(),
                              worn.size(), cast[c].variant, pose, skin, pieces,
                              first)) {
                return 1;
            }
            emit(items, pieces, first, {cast[c].x, 0, 0}, kPi);
        }
        Camera camera;
        camera.eye = {0.0f, 1.10f, 4.0f};
        camera.target = {0.0f, 0.95f, 0.0f};
        camera.aspect = static_cast<float>(config.width) / config.height;
        ok = capture(renderer, camera, items, outDir + "/body_dressed.ppm", 0.12) && ok;
        for (Piece& p : pieces) renderer.destroyLodMesh(p.gpu);
    }

    // ---- 5. LOD chain: the silhouette must survive the budget cuts ---------
    {
        PieceList pieces;
        std::vector<DrawItem> items;
        items.push_back(prop(&ground, {0, 0, 0}, 0.44f, 0.48f, 0.37f));
        for (int i = 0; i < 3; ++i) {
            size_t first = 0;
            if (!addCharacter(renderer, lods[i], {}, nullptr, 0, templateVariant(), idlePose(),
                              skin, pieces, first)) {
                return 1;
            }
            emit(items, pieces, first, {-1.3f + 1.3f * i, 0, 0}, kPi * 0.85f);
        }
        Camera camera;
        camera.eye = {0.0f, 1.05f, 3.6f};
        camera.target = {0.0f, 0.95f, 0.0f};
        camera.aspect = static_cast<float>(config.width) / config.height;
        ok = capture(renderer, camera, items, outDir + "/body_lods.ppm", 0.10) && ok;
        for (Piece& p : pieces) renderer.destroyLodMesh(p.gpu);
    }

    // ---- 6. Close-ups: the parts that betray a bad model ------------------
    {
        PieceList pieces;
        size_t first = 0;
        if (!addCharacter(renderer, lods[0], {}, nullptr, 0, templateVariant(), idlePose(), skin,
                          pieces, first)) {
            return 1;
        }
        std::vector<DrawItem> items;
        items.push_back(prop(&ground, {0, 0, 0}, 0.44f, 0.48f, 0.37f));
        emit(items, pieces, first, {-0.35f, 0, 0}, kPi);
        emit(items, pieces, first, {0.35f, 0, 0}, kPi * 0.62f);

        Camera portrait;
        portrait.eye = {0.0f, 1.60f, 1.35f};
        portrait.target = {0.0f, 1.46f, 0.0f};
        portrait.aspect = static_cast<float>(config.width) / config.height;
        ok = capture(renderer, portrait, items, outDir + "/body_portrait.ppm", 0.05) && ok;

        Camera lower;
        lower.eye = {0.0f, 0.95f, 1.55f};
        lower.target = {0.0f, 0.72f, 0.0f};
        lower.aspect = static_cast<float>(config.width) / config.height;
        ok = capture(renderer, lower, items, outDir + "/body_lower.ppm", 0.05) && ok;
        for (Piece& p : pieces) renderer.destroyLodMesh(p.gpu);
    }

    renderer.destroyLodMesh(ground);
    renderer.shutdown();
    device.shutdown();
    printf("%s\n", ok ? "body_preview: OK" : "body_preview: FAILED");
    return ok ? 0 : 1;
}
