#include "mge/character/animation.h"

#include <cmath>

namespace mge {

namespace {

constexpr size_t idx(Joint j) { return static_cast<size_t>(j); }

float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

}  // namespace

// ------------------------------------------------------------- masks -------

JointMask maskNone() { return JointMask{}; }

JointMask maskAll() {
    JointMask mask;
    for (size_t j = 0; j < kJointCount; ++j) mask.weight[j] = 1.0f;
    return mask;
}

void maskSetJoint(JointMask& mask, Joint joint, float weight) {
    mask.weight[idx(joint)] = clamp01(weight);
}

void maskSetChain(JointMask& mask, const Skeleton& skeleton, Joint root, float weight) {
    const float w = clamp01(weight);
    // The Joint enum is parent-before-child (evaluatePose relies on the same
    // property), so descendants resolve in one forward pass: a joint is in
    // the chain if it IS the root, or if its parent already is.
    bool inChain[kJointCount] = {};
    for (size_t j = 0; j < kJointCount; ++j) {
        const int8_t parent = skeleton.parent[j];
        const bool parentInChain = parent >= 0 && inChain[static_cast<size_t>(parent)];
        inChain[j] = (j == idx(root)) || parentInChain;
        if (inChain[j]) mask.weight[j] = w;
    }
}

JointMask maskUpperBody(const Skeleton& skeleton, float spineFeather) {
    // Everything from the chest up, including both arms, is the action's.
    JointMask mask = maskNone();
    maskSetChain(mask, skeleton, Joint::Chest, 1.0f);
    // ...and the spine gets a fraction, so the difference between a walking
    // pelvis and an acting chest is spread over two joints instead of
    // shearing through one. The hips stay locomotion's outright.
    maskSetJoint(mask, Joint::Spine, spineFeather);
    return mask;
}

JointMask maskLowerBody(const Skeleton& skeleton) {
    JointMask mask = maskNone();
    maskSetChain(mask, skeleton, Joint::ThighL, 1.0f);
    maskSetChain(mask, skeleton, Joint::ThighR, 1.0f);
    maskSetJoint(mask, Joint::Hips, 1.0f);
    return mask;
}

// ------------------------------------------------------------ blending -----

Quat blendRotation(const Quat& a, const Quat& b, float t) {
    if (t <= 0.0f) return a;
    if (t >= 1.0f) return b;
    // q and -q are the same rotation; picking the near one keeps the blend on
    // the short arc. Without this an overlay can travel the long way round
    // and the limb visibly swings backwards through the body.
    const float dot = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    const float s = dot < 0.0f ? -1.0f : 1.0f;
    const Quat r{
        a.x + (s * b.x - a.x) * t,
        a.y + (s * b.y - a.y) * t,
        a.z + (s * b.z - a.z) * t,
        a.w + (s * b.w - a.w) * t,
    };
    return r.normalized();
}

void blendPose(Pose& inOut, const Pose& overlay, const JointMask& mask, float weight) {
    const float gain = clamp01(weight);
    if (gain <= 0.0f) return;
    for (size_t j = 0; j < kJointCount; ++j) {
        const float t = mask.weight[j] * gain;
        if (t <= 0.0f) continue;  // the common case for a masked-out half
        inOut.rotation[j] = blendRotation(inOut.rotation[j], overlay.rotation[j], t);
    }
}

// ------------------------------------------------------- the composer ------

void LayeredPose::reset(const Pose& base) {
    pose_ = base;
    count_ = 0;
    refused_ = 0;
}

bool LayeredPose::addLayer(const Pose& overlay, const JointMask& mask, float weight) {
    if (count_ >= kMaxPoseLayers) {
        // Refuse at the cap rather than grow (P1). The pose is left exactly
        // as it was, and the refusal is counted so it is visible rather than
        // silent.
        ++refused_;
        return false;
    }
    ++count_;
    blendPose(pose_, overlay, mask, weight);
    return true;
}

}  // namespace mge
