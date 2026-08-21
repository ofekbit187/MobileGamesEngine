#pragma once

// Use archetypes (task 14.2, CHARACTERS.md §6.2, P12 / ADR 0008 amendment).
//
// THE LAW THIS IMPLEMENTS. *"A new weapon must not mean new animation work"*
// (P12, owner Dictation 7). An item does not carry an animation; it declares
// what KIND of use it is, and the engine animates that kind, shaped by the
// item's own numbers. Shipping a new sword is: model it, declare `Swing`,
// give it a reach and a weight. No animation authoring, no engine change.
//
// So the nine archetypes below are not nine clips. They are nine procedural
// SHAPES, and the item supplies the dimensions:
//
//   grip   — which arms participate and how hard the torso counter-rotates.
//            This is the mask as much as the motion: a one-handed use leaves
//            the off arm to locomotion, so it keeps swinging with the walk.
//   reach  — the item's length. Sets arc radius and how far the body leans in.
//   weight — sets wind-up / strike / recovery timing, in fractions AND in
//            absolute seconds. A war-hammer and a dagger are the same `Swing`
//            at different speeds and read as completely different weapons.
//
// These produce an ordinary upper-body `Pose` for the 14.1 layering path, so
// an archetype plays OVER locomotion: one pose evaluation, one palette, one
// skin pass, no allocation.
//
// SEAM (AGENTS.md §4, "Item use"). `ItemUse` and `ItemUseRegistry` live in
// `framework/items.h` and belong to gameplay. `UseMotion` below is the
// ANIMATION side of that seam — the parameter block the engine animates
// from. Putting `archetype`, `reach` and `weight` onto `ItemUse` itself is
// task 14.5 and is a seam change requiring an architect ruling; it is raised
// in `docs/status/animation.md`, not taken here.

#include <cstdint>

#include "mge/character/animation.h"
#include "mge/character/humanoid.h"
#include "mge/framework/items.h"
#include "mge/framework/use_archetype.h"

namespace mge {

// `UseArchetype`, `kUseArchetypeCount`, `useArchetypeName` and `ItemGrip` now
// live in `framework/use_archetype.h` and are gameplay's (ADR 0017): the
// archetype vocabulary is design language that an ITEM declares, and this
// area implements it. They are included above, so every name below still
// resolves for anyone including this header.

// Everything an item declares about using it. Four numbers and an enum —
// that is the whole cost of a new weapon.
struct UseMotion {
    UseArchetype archetype = UseArchetype::Swing;
    ItemGrip grip = ItemGrip::OneHanded;
    // Metres, tip to grip. A dagger is ~0.3, a longsword ~1.0, a spear ~2.4.
    // Out-of-range values are CLAMPED, never rejected: item data is content,
    // and content must never be able to produce a motion the rig cannot do
    // (the same stance ADR 0009 takes for body variants).
    float reach = 1.0f;
    // Kilograms-ish. An apple is ~0.2, a sword ~1.4, a maul ~8.
    float weight = 1.4f;
    // Which side leads. Mirrored for left-handed characters (§6.2).
    bool leftHanded = false;
};

extern const VariantRange kUseReachRange;   // 0.20 .. 2.60 m
extern const VariantRange kUseWeightRange;  // 0.10 .. 8.00 kg

UseMotion clampUseMotion(const UseMotion& motion);

// The bridge across the seam (14.5): what an item declares, as something the
// engine can animate. This is the whole of "no engineer in the content loop"
// — an item is four numbers, and this turns them into a motion.
//
// `leftHanded` is the CHARACTER's handedness, not the item's, so it is passed
// in rather than read from the item.
UseMotion motionFromItemUse(const ItemUse& use, bool leftHanded = false);

// True when the item has opted into a bespoke clip instead of the archetype
// (CHARACTERS.md §6.2). Almost nothing should: it is the hero-item luxury,
// and the engine animates the archetype for everything else.
bool usesBespokeClip(const ItemUse& use);

// ------------------------------------------------ the phase timeline -------
//
// Wind-up / strike / recovery as fractions of the motion's own timeline,
// plus how long the whole thing takes. Weight drives both: a heavy item
// spends proportionally longer loading AND takes longer overall, so its
// damage moment lands late *because the motion says so* rather than because
// two numbers were tuned to agree.
//
// (Task 14.3 makes these addressable by gameplay — hanging the Phase 12
// action model's damage moment on `strike`. 14.2 defines them.)
struct UsePhases {
    float windUp = 0.35f;    // fraction of the timeline, [0,1)
    float strike = 0.20f;    // fraction; the committed part of the motion
    float recovery = 0.45f;  // fraction; windUp + strike + recovery == 1
    float duration = 1.0f;   // seconds for the whole motion
};

UsePhases usePhases(const UseMotion& motion);

// --------------------------------------------------------- sampling --------

// The archetype's pose at normalized time t in [0, 1]. Writes the joints the
// grip says participate and leaves every other joint identity, so the result
// is an overlay for `LayeredPose` rather than a whole-body pose.
//
// Allocation-free and stateless: same inputs, same pose, every time.
void sampleUseArchetype(const UseMotion& motion, float t, Pose& out);

// The mask that goes with it — because grip decides which arms participate,
// the mask IS part of the archetype rather than a caller's guess.
//
//   OneHanded  lead arm + chest/neck/head, spine feathered. The OFF ARM IS
//              LEFT TO LOCOMOTION, so it keeps swinging with the walk — which
//              is what a person actually does carrying a sword.
//   TwoHanded  both arms + torso.
//   Draw/Aim   both arms whatever the grip says: a bow is two-armed by
//              construction (one holds, one draws).
JointMask useArchetypeMask(const Skeleton& skeleton, const UseMotion& motion,
                           float spineFeather = 0.5f);

// ------------------------------------------- playing one (14.3, 14.4) -----
//
// The runtime driver: advances an archetype in seconds, says which slice of
// its timeline it is in, tells gameplay the instant the blow lands, and
// blends out instead of snapping when something interrupts it.
//
// Allocation-free and self-contained: one `UseMotion`, a few floats, no heap
// and no pointers. Small enough to live on a character.

enum class UsePhase : uint8_t {
    Idle = 0,   // nothing playing
    WindUp,     // loading
    Strike,     // committed
    Recovery,   // returning to rest
};

const char* usePhaseName(UsePhase phase);

class UsePlayer {
public:
    // Begin a motion. Restarting while one is playing replaces it (the caller
    // decides whether that is allowed — cooldown is gameplay's, not ours).
    void start(const UseMotion& motion, float blendInSeconds = 0.08f);

    // Advance by dt. Returns TRUE on the single frame the strike moment is
    // crossed — the edge gameplay hangs damage on (14.3), replacing the tuned
    // delay the Phase 12 action model uses today. It fires exactly once even
    // if a long dt steps clean over the moment, and never fires for a motion
    // that was interrupted before reaching it.
    bool update(float dt);

    // Blend out over `seconds` instead of stopping dead (14.4). Called when
    // the character is hit, staggered or killed mid-action. An interrupt
    // already blending out is not restarted, so repeated hits do not stall
    // the fade.
    void interrupt(float seconds = 0.15f);

    bool active() const { return state_ != State::Idle; }
    bool interrupted() const { return state_ == State::BlendingOut; }
    UsePhase phase() const;

    // Position within the whole motion, and within the current phase — both
    // in [0, 1]. The second is what an overlay or a sound cue wants.
    float normalizedTime() const;
    float phaseFraction() const;

    // The layer weight to hand LayeredPose::addLayer. Ramps in at the start,
    // ramps out on interrupt, and is 1 in between — so the caller never has
    // to know which of those is happening.
    float weight() const { return weight_; }

    // Seconds from the start of the motion to the damage instant, and from
    // NOW to it (negative once passed). `strikeMoment` is the END of the
    // strike phase: the point of full extension, where the blow has landed.
    float strikeMoment() const;
    float timeUntilStrike() const;
    float elapsed() const { return time_; }

    const UseMotion& motion() const { return motion_; }
    const UsePhases& phases() const { return phases_; }

    // The pose to layer, with useArchetypeMask(skeleton, motion()).
    void samplePose(Pose& out) const;

private:
    enum class State : uint8_t { Idle, Playing, BlendingOut };

    UseMotion motion_;
    UsePhases phases_;
    State state_ = State::Idle;
    float time_ = 0.0f;        // seconds into the motion
    float weight_ = 0.0f;      // current layer gain
    float blendIn_ = 0.08f;    // seconds
    float blendOutRate_ = 0.0f;  // weight per second
    bool strikeFired_ = false;
};

}  // namespace mge
