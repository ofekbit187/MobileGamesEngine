// Use archetypes (task 14.2, P12).
//
// The law under test: a new weapon must never mean new animation work. The
// engine animates the archetype; the item supplies grip, reach and weight.

#include "mge/character/animation.h"
#include "mge/character/use_archetypes.h"
#include "test_framework.h"

using namespace mge;

namespace {

constexpr size_t idx(Joint j) { return static_cast<size_t>(j); }

Skeleton rig() { return buildSkeleton(HumanoidVariant{}); }

// Angle between two rotations, in radians.
//
// NOT 2*acos(dot): acos has an infinite derivative at 1, so a float dot of
// 0.99999988 on two BIT-IDENTICAL quaternions comes back as 0.001 rad of
// "difference". That cost a false failure here before it was measured.
// 4*atan2(|a-b|, |a+b|) is exact in the same neighbourhood.
float angleBetween(const Quat& a, const Quat& b) {
    const float dot = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    const float s = dot < 0.0f ? -1.0f : 1.0f;  // q and -q are one rotation
    const float dx = a.x - s * b.x, dy = a.y - s * b.y, dz = a.z - s * b.z,
                dw = a.w - s * b.w;
    const float sx = a.x + s * b.x, sy = a.y + s * b.y, sz = a.z + s * b.z,
                sw = a.w + s * b.w;
    const float dLen = std::sqrt(dx * dx + dy * dy + dz * dz + dw * dw);
    const float sLen = std::sqrt(sx * sx + sy * sy + sz * sz + sw * sw);
    return 4.0f * std::atan2(dLen, sLen);
}

// How far apart two poses are, in radians, at their most-different joint.
float poseDistance(const Pose& a, const Pose& b) {
    float worst = 0.0f;
    for (size_t j = 0; j < kJointCount; ++j) {
        const float d = angleBetween(a.rotation[j], b.rotation[j]);
        if (d > worst) worst = d;
    }
    return worst;
}

Pose sampleAt(const UseMotion& motion, float t) {
    Pose pose;
    sampleUseArchetype(motion, t, pose);
    return pose;
}

// The moment the motion is most committed — the end of its strike phase.
Pose atStrikeEnd(const UseMotion& motion) {
    const UsePhases p = usePhases(motion);
    return sampleAt(motion, p.windUp + p.strike);
}

// How different two motions are ACROSS THEIR TIMELINES, not at one instant.
// Comparing a single frame is the wrong question: a thrust, a chop and a
// hammer blow all end with the arm extended forward, because that is what
// those motions physically do. What separates them is the path they take to
// get there — so the metric is the widest gap anywhere along the two.
float motionDistance(const UseMotion& a, const UseMotion& b) {
    float worst = 0.0f;
    for (int step = 0; step <= 20; ++step) {
        const float t = static_cast<float>(step) / 20.0f;
        const float d = poseDistance(sampleAt(a, t), sampleAt(b, t));
        if (d > worst) worst = d;
    }
    return worst;
}

}  // namespace

MGE_TEST(every_archetype_is_a_visibly_different_motion) {
    // P12's promise is that declaring a different archetype gives a different
    // motion for free. If two archetypes coincide, one of them is dead data.
    UseMotion motions[kUseArchetypeCount];
    for (size_t i = 0; i < kUseArchetypeCount; ++i) {
        motions[i].archetype = static_cast<UseArchetype>(i);
    }
    for (size_t i = 0; i < kUseArchetypeCount; ++i) {
        for (size_t j = i + 1; j < kUseArchetypeCount; ++j) {
            const float d = motionDistance(motions[i], motions[j]);
            if (d <= 0.40f) {
                printf("  %s and %s never differ by more than %.3f rad\n",
                       useArchetypeName(static_cast<UseArchetype>(i)),
                       useArchetypeName(static_cast<UseArchetype>(j)), d);
            }
            MGE_CHECK(d > 0.40f);
        }
    }
}

MGE_TEST(every_archetype_starts_and_ends_at_rest) {
    // A motion that does not return to where it started snaps when it ends,
    // and cannot be replayed back to back — which `Work` depends on.
    for (size_t i = 0; i < kUseArchetypeCount; ++i) {
        UseMotion motion;
        motion.archetype = static_cast<UseArchetype>(i);
        MGE_CHECK(poseDistance(sampleAt(motion, 0.0f), sampleAt(motion, 1.0f)) < 1e-4f);
    }
}

MGE_TEST(weight_sets_the_timing_and_reach_sets_the_arc) {
    // "A war-hammer and a dagger are the same swing archetype at different
    // speeds, and read as completely different weapons."
    UseMotion dagger;
    dagger.archetype = UseArchetype::Swing;
    dagger.reach = 0.30f;
    dagger.weight = 0.35f;
    UseMotion maul;
    maul.archetype = UseArchetype::Swing;
    maul.reach = 1.15f;
    maul.weight = 7.50f;

    const UsePhases light = usePhases(dagger);
    const UsePhases heavy = usePhases(maul);

    // The heavy one takes longer overall...
    MGE_CHECK(heavy.duration > light.duration * 1.5f);
    // ...spends proportionally longer winding up...
    MGE_CHECK(heavy.windUp > light.windUp);
    // ...and its strike is a smaller slice of a bigger whole.
    MGE_CHECK(heavy.strike < light.strike);
    // But in absolute seconds the heavy strike still takes longer — a maul is
    // slow, not twitchy.
    MGE_CHECK(heavy.strike * heavy.duration > light.strike * light.duration);

    // Every phase set is a valid partition of the timeline.
    for (size_t i = 0; i < kUseArchetypeCount; ++i) {
        for (float w : {0.10f, 1.4f, 8.0f}) {
            UseMotion m;
            m.archetype = static_cast<UseArchetype>(i);
            m.weight = w;
            const UsePhases p = usePhases(m);
            MGE_CHECK_NEAR(p.windUp + p.strike + p.recovery, 1.0f, 1e-5);
            MGE_CHECK(p.windUp > 0.0f && p.strike > 0.0f && p.recovery > 0.0f);
            MGE_CHECK(p.duration > 0.0f);
        }
    }

    // Reach widens the arc: a longer weapon sweeps further at full extension.
    UseMotion shortReach;
    shortReach.archetype = UseArchetype::Swing;
    shortReach.reach = 0.30f;
    UseMotion longReach;
    longReach.archetype = UseArchetype::Swing;
    longReach.reach = 2.50f;
    const Skeleton skeleton = rig();
    Mat4 shortWorld[kJointCount], longWorld[kJointCount];
    evaluatePose(skeleton, atStrikeEnd(shortReach), shortWorld);
    evaluatePose(skeleton, atStrikeEnd(longReach), longWorld);
    const Vec3 shortHand = shortWorld[idx(Joint::HandR)].transformPoint({0, 0, 0});
    const Vec3 longHand = longWorld[idx(Joint::HandR)].transformPoint({0, 0, 0});
    MGE_CHECK((shortHand - longHand).length() > 0.02f);
}

MGE_TEST(grip_decides_which_arms_participate) {
    const Skeleton skeleton = rig();

    UseMotion oneHanded;
    oneHanded.archetype = UseArchetype::Swing;
    oneHanded.grip = ItemGrip::OneHanded;
    const JointMask one = useArchetypeMask(skeleton, oneHanded);
    // The lead arm is the action's...
    MGE_CHECK_NEAR(one.weight[idx(Joint::UpperArmR)], 1.0f, 1e-6);
    MGE_CHECK_NEAR(one.weight[idx(Joint::HandR)], 1.0f, 1e-6);
    // ...and the OFF arm is left to locomotion, so it keeps swinging with the
    // walk. This is the point of deriving the mask from grip.
    MGE_CHECK_NEAR(one.weight[idx(Joint::UpperArmL)], 0.0f, 1e-6);
    MGE_CHECK_NEAR(one.weight[idx(Joint::HandL)], 0.0f, 1e-6);
    // Legs always stay with locomotion.
    MGE_CHECK_NEAR(one.weight[idx(Joint::ThighL)], 0.0f, 1e-6);
    MGE_CHECK_NEAR(one.weight[idx(Joint::FootR)], 0.0f, 1e-6);

    UseMotion twoHanded = oneHanded;
    twoHanded.grip = ItemGrip::TwoHanded;
    const JointMask two = useArchetypeMask(skeleton, twoHanded);
    MGE_CHECK_NEAR(two.weight[idx(Joint::UpperArmL)], 1.0f, 1e-6);
    MGE_CHECK_NEAR(two.weight[idx(Joint::UpperArmR)], 1.0f, 1e-6);
    MGE_CHECK_NEAR(two.weight[idx(Joint::ThighL)], 0.0f, 1e-6);

    // A bow is two-armed however its grip is declared.
    UseMotion bow;
    bow.archetype = UseArchetype::Draw;
    bow.grip = ItemGrip::OneHanded;
    const JointMask bowMask = useArchetypeMask(skeleton, bow);
    MGE_CHECK_NEAR(bowMask.weight[idx(Joint::UpperArmL)], 1.0f, 1e-6);

    // Two-handed drives the torso harder than one-handed.
    const float oneTwist = angleBetween(atStrikeEnd(oneHanded).rotation[idx(Joint::Chest)], Quat{});
    const float twoTwist = angleBetween(atStrikeEnd(twoHanded).rotation[idx(Joint::Chest)], Quat{});
    MGE_CHECK(twoTwist > oneTwist);
}

MGE_TEST(left_handed_mirrors_the_motion) {
    const Skeleton skeleton = rig();
    UseMotion right;
    right.archetype = UseArchetype::Chop;
    UseMotion left = right;
    left.leftHanded = true;

    const Pose r = atStrikeEnd(right);
    const Pose l = atStrikeEnd(left);
    // The lead arm swaps sides and MIRRORS across the sagittal plane: a
    // rotation (x, y, z, w) reflects to (x, -y, -z, w). Plain equality is the
    // wrong assertion — a mirrored arm is not an identical arm.
    const Quat& rl = r.rotation[idx(Joint::UpperArmR)];
    const Quat mirrored{rl.x, -rl.y, -rl.z, rl.w};
    MGE_CHECK(angleBetween(mirrored, l.rotation[idx(Joint::UpperArmL)]) < 1e-4f);
    MGE_CHECK_NEAR(r.rotation[idx(Joint::ForearmR)].x, l.rotation[idx(Joint::ForearmL)].x, 1e-5);
    // The torso twists the other way.
    MGE_CHECK_NEAR(r.rotation[idx(Joint::Chest)].y, -l.rotation[idx(Joint::Chest)].y, 1e-5);
    // And the mask follows the lead hand.
    const JointMask lm = useArchetypeMask(skeleton, left);
    MGE_CHECK_NEAR(lm.weight[idx(Joint::UpperArmL)], 1.0f, 1e-6);
    MGE_CHECK_NEAR(lm.weight[idx(Joint::UpperArmR)], 0.0f, 1e-6);
}

MGE_TEST(item_data_is_clamped_not_rejected) {
    // Item data is content. Content must never be able to ask for a motion the
    // rig cannot do — the same stance ADR 0009 takes for body variants.
    UseMotion absurd;
    absurd.reach = 500.0f;
    absurd.weight = -12.0f;
    const UseMotion clamped = clampUseMotion(absurd);
    MGE_CHECK_NEAR(clamped.reach, kUseReachRange.max, 1e-6);
    MGE_CHECK_NEAR(clamped.weight, kUseWeightRange.min, 1e-6);

    // And sampling absurd data still produces a finite, unit-quaternion pose.
    const Pose pose = atStrikeEnd(absurd);
    for (size_t j = 0; j < kJointCount; ++j) {
        const Quat& q = pose.rotation[j];
        const float len = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
        MGE_CHECK(std::isfinite(len));
        MGE_CHECK_NEAR(len, 1.0f, 1e-4);
    }
}

// The P12 proof, as a test rather than a promise (task 14.6).
MGE_TEST(the_six_item_catalog_needs_no_per_item_animation) {
    const Skeleton skeleton = rig();
    struct CatalogItem {
        const char* name;
        UseArchetype archetype;
        ItemGrip grip;
        float reach, weight;
    };
    // Six items. Every one of them is FIVE NUMBERS — no clip, no code, no
    // per-item animation authoring anywhere.
    const CatalogItem kCatalog[] = {
        {"sword", UseArchetype::Swing, ItemGrip::Versatile, 1.05f, 1.40f},
        {"spear", UseArchetype::Thrust, ItemGrip::TwoHanded, 2.40f, 2.20f},
        {"axe", UseArchetype::Chop, ItemGrip::OneHanded, 0.85f, 2.60f},
        {"hammer", UseArchetype::Work, ItemGrip::OneHanded, 0.45f, 3.20f},
        {"torch", UseArchetype::Raise, ItemGrip::OneHanded, 0.55f, 0.70f},
        {"apple", UseArchetype::Consume, ItemGrip::OneHanded, 0.10f, 0.20f},
    };
    constexpr size_t kCount = sizeof(kCatalog) / sizeof(kCatalog[0]);

    UseMotion motions[kCount];
    for (size_t i = 0; i < kCount; ++i) {
        UseMotion motion;
        motion.archetype = kCatalog[i].archetype;
        motion.grip = kCatalog[i].grip;
        motion.reach = kCatalog[i].reach;
        motion.weight = kCatalog[i].weight;
        motions[i] = motion;
        // Each one composes over a walk through the ordinary 14.1 path.
        LocomotionAnimator anim;
        for (int f = 0; f < 180; ++f) anim.update(1.0f / 60.0f, 1.6f);
        Pose walk;
        anim.samplePose(walk);
        LayeredPose layered;
        layered.reset(walk);
        MGE_CHECK(layered.addLayer(atStrikeEnd(motion), useArchetypeMask(skeleton, motion), 1.0f));
        // ...and the legs keep walking underneath every one of them.
        MGE_CHECK_NEAR(angleBetween(layered.result().rotation[idx(Joint::ThighL)],
                                    walk.rotation[idx(Joint::ThighL)]),
                       0.0f, 1e-6);
    }

    // Visibly distinct: no two of the six read as the same motion.
    for (size_t i = 0; i < kCount; ++i) {
        for (size_t j = i + 1; j < kCount; ++j) {
            const float d = motionDistance(motions[i], motions[j]);
            if (d <= 0.40f) {
                printf("  %s and %s never differ by more than %.3f rad\n", kCatalog[i].name,
                       kCatalog[j].name, d);
            }
            MGE_CHECK(d > 0.40f);
        }
    }
}

// ---------------------------------------------------------------------------
// Does each archetype actually DO what its name says? A motion can be smooth,
// distinct and allocation-free and still have the arm in the wrong place —
// `Raise` pointed the torch backwards until this was rendered and measured.
// These pin the semantics to geometry so the next edit cannot quietly undo it.
// ---------------------------------------------------------------------------

namespace {

Vec3 jointAt(const UseMotion& motion, float t, Joint joint) {
    const Skeleton skeleton = rig();
    Pose pose;
    sampleUseArchetype(motion, t, pose);
    Mat4 world[kJointCount];
    evaluatePose(skeleton, pose, world);
    return world[static_cast<size_t>(joint)].transformPoint({0, 0, 0});
}

Vec3 leadHandAt(const UseMotion& motion, float t) {
    return jointAt(motion, t, motion.leftHanded ? Joint::HandL : Joint::HandR);
}

float strikeTime(const UseMotion& motion) {
    const UsePhases p = usePhases(motion);
    return p.windUp + p.strike;
}

}  // namespace

MGE_TEST(raise_holds_the_item_overhead) {
    UseMotion torch;
    torch.archetype = UseArchetype::Raise;
    torch.reach = 0.55f;
    torch.weight = 0.70f;

    const Vec3 head = jointAt(torch, strikeTime(torch), Joint::Head);
    const Vec3 rest = leadHandAt(torch, 0.0f);
    const Vec3 held = leadHandAt(torch, strikeTime(torch));

    MGE_CHECK(rest.y < head.y);   // starts down by the hip
    MGE_CHECK(held.y > head.y);   // ends ABOVE the head — that is "aloft"
    MGE_CHECK(held.y > rest.y + 0.6f);
    // ...and it HOLDS: the pose barely moves through the strike phase.
    const UsePhases p = usePhases(torch);
    const Vec3 early = leadHandAt(torch, p.windUp);
    MGE_CHECK((held - early).length() < 0.08f);
}

MGE_TEST(consume_brings_the_hand_to_the_head) {
    UseMotion apple;
    apple.archetype = UseArchetype::Consume;
    apple.reach = 0.10f;
    apple.weight = 0.20f;

    const float t = strikeTime(apple);
    const Vec3 head = jointAt(apple, t, Joint::Head);
    const float atRest = (leadHandAt(apple, 0.0f) - jointAt(apple, 0.0f, Joint::Head)).length();
    const float atMouth = (leadHandAt(apple, t) - head).length();
    MGE_CHECK(atRest > 0.60f);   // the hand starts nowhere near the head
    // Measured against the HEAD JOINT, which sits at the centre of the skull,
    // not at the mouth — and the arm's own bind offsets carry the hand ~0.22 m
    // outward whatever the shoulder does. 0.50 m to the joint puts the hand
    // against the lower face on this body; asserting 0.30 would be asserting a
    // rig the engine does not have.
    MGE_CHECK(atMouth < 0.50f);
    MGE_CHECK(atMouth < atRest * 0.6f);
}

MGE_TEST(thrust_goes_forward_where_swing_goes_across) {
    // A stab is a straight line and an arc is not, so the two must separate on
    // WHICH WAY the hand travels. Stated as a contrast rather than as absolute
    // ratios: the absolute numbers are properties of this rig's bone lengths,
    // but "a thrust advances and a swing crosses" is a property of the motions.
    UseMotion spear;
    spear.archetype = UseArchetype::Thrust;
    spear.reach = 2.40f;
    spear.weight = 2.20f;
    UseMotion sword;
    sword.archetype = UseArchetype::Swing;
    sword.reach = 1.05f;
    sword.weight = 1.40f;

    const Vec3 tRest = leadHandAt(spear, 0.0f);
    const Vec3 tOut = leadHandAt(spear, strikeTime(spear));
    const Vec3 sRest = leadHandAt(sword, 0.0f);
    const Vec3 sOut = leadHandAt(sword, strikeTime(sword));

    const float thrustForward = tRest.z - tOut.z;  // local -Z is forward
    const float thrustSide = std::fabs(tOut.x - tRest.x);
    const float swingForward = sRest.z - sOut.z;
    const float swingSide = std::fabs(sOut.x - sRest.x);

    // The thrust actually advances — it used to RETREAT, because the torso
    // twist drove the lead shoulder the wrong way.
    MGE_CHECK(thrustForward > 0.15f);
    MGE_CHECK(thrustForward > thrustSide);
    // The swing crosses the body instead.
    MGE_CHECK(swingSide > swingForward);
    // And each out-does the other at its own thing, by a wide margin.
    MGE_CHECK(thrustForward > swingForward * 2.0f);
    MGE_CHECK(swingSide > thrustSide * 2.0f);
}

MGE_TEST(chop_passes_overhead_then_comes_down) {
    UseMotion axe;
    axe.archetype = UseArchetype::Chop;
    axe.reach = 0.85f;
    axe.weight = 2.60f;
    const UsePhases p = usePhases(axe);

    const Vec3 loaded = leadHandAt(axe, p.windUp);
    const Vec3 head = jointAt(axe, p.windUp, Joint::Head);
    const Vec3 through = leadHandAt(axe, strikeTime(axe));
    MGE_CHECK(loaded.y > head.y);            // wound up above the head
    MGE_CHECK(through.y < loaded.y - 0.50f);  // and driven well down through it
    MGE_CHECK(through.y < head.y - 0.25f);    // ending below the chin, not at it
    // Vertical, not horizontal — that is what separates Chop from Swing.
    MGE_CHECK((loaded.y - through.y) > std::fabs(through.x - loaded.x) * 2.0f);
}

MGE_TEST(draw_pulls_the_off_hand_back_while_the_lead_arm_holds) {
    UseMotion bow;
    bow.archetype = UseArchetype::Draw;
    bow.reach = 1.60f;
    bow.weight = 0.90f;
    const UsePhases p = usePhases(bow);

    // The lead arm holds the bow steady through the release.
    const Vec3 leadAtDraw = leadHandAt(bow, p.windUp);
    const Vec3 leadAtRelease = leadHandAt(bow, strikeTime(bow));
    MGE_CHECK((leadAtRelease - leadAtDraw).length() < 0.05f);
    // ...and it holds it out in front.
    MGE_CHECK(leadAtDraw.z < jointAt(bow, p.windUp, Joint::Chest).z - 0.25f);

    // The off hand does the work: drawn back near the face, then released.
    const Vec3 offAtDraw = jointAt(bow, p.windUp, Joint::HandL);
    const Vec3 offAtRelease = jointAt(bow, strikeTime(bow), Joint::HandL);
    MGE_CHECK((offAtDraw - jointAt(bow, p.windUp, Joint::Head)).length() < 0.55f);
    MGE_CHECK((offAtRelease - offAtDraw).length() > 0.15f);
}
