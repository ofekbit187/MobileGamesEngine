#pragma once

// The humanoid (Phase 8, CHARACTERS.md §4–§6): canonical rig, data-driven
// body variants, default locomotion animations, and parametric wearables
// that fit every variant.
//
// v1 body: the template is PARAMETRIC — parts generated from the variant's
// proportions and rigidly attached to joints. Because wearables are generated
// from the same proportions (+ layer thickness), "authored once, fits every
// variant" holds by construction, and masking is exact (covered body parts
// are simply not emitted). When an artist-made template body is imported
// (glTF), smooth skinning replaces rigid parts behind these same interfaces.

#include <cstdint>
#include <vector>

#include "mge/core/math.h"
#include "mge/framework/character.h"
#include "mge/graphics/mesh_data.h"

namespace mge {

// ------------------------------------------------------------------- rig ---

enum class Joint : uint8_t {
    Hips = 0,
    Spine,
    Chest,
    Neck,
    Head,
    UpperArmL,
    ForearmL,
    HandL,
    UpperArmR,
    ForearmR,
    HandR,
    ThighL,
    ShinL,
    FootL,
    ThighR,
    ShinR,
    FootR,
    Count,
};
constexpr size_t kJointCount = static_cast<size_t>(Joint::Count);

struct Skeleton {
    int8_t parent[kJointCount];
    Vec3 bindOffset[kJointCount];  // parent-relative, bind pose
    // Bone lengths the body/animation builders need.
    float thighLength = 0, shinLength = 0, upperArmLength = 0, forearmLength = 0;
    float torsoLength = 0, headSize = 0;
};

struct Pose {
    Quat rotation[kJointCount];  // local joint rotations
};

// World (character-local) matrix per joint from skeleton + pose.
void evaluatePose(const Skeleton& skeleton, const Pose& pose, Mat4 outWorld[kJointCount]);

// ------------------------------------------------------ variants (8.13) ----
//
// The variation scope: everything one humanoid may differ from another by,
// and by how much. Two mechanisms realize it, exactly as CHARACTERS.md §4.1
// dictates, and every parameter below belongs to one of them:
//
//   * PROPORTIONS — skeleton scaling. A number in real units (metres, or a
//     ratio of height). It moves joints, so the mesh follows through the
//     skinning palette and every garment follows with it, for free.
//   * SHAPE — morph deltas on the shared template mesh. A number in
//     [-1, +1] where 0 is the template and the sign picks a direction. One
//     stored delta serves both directions.
//
// Neither mechanism ever produces a per-character mesh (P1): proportions are
// a 17-matrix palette, shape is a handful of weights.

// The face is its own sub-schema (§4.1), designed to grow. Every field is a
// shape parameter in [-1, +1]; 0 is the template face.
struct FaceVariant {
    float skull = 0;       // -1 narrow and long  ..  +1 round and broad
    float brow = 0;        // -1 flat             ..  +1 heavy brow ridge
    float cheeks = 0;      // -1 hollow           ..  +1 high wide cheekbones
    float jawWidth = 0;    // -1 tapered          ..  +1 square
    float chin = 0;        // -1 receding         ..  +1 long and projecting
    float noseLength = 0;  // -1 short            ..  +1 long
    float noseWidth = 0;   // -1 narrow           ..  +1 broad
    float mouth = 0;       // -1 small            ..  +1 wide
    float eyes = 0;        // -1 close-set        ..  +1 wide-set
    float ears = 0;        // -1 small            ..  +1 large
};

// The variant data file's contents: everything humanoid variety comes from.
struct HumanoidVariant {
    // --- proportions: skeleton scaling, in real units ---
    float height = 1.75f;         // meters, sole to crown
    float shoulderWidth = 0.37f;  // meters, shoulder joint to shoulder joint
    float hipWidth = 0.20f;       // femoral head to femoral head
    float legRatio = 0.50f;       // legs as a fraction of height
    float armRatio = 0.44f;       // arm length as a fraction of height
    float bulk = 1.0f;            // limb/torso thickness scale
    float headScale = 1.0f;       // head size against the body
    float footScale = 1.0f;       // foot size against the body

    // --- shape: morph deltas, each in [-1, +1] ---
    float chest = 0;   // -1 shallow  ..  +1 deep, broad ribcage
    float belly = 0;   // -1 lean     ..  +1 heavy waist
    float seat = 0;    // -1 flat     ..  +1 full hips and seat
    float muscle = 0;  // -1 soft     ..  +1 defined arms and legs
    float neck = 0;    // -1 slender  ..  +1 thick
    FaceVariant face;

    float skin[4] = {0.80f, 0.62f, 0.48f, 1.0f};
};

// The scope each parameter is defined over. A variant outside it is not
// rejected — it is CLAMPED — because variant files are content and content
// must never be able to produce a body the animation or the wearables cannot
// cope with (docs/adr/0006-humanoid-variation-scope.md).
struct VariantRange {
    float min, max, standard;
};
extern const VariantRange kHeightRange;
extern const VariantRange kShoulderWidthRange;
extern const VariantRange kHipWidthRange;
extern const VariantRange kLegRatioRange;
extern const VariantRange kArmRatioRange;
extern const VariantRange kBulkRange;
extern const VariantRange kHeadScaleRange;
extern const VariantRange kFootScaleRange;

// Clamps every field into the scope. Called by buildSkeleton and by the
// palette builder, so no path can be handed an out-of-scope body.
HumanoidVariant clampToScope(const HumanoidVariant& variant);

// Bone proportions from variant data — all animation retargets for free
// because every variant shares this rig (CHARACTERS.md §4.2).
Skeleton buildSkeleton(const HumanoidVariant& variant);

// ------------------------------------------- default animations (8.14) -----

// The engine's shipped locomotion set: idle and a walk/run cycle, blended by
// normalized speed. Phase advances with distance so feet don't slide.
class LocomotionAnimator {
public:
    // speed in m/s; blends idle (0) -> walk -> run (>= runSpeed).
    void update(float dt, float speed, float runSpeed = 4.0f);
    void samplePose(Pose& out) const;
    float phase() const { return phase_; }

private:
    float phase_ = 0;
    float blend_ = 0;  // 0 idle .. 1 full stride
    float idleTime_ = 0;
};

// ------------------------------------------------- body & wearables --------

// A renderable piece attached to a joint (mesh in joint-local space).
struct RigPart {
    Joint joint = Joint::Hips;
    MeshData mesh;
    float color[4] = {1, 1, 1, 1};
};

// Which body regions a garment covers (masking: covered body parts are not
// emitted — clipping is impossible by construction, CHARACTERS.md §5.1).
enum CoverBits : uint32_t {
    kCoverTorso = 1u << 0,
    kCoverArms = 1u << 1,
    kCoverLegs = 1u << 2,
    kCoverFeet = 1u << 3,
    kCoverScalp = 1u << 4,
};

// Wearable kinds shipped with the engine (parametric templates).
enum class WearableKind : uint8_t {
    Tunic = 0,   // torso, mid layer
    Armor,       // torso, outer layer
    Pants,       // legs
    Boots,       // feet
    HairShort,   // head_hair slot
    HairLong,
    Sword,       // held: blade + guard at the hand, or on the back sheathed
};

struct WearableInstance {
    WearableKind kind;
    uint8_t layer = 1;      // 0 base / 1 mid / 2 outer
    bool sheathed = false;  // held items
    float color[4] = {1, 1, 1, 1};
};

// Builds the full visual for one character: body parts generated from the
// variant, minus regions covered by wearables, plus the wearables generated
// against the SAME proportions (+ per-layer thickness). Output parts render
// as draw items transformed by their joint's pose matrix.
void buildHumanoidVisual(const HumanoidVariant& variant,
                         const WearableInstance* wearables, size_t wearableCount,
                         std::vector<RigPart>& outParts);

}  // namespace mge
