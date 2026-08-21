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

namespace mge {

// The nine kinds of use (CHARACTERS.md §6.2). Extending this set is a design
// change, not a content change — the whole point is that items pick from it
// rather than bringing their own motion.
enum class UseArchetype : uint8_t {
    Swing = 0,  // arcing horizontal/diagonal melee — sword, axe, club, staff
    Thrust,     // straight-line stab — spear, dagger, rapier
    Chop,       // overhead descending — axe, pick, maul
    Work,       // repeated, sustained tool motion — hammer, saw, shovel
    Draw,       // charge-and-release — bow, sling
    Aim,        // raise, steady, release — crossbow
    Raise,      // lift and hold a pose — torch, lantern, shield, banner
    Consume,    // bring to the mouth — food, drink, potion
    Gesture,    // free-hand motion, no object — spellcasting, pointing
    Count,
};
constexpr size_t kUseArchetypeCount = static_cast<size_t>(UseArchetype::Count);

// Human-readable name, for logs and demo sheets.
const char* useArchetypeName(UseArchetype archetype);

// How the item is held (CHARACTERS.md §6.1). Grip is item data; this is the
// animation's view of it. `Versatile` animates as one-handed — a caller that
// knows the item is currently held in both hands passes `TwoHanded`.
enum class ItemGrip : uint8_t {
    OneHanded = 0,
    TwoHanded,
    Versatile,
};

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

}  // namespace mge
