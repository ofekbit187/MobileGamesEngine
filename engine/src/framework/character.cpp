#include "mge/framework/character.h"

#include <cmath>

#include "mge/core/log.h"

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

bool CharacterSystem::setSheathed(EntityId entity, bool sheathed) {
    CharacterComponent* character = get(entity);
    if (character == nullptr) return false;
    EquippedItem& held = character->equipment[static_cast<size_t>(EquipSlot::HeldMain)];
    if (held.item.count == 0) return false;
    held.sheathed = sheathed;
    return true;
}

}  // namespace mge
