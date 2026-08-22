#include "mge/framework/character_render.h"

namespace mge {

void composeCharacterFrame(const TransformComponent& transform, float alpha,
                           const HumanoidVariant& variant, LocomotionAnimator& anim,
                           const UsePlayer& use, const UseMotion& motion,
                           const JointMask& useMask, bool needJointWorld,
                           Mat4 outPalette[kJointCount], CharacterFrame& out) {
    const Vec3 pos = lerp(transform.prevPosition, transform.position, alpha);
    const float yaw = transform.prevYaw + (transform.yaw - transform.prevYaw) * alpha;

    anim.samplePose(out.pose);
    if (use.active()) {
        // The overlay rides on top rather than replacing: the archetype's own
        // mask decides which joints it takes, which is why the walk survives
        // the swing instead of being interrupted by it.
        Pose overlay;
        sampleUseArchetype(motion, use.normalizedTime(), overlay);
        LayeredPose layered;
        layered.reset(out.pose);
        layered.addLayer(overlay, useMask, use.weight());
        out.pose = layered.result();
    }

    if (outPalette != nullptr) buildSkinPalette(variant, out.pose, outPalette);

    out.model = Mat4::translation(pos) * Mat4::rotation(Quat::fromAxisAngle({0, 1, 0}, -yaw));
    out.bounds = Aabb::fromCenterExtents(pos + Vec3{0, 1.0f, 0}, {1.4f, 1.4f, 1.4f});
    out.lodReference = pos;

    // Only when something is actually held: 17 joint matrices is cheap, but
    // it is not free, and most characters carry nothing.
    out.jointWorldValid = needJointWorld;
    if (needJointWorld) evaluatePose(buildSkeleton(variant), out.pose, out.jointWorld);
}

}  // namespace mge
