#include "mge/framework/character.h"

#include "mge/core/log.h"

namespace mge {

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

bool CharacterSystem::setSheathed(EntityId entity, bool sheathed) {
    CharacterComponent* character = get(entity);
    if (character == nullptr) return false;
    EquippedItem& held = character->equipment[static_cast<size_t>(EquipSlot::HeldMain)];
    if (held.item.count == 0) return false;
    held.sheathed = sheathed;
    return true;
}

}  // namespace mge
