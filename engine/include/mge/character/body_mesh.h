#pragma once

// The humanoid template body (CHARACTERS.md §4, task 8.12) — the engine's
// canonical human-shaped model. Every humanoid in every game is this one mesh.
//
// The model itself is CONTENT: Blender Studio's CC0 human base mesh, placed
// and rigged (never re-shaped) by tools/model/humanoid_template.py, baked to
// `.mgeskin` by the import tool, loaded here. Re-authoring the body never
// touches engine code.
//
// Contract (docs/adr/0005-humanoid-template-body.md):
//   * ONE mesh serves every variant. Variety comes from the skinning palette
//     (per-joint length/thickness scale), never from a per-character mesh —
//     a crowd of 60 characters costs one mesh plus 60 small joint palettes
//     instead of 60 meshes (P1).
//   * Three authored LODs, so every level keeps the silhouette.
//   * Segmented into body regions (§6) derived from the rig: a region is what
//     a wearable covers and what gets masked. The body is ONE watertight
//     shell, so regions are a partition of its triangles, not separate shells.
//     Garments are cut out of that same surface, so they fit it exactly, share
//     its skin weights, and cover exactly what masking removes (§5.1).
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

// Body-part segmentation (CHARACTERS.md §6), derived from each vertex's
// dominant bone. Masking drops a region's whole triangle range; because the
// garment that masks it was cut from those same triangles, a covered body part
// leaves no hole and no exposed backface — clipping is impossible by
// construction.
//
// Face is declared but currently EMPTY: the imported head is a single shell
// and every vertex on it is dominated by the Head joint, so it all lands in
// Scalp. Nothing masks Face yet (hair masks nothing). Splitting it is an
// authoring change, not an engine one (ADR 0005).
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
// mobile GPU agrees on; the shipped template averages 2.2, which keeps the
// skinning inner loop short.
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

// Three levels decimated from the same base mesh, so the silhouette survives.
//   Lod0  close-up / player         2200 triangles / 1640 vertices
//   Lod1  crowd distance            1200 / 1021
//   Lod2  far crowd, still animated  560 /  573
enum class BodyLod : uint8_t { Lod0 = 0, Lod1, Lod2, Count };
constexpr size_t kBodyLodCount = static_cast<size_t>(BodyLod::Count);

struct BodyBuildDesc {
    BodyLod lod = BodyLod::Lod0;
    uint32_t regions = kAllRegions;  // regions to emit (masking, §5.1)
};

// Where the shipped character assets live (`.mgeskin` files). Host builds
// default to the repository's assets/models; Android points this at the
// extracted asset pack. Set it before the first character is built.
void setCharacterAssetDir(const char* dir);
const char* characterAssetDir();

// The canonical template body in bind pose, at the reference proportions,
// with the requested regions kept.
void buildTemplateBody(const BodyBuildDesc& desc, SkinnedMeshData& out);

// The full LOD chain, ready to upload as one asset.
void buildTemplateBodyLods(uint32_t regions, std::vector<SkinnedMeshData>& out);

// The proportions the template mesh is modelled at. Every variant is this
// body rescaled by the skinning palette.
const HumanoidVariant& templateVariant();

// ------------------------------------------------- variants via the rig ----

// Per-joint skinning matrices for one character:
//     palette[j] = poseWorld[j] * R[j] * scale[j] * R[j]^T
//                                        * translate(-templateBindPos[j])
//
// R[j] is the bone's own frame (+Y along the bone): the template's bind pose
// is the base mesh's relaxed A-pose, so scaling in character axes would make a
// bulky character's arms longer instead of thicker.
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

// A garment is the body's own SURFACE, restricted to the regions it covers and
// pushed out by the layer's thickness, so it fits every variant exactly as the
// body does and each layer encloses the one beneath it (CHARACTERS.md §5.4).
// Cut once in Blender against the template — the same mesh rides every
// variant.
struct GarmentBuildDesc {
    WearableKind kind = WearableKind::Tunic;
    uint8_t layer = 1;  // 0 base / 1 mid / 2 outer — sets the offset distance
    BodyLod lod = BodyLod::Lod0;
};

void buildGarmentMesh(const GarmentBuildDesc& desc, SkinnedMeshData& out);

// Which body regions a garment hides (the masking cascade of §5.4).
uint32_t garmentCoverage(WearableKind kind);

// True when wearables[index] is sealed under a strictly outer garment that
// covers everything it covers — it can never be seen, so it is neither skinned
// nor drawn (the "hidden inner geometry costs nothing" half of §5.4). Items
// that cover nothing (hair, held things) are never hidden this way.
bool wearableHidden(const WearableInstance* wearables, size_t count, size_t index);

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

// The cached authored garment for a wearable kind (empty for held items).
const SkinnedMeshData& sharedGarment(WearableKind kind);

}  // namespace mge
