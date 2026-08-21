#pragma once

// Layered poses with masks (task 14.1, CHARACTERS.md §6.2, P12).
//
// THE PROBLEM. `LocomotionAnimator` produces one whole-body `Pose`, so
// locomotion owns every joint and a character cannot swing a sword while
// walking. Every use archetype (14.2), every interruption (14.4) and the
// facial expressions (8.20) need an action to drive part of the skeleton
// while locomotion keeps the rest.
//
// THE MECHANISM. Layering happens in POSE SPACE — local joint rotations —
// BEFORE `evaluatePose`. A base pose is blended with overlay poses per joint,
// weighted by a `JointMask`, and the result is a single ordinary `Pose`.
// Everything downstream is untouched:
//
//     locomotion ──▶ base Pose ─┐
//                               ├─▶ LayeredPose ─▶ one Pose ─▶ evaluatePose
//     action     ──▶ overlay  ──┘   (mask blend)              ─▶ one palette
//                                                             ─▶ one skin pass
//
// This is why layering cannot multiply the frame's work: it finishes before
// the frame path begins. Nothing downstream of the composer can tell a
// layered character from an unlayered one — same palette, same upload, same
// draw. A design needing a second palette per character would be the wrong
// design (and is explicitly refused here).
//
// P1. No heap, anywhere on this path. `Pose` is 17 quaternions by value and
// `JointMask` is 17 floats by value. `LayeredPose` blends each layer in
// place as it arrives rather than storing it, so the whole composer is one
// `Pose` plus two counters (~280 bytes) no matter how many layers pass
// through it — and it is SCRATCH, not per-character state. Live it on the
// stack for the duration of a pose update and let a whole crowd share it;
// the state a character actually keeps is its animator's few floats.
//
// The layer cap is a frame-cost bound, not a storage bound: it limits how
// many blends one pose may pay for. Past the cap `addLayer` REFUSES and says
// so, rather than growing.
//
// THE RIG IS A SEAM (AGENTS.md §4). Nothing here defines, adds or renames a
// joint; the `Joint` enum, `Skeleton`, bind offsets and the 17-joint count
// stay the character asset pipeline's to define and this module's to obey.
// Mask helpers walk `Skeleton::parent` rather than restating the hierarchy,
// so a rig re-parenting carries into every mask for free instead of silently
// disagreeing with it.

#include <cstddef>
#include <cstdint>

#include "mge/character/humanoid.h"
#include "mge/core/math.h"

namespace mge {

// ------------------------------------------------------------- masks -------

// How much say a layer has over each joint. 0 leaves the joint entirely to
// what is underneath; 1 hands it to the layer outright; in between blends.
// Fractional weights are what keep a waist from shearing — see maskUpperBody.
struct JointMask {
    float weight[kJointCount] = {};
};

JointMask maskNone();
JointMask maskAll();

// One joint, clamped into [0, 1].
void maskSetJoint(JointMask& mask, Joint joint, float weight);

// `root` and every joint descended from it, per the skeleton's own parent
// table. Walking the rig rather than restating it is deliberate: this is the
// seam, and a hard-coded copy of the hierarchy is how two sides drift apart.
void maskSetChain(JointMask& mask, const Skeleton& skeleton, Joint root, float weight);

// The 14.1 split. Spine upward plus both arms — the half an action drives
// while locomotion keeps the hips and legs.
//
// `spineFeather` is the weight given to the Spine joint, and it is the whole
// reason this is not a bitmask. A hard 0-to-1 cut at the waist makes the
// torso shear: the chest snaps to the action's rotation while the hips are
// still walking, and the joint between them absorbs all of it. Blending the
// spine halfway spreads that difference over two joints instead of one.
JointMask maskUpperBody(const Skeleton& skeleton, float spineFeather = 0.5f);

// The complement: hips and both legs. For an overlay that drives the lower
// body instead (a kick, a stumble) while something else holds the torso.
JointMask maskLowerBody(const Skeleton& skeleton);

// ------------------------------------------------------------ blending -----

// Shortest-arc normalized lerp. Cheap, allocation-free, and the standard
// choice for pose blending: the constant-velocity property slerp buys is
// invisible between two poses a frame apart, and this costs no trig.
//
// Kept local to this module on purpose. `Quat` in core/math.h is a shared
// header several areas read; widening it is not this area's call to take.
Quat blendRotation(const Quat& a, const Quat& b, float t);

// Blend `overlay` over `inOut` in place, joint by joint, at
// mask.weight[j] * weight. The primitive underneath LayeredPose, exposed for
// callers that already own their accumulator.
void blendPose(Pose& inOut, const Pose& overlay, const JointMask& mask, float weight);

// ------------------------------------------------------- the composer ------

// How many overlay layers one pose may pay for. Locomotion is the base and
// costs nothing; four layers covers an action, an interruption, an additive
// idle and one spare, which is more than anything in Phase 14 asks for.
inline constexpr size_t kMaxPoseLayers = 4;

// Fixed-capacity layer composition. Scratch, not state: `reset` it, push
// layers, take the result, and let it die on the stack.
class LayeredPose {
public:
    // Start from the base pose — typically LocomotionAnimator::samplePose.
    void reset(const Pose& base);

    // Blend one masked overlay in. Returns false and counts a refusal if the
    // layer cap is already spent; the pose is left exactly as it was. A
    // weight of 0 is applied as a no-op but still consumes a layer, so a
    // caller fading a layer out sees a stable cost rather than a cliff.
    bool addLayer(const Pose& overlay, const JointMask& mask, float weight);

    const Pose& result() const { return pose_; }

    size_t layerCount() const { return count_; }
    size_t refusedCount() const { return refused_; }
    static constexpr size_t capacity() { return kMaxPoseLayers; }

private:
    Pose pose_;
    uint32_t count_ = 0;
    uint32_t refused_ = 0;
};

}  // namespace mge
