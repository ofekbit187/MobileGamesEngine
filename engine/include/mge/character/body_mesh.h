#pragma once

// The humanoid template body (CHARACTERS.md §4, task 8.12) — the engine's
// canonical human-shaped model. Every humanoid in every game is this one mesh.
//
// Modelling contract (docs/adr/0005-humanoid-template-body.md):
//   * ONE mesh serves every variant. Variety comes from the skinning palette
//     (per-joint length/thickness scale), never from a per-character mesh —
//     a crowd of 60 characters costs one mesh plus 60 small joint palettes
//     instead of 60 meshes (P1).
//   * Shaped by cross-section rings along each limb, so the silhouette is a
//     real body profile (deltoid, calf, glute, jaw) rather than a stack of
//     primitives, and so the same profile code generates garments that fit
//     every variant by construction (CHARACTERS.md §5.1).
//   * Built at three LODs from the same profiles: matching silhouettes, no
//     decimation artifacts, budget known up front.
//   * Segmented into body regions (§6): a region is what a wearable covers,
//     what gets masked, and what a skin texture island belongs to.
//
// Everything here is load-time work. The frame path sees GPU buffers and a
// 17-joint palette per character, never this.

#include <cstdint>
#include <vector>

#include "mge/character/humanoid.h"
#include "mge/core/math.h"
#include "mge/graphics/mesh_data.h"

namespace mge {

// ------------------------------------------------------------- regions -----

// Body-part segmentation (CHARACTERS.md §6). Each region is one closed shell:
// masking a region drops it whole, so a covered body part leaves no hole and
// no exposed backface — clipping is impossible by construction.
enum class BodyRegion : uint8_t {
    Scalp = 0,  // cranium — masked by hair/helmets
    Face,       // front of the head — masked by masks/visors
    Neck,
    Torso,
    ArmL,
    ArmR,
    HandL,
    HandR,
    LegL,
    LegR,
    FootL,
    FootR,
    Count,
};
constexpr size_t kBodyRegionCount = static_cast<size_t>(BodyRegion::Count);

constexpr uint32_t regionBit(BodyRegion r) { return 1u << static_cast<uint32_t>(r); }
constexpr uint32_t kAllRegions = (1u << kBodyRegionCount) - 1u;
constexpr uint32_t kRegionsArms = regionBit(BodyRegion::ArmL) | regionBit(BodyRegion::ArmR);
constexpr uint32_t kRegionsHands = regionBit(BodyRegion::HandL) | regionBit(BodyRegion::HandR);
constexpr uint32_t kRegionsLegs = regionBit(BodyRegion::LegL) | regionBit(BodyRegion::LegR);
constexpr uint32_t kRegionsFeet = regionBit(BodyRegion::FootL) | regionBit(BodyRegion::FootR);
constexpr uint32_t kRegionsHead = regionBit(BodyRegion::Scalp) | regionBit(BodyRegion::Face);

// --------------------------------------------------------- skinned mesh ----

// 36 bytes: position, normal, UV (unit-normalized uint16), and four bone
// influences with uint8 weights. Four is the hardware-skinning maximum every
// mobile GPU agrees on; the template itself never needs more than two, which
// keeps the skinning inner loop short.
struct SkinVertex {
    Vec3 position;
    Vec3 normal;
    uint16_t uv[2] = {0, 0};
    uint8_t joints[4] = {0, 0, 0, 0};
    uint8_t weights[4] = {255, 0, 0, 0};  // sum == 255
};
static_assert(sizeof(SkinVertex) == 36, "skinned vertex layout is a pipeline contract");

// One region's triangles inside the shared index buffer. Masking is a draw-
// range decision (skip the part), not a mesh rebuild.
struct MeshPart {
    BodyRegion region = BodyRegion::Torso;
    uint32_t firstIndex = 0;
    uint32_t indexCount = 0;
};

struct SkinnedMeshData {
    std::vector<SkinVertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<MeshPart> parts;
    Aabb bounds{};

    void computeBounds();
    size_t triangleCount() const { return indices.size() / 3; }
    void clear() {
        vertices.clear();
        indices.clear();
        parts.clear();
        bounds = Aabb{};
    }
};

// The right-side twin of a left-side joint (the rig mirrors in pairs).
Joint mirrored(Joint j);

// ------------------------------------------------------------- building ----

// Three levels built from the same profiles — the silhouette is preserved
// because the rings are the same rings, just fewer of them.
//   Lod0  close-up / player         1756 triangles
//   Lod1  crowd distance             1012
//   Lod2  far crowd, still animated   520
enum class BodyLod : uint8_t { Lod0 = 0, Lod1, Lod2, Count };
constexpr size_t kBodyLodCount = static_cast<size_t>(BodyLod::Count);

struct BodyBuildDesc {
    BodyLod lod = BodyLod::Lod0;
    uint32_t regions = kAllRegions;  // regions to emit (masking, §5.1)
};

// The canonical template body in bind pose, at the reference proportions.
// Deterministic: same desc in, same vertices out.
void buildTemplateBody(const BodyBuildDesc& desc, SkinnedMeshData& out);

// The full LOD chain, ready to upload as one asset.
void buildTemplateBodyLods(uint32_t regions, std::vector<SkinnedMeshData>& out);

// The proportions the template mesh is modelled at. Every variant is this
// body rescaled by the skinning palette.
const HumanoidVariant& templateVariant();

// ------------------------------------------------- variants via the rig ----

// Per-joint skinning matrices for one character:
//     palette[j] = poseWorld[j] * scale[j] * translate(-templateBindPos[j])
// The scale term is what turns the one template mesh into this variant's
// body — bone-length scaling for height/legs/arms, cross-section scaling for
// bulk/shoulders/hips (CHARACTERS.md §4.1). Animation is unaffected: the pose
// is the same pose on the same rig, so every clip retargets for free (§4.2).
void buildSkinPalette(const HumanoidVariant& variant, const Pose& pose,
                      Mat4 outPalette[kJointCount]);

// Bind-pose joint positions of the template body — what the mesh is bound to.
void templateBindPositions(Vec3 out[kJointCount]);

// CPU reference skinning: the definition GPU skinning must match, and the
// path tools and tests use. Load-time/offline only — never the frame path.
void skinMesh(const SkinnedMeshData& mesh, const Mat4 palette[kJointCount], MeshData& out);

// --------------------------------------------------------- wearables -------

// A garment is the body's own profile pushed out by the layer's thickness, so
// it fits every variant exactly as the body does and each layer encloses the
// one beneath it (CHARACTERS.md §5.4). Authored once, here, against the
// template — the same mesh rides every variant.
struct GarmentBuildDesc {
    WearableKind kind = WearableKind::Tunic;
    uint8_t layer = 1;  // 0 base / 1 mid / 2 outer — sets the offset distance
    BodyLod lod = BodyLod::Lod0;
};

void buildGarmentMesh(const GarmentBuildDesc& desc, SkinnedMeshData& out);

// Which body regions a garment hides (the masking cascade of §5.4).
uint32_t garmentCoverage(WearableKind kind);

// --------------------------------------------------- one-call integration --

// A drawable piece of one character: a posed mesh in character-local space
// plus the colour it renders with.
struct CharacterPiece {
    MeshData mesh;
    float color[4] = {1, 1, 1, 1};
};

// Everything a renderer needs for one humanoid: the template body (with the
// regions its clothes cover masked away) and each garment, all posed by the
// same palette. Template and garment meshes are built once and cached; this
// call skins them, so it belongs on a load/spawn path or in tooling — the
// frame path should skin on the GPU from the cached meshes + palette instead.
void buildPosedCharacter(const HumanoidVariant& variant, const WearableInstance* wearables,
                         size_t wearableCount, const Pose& pose, BodyLod lod,
                         std::vector<CharacterPiece>& out);

// The cached template LOD chain — one copy process-wide, shared by every
// character (this is the P1 claim of the whole design).
const std::vector<SkinnedMeshData>& sharedTemplateLods();

}  // namespace mge
