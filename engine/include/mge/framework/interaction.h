#pragma once

// Interaction v1 (Phase 11, tasks 11.5–11.7): acting on the world.
//
// An interactable is a world entity plus what it offers: a localized prompt
// and a verb. The system answers one question every frame — "what is this
// character about to act on?" — and performs the built-in verbs, reporting
// what happened so the game layer can react. Focus needs the collision world
// for line of sight: you cannot use a chest through a wall.
//
// EVERY CHARACTER CAN INTERACT (owner ruling, P9). Nothing here knows what a
// player is: `focus` and `interact` take the acting character, so a villager
// picking an apple off the ground runs the same code as your tap. Characters
// are equally valid targets — the player can be the one spoken to.

#include <cstdint>
#include <vector>

#include "mge/framework/character.h"
#include "mge/framework/collision.h"
#include "mge/framework/items.h"

namespace mge {

enum class InteractionKind : uint8_t {
    None = 0,
    PickUp,     // the item moves into the actor's inventory; the entity goes
    Container,  // opens a registered item collection (chest, barrel, stock)
    Talk,       // the person says a line (voice take + subtitle)
    Custom,     // the game defines the meaning; the engine only reports it
};

struct InteractableComponent {
    InteractionKind kind = InteractionKind::None;
    // Localization key for the on-screen prompt ("prompt.take", "prompt.open").
    const char* promptKey = "";
    float range = 2.2f;
    // Facing tolerance: the dot between the actor's forward and the direction
    // to the target must exceed this (0.5 ≈ a 60° cone in front).
    float facingDot = 0.4f;
    bool enabled = true;

    Item item;             // PickUp: what is taken
    uint64_t collectionId = 0;  // Container: registered collection to open
    uint32_t payload = 0;  // Talk: line index; Custom: game-defined
};

class InteractionSystem {
public:
    InteractionSystem(World& world, CharacterSystem& characters, uint32_t capacity = 256);

    InteractableComponent* attach(EntityId entity, const InteractableComponent& interactable);
    InteractableComponent* get(EntityId entity);
    const InteractableComponent* get(EntityId entity) const;
    void detach(EntityId entity);
    uint32_t count() const { return liveCount_; }

    // Line of sight (optional): with a collision world set, a target behind a
    // wall is not focusable.
    void setCollisionWorld(const CollisionWorld* collision) { collision_ = collision; }

    // What `actor` is about to act on: in range, inside the facing cone, and
    // visible. Nearest-in-front wins. kInvalidEntity when nothing qualifies.
    EntityId focus(EntityId actor) const;

    // "What of this kind is around me?" — position-based, no facing or line
    // of sight, for characters deciding what to walk toward (AI gathering,
    // quest markers, spawn placement).
    EntityId nearestOfKind(const Vec3& from, float radius, InteractionKind kind) const;

    struct Result {
        bool handled = false;
        InteractionKind kind = InteractionKind::None;
        EntityId target = kInvalidEntity;
        uint64_t collectionId = 0;
        uint32_t payload = 0;
        Item item;  // what was picked up, when kind == PickUp
    };

    // Acts on the focused target (or on `target` explicitly). PickUp is
    // performed by the engine (inventory + despawn); Container and Talk are
    // reported for the game layer to present.
    Result interact(EntityId actor);
    Result interactWith(EntityId actor, EntityId target);

private:
    int32_t indexOf(EntityId entity) const;

    World& world_;
    CharacterSystem& characters_;
    const CollisionWorld* collision_ = nullptr;
    uint32_t capacity_;
    uint32_t liveCount_ = 0;
    std::vector<uint8_t> used_;
    std::vector<EntityId> entities_;
    std::vector<InteractableComponent> components_;
};

}  // namespace mge
