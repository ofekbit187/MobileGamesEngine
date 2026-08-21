#include "mge/character/use_archetypes.h"

#include <cmath>

namespace mge {

namespace {

constexpr size_t idx(Joint j) { return static_cast<size_t>(j); }

Quat rotX(float a) { return Quat::fromAxisAngle({1, 0, 0}, a); }
Quat rotY(float a) { return Quat::fromAxisAngle({0, 1, 0}, a); }
Quat rotZ(float a) { return Quat::fromAxisAngle({0, 0, 1}, a); }

float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
float mix(float a, float b, float t) { return a + (b - a) * t; }
float smooth(float t) { return t * t * (3.0f - 2.0f * t); }

// Where a value sits in its range, as [0, 1].
float normalized(float value, const VariantRange& range) {
    return clampf((value - range.min) / (range.max - range.min), 0.0f, 1.0f);
}

// ---------------------------------------------------------- the shapes -----
//
// Angles are authored for a RIGHT-handed character and mirrored for a
// left-handed one. `pitch` is rotX on the upper arm (positive swings the arm
// forward and over, negative back and over); `abduct` is how far the arm is
// held away from the body; `elbow` is forearm flex.

struct ArmKey {
    float pitch = 0.10f;
    float abduct = 0.10f;
    float elbow = 0.30f;
};

struct MotionKey {
    ArmKey lead;
    ArmKey off;
    float twist = 0.0f;      // chest rotation toward the lead side
    float lean = 0.0f;       // chest pitch: positive leans into the motion
    float headPitch = 0.0f;  // chin down; only `Consume` really uses it
};

// rest -> loaded (end of wind-up) -> extended (end of strike) -> rest.
struct MotionKeys {
    MotionKey rest;
    MotionKey loaded;
    MotionKey extended;
};

const ArmKey kRestArm{0.10f, 0.10f, 0.30f};
const ArmKey kBraceArm{-0.28f, 0.26f, 0.80f};  // the off hand, steadying

MotionKeys archetypeKeys(UseArchetype archetype) {
    MotionKeys k;
    k.rest.lead = kRestArm;
    k.rest.off = kRestArm;

    switch (archetype) {
        case UseArchetype::Swing:
            // The signature is the TWIST reversing: the arc is horizontal, so
            // the torso winds one way and unwinds through the blow.
            k.loaded.lead = {-0.45f, 0.55f, 1.50f};
            k.loaded.off = kBraceArm;
            k.loaded.twist = 0.80f;
            k.loaded.lean = -0.05f;
            k.extended.lead = {0.35f, -0.85f, 0.20f};  // swept across the body
            k.extended.off = kBraceArm;
            k.extended.twist = -0.65f;
            k.extended.lean = 0.12f;
            break;
        case UseArchetype::Thrust:
            // Elbow-dominated and straight: almost no abduction, and the lean
            // is what sells the distance.
            k.loaded.lead = {-0.20f, 0.15f, 1.95f};
            k.loaded.off = kBraceArm;
            k.loaded.twist = 0.30f;
            k.loaded.lean = -0.06f;
            k.extended.lead = {0.60f, 0.05f, 0.05f};
            k.extended.off = kBraceArm;
            k.extended.twist = -0.12f;
            k.extended.lean = 0.22f;
            break;
        case UseArchetype::Chop:
            // Vertical: overhead and back, then down and through. Barely any
            // twist — that is what separates it from Swing.
            k.loaded.lead = {-2.55f, 0.20f, 0.85f};
            k.loaded.off = kBraceArm;
            k.loaded.twist = 0.25f;
            k.loaded.lean = -0.22f;
            k.extended.lead = {0.22f, 0.10f, 0.20f};
            k.extended.off = kBraceArm;
            k.extended.twist = -0.10f;
            k.extended.lean = 0.34f;
            break;
        case UseArchetype::Work:
            // Authored so the end state IS the rest state: replayed back to
            // back it loops seamlessly, which is what "sustained" means.
            k.rest.lead = {0.05f, 0.12f, 0.35f};
            k.rest.lean = 0.05f;
            k.loaded.lead = {-1.05f, 0.18f, 1.30f};
            k.loaded.off = kBraceArm;
            k.loaded.twist = 0.20f;
            k.loaded.lean = -0.06f;
            k.extended.lead = {0.45f, 0.12f, 0.25f};
            k.extended.off = kBraceArm;
            k.extended.twist = -0.08f;
            k.extended.lean = 0.20f;
            break;
        case UseArchetype::Draw:
            // Two-armed by construction: the lead arm HOLDS the bow steady
            // while the off hand draws to the cheek and releases.
            // The bow arm points AT the target, so abduction and twist stay
            // small: with them at 0.30 the arm reached out sideways and the
            // bow ended up at the hip. Pinned by draw_pulls_the_off_hand_back.
            k.loaded.lead = {1.48f, 0.02f, 0.10f};
            k.loaded.off = {1.05f, 0.25f, 2.05f};
            k.loaded.twist = 0.12f;
            k.loaded.lean = 0.04f;
            k.extended.lead = {1.48f, 0.02f, 0.10f};  // deliberately unchanged
            k.extended.off = {0.55f, 0.55f, 0.85f};
            // ...and the twist is unchanged too: the bow arm can only be
            // steady if the chest it hangs from is steady.
            k.extended.twist = 0.12f;
            break;
        case UseArchetype::Aim:
            // Raise, steady, and a recoil small enough to read as a mechanism
            // rather than a swing.
            k.loaded.lead = {1.40f, 0.18f, 0.30f};
            k.loaded.off = {1.25f, 0.45f, 0.75f};
            k.loaded.twist = 0.18f;
            k.loaded.lean = 0.05f;
            k.extended.lead = {1.28f, 0.18f, 0.36f};
            k.extended.off = {1.15f, 0.45f, 0.82f};
            k.extended.twist = 0.18f;
            k.extended.lean = 0.03f;
            break;
        case UseArchetype::Raise:
            // Goes up and STAYS: loaded and extended are the same pose, so
            // the "strike" phase holds instead of striking.
            // Pitch is measured from the arm hanging down: +pi/2 is straight
            // forward and +pi is straight up. -1.55 pointed the arm BACKWARD
            // and horizontal — a torch held behind the hip, not aloft. Caught
            // by rendering it; now asserted by raise_holds_the_item_overhead.
            k.loaded.lead = {2.05f, 0.30f, 0.55f};
            k.loaded.off = kRestArm;
            k.loaded.twist = 0.05f;
            k.loaded.lean = -0.03f;
            k.extended.lead = {2.15f, 0.30f, 0.50f};
            k.extended.off = kRestArm;
            k.extended.twist = 0.05f;
            k.extended.lean = -0.03f;
            break;
        case UseArchetype::Consume:
            // Elbow-dominated, hand arriving at the head, chin coming down to
            // meet it.
            // The arm adducts (negative abduct) so the hand comes up in FRONT
            // of the face rather than beside the ear, and the elbow does most
            // of the work. Asserted by consume_brings_the_hand_to_the_head.
            k.loaded.lead = {-0.10f, -1.00f, 2.70f};
            k.loaded.off = kRestArm;
            k.loaded.twist = 0.10f;
            k.loaded.lean = 0.06f;
            k.loaded.headPitch = 0.16f;
            k.extended.lead = {-0.10f, -1.06f, 2.78f};
            k.extended.off = kRestArm;
            k.extended.twist = 0.10f;
            k.extended.lean = 0.08f;
            k.extended.headPitch = 0.18f;
            break;
        case UseArchetype::Gesture:
            // Opens outward and resolves. No impact anywhere in it.
            k.loaded.lead = {-0.60f, 0.45f, 1.05f};
            k.loaded.off = kRestArm;
            k.loaded.twist = 0.18f;
            k.extended.lead = {-0.30f, 0.95f, 0.45f};
            k.extended.off = kRestArm;
            k.extended.twist = -0.12f;
            k.extended.lean = 0.04f;
            break;
        case UseArchetype::Count:
            break;
    }
    return k;
}

// Base timing per archetype, before weight modulates it. A bow charges; a
// dagger does not.
struct BaseTiming {
    float windUp, strike, durationScale;
};

BaseTiming baseTiming(UseArchetype archetype) {
    switch (archetype) {
        case UseArchetype::Swing:   return {0.34f, 0.18f, 1.00f};
        case UseArchetype::Thrust:  return {0.30f, 0.16f, 0.85f};
        case UseArchetype::Chop:    return {0.40f, 0.16f, 1.15f};
        case UseArchetype::Work:    return {0.42f, 0.20f, 0.95f};
        case UseArchetype::Draw:    return {0.50f, 0.12f, 1.30f};
        case UseArchetype::Aim:     return {0.45f, 0.08f, 1.35f};
        case UseArchetype::Raise:   return {0.35f, 0.10f, 1.10f};
        case UseArchetype::Consume: return {0.40f, 0.22f, 1.40f};
        case UseArchetype::Gesture: return {0.38f, 0.20f, 0.90f};
        case UseArchetype::Count:   break;
    }
    return {0.35f, 0.20f, 1.0f};
}

ArmKey lerpArm(const ArmKey& a, const ArmKey& b, float t) {
    return {mix(a.pitch, b.pitch, t), mix(a.abduct, b.abduct, t), mix(a.elbow, b.elbow, t)};
}

MotionKey lerpKey(const MotionKey& a, const MotionKey& b, float t) {
    MotionKey k;
    k.lead = lerpArm(a.lead, b.lead, t);
    k.off = lerpArm(a.off, b.off, t);
    k.twist = mix(a.twist, b.twist, t);
    k.lean = mix(a.lean, b.lean, t);
    k.headPitch = mix(a.headPitch, b.headPitch, t);
    return k;
}

// Both arms on the haft: the off hand takes the lead's shape.
ArmKey mirroredHaft(const ArmKey& lead) {
    return {lead.pitch * 0.92f, lead.abduct * 0.55f, lead.elbow * 1.15f};
}

bool inherentlyTwoArmed(UseArchetype a) {
    return a == UseArchetype::Draw || a == UseArchetype::Aim;
}

bool usesBothArms(const UseMotion& motion) {
    return motion.grip == ItemGrip::TwoHanded || inherentlyTwoArmed(motion.archetype);
}

}  // namespace

// ------------------------------------------------------------ the scope ----

const VariantRange kUseReachRange{0.20f, 2.60f, 1.00f};
const VariantRange kUseWeightRange{0.10f, 8.00f, 1.40f};

UseMotion clampUseMotion(const UseMotion& motion) {
    UseMotion m = motion;
    m.reach = clampf(m.reach, kUseReachRange.min, kUseReachRange.max);
    m.weight = clampf(m.weight, kUseWeightRange.min, kUseWeightRange.max);
    return m;
}

const char* useArchetypeName(UseArchetype archetype) {
    switch (archetype) {
        case UseArchetype::Swing:   return "swing";
        case UseArchetype::Thrust:  return "thrust";
        case UseArchetype::Chop:    return "chop";
        case UseArchetype::Work:    return "work";
        case UseArchetype::Draw:    return "draw";
        case UseArchetype::Aim:     return "aim";
        case UseArchetype::Raise:   return "raise";
        case UseArchetype::Consume: return "consume";
        case UseArchetype::Gesture: return "gesture";
        case UseArchetype::Count:   break;
    }
    return "?";
}

// ------------------------------------------------------------- timing ------

UsePhases usePhases(const UseMotion& motion) {
    const UseMotion m = clampUseMotion(motion);
    const BaseTiming base = baseTiming(m.archetype);
    const float w = normalized(m.weight, kUseWeightRange);

    UsePhases phases;
    // Heavier spends proportionally longer loading and commits harder, so the
    // strike is a smaller slice of a longer whole.
    phases.windUp = clampf(base.windUp + 0.16f * w, 0.05f, 0.80f);
    phases.strike = clampf(base.strike - 0.05f * w, 0.04f, 0.60f);
    phases.recovery = 1.0f - phases.windUp - phases.strike;
    if (phases.recovery < 0.05f) {  // keep the three a valid partition
        const float excess = 0.05f - phases.recovery;
        phases.windUp -= excess;
        phases.recovery = 0.05f;
    }
    // ...and takes longer overall, which is why a maul's damage lands late
    // because the motion says so rather than because a delay was tuned.
    phases.duration = (0.42f + 0.80f * w) * base.durationScale;
    return phases;
}

// ----------------------------------------------------------- sampling ------

void sampleUseArchetype(const UseMotion& motion, float t, Pose& out) {
    for (size_t j = 0; j < kJointCount; ++j) out.rotation[j] = Quat{};

    const UseMotion m = clampUseMotion(motion);
    const MotionKeys keys = archetypeKeys(m.archetype);
    const UsePhases phases = usePhases(m);
    const float time = clampf(t, 0.0f, 1.0f);

    // Which slice of its own timeline the motion is in.
    MotionKey key;
    if (time < phases.windUp) {
        key = lerpKey(keys.rest, keys.loaded, smooth(time / phases.windUp));
    } else if (time < phases.windUp + phases.strike) {
        // The strike accelerates rather than easing: a blow that eases in
        // reads as a reach, not a hit.
        const float u = (time - phases.windUp) / phases.strike;
        key = lerpKey(keys.loaded, keys.extended, u * u);
    } else {
        const float span = 1.0f - phases.windUp - phases.strike;
        const float u = span > 0.0f ? (time - phases.windUp - phases.strike) / span : 1.0f;
        key = lerpKey(keys.extended, keys.rest, smooth(clampf(u, 0.0f, 1.0f)));
    }

    // --- the item's own numbers shape the motion (P12) --------------------
    const float r = normalized(m.reach, kUseReachRange);
    const float w = normalized(m.weight, kUseWeightRange);
    // Reach: a longer item sweeps a wider arc and leans further in.
    const float arc = 0.88f + 0.24f * r;
    const float lean = key.lean * (0.90f + 0.35f * w) + 0.20f * r * (key.lean >= 0.0f ? 1.0f : 0.0f);
    // Weight: a heavy item drags the whole torso into the motion.
    const float twist = key.twist * (0.90f + 0.30f * w);

    // --- which side leads -------------------------------------------------
    // Angles are authored right-handed. `sign` is +1 for a right-side joint
    // and -1 for a left-side one, so one rule mirrors abduction and twist.
    const float leadSign = m.leftHanded ? -1.0f : 1.0f;
    const float offSign = -leadSign;
    const Joint leadUpper = m.leftHanded ? Joint::UpperArmL : Joint::UpperArmR;
    const Joint leadFore = m.leftHanded ? Joint::ForearmL : Joint::ForearmR;
    const Joint offUpper = m.leftHanded ? Joint::UpperArmR : Joint::UpperArmL;
    const Joint offFore = m.leftHanded ? Joint::ForearmR : Joint::ForearmL;

    const ArmKey leadArm = key.lead;
    // Grip decides the off hand: both hands on the haft, or a light brace.
    const ArmKey offArm = (m.grip == ItemGrip::TwoHanded && !inherentlyTwoArmed(m.archetype))
                              ? mirroredHaft(leadArm)
                              : key.off;

    out.rotation[idx(leadUpper)] = rotX(leadArm.pitch * arc) * rotZ(-leadArm.abduct * arc * leadSign);
    out.rotation[idx(leadFore)] = rotX(leadArm.elbow);
    out.rotation[idx(offUpper)] = rotX(offArm.pitch * arc) * rotZ(-offArm.abduct * arc * offSign);
    out.rotation[idx(offFore)] = rotX(offArm.elbow);

    // Two-handed counter-rotates the torso harder — the body drives the blow
    // rather than the arm (CHARACTERS.md §6.2).
    const float torso = usesBothArms(m) ? 1.35f : 1.0f;
    // Sign note, measured rather than assumed: in this rig the `*R` joints sit
    // at NEGATIVE local X while forward is -Z, so a twist authored "toward the
    // lead side" has to rotate the chest the opposite way from what the joint
    // NAME suggests. Getting this backwards drove the lead shoulder AWAY at
    // full extension — a thrust that retreated. thrust_reaches_further_forward
    // pins it down now.
    out.rotation[idx(Joint::Chest)] = rotY(twist * torso * leadSign) * rotX(lean);
    out.rotation[idx(Joint::Spine)] = rotY(twist * torso * 0.5f * leadSign) * rotX(lean * 0.4f);
    out.rotation[idx(Joint::Head)] = rotY(-twist * 0.25f * leadSign) * rotX(key.headPitch);
}

// -------------------------------------------------------------- masks ------

JointMask useArchetypeMask(const Skeleton& skeleton, const UseMotion& motion,
                           float spineFeather) {
    JointMask mask = maskNone();
    // The torso is always the action's — it is what makes the motion read as
    // whole-body rather than an arm waving.
    maskSetJoint(mask, Joint::Chest, 1.0f);
    maskSetJoint(mask, Joint::Neck, 1.0f);
    maskSetJoint(mask, Joint::Head, 1.0f);
    maskSetJoint(mask, Joint::Spine, spineFeather);

    const Joint leadUpper = motion.leftHanded ? Joint::UpperArmL : Joint::UpperArmR;
    maskSetChain(mask, skeleton, leadUpper, 1.0f);

    if (usesBothArms(motion)) {
        const Joint offUpper = motion.leftHanded ? Joint::UpperArmR : Joint::UpperArmL;
        maskSetChain(mask, skeleton, offUpper, 1.0f);
    }
    // ...and if it does NOT, the off arm is deliberately left out, so it goes
    // on swinging with the walk. That is what a person carrying a sword in
    // one hand actually does, and it costs nothing to get right.
    return mask;
}

}  // namespace mge
