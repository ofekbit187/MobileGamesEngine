#pragma once

// How an item is used — the vocabulary (CHARACTERS.md §6.2, P12, ADR 0017).
//
// This is DESIGN LANGUAGE, not animation. An item declares what kind of use
// it is; the animation area implements what that looks like. The two live
// apart on purpose, and the split follows the dependency direction the engine
// already has: `character/` includes `framework/`, never the reverse. Putting
// these enums in `character/` and reaching for them from `items.h` would
// close that into a cycle at the directory level (ADR 0017 measured it).
//
// So: the vocabulary lives here, with the item data that declares it. The
// motion that realizes it — `UseMotion`, `sampleUseArchetype`, `UsePlayer` —
// lives in `character/use_archetypes.h`, which includes this file.
//
// Owned by gameplay mechanics. Added by the animation session under ADR 0017
// Ruling 3 (an area with no live session is not a deadlock): the change was
// fully specified by that ruling, is recorded in it, and does not extend
// beyond the four fields it names.

#include <cstddef>
#include <cstdint>

namespace mge {

// The nine kinds of use. Extending this set is a DESIGN change, not a content
// change — the whole point of P12 is that a new item picks from this list
// rather than bringing an animation of its own.
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

// Human-readable name, for logs, tools and data files.
const char* useArchetypeName(UseArchetype archetype);

// How the item is held (CHARACTERS.md §6.1). `Versatile` animates one-handed;
// a caller that knows the item is currently held in both hands says so.
enum class ItemGrip : uint8_t {
    OneHanded = 0,
    TwoHanded,
    Versatile,
};

const char* itemGripName(ItemGrip grip);

}  // namespace mge
