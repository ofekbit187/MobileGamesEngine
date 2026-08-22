#pragma once

// Actions (Phase 12, CHARACTERS.md §3.1): what a character CAN do.
//
// An action is a named capability, not a behaviour. A character's action set
// is its vocabulary; deciding WHEN to use a word from it belongs to whoever
// steers the character — a tap, or an AI, or one day an occupation and a
// schedule. The set comes from what the character IS, in two tiers:
//
//   universal  — seeded on every character at creation (interact). This is
//                why "every character can interact" needs no flag: there is
//                nowhere to write one down.
//   body       — granted by the body definition (a humanoid walks, jumps and
//                uses what is in its hands; a creature declares its own).
//
// Two different failures, deliberately distinguished: an action that is NOT
// GRANTED is about what you are; one that is REFUSED is about the moment
// (jumping in mid-air, an empty hand, a cooldown still running).

#include <cstdint>

#include "mge/core/math.h"
#include "mge/framework/asset_registry.h"
#include "mge/framework/entity.h"
#include "mge/framework/items.h"

namespace mge {

// Same FNV-1a id space as assets, so action names are stable across builds
// and cheap to compare. Games mint their own with actionId("action/...").
using ActionId = uint64_t;
constexpr ActionId kInvalidAction = 0;

constexpr ActionId actionId(const char* name) { return assetIdFromName(name); }

// --- universal (every character, always) ---
inline ActionId actionInteract() { return actionId("action/interact"); }
// --- body-level (granted by the body that has them) ---
inline ActionId actionWalk() { return actionId("action/walk"); }
inline ActionId actionJump() { return actionId("action/jump"); }
inline ActionId actionUseHeld() { return actionId("action/use_held"); }

// A character's vocabulary. Fixed capacity, refuses past the cap (P1).
class ActionSet {
public:
    static constexpr uint32_t kCapacity = 16;

    bool has(ActionId id) const {
        for (uint32_t i = 0; i < count_; ++i) {
            if (ids_[i] == id) return true;
        }
        return false;
    }

    bool grant(ActionId id) {
        if (id == kInvalidAction) return false;
        if (has(id)) return true;
        if (count_ >= kCapacity) return false;  // cap refuses, never grows
        ids_[count_++] = id;
        return true;
    }

    void revoke(ActionId id) {
        for (uint32_t i = 0; i < count_; ++i) {
            if (ids_[i] != id) continue;
            for (uint32_t j = i; j + 1 < count_; ++j) ids_[j] = ids_[j + 1];
            --count_;
            return;
        }
    }

    void clear() { count_ = 0; }
    uint32_t size() const { return count_; }
    ActionId at(uint32_t index) const { return index < count_ ? ids_[index] : kInvalidAction; }

private:
    ActionId ids_[kCapacity] = {};
    uint32_t count_ = 0;
};

// What a character was asked to do. Most actions need nothing but the id;
// the rest read the fields that concern them.
struct ActionRequest {
    ActionId id = kInvalidAction;
    EntityId target = kInvalidEntity;  // interact: act on this instead of the focus
    Vec3 direction{};                  // reserved: aimed actions
    float magnitude = 1.0f;            // reserved: throttle / charge
};

// Why an action did not happen. "NotGranted" is about what the character is;
// everything after it is about this moment.
enum class ActionRefusal : uint8_t {
    None = 0,
    NotGranted,     // this character does not have the action at all
    NoActor,        // not a character / not alive
    NotGrounded,    // jump: already in the air
    NothingHeld,    // use_held: empty hand, or an item with no use defined
    OnCooldown,     // the last use has not finished
    NoTarget,       // interact: nothing focused
    NotPerformed,   // the engine reports it; the game performs it (launch/custom)
};

// What happened. `performed` is the engine's own answer: it changed the world.
// A reported-but-not-performed action (launch, custom) comes back with
// performed = false and refusal = NotPerformed, plus everything the game
// needs to do it itself.
struct ActionResult {
    bool performed = false;
    ActionId id = kInvalidAction;
    ActionRefusal refusal = ActionRefusal::None;

    ItemUseKind useKind = ItemUseKind::None;  // use_held: what the item meant
    EntityId target = kInvalidEntity;         // who/what it landed on
    Item item;                                // the item involved, if any
    float amount = 0;                         // damage dealt, healing given
    // The blow has not happened YET (ADR 0021). A Strike with a strike timing
    // installed starts the swing here and lands it at the motion's strike
    // moment, so `target` and `amount` are empty on purpose — poll
    // CharacterSystem::consumeStrike for the outcome. Without this flag a
    // caller cannot tell "not yet" from "missed", and they mean opposite
    // things.
    bool pending = false;
    bool toggledOn = false;                   // toggle: the new state
    uint32_t payload = 0;                     // custom/launch: game-defined
    uint64_t collectionId = 0;                // interact: container opened
    const char* animKey = "";                 // the cue the presentation plays
};

}  // namespace mge
