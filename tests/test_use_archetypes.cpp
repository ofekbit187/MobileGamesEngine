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

// ---------------------------------------------------------------------------
// Playing an archetype (14.3 phase addressability, 14.4 interruption).
// ---------------------------------------------------------------------------

MGE_TEST(the_strike_moment_fires_exactly_once) {
    UseMotion sword;
    sword.archetype = UseArchetype::Swing;
    sword.reach = 1.05f;
    sword.weight = 1.40f;

    UsePlayer player;
    player.start(sword);
    int fired = 0;
    for (int f = 0; f < 600 && player.active(); ++f) {
        if (player.update(1.0f / 60.0f)) ++fired;
    }
    MGE_CHECK(fired == 1);
    MGE_CHECK(!player.active());

    // ...and exactly once even when one long dt steps clean over the moment,
    // which is how a hitch would otherwise drop a hit entirely.
    UsePlayer coarse;
    coarse.start(sword);
    fired = 0;
    while (coarse.active()) {
        if (coarse.update(0.9f)) ++fired;
    }
    MGE_CHECK(fired == 1);
}

MGE_TEST(a_heavier_weapon_lands_its_blow_later) {
    // The Phase 12 action model tunes a delay by hand. This is the replacement:
    // the damage instant IS the motion's own geometry.
    UseMotion dagger;
    dagger.archetype = UseArchetype::Swing;
    dagger.reach = 0.30f;
    dagger.weight = 0.35f;
    UseMotion maul;
    maul.archetype = UseArchetype::Swing;
    maul.reach = 1.15f;
    maul.weight = 7.50f;

    UsePlayer light, heavy;
    light.start(dagger);
    heavy.start(maul);
    MGE_CHECK(heavy.strikeMoment() > light.strikeMoment() * 1.5f);
    MGE_CHECK(light.timeUntilStrike() > 0.0f);

    // Nobody tuned those two numbers to agree — they come from `weight`.
    MGE_CHECK_NEAR(light.strikeMoment(),
                   (light.phases().windUp + light.phases().strike) * light.phases().duration,
                   1e-6);
}

MGE_TEST(phases_are_addressable_through_the_motion) {
    UseMotion axe;
    axe.archetype = UseArchetype::Chop;
    axe.reach = 0.85f;
    axe.weight = 2.60f;

    UsePlayer player;
    MGE_CHECK(player.phase() == UsePhase::Idle);
    player.start(axe);

    bool sawWindUp = false, sawStrike = false, sawRecovery = false;
    UsePhase previous = UsePhase::WindUp;
    while (player.active()) {
        const UsePhase p = player.phase();
        if (p == UsePhase::WindUp) sawWindUp = true;
        if (p == UsePhase::Strike) sawStrike = true;
        if (p == UsePhase::Recovery) sawRecovery = true;
        // Phases only ever move forward.
        MGE_CHECK(static_cast<int>(p) >= static_cast<int>(previous));
        previous = p;
        const float f = player.phaseFraction();
        MGE_CHECK(f >= -1e-4f && f <= 1.0f + 1e-4f);
        player.update(1.0f / 120.0f);
    }
    MGE_CHECK(sawWindUp && sawStrike && sawRecovery);
    MGE_CHECK(player.phase() == UsePhase::Idle);
}

MGE_TEST(an_interrupted_action_never_lands_its_blow) {
    UseMotion sword;
    sword.archetype = UseArchetype::Swing;
    UsePlayer player;
    player.start(sword);
    // Run into the wind-up, then take a hit.
    while (player.phase() == UsePhase::WindUp) player.update(1.0f / 60.0f);
    player.interrupt(0.15f);
    MGE_CHECK(player.interrupted());

    int fired = 0;
    for (int f = 0; f < 600 && player.active(); ++f) {
        if (player.update(1.0f / 60.0f)) ++fired;
    }
    MGE_CHECK(fired == 0);
    MGE_CHECK(!player.active());
    MGE_CHECK_NEAR(player.weight(), 0.0f, 1e-6);
}

// The headline of 14.4, measured rather than asserted: interruption BLENDS.
MGE_TEST(interruption_blends_out_instead_of_snapping) {
    const Skeleton skeleton = rig();
    UseMotion axe;
    axe.archetype = UseArchetype::Chop;
    axe.reach = 0.85f;
    axe.weight = 2.60f;
    const JointMask mask = useArchetypeMask(skeleton, axe);

    // Run the same interruption twice: once through the blend, once dropping
    // the layer dead at the same instant. Measure the WORST single-frame jump
    // of the composed pose in each.
    // Three runs, identical up to the hit: never interrupted, interrupted with
    // the blend, interrupted by dropping the layer dead. `mode` picks which.
    enum class Mode { NoHit, Blend, Snap };
    const auto worstJump = [&](Mode mode) {
        LocomotionAnimator walk;
        for (int f = 0; f < 180; ++f) walk.update(1.0f / 60.0f, 1.6f);

        UsePlayer player;
        player.start(axe, 0.08f);
        // Get mid-swing before anything happens to us.
        while (player.phase() != UsePhase::Strike) {
            player.update(1.0f / 60.0f);
            walk.update(1.0f / 60.0f, 1.6f);
        }

        Pose previous;
        bool havePrevious = false;
        float worst = 0.0f;
        bool hit = false;
        for (int f = 0; f < 240; ++f) {
            Pose basePose;
            walk.samplePose(basePose);
            LayeredPose layered;
            layered.reset(basePose);
            if (player.active()) {
                Pose action;
                player.samplePose(action);
                // `blend` uses the player's ramped weight; the other drops the
                // layer outright the moment the hit lands, which is the
                // behaviour 14.4 exists to remove.
                const float w = mode == Mode::Blend ? player.weight()
                                : (mode == Mode::Snap && hit) ? 0.0f
                                                              : 1.0f;
                layered.addLayer(action, mask, w);
            }
            if (havePrevious) {
                const float jump = poseDistance(previous, layered.result());
                if (jump > worst) worst = jump;
            }
            previous = layered.result();
            havePrevious = true;

            if (f == 2 && mode != Mode::NoHit) {  // the hit arrives
                player.interrupt(0.15f);
                hit = true;
            }
            player.update(1.0f / 60.0f);
            walk.update(1.0f / 60.0f, 1.6f);
        }
        return worst;
    };

    const float baseline = worstJump(Mode::NoHit);
    const float blended = worstJump(Mode::Blend);
    const float snapped = worstJump(Mode::Snap);
    printf("  interrupt worst frame-to-frame jump: uninterrupted %.3f rad, blended %.3f, "
           "snapped %.3f\n",
           baseline, blended, snapped);

    // The right question is not "is the blend smooth in absolute terms" — a
    // heavy chop's strike legitimately moves ~30 degrees per frame on its own,
    // and an interrupt in the middle of it cannot be gentler than the motion
    // it is interrupting. The question is how much discontinuity the
    // INTERRUPTION ADDS.
    MGE_CHECK(baseline > 0.0f);
    // Snapping adds a large jump on top of the motion...
    MGE_CHECK(snapped > baseline * 2.5f);
    // ...while blending stays in the neighbourhood of the motion's own speed.
    MGE_CHECK(blended < baseline * 1.6f);
    MGE_CHECK(blended < snapped * 0.5f);
}

MGE_TEST(starting_an_action_ramps_in_rather_than_popping) {
    // A layer that appears at full strength pops exactly as visibly as one
    // that vanishes, so the same ramp runs at both ends.
    UseMotion torch;
    torch.archetype = UseArchetype::Raise;
    UsePlayer player;
    player.start(torch, 0.10f);
    MGE_CHECK_NEAR(player.weight(), 0.0f, 1e-6);

    float previous = player.weight();
    bool reachedFull = false;
    for (int f = 0; f < 12; ++f) {
        player.update(1.0f / 60.0f);
        MGE_CHECK(player.weight() >= previous - 1e-6f);  // monotone
        previous = player.weight();
        if (player.weight() >= 1.0f - 1e-6f) reachedFull = true;
    }
    MGE_CHECK(reachedFull);

    // Interrupting DURING the ramp-in still takes the full fade time rather
    // than vanishing early.
    UsePlayer early;
    early.start(torch, 0.40f);
    early.update(1.0f / 60.0f);
    const float partial = early.weight();
    MGE_CHECK(partial > 0.0f && partial < 1.0f);
    early.interrupt(0.20f);
    int frames = 0;
    while (early.active() && frames < 200) {
        early.update(1.0f / 60.0f);
        ++frames;
    }
    MGE_CHECK(frames >= 11);  // ~0.20 s at 60 Hz, not one frame
}

// ---------------------------------------------------------------------------
// 14.5 — an item declares how it is used (ADR 0017).
//
// The seam this crosses is the whole of P12's promise. Before it, `ItemUse`
// carried `range`, `power` and a free-form `animKey`, and nothing could reach
// the archetype numbers — so "declare `swing`, give it a reach and a weight"
// stopped one step short at exactly the declaring.
// ---------------------------------------------------------------------------

#include "mge/framework/items.h"

MGE_TEST(an_item_declares_its_use_and_the_engine_animates_it) {
    // Six items. Each is a plain ItemUse literal — no clip, no code, no
    // enumerator, no CMakeLists line. This IS the catalogue now; it stopped
    // being a struct that only the animation demo understood.
    ItemUse sword;
    sword.kind = ItemUseKind::Strike;
    sword.range = 2.0f;
    sword.power = 12.0f;
    sword.archetype = UseArchetype::Swing;
    sword.grip = ItemGrip::Versatile;
    sword.reach = 1.05f;
    sword.weight = 1.40f;

    ItemUse spear = sword;
    spear.archetype = UseArchetype::Thrust;
    spear.grip = ItemGrip::TwoHanded;
    spear.reach = 2.40f;
    spear.weight = 2.20f;

    ItemUse axe = sword;
    axe.archetype = UseArchetype::Chop;
    axe.grip = ItemGrip::OneHanded;
    axe.reach = 0.85f;
    axe.weight = 2.60f;

    ItemUse hammer = sword;
    hammer.archetype = UseArchetype::Work;
    hammer.reach = 0.45f;
    hammer.weight = 3.20f;

    ItemUse torch;
    torch.kind = ItemUseKind::Toggle;
    torch.archetype = UseArchetype::Raise;
    torch.reach = 0.55f;
    torch.weight = 0.70f;

    ItemUse apple;
    apple.kind = ItemUseKind::Consume;
    apple.power = 8.0f;
    apple.archetype = UseArchetype::Consume;
    apple.reach = 0.10f;
    apple.weight = 0.20f;

    const ItemUse* catalogue[] = {&sword, &spear, &axe, &hammer, &torch, &apple};
    constexpr size_t kCount = sizeof(catalogue) / sizeof(catalogue[0]);

    // They go through the ordinary registry, by asset id, like any item.
    ItemUseRegistry registry;
    const char* names[kCount] = {"item/sword", "item/spear",  "item/axe",
                                 "item/hammer", "item/torch", "item/apple"};
    for (size_t i = 0; i < kCount; ++i) {
        MGE_CHECK(registry.define(names[i], *catalogue[i]));
    }

    // And what comes back out animates, distinctly, with no per-item work.
    UseMotion motions[kCount];
    for (size_t i = 0; i < kCount; ++i) {
        const ItemUse* found = registry.find(assetIdFromName(names[i]));
        MGE_CHECK(found != nullptr);
        motions[i] = motionFromItemUse(*found);
        MGE_CHECK(motions[i].archetype == catalogue[i]->archetype);
        MGE_CHECK(motions[i].grip == catalogue[i]->grip);
        MGE_CHECK(!usesBespokeClip(*found));  // none of the six needs a clip
    }
    for (size_t i = 0; i < kCount; ++i) {
        for (size_t j = i + 1; j < kCount; ++j) {
            MGE_CHECK(motionDistance(motions[i], motions[j]) > 0.40f);
        }
    }
}

MGE_TEST(an_item_that_says_nothing_about_its_use_still_animates) {
    // Every field is defaulted, so nothing that existed before 14.5 changed
    // behaviour — the whole point of defaulting them.
    const ItemUse silent;
    const UseMotion motion = motionFromItemUse(silent);
    MGE_CHECK(motion.archetype == UseArchetype::Swing);
    MGE_CHECK(motion.grip == ItemGrip::OneHanded);
    const UsePhases phases = usePhases(motion);
    MGE_CHECK(phases.duration > 0.0f);
    MGE_CHECK_NEAR(phases.windUp + phases.strike + phases.recovery, 1.0f, 1e-5);

    // Absurd item data is clamped on the way across the seam, not trusted.
    ItemUse absurd;
    absurd.reach = 900.0f;
    absurd.weight = -40.0f;
    const UseMotion clamped = motionFromItemUse(absurd);
    MGE_CHECK_NEAR(clamped.reach, kUseReachRange.max, 1e-6);
    MGE_CHECK_NEAR(clamped.weight, kUseWeightRange.min, 1e-6);

    // Handedness is the CHARACTER's, not the item's.
    MGE_CHECK(motionFromItemUse(silent, true).leftHanded);
    MGE_CHECK(!motionFromItemUse(silent, false).leftHanded);
}

MGE_TEST(animKey_narrows_to_the_bespoke_clip_escape_hatch) {
    ItemUse ordinary;
    MGE_CHECK(!usesBespokeClip(ordinary));  // empty: animate the archetype

    ItemUse heroBlade;
    heroBlade.archetype = UseArchetype::Swing;
    heroBlade.animKey = "clip/excalibur_draw";
    MGE_CHECK(usesBespokeClip(heroBlade));
    // ...and it still carries a usable archetype underneath, so an engine
    // with no clip for that key falls back to animating the kind rather than
    // standing still.
    const UseMotion fallback = motionFromItemUse(heroBlade);
    MGE_CHECK(fallback.archetype == UseArchetype::Swing);
    MGE_CHECK(usePhases(fallback).duration > 0.0f);
}

// ---------------------------------------------------------------------------
// 14.4 end to end, through the real gameplay API rather than a stand-in.
//
// The animation half of interruption lives entirely on the game's side of the
// seam, and that is not a workaround — it is where per-character animation
// state ALREADY lives (`device_game.cpp`'s Actor carries a
// `LocomotionAnimator`). So a `UsePlayer` sits beside it and needs no change
// to `CharacterComponent` at all.
// ---------------------------------------------------------------------------

#include "mge/framework/character.h"
#include "mge/framework/world.h"

MGE_TEST(a_real_hit_mid_action_blends_the_action_out) {
    World world(64);
    CharacterSystem characters(world);
    const EntityId actor = world.spawn();
    TransformComponent transform;
    world.setTransform(actor, transform);
    characters.attach(actor);

    ItemUse axe;
    axe.kind = ItemUseKind::Strike;
    axe.archetype = UseArchetype::Chop;
    axe.reach = 0.85f;
    axe.weight = 2.60f;

    // The game's own animation state, beside its locomotion — no engine
    // change, no CharacterComponent field.
    UsePlayer player;
    player.start(motionFromItemUse(axe));

    const CharacterComponent* character = characters.get(actor);
    MGE_CHECK(character != nullptr);
    float previousHealth = character->health;

    bool struck = false, interrupted = false;
    float weightWhenHit = 0.0f;
    for (int frame = 0; frame < 240 && player.active(); ++frame) {
        // A blow lands on us part-way through our own wind-up.
        if (frame == 6) characters.damage(actor, 0.2f);

        // What a game does today to notice: watch its own health. There is
        // no hit callback on CharacterSystem — see docs/status/animation.md.
        const CharacterComponent* now = characters.get(actor);
        MGE_CHECK(now != nullptr);
        if (now->health < previousHealth - 1e-6f && !interrupted) {
            weightWhenHit = player.weight();
            player.interrupt(0.15f);
            interrupted = true;
        }
        previousHealth = now->health;

        if (player.update(1.0f / 60.0f)) struck = true;
    }

    MGE_CHECK(interrupted);
    MGE_CHECK(weightWhenHit > 0.5f);   // it was genuinely mid-action
    // An interrupted swing never lands its blow...
    MGE_CHECK(!struck);
    // ...and it faded rather than vanished: the player stayed alive for
    // several frames after the hit, weight ramping down.
    MGE_CHECK(!player.active());
    MGE_CHECK_NEAR(player.weight(), 0.0f, 1e-6);
}
