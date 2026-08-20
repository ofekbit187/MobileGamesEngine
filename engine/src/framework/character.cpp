#include "mge/framework/character.h"

#include <cmath>

#include "mge/core/log.h"
#include "mge/framework/camera_controller.h"
#include "mge/framework/interaction.h"

namespace mge {

// ------------------------------------------------------------ intents (8.2) --

void applyIntent(World& world, EntityId entity, const CharacterIntent& intent) {
    TransformComponent* transform = world.transform(entity);
    MovementComponent* movement = world.movement(entity);
    if (transform == nullptr || movement == nullptr) return;
    transform->yaw += intent.lookDelta;
    Vec3 move = intent.move;
    move.y = 0;
    if (move.lengthSq() < 1e-8f || intent.speed <= 0.0f) {
        movement->velocity = {0, 0, 0};
        return;
    }
    movement->velocity = move * intent.speed;
    if (intent.faceMove) {
        const Vec3 dir = move.normalized();
        transform->yaw = std::atan2(dir.x, -dir.z);
    }
}

CharacterSystem::CharacterSystem(World& world, uint32_t capacity)
    : world_(world),
      capacity_(capacity),
      used_(capacity, 0),
      entities_(capacity),
      components_(capacity) {}

int32_t CharacterSystem::indexOf(EntityId entity) const {
    for (uint32_t i = 0; i < capacity_; ++i) {
        if (used_[i] && entities_[i] == entity) return static_cast<int32_t>(i);
    }
    return -1;
}

CharacterComponent* CharacterSystem::attach(EntityId entity) {
    if (!world_.entities().isAlive(entity)) return nullptr;
    if (CharacterComponent* existing = get(entity)) return existing;
    for (uint32_t i = 0; i < capacity_; ++i) {
        if (!used_[i]) {
            used_[i] = 1;
            entities_[i] = entity;
            components_[i] = CharacterComponent{};
            // Universal tier (CHARACTERS.md §3.1): every character can
            // interact, seeded here so there is no flag to forget.
            components_[i].actions.grant(actionInteract());
            ++liveCount_;
            return &components_[i];
        }
    }
    MGE_LOGW("character", "character capacity refused an attach");  // refuse, never grow
    return nullptr;
}

CharacterComponent* CharacterSystem::get(EntityId entity) {
    const int32_t index = indexOf(entity);
    return index >= 0 ? &components_[index] : nullptr;
}

const CharacterComponent* CharacterSystem::get(EntityId entity) const {
    const int32_t index = indexOf(entity);
    return index >= 0 ? &components_[index] : nullptr;
}

void CharacterSystem::detach(EntityId entity) {
    const int32_t index = indexOf(entity);
    if (index < 0) return;
    used_[index] = 0;
    --liveCount_;
}

Stance CharacterSystem::stanceBetween(EntityId a, EntityId b) const {
    const CharacterComponent* ca = get(a);
    const CharacterComponent* cb = get(b);
    if (ca == nullptr || cb == nullptr) return Stance::Neutral;
    return factions_.between(ca->faction, cb->faction);
}

bool CharacterSystem::damage(EntityId entity, float amount, DroppedLoot* outLoot) {
    CharacterComponent* character = get(entity);
    if (character == nullptr || !character->alive) return false;
    character->health -= amount;
    if (character->health > 0.0f) return false;

    // Death (task 8.4): everything carried drops as loot.
    character->health = 0.0f;
    character->alive = false;
    if (outLoot != nullptr) {
        outLoot->count = 0;
        for (uint32_t i = 0; i < character->inventory.size(); ++i) {
            outLoot->items[outLoot->count++] = *character->inventory.at(i);
        }
        for (const EquippedItem& equipped : character->equipment) {
            if (equipped.item.count > 0) outLoot->items[outLoot->count++] = equipped.item;
        }
    }
    while (character->inventory.size() > 0) character->inventory.removeAt(0, UINT32_MAX);
    for (EquippedItem& equipped : character->equipment) equipped = EquippedItem{};
    // The body stops: no more movement.
    if (MovementComponent* movement = world_.movement(entity)) {
        movement->velocity = {0, 0, 0};
    }
    return true;
}

bool CharacterSystem::equip(EntityId entity, uint32_t inventoryIndex, EquipSlot slot,
                            uint8_t layer) {
    CharacterComponent* character = get(entity);
    if (character == nullptr || !character->alive) return false;
    const Item* item = character->inventory.at(inventoryIndex);
    if (item == nullptr) return false;

    EquippedItem& target = character->equipment[static_cast<size_t>(slot)];
    const Item incoming = *item;
    character->inventory.removeAt(inventoryIndex, incoming.count);
    if (target.item.count > 0) {
        character->inventory.add(target.item);  // swap occupant back
    }
    target.item = incoming;
    target.layer = layer;
    target.sheathed = false;
    return true;
}

bool CharacterSystem::unequip(EntityId entity, EquipSlot slot) {
    CharacterComponent* character = get(entity);
    if (character == nullptr) return false;
    EquippedItem& equipped = character->equipment[static_cast<size_t>(slot)];
    if (equipped.item.count == 0) return false;
    if (!character->inventory.add(equipped.item)) return false;  // inventory full: refuse
    equipped = EquippedItem{};
    return true;
}

// --------------------------------------------- status effects (task 9.6) ---

bool CharacterSystem::addEffect(EntityId entity, const StatusEffect& effect) {
    CharacterComponent* character = get(entity);
    if (character == nullptr) return false;
    for (uint8_t i = 0; i < character->effectCount; ++i) {
        if (character->effects[i].id == effect.id) {
            character->effects[i] = effect;  // refresh
            return true;
        }
    }
    if (character->effectCount >= kMaxStatusEffects) return false;  // refuse (P1)
    character->effects[character->effectCount++] = effect;
    return true;
}

bool CharacterSystem::removeEffect(EntityId entity, uint64_t effectId) {
    CharacterComponent* character = get(entity);
    if (character == nullptr) return false;
    for (uint8_t i = 0; i < character->effectCount; ++i) {
        if (character->effects[i].id == effectId) {
            character->effects[i] = character->effects[--character->effectCount];
            return true;
        }
    }
    return false;
}

const StatusEffect* CharacterSystem::findEffect(EntityId entity, uint64_t effectId) const {
    const CharacterComponent* character = get(entity);
    if (character == nullptr) return nullptr;
    for (uint8_t i = 0; i < character->effectCount; ++i) {
        if (character->effects[i].id == effectId) return &character->effects[i];
    }
    return nullptr;
}

float CharacterSystem::sumMagnitude(EntityId entity, uint32_t tagMask) const {
    const CharacterComponent* character = get(entity);
    if (character == nullptr) return 0.0f;
    float sum = 0.0f;
    for (uint8_t i = 0; i < character->effectCount; ++i) {
        if ((character->effects[i].tags & tagMask) != 0) sum += character->effects[i].magnitude;
    }
    return sum;
}

void CharacterSystem::tickEffects(float dt) {
    for (uint32_t c = 0; c < capacity_; ++c) {
        if (!used_[c]) continue;
        CharacterComponent& character = components_[c];
        if (character.useCooldown > 0.0f) {
            character.useCooldown -= dt;
            if (character.useCooldown < 0.0f) character.useCooldown = 0.0f;
        }
        for (uint8_t i = 0; i < character.effectCount;) {
            StatusEffect& effect = character.effects[i];
            if (effect.duration >= 0.0f) {
                effect.duration -= dt;
                if (effect.duration <= 0.0f) {
                    character.effects[i] = character.effects[--character.effectCount];
                    continue;  // re-check the swapped-in slot
                }
            }
            ++i;
        }
    }
}

// -------------------------------------------------- persistence (task 8.9) --

void captureCharacter(const CharacterComponent& component, SavedCharacter& out) {
    out = SavedCharacter{};
    out.persistentId = component.persistentId;
    out.health = component.health;
    out.maxHealth = component.maxHealth;
    out.faction = component.faction;
    out.alive = component.alive ? 1 : 0;
    out.controller = static_cast<uint8_t>(component.controller);
    out.sightRange = component.sightRange;
    out.items.reserve(component.inventory.size());
    for (uint32_t i = 0; i < component.inventory.size(); ++i) {
        const Item& item = *component.inventory.at(i);
        SavedCharacterItem saved;
        saved.asset = item.asset;
        saved.count = item.count;
        for (int c = 0; c < 4; ++c) saved.color[c] = item.color[c];
        out.items.push_back(saved);
    }
    for (size_t s = 0; s < static_cast<size_t>(EquipSlot::Count); ++s) {
        const EquippedItem& equipped = component.equipment[s];
        out.equipment[s].item.asset = equipped.item.asset;
        out.equipment[s].item.count = equipped.item.count;
        for (int c = 0; c < 4; ++c) out.equipment[s].item.color[c] = equipped.item.color[c];
        out.equipment[s].layer = equipped.layer;
        out.equipment[s].sheathed = equipped.sheathed ? 1 : 0;
    }
    out.effects.reserve(component.effectCount);
    for (uint8_t i = 0; i < component.effectCount; ++i) {
        const StatusEffect& effect = component.effects[i];
        out.effects.push_back({effect.id, effect.tags, effect.magnitude, effect.duration});
    }
}

void applyCharacter(const SavedCharacter& saved, CharacterComponent& out) {
    // Name keys re-derive from the asset registry; identity is the asset id.
    const ItemCollection empty;
    out.inventory = empty;
    out.persistentId = saved.persistentId;
    out.health = saved.health;
    out.maxHealth = saved.maxHealth;
    out.faction = saved.faction;
    out.alive = saved.alive != 0;
    out.controller = static_cast<ControllerKind>(saved.controller);
    out.sightRange = saved.sightRange;
    out.aiIndex = UINT16_MAX;  // AI re-attaches after restore
    for (const SavedCharacterItem& item : saved.items) {
        out.inventory.add({item.asset, "", item.count,
                           {item.color[0], item.color[1], item.color[2], item.color[3]}});
    }
    for (size_t s = 0; s < static_cast<size_t>(EquipSlot::Count); ++s) {
        const SavedEquippedItem& savedSlot = saved.equipment[s];
        EquippedItem& slot = out.equipment[s];
        slot.item = {savedSlot.item.asset, "", savedSlot.item.count,
                     {savedSlot.item.color[0], savedSlot.item.color[1], savedSlot.item.color[2],
                      savedSlot.item.color[3]}};
        slot.layer = savedSlot.layer;
        slot.sheathed = savedSlot.sheathed != 0;
    }
    out.effectCount = 0;
    for (size_t i = 0; i < saved.effects.size() && i < kMaxStatusEffects; ++i) {
        const SavedStatusEffect& effect = saved.effects[i];
        out.effects[out.effectCount++] = {effect.id, effect.tags, effect.magnitude,
                                          effect.duration};
    }
}

void CharacterSystem::snapshot(std::vector<SavedCharacter>& out) const {
    for (uint32_t i = 0; i < capacity_; ++i) {
        if (!used_[i] || components_[i].persistentId == 0) continue;
        out.emplace_back();
        captureCharacter(components_[i], out.back());
    }
}

CharacterComponent* CharacterSystem::restore(EntityId entity, const SavedCharacter& saved) {
    CharacterComponent* component = attach(entity);
    if (component == nullptr) return nullptr;
    applyCharacter(saved, *component);
    return component;
}

void CharacterSystem::pruneDead(std::vector<SavedCharacter>* parked) {
    for (uint32_t i = 0; i < capacity_; ++i) {
        if (!used_[i] || world_.entities().isAlive(entities_[i])) continue;
        if (parked != nullptr && components_[i].persistentId != 0) {
            parked->emplace_back();
            captureCharacter(components_[i], parked->back());
        }
        used_[i] = 0;
        --liveCount_;
    }
}

EntityId CharacterSystem::findByPersistentId(uint32_t persistentId) const {
    if (persistentId != 0) {
        for (uint32_t i = 0; i < capacity_; ++i) {
            if (used_[i] && components_[i].persistentId == persistentId) return entities_[i];
        }
    }
    return kInvalidEntity;
}

// -------------------------------------------------------- actions (§3.1) ---

bool CharacterSystem::can(EntityId actor, ActionId action) const {
    const CharacterComponent* character = get(actor);
    return character != nullptr && character->actions.has(action);
}

bool CharacterSystem::steer(EntityId actor, const CharacterIntent& intent) {
    const CharacterComponent* character = get(actor);
    if (character == nullptr || !character->alive) return false;
    if (character->actions.has(actionWalk())) {
        applyIntent(world_, actor, intent);
        return true;
    }
    CharacterIntent rooted = intent;  // it may still turn, it just cannot go
    rooted.move = Vec3{};
    rooted.speed = 0;
    applyIntent(world_, actor, rooted);
    return false;
}

bool CharacterSystem::grant(EntityId actor, ActionId action) {
    CharacterComponent* character = get(actor);
    return character != nullptr && character->actions.grant(action);
}

void CharacterSystem::revoke(EntityId actor, ActionId action) {
    if (CharacterComponent* character = get(actor)) character->actions.revoke(action);
}

ActionResult CharacterSystem::perform(EntityId actor, const ActionRequest& request) {
    ActionResult result;
    result.id = request.id;

    CharacterComponent* character = get(actor);
    if (character == nullptr || !character->alive) {
        result.refusal = ActionRefusal::NoActor;
        return result;
    }
    // What you ARE decides whether the word is in your vocabulary at all.
    if (!character->actions.has(request.id)) {
        result.refusal = ActionRefusal::NotGranted;
        return result;
    }

    if (request.id == actionInteract()) {
        InteractionResult interaction;
        const EntityId target =
            request.target != kInvalidEntity ? request.target : focus(actor);
        if (target == kInvalidEntity) {
            result.refusal = ActionRefusal::NoTarget;
            return result;
        }
        interactWith(actor, target, &interaction);
        result.performed = interaction.handled;
        result.target = interaction.target;
        result.item = interaction.item;
        result.collectionId = interaction.collectionId;
        result.payload = interaction.payload;
        if (!interaction.handled) result.refusal = ActionRefusal::NoTarget;
        return result;
    }

    if (request.id == actionJump()) return performJump(actor, *character);
    if (request.id == actionUseHeld()) return performUseHeld(actor, *character);

    // Granted but not one the engine performs: the game defines it.
    result.refusal = ActionRefusal::NotPerformed;
    return result;
}

ActionResult CharacterSystem::performJump(EntityId actor, CharacterComponent& character) {
    ActionResult result;
    result.id = actionJump();
    result.animKey = "anim/jump";
    // You jump off the ground, not out of the air. Granted-but-refused: the
    // character can jump, just not this instant.
    if (!character.grounded) {
        result.refusal = ActionRefusal::NotGrounded;
        return result;
    }
    character.verticalVelocity = character.jumpSpeed;
    character.grounded = false;
    result.performed = true;
    result.amount = character.jumpSpeed;
    (void)actor;
    return result;
}

ActionResult CharacterSystem::performUseHeld(EntityId actor, CharacterComponent& character) {
    ActionResult result;
    result.id = actionUseHeld();

    const size_t held = static_cast<size_t>(EquipSlot::HeldMain);
    EquippedItem& slot = character.equipment[held];
    if (slot.item.count == 0 || itemUses_ == nullptr) {
        result.refusal = ActionRefusal::NothingHeld;
        return result;
    }
    const ItemUse* use = itemUses_->find(slot.item.asset);
    if (use == nullptr || use->kind == ItemUseKind::None) {
        // Holding something the game never said anything about: nothing
        // happens, and that is a complete answer.
        result.refusal = ActionRefusal::NothingHeld;
        return result;
    }
    if (character.useCooldown > 0.0f) {
        result.refusal = ActionRefusal::OnCooldown;
        return result;
    }

    result.useKind = use->kind;
    result.item = slot.item;
    result.animKey = use->animKey;
    // You cannot swing what is on your back (CHARACTERS.md §6.1).
    if (slot.sheathed) slot.sheathed = false;
    character.useCooldown = use->cooldown;

    switch (use->kind) {
        case ItemUseKind::Strike: {
            const EntityId victim = strikeTarget(actor, use->range);
            result.target = victim;
            if (victim != kInvalidEntity) {
                damage(victim, use->power);
                result.amount = use->power;
            }
            result.performed = true;  // the swing happened, hit or miss
            break;
        }
        case ItemUseKind::Consume: {
            if (use->power > 0.0f) {
                character.health += use->power;
                if (character.health > character.maxHealth) {
                    character.health = character.maxHealth;
                }
                result.amount = use->power;
            }
            if (use->effectId != 0) {
                StatusEffect effect;
                effect.id = use->effectId;
                effect.tags = use->effectTags;
                effect.magnitude = use->effectMagnitude;
                effect.duration = use->effectDuration;
                addEffect(actor, effect);
            }
            // Spent: one leaves the hand, and an emptied slot clears.
            if (slot.item.count > 1) {
                --slot.item.count;
            } else {
                slot.item = Item{};
            }
            result.performed = true;
            break;
        }
        case ItemUseKind::Toggle: {
            slot.active = !slot.active;
            result.toggledOn = slot.active;
            result.performed = true;
            break;
        }
        case ItemUseKind::Launch:
        case ItemUseKind::Custom:
            // Reported, not performed: there are no projectiles yet, and a
            // custom meaning is the game's to give (as status effects are).
            result.payload = use->payload;
            result.amount = use->range;
            result.refusal = ActionRefusal::NotPerformed;
            break;
        case ItemUseKind::None:
            result.refusal = ActionRefusal::NothingHeld;
            break;
    }
    return result;
}

EntityId CharacterSystem::strikeTarget(EntityId actor, float reach) const {
    const TransformComponent* actorTransform = const_cast<World&>(world_).transform(actor);
    if (actorTransform == nullptr) return kInvalidEntity;
    const Vec3 forward = yawForward(actorTransform->yaw);

    EntityId best = kInvalidEntity;
    float bestScore = -1.0f;
    for (uint32_t i = 0; i < capacity_; ++i) {
        if (!used_[i] || entities_[i] == actor) continue;
        if (!components_[i].alive) continue;
        const TransformComponent* transform =
            const_cast<World&>(world_).transform(entities_[i]);
        if (transform == nullptr) continue;
        Vec3 delta = transform->position - actorTransform->position;
        delta.y = 0;
        const float distance = delta.length();
        if (distance > reach) continue;
        // In front of the swing, not behind it.
        const float facing = distance > 0.001f ? delta.normalized().dot(forward) : 1.0f;
        if (facing < 0.35f) continue;
        const float score = facing * 2.0f - distance * 0.1f;
        if (score > bestScore) {
            bestScore = score;
            best = entities_[i];
        }
    }
    return best;
}

// ---------------------------------------------------- locomotion (12.3) ---
// Run once per fixed step AFTER World::step has integrated velocities: the
// horizontal delta is what the world already applied, the vertical one is
// this system's own (gravity, or a jump's launch speed).

void CharacterSystem::stepLocomotion(float dt) {
    if (collision_ == nullptr) return;
    for (uint32_t i = 0; i < capacity_; ++i) {
        if (!used_[i]) continue;
        CharacterComponent& character = components_[i];
        TransformComponent* transform = world_.transform(entities_[i]);
        if (transform == nullptr) continue;

        Vec3 delta = transform->position - transform->prevPosition;
        delta.y = 0;

        character.verticalVelocity += gravity_ * dt;
        float vertical = character.verticalVelocity * dt;
        // A grounded character with no upward intent keeps the walker's
        // behaviour — feet following the surface — instead of accumulating
        // downward velocity while standing still.
        if (character.grounded && character.verticalVelocity <= 0.0f) {
            character.verticalVelocity = 0.0f;
            vertical = 0.0f;
        }
        delta.y = vertical;

        const MoveResult move =
            collision_->moveCharacter(transform->prevPosition, character.shape, delta);
        transform->position = move.position;
        if (move.hitCeiling && character.verticalVelocity > 0.0f) {
            character.verticalVelocity = 0.0f;  // head hit: start falling
        }
        if (move.grounded) {
            if (character.verticalVelocity < 0.0f) character.verticalVelocity = 0.0f;
            character.grounded = true;
        } else {
            character.grounded = false;
        }
    }
}

// ---------------------------------------------------- interaction (P9) ---
// Every character can interact (owner ruling, Phase 11). There is deliberately
// no capability flag to consult here: if you are a character, these are yours.

EntityId CharacterSystem::focus(EntityId actor) const {
    if (interactions_ == nullptr) return kInvalidEntity;
    const CharacterComponent* character = get(actor);
    if (character == nullptr || !character->alive) return kInvalidEntity;
    return interactions_->focus(actor);
}

bool CharacterSystem::interact(EntityId actor, InteractionResult* out) {
    return interactWith(actor, focus(actor), out);
}

bool CharacterSystem::interactWith(EntityId actor, EntityId target, InteractionResult* out) {
    if (interactions_ == nullptr) return false;
    const InteractionResult result = interactions_->interactWith(actor, target);
    if (out != nullptr) *out = result;
    return result.handled;
}

bool CharacterSystem::setSheathed(EntityId entity, bool sheathed) {
    CharacterComponent* character = get(entity);
    if (character == nullptr) return false;
    EquippedItem& held = character->equipment[static_cast<size_t>(EquipSlot::HeldMain)];
    if (held.item.count == 0) return false;
    held.sheathed = sheathed;
    return true;
}

}  // namespace mge
