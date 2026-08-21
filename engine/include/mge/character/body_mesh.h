#pragma once

// The humanoid template body (CHARACTERS.md §4, task 8.12) — the engine's
// canonical human-shaped model. Every humanoid in every game is this one mesh.
//
// The model itself is CONTENT: Blender Studio's CC0 human base mesh, placed
// and rigged (never re-shaped) by tools/model/humanoid_template.py, baked to
// `.mgeskin` by the import tool, loaded here. Re-authoring the body never
// touches engine code.
//
// Modelling contract (docs/adr/0007-humanoid-template-body.md):
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

// ------------------------------------------------------------- morphs ------

// A morph target is a SPARSE list of per-vertex displacements: the vertices a
// shape parameter moves, and by how much. Sparse because a facial parameter
// touches a few dozen vertices out of 1640, and storing zeroes for the rest
// would cost more than the mesh.
//
// Positions are quantized to int16 against the target's own scale, normals to
// int8 — 12 bytes per moved vertex. The whole set of 15 targets costs a few
// tens of KB ONCE, process-wide; a character costs 15 floats (P1).
struct MorphDelta {
    uint16_t vertex = 0;
    int16_t position[3] = {0, 0, 0};  // * scale / 32767, in metres
    int8_t normal[3] = {0, 0, 0};     // / 127
    uint8_t pad = 0;
};
static_assert(sizeof(MorphDelta) == 12, "morph delta layout is a file-format contract");

// Which shape parameter a target belongs to. The order is the file order and
// the weight order — appending is safe, reordering is not.
enum class Morph : uint8_t {
    BodyChest = 0,
    BodyBelly,
    BodySeat,
    BodyMuscle,
    BodyNeck,
    FaceSkull,
    FaceBrow,
    FaceCheeks,
    FaceJawWidth,
    FaceChin,
    FaceNoseLength,
    FaceNoseWidth,
    FaceMouth,
    FaceEyes,
    FaceEars,
    Count,
};
constexpr size_t kMorphCount = static_cast<size_t>(Morph::Count);

// The name a target carries in the authored glTF, and in the baked asset.
const char* morphName(Morph morph);

struct MorphTarget {
    Morph morph = Morph::BodyChest;
    float scale = 0;  // metres that int16 32767 stands for
    std::vector<MorphDelta> deltas;
};

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
    std::vector<MorphTarget> morphs;
    Aabb bounds{};

    void computeBounds();
    size_t triangleCount() const { return indices.size() / 3; }
    const MorphTarget* morph(Morph which) const {
        for (const MorphTarget& t : morphs) {
            if (t.morph == which) return &t;
        }
        return nullptr;
    }
    void clear() {
        vertices.clear();
        indices.clear();
        parts.clear();
        morphs.clear();
        bounds = Aabb{};
    }
};

// The right-side twin of a left-side joint (the rig mirrors in pairs).
Joint mirrored(Joint j);

// ------------------------------------------------------------- building ----

// Three levels decimated from the same base mesh, so the silhouette survives.
//   Lod0  close-up / player         2388 triangles / 2097 vertices
//   Lod1  crowd distance            1200 / 1279
//   Lod2  far crowd, still animated  560 /  733
// (LOD0's cap is 2400 since ADR 0012 funded B-9's hairline loop; LOD1/LOD2 are
// unchanged at 1300/650, because the crowd is drawn from those.)
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

// ----------------------------------------------------------- hem loops ----
//
// B-11. A garment terminates its openings on one of these, so that when
// masking removes the limb underneath, the garment's edge and the body's
// surviving edge are the same ring and no gap opens between them.
//
// Every loop is DERIVED FROM THE RIG, not chosen by eye: a trunk loop is a
// horizontal plane through a joint, and a limb loop is a plane perpendicular
// to the bone, because the template's arms hang about 21 degrees out and a
// horizontal cut across an arm is an ellipse, not a cuff. That also means the
// table follows the rig if the rig ever moves, instead of drifting away from
// it silently.
struct HemLoop {
    const char* name = "";
    BodyRegion region = BodyRegion::Torso;
    Vec3 point{};    // a point on the loop's plane, in bind pose
    Vec3 normal{};   // unit normal of that plane
};

size_t templateHemLoopCount();
const HemLoop& templateHemLoop(size_t index);
// By name, or nullptr. Names are lower_snake_case, sided ones end _l / _r.
const HemLoop* findHemLoop(const char* name);

// What the body actually DOES at a declared loop — measured on the mesh, not
// promised by the table. A loop that names a height nothing encircles is worse
// than no table, because a garment authored against it terminates on nothing.
struct HemLoopFit {
    int rings = 0;              // closed rings the plane cuts near the loop
    int openChains = 0;         // chains that did not close — should be zero
    float circumference = 0;    // metres, summed over the rings
    float radius = 0;           // metres, furthest point from the ring's centre
    float offCentre = 0;        // metres between the ring's centre and the declared point
    // How much of the chosen ring is actually made of the loop's own region.
    // A cuff, an elbow or a knee comes back near 1.0. A low share means the
    // plane did not separate the part from its neighbour — on this body the
    // upper arm hangs about 21 degrees out, so a plane perpendicular to its
    // bone keeps clipping the trunk until roughly 56% of the way down, and a
    // "shoulder" ring measured above that is mostly chest.
    float regionShare = 0;
};
HemLoopFit fitHemLoop(const SkinnedMeshData& body, const HemLoop& loop);

// ------------------------------------------------- region vertex groups ----
//
// B-25. Every body vertex tagged with its region, so the fitting pipeline can
// refuse to bind a sleeve to the torso. The regions partition the triangles,
// and since the body is exported one primitive per region no vertex is shared
// between two of them — so this is a total function, and `bodyVertexRegions`
// returning false means the body is malformed rather than merely untagged.
bool bodyVertexRegions(const SkinnedMeshData& body, std::vector<BodyRegion>& out);


// The shape half of the variant, as one weight per morph target in [-1, +1].
// A negative weight applies the stored delta negated, so one authored target
// serves both directions of a parameter.
void morphWeights(const HumanoidVariant& variant, float out[kMorphCount]);

// CPU reference skinning: the definition GPU skinning must match, and the
// path tools and tests use. Load-time/offline only — never the frame path.
//
// The morph pass runs first, on the bind-pose vertex, then the skinning
// palette — the same order the shader will use, so this stays the reference.
void skinMesh(const SkinnedMeshData& mesh, const Mat4 palette[kJointCount], MeshData& out);
void skinMesh(const SkinnedMeshData& mesh, const Mat4 palette[kJointCount],
              const float weights[kMorphCount], MeshData& out);

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

// The cached surface binding for a wearable kind (ADR 0008) — empty when the
// garment has no baked `.mgefit`, in which case it still renders but cannot
// follow morph-driven shape. Baked by `tools/garment_fit`.
struct GarmentBinding;
const GarmentBinding& sharedGarmentBinding(WearableKind kind);

// The same three, addressed by CATALOGUE INDEX rather than by the enum that
// names the shipped six (task 13.12). A garment added as data has an index and
// no enumerator, so this is the path that does not require a code change.
const SkinnedMeshData& sharedGarmentById(size_t index);
const GarmentBinding& sharedGarmentBindingById(size_t index);
uint32_t garmentCoverageById(size_t index);

}  // namespace mge
