// Layered poses with masks (task 14.1).
//
// The behaviour under test is one sentence: locomotion drives the lower body
// while an action drives the upper body, and the result is still ONE pose.

#include "mge/character/animation.h"
#include "mge/character/humanoid.h"
#include "test_framework.h"

using namespace mge;

namespace {

constexpr size_t idx(Joint j) { return static_cast<size_t>(j); }

Skeleton rig() { return buildSkeleton(HumanoidVariant{}); }

bool quatNear(const Quat& a, const Quat& b, float eps = 1e-5f) {
    return std::fabs(a.x - b.x) <= eps && std::fabs(a.y - b.y) <= eps &&
           std::fabs(a.z - b.z) <= eps && std::fabs(a.w - b.w) <= eps;
}

bool isIdentity(const Quat& q) { return quatNear(q, Quat{}); }

// A stand-in for an upper-body action: right arm raised and forward, torso
// turned into it. This is NOT the archetype library (task 14.2) — it exists
// only to give the mask something recognisable to carry.
Pose testActionPose() {
    Pose pose;
    pose.rotation[idx(Joint::UpperArmR)] = Quat::fromAxisAngle({1, 0, 0}, -1.4f);
    pose.rotation[idx(Joint::ForearmR)] = Quat::fromAxisAngle({1, 0, 0}, 0.9f);
    pose.rotation[idx(Joint::Chest)] = Quat::fromAxisAngle({0, 1, 0}, 0.35f);
    pose.rotation[idx(Joint::Spine)] = Quat::fromAxisAngle({0, 1, 0}, 0.20f);
    // Deliberately also poses a LEG, so a mask that leaks is caught.
    pose.rotation[idx(Joint::ThighR)] = Quat::fromAxisAngle({1, 0, 0}, 1.2f);
    return pose;
}

Pose walkPose(float seconds, float speed = 1.6f) {
    LocomotionAnimator anim;
    for (int f = 0; f < static_cast<int>(seconds * 60.0f); ++f) anim.update(1.0f / 60.0f, speed);
    Pose pose;
    anim.samplePose(pose);
    return pose;
}

}  // namespace

MGE_TEST(mask_chain_follows_the_rig_hierarchy) {
    const Skeleton skeleton = rig();
    JointMask mask = maskNone();
    maskSetChain(mask, skeleton, Joint::Chest, 1.0f);

    // Chest and everything descended from it.
    MGE_CHECK_NEAR(mask.weight[idx(Joint::Chest)], 1.0f, 1e-6);
    MGE_CHECK_NEAR(mask.weight[idx(Joint::Neck)], 1.0f, 1e-6);
    MGE_CHECK_NEAR(mask.weight[idx(Joint::Head)], 1.0f, 1e-6);
    MGE_CHECK_NEAR(mask.weight[idx(Joint::UpperArmL)], 1.0f, 1e-6);
    MGE_CHECK_NEAR(mask.weight[idx(Joint::HandL)], 1.0f, 1e-6);
    MGE_CHECK_NEAR(mask.weight[idx(Joint::HandR)], 1.0f, 1e-6);
    // Nothing above it, and nothing on the other branch.
    MGE_CHECK_NEAR(mask.weight[idx(Joint::Hips)], 0.0f, 1e-6);
    MGE_CHECK_NEAR(mask.weight[idx(Joint::Spine)], 0.0f, 1e-6);
    MGE_CHECK_NEAR(mask.weight[idx(Joint::ThighL)], 0.0f, 1e-6);
    MGE_CHECK_NEAR(mask.weight[idx(Joint::FootR)], 0.0f, 1e-6);
}

MGE_TEST(upper_and_lower_masks_partition_the_skeleton) {
    const Skeleton skeleton = rig();
    const JointMask upper = maskUpperBody(skeleton, 0.5f);
    const JointMask lower = maskLowerBody(skeleton);

    // The hips belong to locomotion outright; the spine is shared by design.
    MGE_CHECK_NEAR(upper.weight[idx(Joint::Hips)], 0.0f, 1e-6);
    MGE_CHECK_NEAR(upper.weight[idx(Joint::Spine)], 0.5f, 1e-6);
    MGE_CHECK_NEAR(upper.weight[idx(Joint::Chest)], 1.0f, 1e-6);
    MGE_CHECK_NEAR(upper.weight[idx(Joint::HandR)], 1.0f, 1e-6);
    MGE_CHECK_NEAR(upper.weight[idx(Joint::ThighL)], 0.0f, 1e-6);
    MGE_CHECK_NEAR(upper.weight[idx(Joint::FootL)], 0.0f, 1e-6);

    MGE_CHECK_NEAR(lower.weight[idx(Joint::Hips)], 1.0f, 1e-6);
    MGE_CHECK_NEAR(lower.weight[idx(Joint::FootR)], 1.0f, 1e-6);
    MGE_CHECK_NEAR(lower.weight[idx(Joint::Chest)], 0.0f, 1e-6);

    // Every joint is claimed by exactly one of them, except the feathered
    // spine — which is the one joint that is deliberately shared.
    for (size_t j = 0; j < kJointCount; ++j) {
        if (j == idx(Joint::Spine)) continue;
        const float sum = upper.weight[j] + lower.weight[j];
        MGE_CHECK_NEAR(sum, 1.0f, 1e-6);
    }
}

MGE_TEST(blend_rotation_takes_the_short_arc) {
    const Quat a = Quat::fromAxisAngle({0, 1, 0}, 0.0f);
    const Quat b = Quat::fromAxisAngle({0, 1, 0}, 1.0f);
    // The same rotation as b, negated — must blend identically, not the long
    // way round.
    const Quat bNeg{-b.x, -b.y, -b.z, -b.w};

    const Quat viaB = blendRotation(a, b, 0.5f);
    const Quat viaNeg = blendRotation(a, bNeg, 0.5f);
    MGE_CHECK(quatNear(viaB, viaNeg, 1e-5f) ||
              quatNear(viaB, Quat{-viaNeg.x, -viaNeg.y, -viaNeg.z, -viaNeg.w}, 1e-5f));

    // Endpoints are exact, so masked-in/out joints are bit-stable.
    MGE_CHECK(quatNear(blendRotation(a, b, 0.0f), a, 0.0f));
    MGE_CHECK(quatNear(blendRotation(a, b, 1.0f), b, 0.0f));
    // And the blend stays a unit quaternion.
    const Quat mid = blendRotation(a, b, 0.5f);
    MGE_CHECK_NEAR(std::sqrt(mid.x * mid.x + mid.y * mid.y + mid.z * mid.z + mid.w * mid.w),
                   1.0f, 1e-5);
}

// The headline of 14.1: a character walks and acts at the same time.
MGE_TEST(walking_while_acting_keeps_both_halves) {
    const Skeleton skeleton = rig();
    const Pose walk = walkPose(3.0f);
    const Pose action = testActionPose();

    // Precondition: the legs must actually be MOVING, or "locomotion still
    // drives the legs" would pass on a frozen character.
    MGE_CHECK(!isIdentity(walk.rotation[idx(Joint::ThighL)]));
    MGE_CHECK(!isIdentity(walk.rotation[idx(Joint::ThighR)]));

    LayeredPose layered;
    layered.reset(walk);
    MGE_CHECK(layered.addLayer(action, maskUpperBody(skeleton, 0.5f), 1.0f));
    const Pose& out = layered.result();

    // Lower body: untouched by the action, bit-identical to locomotion —
    // including the leg the action pose deliberately rotated.
    MGE_CHECK(quatNear(out.rotation[idx(Joint::Hips)], walk.rotation[idx(Joint::Hips)], 0.0f));
    MGE_CHECK(quatNear(out.rotation[idx(Joint::ThighL)], walk.rotation[idx(Joint::ThighL)], 0.0f));
    MGE_CHECK(quatNear(out.rotation[idx(Joint::ThighR)], walk.rotation[idx(Joint::ThighR)], 0.0f));
    MGE_CHECK(quatNear(out.rotation[idx(Joint::ShinL)], walk.rotation[idx(Joint::ShinL)], 0.0f));
    MGE_CHECK(quatNear(out.rotation[idx(Joint::FootR)], walk.rotation[idx(Joint::FootR)], 0.0f));

    // Upper body: the action's, outright.
    MGE_CHECK(quatNear(out.rotation[idx(Joint::UpperArmR)],
                       action.rotation[idx(Joint::UpperArmR)], 0.0f));
    MGE_CHECK(quatNear(out.rotation[idx(Joint::ForearmR)],
                       action.rotation[idx(Joint::ForearmR)], 0.0f));
    MGE_CHECK(quatNear(out.rotation[idx(Joint::Chest)], action.rotation[idx(Joint::Chest)], 0.0f));

    // The spine is neither one nor the other — that is the feather doing its
    // job, and it is what stops the waist shearing.
    MGE_CHECK(!quatNear(out.rotation[idx(Joint::Spine)], walk.rotation[idx(Joint::Spine)], 1e-4f));
    MGE_CHECK(!quatNear(out.rotation[idx(Joint::Spine)], action.rotation[idx(Joint::Spine)],
                        1e-4f));

    // And the whole thing is still one ordinary Pose: it evaluates through
    // the unchanged rig path, one palette, no special case.
    Mat4 world[kJointCount];
    evaluatePose(skeleton, out, world);
    Mat4 walkWorld[kJointCount];
    evaluatePose(skeleton, walk, walkWorld);
    // The acting hand has moved; the planted foot has not.
    const Vec3 hand = world[idx(Joint::HandR)].transformPoint({0, 0, 0});
    const Vec3 walkHand = walkWorld[idx(Joint::HandR)].transformPoint({0, 0, 0});
    MGE_CHECK((hand - walkHand).length() > 0.10f);
    const Vec3 foot = world[idx(Joint::FootL)].transformPoint({0, 0, 0});
    const Vec3 walkFoot = walkWorld[idx(Joint::FootL)].transformPoint({0, 0, 0});
    MGE_CHECK_NEAR((foot - walkFoot).length(), 0.0, 1e-6);
}

MGE_TEST(layer_weight_fades_an_action_in_and_out) {
    const Skeleton skeleton = rig();
    const Pose walk = walkPose(3.0f);
    const Pose action = testActionPose();
    const JointMask upper = maskUpperBody(skeleton);

    // Weight 0: the action is present but says nothing (this is how 14.4's
    // interruption will blend out rather than snap).
    LayeredPose off;
    off.reset(walk);
    MGE_CHECK(off.addLayer(action, upper, 0.0f));
    MGE_CHECK(quatNear(off.result().rotation[idx(Joint::UpperArmR)],
                       walk.rotation[idx(Joint::UpperArmR)], 0.0f));

    // Weight 1: fully the action's.
    LayeredPose on;
    on.reset(walk);
    MGE_CHECK(on.addLayer(action, upper, 1.0f));
    MGE_CHECK(quatNear(on.result().rotation[idx(Joint::UpperArmR)],
                       action.rotation[idx(Joint::UpperArmR)], 0.0f));

    // Halfway: strictly between the two, and monotone across the ramp.
    float previous = 0.0f;
    for (int step = 0; step <= 10; ++step) {
        const float w = static_cast<float>(step) / 10.0f;
        LayeredPose mid;
        mid.reset(walk);
        mid.addLayer(action, upper, w);
        const Quat& q = mid.result().rotation[idx(Joint::UpperArmR)];
        const float distance = std::fabs(q.x - walk.rotation[idx(Joint::UpperArmR)].x);
        MGE_CHECK(distance >= previous - 1e-6f);  // never travels backwards
        previous = distance;
    }
}

MGE_TEST(layer_cap_refuses_rather_than_grows) {
    const Skeleton skeleton = rig();
    const Pose walk = walkPose(3.0f);
    const Pose action = testActionPose();
    const JointMask upper = maskUpperBody(skeleton);

    LayeredPose layered;
    layered.reset(walk);
    for (size_t i = 0; i < LayeredPose::capacity(); ++i) {
        MGE_CHECK(layered.addLayer(action, upper, 1.0f));
    }
    MGE_CHECK(layered.layerCount() == LayeredPose::capacity());
    MGE_CHECK(layered.refusedCount() == 0);

    // Past the cap: refused, counted, and the pose is left exactly as it was.
    const Pose atCap = layered.result();
    MGE_CHECK(!layered.addLayer(action, upper, 1.0f));
    MGE_CHECK(!layered.addLayer(action, upper, 1.0f));
    MGE_CHECK(layered.refusedCount() == 2);
    MGE_CHECK(layered.layerCount() == LayeredPose::capacity());
    for (size_t j = 0; j < kJointCount; ++j) {
        MGE_CHECK(quatNear(layered.result().rotation[j], atCap.rotation[j], 0.0f));
    }

    // reset clears both counters, so a reused composer starts clean.
    layered.reset(walk);
    MGE_CHECK(layered.layerCount() == 0);
    MGE_CHECK(layered.refusedCount() == 0);
}

MGE_TEST(layers_compose_in_order) {
    const Skeleton skeleton = rig();
    const Pose base = walkPose(3.0f);

    Pose armOnly;
    armOnly.rotation[idx(Joint::UpperArmR)] = Quat::fromAxisAngle({1, 0, 0}, -1.0f);
    Pose headOnly;
    headOnly.rotation[idx(Joint::Head)] = Quat::fromAxisAngle({0, 1, 0}, 0.7f);

    JointMask armMask = maskNone();
    maskSetChain(armMask, skeleton, Joint::UpperArmR, 1.0f);
    JointMask headMask = maskNone();
    maskSetJoint(headMask, Joint::Head, 1.0f);

    // Two disjoint layers: each keeps its own joints, neither disturbs the
    // other, and the legs are still locomotion's.
    LayeredPose layered;
    layered.reset(base);
    MGE_CHECK(layered.addLayer(armOnly, armMask, 1.0f));
    MGE_CHECK(layered.addLayer(headOnly, headMask, 1.0f));
    const Pose& out = layered.result();
    MGE_CHECK(quatNear(out.rotation[idx(Joint::UpperArmR)],
                       armOnly.rotation[idx(Joint::UpperArmR)], 0.0f));
    MGE_CHECK(quatNear(out.rotation[idx(Joint::Head)], headOnly.rotation[idx(Joint::Head)], 0.0f));
    MGE_CHECK(quatNear(out.rotation[idx(Joint::ThighL)], base.rotation[idx(Joint::ThighL)], 0.0f));

    // Overlapping layers: the later one wins where both claim a joint, which
    // is what lets an interruption override an action already playing.
    LayeredPose overlap;
    overlap.reset(base);
    overlap.addLayer(armOnly, maskAll(), 1.0f);
    overlap.addLayer(headOnly, maskAll(), 1.0f);
    MGE_CHECK(quatNear(overlap.result().rotation[idx(Joint::Head)],
                       headOnly.rotation[idx(Joint::Head)], 0.0f));
}

MGE_TEST(layering_works_on_every_body_variant) {
    // The rig is shared, so a mask built for one variant must hold for all of
    // them (CHARACTERS.md §4.2 — retargeting inside the family is free).
    HumanoidVariant small;
    small.height = 1.40f;
    small.armRatio = 0.39f;
    HumanoidVariant large;
    large.height = 2.10f;
    large.bulk = 1.5f;

    const Pose walk = walkPose(3.0f);
    const Pose action = testActionPose();
    for (const HumanoidVariant& variant : {small, large}) {
        const Skeleton skeleton = buildSkeleton(variant);
        LayeredPose layered;
        layered.reset(walk);
        MGE_CHECK(layered.addLayer(action, maskUpperBody(skeleton), 1.0f));
        MGE_CHECK(quatNear(layered.result().rotation[idx(Joint::UpperArmR)],
                           action.rotation[idx(Joint::UpperArmR)], 0.0f));
        MGE_CHECK(quatNear(layered.result().rotation[idx(Joint::ThighL)],
                           walk.rotation[idx(Joint::ThighL)], 0.0f));
    }
}

// ---------------------------------------------------------------------------
// Does layering shear the SKIN? (task 16.2)
//
// This is the test the first version of this area could not have written,
// because it previewed on the deprecated v1 box rig. Separated boxes have no
// surface between them, so a joint mask that snaps 0 -> 1 across a joint looks
// perfect on boxes and can tear on continuous skinned geometry, where the skin
// weights blend across that same joint.
//
// So: measure it on the real imported body, on the CPU, with no picture
// involved. The metric is per-edge strain — how much each mesh edge's length
// changes from bind — because that is what tearing and pinching ARE.
//
// The question is not "is the layered pose distorted" (every pose distorts a
// skin) but "does LAYERING add distortion that neither source pose had". So
// the number under test is
//
//     excess = strain(layered) - max(strain(locomotion), strain(action))
//
// and the bar it must clear is locomotion's own worst distortion: if blending
// two poses adds less than the walk cycle already does by itself, layering is
// not what will break the picture.
// ---------------------------------------------------------------------------

#include "mge/character/body_mesh.h"
#include "mge/character/use_archetypes.h"

#include <algorithm>
#include <set>
#include <vector>

namespace {

struct SkinProbe {
    const SkinnedMeshData* mesh = nullptr;
    std::vector<std::pair<uint32_t, uint32_t>> edges;
    std::vector<float> bindLength;

    void build() {
        mesh = &sharedTemplateLods()[0];
        std::set<std::pair<uint32_t, uint32_t>> unique;
        const auto add = [&](uint32_t a, uint32_t b) {
            unique.insert({std::min(a, b), std::max(a, b)});
        };
        for (size_t t = 0; t + 2 < mesh->indices.size(); t += 3) {
            add(mesh->indices[t], mesh->indices[t + 1]);
            add(mesh->indices[t + 1], mesh->indices[t + 2]);
            add(mesh->indices[t + 2], mesh->indices[t]);
        }
        edges.assign(unique.begin(), unique.end());
        bindLength.resize(edges.size());
        for (size_t i = 0; i < edges.size(); ++i) {
            bindLength[i] = (mesh->vertices[edges[i].first].position -
                             mesh->vertices[edges[i].second].position)
                                .length();
        }
    }

    std::vector<float> strain(const HumanoidVariant& variant, const Pose& pose) const {
        Mat4 palette[kJointCount];
        buildSkinPalette(variant, pose, palette);
        MeshData posed;
        skinMesh(*mesh, palette, posed);
        std::vector<float> out(edges.size(), 0.0f);
        for (size_t i = 0; i < edges.size(); ++i) {
            if (bindLength[i] < 1e-6f) continue;
            const float len = (posed.vertices[edges[i].first].position -
                               posed.vertices[edges[i].second].position)
                                  .length();
            out[i] = std::fabs(len / bindLength[i] - 1.0f);
        }
        return out;
    }
};

float worstOf(const std::vector<float>& v) {
    float w = 0.0f;
    for (float x : v) w = std::max(w, x);
    return w;
}

}  // namespace

MGE_TEST(layering_does_not_shear_the_real_skinned_body) {
    const HumanoidVariant variant;
    const Skeleton skeleton = buildSkeleton(variant);
    SkinProbe probe;
    probe.build();
    MGE_CHECK(probe.mesh->vertices.size() > 1000);  // the real body, not a stub
    MGE_CHECK(probe.edges.size() > 3000);

    // What the shipped walk cycle does to this skin, all by itself. Everything
    // below is judged against this rather than against zero.
    LocomotionAnimator anim;
    for (int f = 0; f < 180; ++f) anim.update(1.0f / 60.0f, 1.6f);
    Pose walk;
    anim.samplePose(walk);
    const std::vector<float> walkStrain = probe.strain(variant, walk);
    const float walkWorst = worstOf(walkStrain);
    MGE_CHECK(walkWorst > 0.0f);

    float worstExcess = 0.0f;
    const UseArchetype kSampled[] = {UseArchetype::Swing, UseArchetype::Chop,
                                     UseArchetype::Thrust};
    for (UseArchetype archetype : kSampled) {
        UseMotion motion;
        motion.archetype = archetype;
        for (int step = 0; step <= 4; ++step) {
            Pose action;
            sampleUseArchetype(motion, static_cast<float>(step) / 4.0f, action);
            const std::vector<float> actionStrain = probe.strain(variant, action);

            LayeredPose layered;
            layered.reset(walk);
            layered.addLayer(action, useArchetypeMask(skeleton, motion), 1.0f);
            const std::vector<float> layeredStrain = probe.strain(variant, layered.result());

            for (size_t i = 0; i < probe.edges.size(); ++i) {
                const float excess =
                    layeredStrain[i] - std::max(walkStrain[i], actionStrain[i]);
                worstExcess = std::max(worstExcess, excess);
            }
        }
    }
    printf("  skin shear: locomotion's own worst edge strain %.3f; worst excess ADDED by "
           "layering %.3f\n",
           walkWorst, worstExcess);
    // Layering must not distort the skin more than the walk cycle already does.
    MGE_CHECK(worstExcess < walkWorst);
}
