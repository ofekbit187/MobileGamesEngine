// Phase 8 (humanoid side): rig from variant data, pose evaluation, the
// default locomotion set, and parametric wearable fitting/masking — all
// deterministic, headless.

#include <cmath>

#include "mge/character/humanoid.h"
#include "test_framework.h"

using namespace mge;

namespace {

constexpr size_t J(Joint j) { return static_cast<size_t>(j); }

Vec3 jointPosition(const Skeleton& skeleton, const Pose& pose, Joint joint) {
    Mat4 world[kJointCount];
    evaluatePose(skeleton, pose, world);
    return world[J(joint)].transformPoint({0, 0, 0});
}

// Furthest x-extent of any part attached to `joint` (bind-space, part-local).
float maxRadiusAt(const std::vector<RigPart>& parts, Joint joint) {
    float best = 0;
    for (const RigPart& part : parts) {
        if (part.joint != joint) continue;
        for (const Vertex& v : part.mesh.vertices) {
            best = std::fmax(best, std::fabs(v.position.x));
        }
    }
    return best;
}

bool hasPartAt(const std::vector<RigPart>& parts, Joint joint, const float color[4]) {
    for (const RigPart& part : parts) {
        if (part.joint == joint && std::fabs(part.color[0] - color[0]) < 1e-4f &&
            std::fabs(part.color[1] - color[1]) < 1e-4f) {
            return true;
        }
    }
    return false;
}

}  // namespace

MGE_TEST(skeleton_matches_variant_proportions) {
    // Data drives the rig (8.13): height lands at the crown, widths at the
    // shoulder/hip joints — for ANY variant.
    HumanoidVariant avg;  // defaults: 1.75 m
    const Skeleton s = buildSkeleton(avg);
    Pose bind;  // identity
    const Vec3 head = jointPosition(s, bind, Joint::Head);
    MGE_CHECK_NEAR(head.y + s.headSize, avg.height, 0.06f);
    const Vec3 footL = jointPosition(s, bind, Joint::FootL);
    // The ankle, where the template body's ankle actually is: ~6.6% of height
    // above the sole. (The rig is fitted to the mesh, not the mesh to the rig.)
    MGE_CHECK(footL.y > 0.04f * avg.height && footL.y < 0.10f * avg.height);
    MGE_CHECK_NEAR(jointPosition(s, bind, Joint::UpperArmL).x, avg.shoulderWidth * 0.5f, 1e-4f);
    MGE_CHECK_NEAR(jointPosition(s, bind, Joint::ThighR).x, -avg.hipWidth * 0.5f, 1e-4f);

    // A giant variant scales the whole rig from the same data file fields —
    // up to the edge of the scope, which is where a variant file's ambition
    // stops (ADR 0009).
    HumanoidVariant giant;
    giant.height = kHeightRange.max;
    giant.shoulderWidth = kShoulderWidthRange.max;
    const Skeleton g = buildSkeleton(giant);
    const Vec3 giantHead = jointPosition(g, bind, Joint::Head);
    MGE_CHECK_NEAR(giantHead.y + g.headSize, giant.height, 0.08f);
    MGE_CHECK(jointPosition(g, bind, Joint::UpperArmL).x > jointPosition(s, bind, Joint::UpperArmL).x);
}

MGE_TEST(variants_are_clamped_into_the_scope) {
    // Variant files are CONTENT. Content must never be able to ask for a body
    // the animation or the wearables cannot cope with, so the scope clamps
    // rather than rejects — a silly number produces the nearest sane body.
    HumanoidVariant absurd;
    absurd.height = 40.0f;
    absurd.shoulderWidth = -3.0f;
    absurd.bulk = 100.0f;
    absurd.legRatio = 0.99f;
    absurd.belly = 7.5f;
    absurd.face.jawWidth = -9.0f;
    const HumanoidVariant v = clampToScope(absurd);
    MGE_CHECK_NEAR(v.height, kHeightRange.max, 1e-5f);
    MGE_CHECK_NEAR(v.shoulderWidth, kShoulderWidthRange.min, 1e-5f);
    MGE_CHECK_NEAR(v.bulk, kBulkRange.max, 1e-5f);
    MGE_CHECK_NEAR(v.legRatio, kLegRatioRange.max, 1e-5f);
    MGE_CHECK_NEAR(v.belly, 1.0f, 1e-5f);
    MGE_CHECK_NEAR(v.face.jawWidth, -1.0f, 1e-5f);

    // And the clamp is on the path, not just available to call: an absurd
    // variant still produces a skeleton inside the scope.
    const Skeleton clamped = buildSkeleton(absurd);
    Pose bind;
    MGE_CHECK(jointPosition(clamped, bind, Joint::Head).y + clamped.headSize <=
              kHeightRange.max + 0.01f);

    // Every default is the standard body, and every standard is inside its
    // own range — a scope whose middle is outside itself is a broken scope.
    const HumanoidVariant standard;
    MGE_CHECK_NEAR(standard.height, kHeightRange.standard, 1e-5f);
    MGE_CHECK_NEAR(standard.shoulderWidth, kShoulderWidthRange.standard, 1e-5f);
    MGE_CHECK_NEAR(standard.hipWidth, kHipWidthRange.standard, 1e-5f);
    const VariantRange* ranges[] = {&kHeightRange,   &kShoulderWidthRange, &kHipWidthRange,
                                    &kLegRatioRange, &kArmRatioRange,      &kBulkRange,
                                    &kHeadScaleRange, &kFootScaleRange};
    for (const VariantRange* r : ranges) {
        MGE_CHECK(r->min < r->standard && r->standard < r->max);
    }
}

MGE_TEST(pose_evaluation_moves_the_chain) {
    const Skeleton s = buildSkeleton(HumanoidVariant{});
    Pose pose;
    const Vec3 footRest = jointPosition(s, pose, Joint::FootL);
    // Swing the left thigh 90 degrees forward: the whole chain follows —
    // the foot rises to about hip height and moves toward -Z (forward).
    pose.rotation[J(Joint::ThighL)] = Quat::fromAxisAngle({1, 0, 0}, kPi * 0.5f);
    const Vec3 footUp = jointPosition(s, pose, Joint::FootL);
    MGE_CHECK(footUp.y > footRest.y + 0.5f);
    MGE_CHECK(footUp.z < footRest.z - 0.5f);
    // The other leg is untouched.
    const Vec3 footR = jointPosition(s, pose, Joint::FootR);
    MGE_CHECK_NEAR(footR.y, footRest.y, 1e-4f);
}

MGE_TEST(locomotion_walk_cycle_strides) {
    const Skeleton s = buildSkeleton(HumanoidVariant{});
    LocomotionAnimator anim;

    // Standing: phase frozen (no foot slide), pose ~neutral.
    for (int i = 0; i < 60; ++i) anim.update(1.0f / 60.0f, 0.0f);
    MGE_CHECK_NEAR(anim.phase(), 0.0f, 1e-4f);
    Pose idle;
    anim.samplePose(idle);
    const Vec3 idleFootL = jointPosition(s, idle, Joint::FootL);
    const Vec3 idleFootR = jointPosition(s, idle, Joint::FootR);
    MGE_CHECK_NEAR(idleFootL.z, idleFootR.z, 0.02f);

    // Walking: legs stride in counter-phase; over a cycle each foot leads
    // at some point.
    bool leftLeads = false, rightLeads = false;
    for (int i = 0; i < 60 * 3; ++i) {
        anim.update(1.0f / 60.0f, 1.4f);
        Pose pose;
        anim.samplePose(pose);
        const float zl = jointPosition(s, pose, Joint::FootL).z;
        const float zr = jointPosition(s, pose, Joint::FootR).z;
        if (zl < zr - 0.05f) leftLeads = true;   // forward is -Z
        if (zr < zl - 0.05f) rightLeads = true;
    }
    MGE_CHECK(leftLeads);
    MGE_CHECK(rightLeads);

    // Running swings wider than walking.
    LocomotionAnimator walk, run;
    float walkSpread = 0, runSpread = 0;
    for (int i = 0; i < 60 * 3; ++i) {
        walk.update(1.0f / 60.0f, 1.4f);
        run.update(1.0f / 60.0f, 4.5f);
        Pose pw, pr;
        walk.samplePose(pw);
        run.samplePose(pr);
        walkSpread = std::fmax(walkSpread,
                               std::fabs(jointPosition(s, pw, Joint::FootL).z -
                                         jointPosition(s, pw, Joint::FootR).z));
        runSpread = std::fmax(runSpread, std::fabs(jointPosition(s, pr, Joint::FootL).z -
                                                   jointPosition(s, pr, Joint::FootR).z));
    }
    MGE_CHECK(runSpread > walkSpread * 1.3f);
}

MGE_TEST(wearables_mask_covered_body_parts) {
    HumanoidVariant variant;
    std::vector<RigPart> bare;
    buildHumanoidVisual(variant, nullptr, 0, bare);
    // Bare body has skin at the thighs and feet.
    MGE_CHECK(hasPartAt(bare, Joint::ThighL, variant.skin));
    MGE_CHECK(hasPartAt(bare, Joint::FootL, variant.skin));

    WearableInstance outfit[2];
    outfit[0].kind = WearableKind::Pants;
    outfit[0].color[0] = 0.25f; outfit[0].color[1] = 0.20f; outfit[0].color[2] = 0.15f;
    outfit[1].kind = WearableKind::Boots;
    outfit[1].color[0] = 0.30f; outfit[1].color[1] = 0.22f; outfit[1].color[2] = 0.12f;
    std::vector<RigPart> dressed;
    buildHumanoidVisual(variant, outfit, 2, dressed);
    // Covered skin is NOT emitted (masking — clipping impossible), garment is.
    MGE_CHECK(!hasPartAt(dressed, Joint::ThighL, variant.skin));
    MGE_CHECK(!hasPartAt(dressed, Joint::FootL, variant.skin));
    MGE_CHECK(hasPartAt(dressed, Joint::ThighL, outfit[0].color));
    MGE_CHECK(hasPartAt(dressed, Joint::FootL, outfit[1].color));
    // Head/hands stay skin.
    MGE_CHECK(hasPartAt(dressed, Joint::Head, variant.skin));
    MGE_CHECK(hasPartAt(dressed, Joint::HandL, variant.skin));
}

MGE_TEST(wearables_fit_every_variant_and_layer) {
    // The SAME wearable declaration on two very different bodies: the garment
    // is generated per-body, so it always encloses the limb it covers.
    WearableInstance pants[1];
    pants[0].kind = WearableKind::Pants;

    for (float bulk : {0.8f, 1.6f}) {
        HumanoidVariant variant;
        variant.bulk = bulk;
        std::vector<RigPart> bare, dressed;
        buildHumanoidVisual(variant, nullptr, 0, bare);
        buildHumanoidVisual(variant, pants, 1, dressed);
        const float bareThigh = maxRadiusAt(bare, Joint::ThighL);
        const float pantsThigh = maxRadiusAt(dressed, Joint::ThighL);
        MGE_CHECK(pantsThigh > bareThigh);  // garment outside the skin
    }

    // Layering (8.16): an outer layer is generated thicker than a mid layer,
    // so armor over tunic never intersects the tunic.
    HumanoidVariant variant;
    WearableInstance layered[2];
    layered[0].kind = WearableKind::Tunic;
    layered[0].layer = 1;
    layered[0].color[0] = 0.1f;
    layered[1].kind = WearableKind::Armor;
    layered[1].layer = 2;
    layered[1].color[0] = 0.6f;
    std::vector<RigPart> dressed;
    buildHumanoidVisual(variant, layered, 2, dressed);
    float tunicWidth = 0, armorWidth = 0;
    for (const RigPart& part : dressed) {
        if (part.joint != Joint::Chest) continue;
        for (const Vertex& v : part.mesh.vertices) {
            if (std::fabs(part.color[0] - 0.1f) < 1e-4f) {
                tunicWidth = std::fmax(tunicWidth, std::fabs(v.position.x));
            }
            if (std::fabs(part.color[0] - 0.6f) < 1e-4f) {
                armorWidth = std::fmax(armorWidth, std::fabs(v.position.x));
            }
        }
    }
    MGE_CHECK(tunicWidth > 0 && armorWidth > tunicWidth);
}

MGE_TEST(held_sword_sheathes_to_the_back) {
    HumanoidVariant variant;
    WearableInstance sword[1];
    sword[0].kind = WearableKind::Sword;
    // Steel — distinct from skin, identifies the sword part below.
    sword[0].color[0] = 0.71f; sword[0].color[1] = 0.74f; sword[0].color[2] = 0.78f;

    sword[0].sheathed = false;
    std::vector<RigPart> drawn;
    buildHumanoidVisual(variant, sword, 1, drawn);
    MGE_CHECK(hasPartAt(drawn, Joint::HandR, sword[0].color));

    sword[0].sheathed = true;
    std::vector<RigPart> sheathed;
    buildHumanoidVisual(variant, sword, 1, sheathed);
    MGE_CHECK(!hasPartAt(sheathed, Joint::HandR, sword[0].color));
    bool onBack = false;
    for (const RigPart& part : sheathed) {
        if (part.joint != Joint::Chest ||
            std::fabs(part.color[0] - sword[0].color[0]) > 1e-4f) {
            continue;
        }
        // Behind the torso: +Z in character space.
        for (const Vertex& v : part.mesh.vertices) onBack |= v.position.z > 0.05f;
    }
    MGE_CHECK(onBack);
}
