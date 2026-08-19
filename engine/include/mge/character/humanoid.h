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

// The variant data file's contents: everything humanoid variety comes from.
struct HumanoidVariant {
    float height = 1.75f;         // meters, sole to crown
    float shoulderWidth = 0.42f;  // meters
    float hipWidth = 0.30f;
    float legRatio = 0.50f;       // legs as a fraction of height
    float armRatio = 0.44f;       // arm length as a fraction of height
    float bulk = 1.0f;            // limb/torso thickness scale
    float headScale = 1.0f;
    float skin[4] = {0.80f, 0.62f, 0.48f, 1.0f};
};

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
