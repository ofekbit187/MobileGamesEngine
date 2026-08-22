// The device build's per-frame render composition (task 19.7).
//
// This code was inside device_game.cpp until the P1 gate was closed, where no
// host tool compiled it and no test could reach it. These are the invariants
// the device relies on, now that it is engine code and testable.

#include <vector>

#include "mge/framework/character_render.h"
#include "test_framework.h"

using namespace mge;

namespace {

constexpr size_t idx(Joint j) { return static_cast<size_t>(j); }

float angleBetween(const Quat& a, const Quat& b) {
    const float dot = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    const Quat n = dot < 0 ? Quat{-b.x, -b.y, -b.z, -b.w} : b;
    const float dx = a.x - n.x, dy = a.y - n.y, dz = a.z - n.z, dw = a.w - n.w;
    const float sx = a.x + n.x, sy = a.y + n.y, sz = a.z + n.z, sw = a.w + n.w;
    return 4.0f * std::atan2(std::sqrt(dx * dx + dy * dy + dz * dz + dw * dw),
                             std::sqrt(sx * sx + sy * sy + sz * sz + sw * sw));
}

UseMotion swordMotion() {
    ItemUse use;
    use.kind = ItemUseKind::Strike;
    use.archetype = UseArchetype::Swing;
    use.reach = 1.0f;
    use.weight = 1.4f;
    return motionFromItemUse(use, /*leftHanded=*/false);
}

// Stand-ins with the same shape as the renderer's items, for the same reason
// the host runner has them: DrawItem lives behind <vulkan/vulkan.h>.
struct RenderRow {
    const void* mesh = nullptr;
    Mat4 model;
    Aabb worldBounds{};
    Vec3 lodReference{};
    float baseColor[4] = {1, 1, 1, 1};
    uint32_t firstIndex = 0;
    uint32_t indexCount = 0;
};

struct Garment {
    const void* mesh = nullptr;
    float color[4] = {1, 1, 1, 1};
};

struct Held {
    const void* mesh = nullptr;
    AttachPoint anchor = AttachPoint::HandR;
    HeldItemDef def{};
    float color[4] = {1, 1, 1, 1};
};

}  // namespace

MGE_TEST(a_use_motion_layers_over_locomotion_without_stopping_the_legs) {
    // The invariant the whole device swing rests on: pressing the action
    // button must not freeze the walk. A one-handed grip takes the lead arm
    // and torso and leaves everything else to locomotion.
    const HumanoidVariant variant;
    const Skeleton skeleton = buildSkeleton(variant);
    const UseMotion motion = swordMotion();
    const JointMask mask = useArchetypeMask(skeleton, motion);

    TransformComponent t;
    LocomotionAnimator anim;
    anim.update(0.25f, 1.6f);  // mid-stride

    Mat4 palette[kJointCount];

    // Same animator state, composed with and without a motion playing.
    LocomotionAnimator walkOnly = anim;
    UsePlayer idle;
    CharacterFrame plain;
    composeCharacterFrame(t, 0.5f, variant, walkOnly, idle, motion, mask, false, palette, plain);

    LocomotionAnimator walkAndSwing = anim;
    UsePlayer swinging;
    swinging.start(motion);
    swinging.update(motion.reach > 0 ? usePhases(motion).duration * 0.5f : 0.2f);
    CharacterFrame swung;
    composeCharacterFrame(t, 0.5f, variant, walkAndSwing, swinging, motion, mask, false, palette,
                          swung);

    // The legs are locomotion's, untouched by the overlay.
    for (Joint leg : {Joint::ThighL, Joint::ShinL, Joint::FootL, Joint::ThighR, Joint::ShinR,
                      Joint::FootR}) {
        MGE_CHECK(mask.weight[idx(leg)] == 0.0f);
        MGE_CHECK(angleBetween(plain.pose.rotation[idx(leg)],
                                     swung.pose.rotation[idx(leg)]) < 1e-4f);
    }
    // The sword arm is not.
    MGE_CHECK(angleBetween(plain.pose.rotation[idx(Joint::UpperArmR)],
                                 swung.pose.rotation[idx(Joint::UpperArmR)]) > 0.05f);
}

MGE_TEST(joint_transforms_are_evaluated_only_when_something_is_held) {
    // 17 joint matrices per character per frame is cheap, not free, and most
    // characters carry nothing.
    const HumanoidVariant variant;
    TransformComponent t;
    LocomotionAnimator anim;
    UsePlayer idle;
    UseMotion motion = swordMotion();
    JointMask mask = useArchetypeMask(buildSkeleton(variant), motion);
    Mat4 palette[kJointCount];

    CharacterFrame empty;
    composeCharacterFrame(t, 0.0f, variant, anim, idle, motion, mask, false, palette, empty);
    MGE_CHECK(!empty.jointWorldValid);

    CharacterFrame armed;
    composeCharacterFrame(t, 0.0f, variant, anim, idle, motion, mask, true, palette, armed);
    MGE_CHECK(armed.jointWorldValid);
}

MGE_TEST(a_covered_body_region_is_simply_not_submitted) {
    // Masking is a draw range, not a second mesh: a covered region drops out
    // of the list entirely.
    std::vector<MeshPart> parts;
    for (size_t r = 0; r < kBodyRegionCount; ++r) {
        MeshPart p;
        p.region = static_cast<BodyRegion>(r);
        p.firstIndex = static_cast<uint32_t>(r * 10);
        p.indexCount = 10;
        parts.push_back(p);
    }
    RenderRow proto;
    std::vector<RenderRow> out;

    emitBodyParts(proto, parts.data(), parts.size(), kAllRegions, out);
    MGE_CHECK(out.size() == kBodyRegionCount);

    out.clear();
    const uint32_t clothed = kAllRegions & ~regionBit(BodyRegion::Torso);
    emitBodyParts(proto, parts.data(), parts.size(), clothed, out);
    MGE_CHECK(out.size() == kBodyRegionCount - 1);
    for (const RenderRow& item : out) {
        MGE_CHECK(item.firstIndex !=
                        static_cast<uint32_t>(static_cast<size_t>(BodyRegion::Torso) * 10));
    }
}

MGE_TEST(garments_ride_the_bodys_palette_and_transform) {
    // What makes a garment deform with the skin for free: it is the same
    // draw, with a different mesh and colour.
    Mat4 palette[kJointCount];
    RenderRow proto;
    proto.model = Mat4::translation({1, 2, 3});
    proto.mesh = palette;  // any non-null stand-in

    const int garmentMesh = 0;
    Garment worn[2];
    worn[0].mesh = &garmentMesh;
    worn[0].color[0] = 0.5f;
    worn[1].mesh = &garmentMesh;

    std::vector<RenderRow> out;
    emitGarments(proto, worn, 2, out);
    MGE_CHECK(out.size() == static_cast<size_t>(2));
    for (const RenderRow& item : out) {
        MGE_CHECK(item.mesh == &garmentMesh);
        MGE_CHECK(item.indexCount == 0u);  // the whole garment
        MGE_CHECK(item.model.m[12] == proto.model.m[12]);
    }
    MGE_CHECK(out[0].baseColor[0] == 0.5f);
}

MGE_TEST(a_held_item_needs_joint_transforms_and_refuses_without_them) {
    // Rather than placing the sword at the origin, which is what reading
    // uninitialised joint matrices would do.
    const HumanoidVariant variant;
    const int swordMesh = 0;
    Held sword;
    sword.mesh = &swordMesh;

    CharacterFrame frame;
    frame.jointWorldValid = false;
    std::vector<RenderRow> out;
    emitHeldItems<RenderRow>(frame, variant, &sword, 1, out);
    MGE_CHECK(out.empty());

    TransformComponent t;
    LocomotionAnimator anim;
    UsePlayer idle;
    UseMotion motion = swordMotion();
    JointMask mask = useArchetypeMask(buildSkeleton(variant), motion);
    Mat4 palette[kJointCount];
    composeCharacterFrame(t, 0.0f, variant, anim, idle, motion, mask, true, palette, frame);

    emitHeldItems<RenderRow>(frame, variant, &sword, 1, out);
    MGE_CHECK(out.size() == static_cast<size_t>(1));
    // The grip sits where the hand is, not at the character's origin.
    const float gripHeight = out[0].model.m[13];
    MGE_CHECK(gripHeight > 0.4f);
}
