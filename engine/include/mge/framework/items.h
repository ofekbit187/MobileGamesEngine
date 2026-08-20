#pragma once

// Item collections (task 5.9 data side, P6): a registered item collection —
// the player's backpack, a chest's contents, a merchant's stock — is game
// data the UI binds to by id. Fixed capacity, allocation-free mutation (P1).

#include <cstdint>

#include "mge/framework/asset_registry.h"

namespace mge {

struct Item {
    AssetId asset = kInvalidAsset;   // for icons / live-object views
    const char* nameKey = "";        // localization key (P11)
    uint32_t count = 0;
    float color[4] = {1, 1, 1, 1};   // v1 icon tint until item icons land
};

// ----------------------------------------------- what using an item means --
// Dictation 6: `action/use_held` is ONE action whose meaning is entirely the
// item's. The character knows how to use what it is holding; the item says
// what that is. Adding a new kind of tool is a data change (P5).

enum class ItemUseKind : uint8_t {
    None = 0,
    Strike,   // reach in front of the actor; the first character hit takes damage
    Consume,  // the item is spent: heal and/or apply a status effect
    Toggle,   // the held item flips on/off (a lit torch, a raised shield)
    Launch,   // REPORTED, not performed — there are no projectiles yet
    Custom,   // the game defines the meaning entirely (as status effects do)
};

struct ItemUse {
    ItemUseKind kind = ItemUseKind::None;
    float cooldown = 0.5f;    // seconds before this character can use again
    float range = 2.0f;       // Strike reach / Launch distance
    float power = 0.0f;       // Strike damage / Consume healing
    // Consume: the status effect left behind (id 0 = none). Kept as plain
    // fields so items.h stays independent of the character header.
    uint64_t effectId = 0;
    uint32_t effectTags = 0;
    float effectMagnitude = 0;
    float effectDuration = -1;
    uint32_t payload = 0;      // Custom / Launch: game-defined
    const char* animKey = "";  // presentation cue
};

// Item definitions keyed by asset id: the held item's identity is what the
// use action looks up. Fixed capacity, refuses at the cap (P1).
class ItemUseRegistry {
public:
    static constexpr size_t kMaxItems = 64;

    bool define(AssetId asset, const ItemUse& use) {
        if (asset == kInvalidAsset) return false;
        for (size_t i = 0; i < count_; ++i) {
            if (assets_[i] == asset) {
                uses_[i] = use;
                return true;
            }
        }
        if (count_ >= kMaxItems) return false;
        assets_[count_] = asset;
        uses_[count_] = use;
        ++count_;
        return true;
    }
    bool define(const char* name, const ItemUse& use) {
        return define(assetIdFromName(name), use);
    }

    const ItemUse* find(AssetId asset) const {
        for (size_t i = 0; i < count_; ++i) {
            if (assets_[i] == asset) return &uses_[i];
        }
        return nullptr;  // "no defined use" is a normal answer: nothing happens
    }

    size_t size() const { return count_; }

private:
    AssetId assets_[kMaxItems] = {};
    ItemUse uses_[kMaxItems] = {};
    size_t count_ = 0;
};

class ItemCollection {
public:
    static constexpr uint32_t kCapacity = 48;

    bool add(const Item& item) {
        // Stack onto an existing entry of the same asset.
        for (uint32_t i = 0; i < count_; ++i) {
            if (items_[i].asset == item.asset && item.asset != kInvalidAsset) {
                items_[i].count += item.count;
                return true;
            }
        }
        if (count_ >= kCapacity) return false;  // full: refuse (P1)
        items_[count_++] = item;
        return true;
    }

    bool removeAt(uint32_t index, uint32_t amount = 1) {
        if (index >= count_) return false;
        if (items_[index].count > amount) {
            items_[index].count -= amount;
            return true;
        }
        for (uint32_t i = index; i + 1 < count_; ++i) items_[i] = items_[i + 1];
        --count_;
        return true;
    }

    const Item* at(uint32_t index) const { return index < count_ ? &items_[index] : nullptr; }
    uint32_t size() const { return count_; }
    uint32_t capacity() const { return kCapacity; }

private:
    Item items_[kCapacity];
    uint32_t count_ = 0;
};

// Registry: UI layouts reference collections by stable id (same FNV-1a id
// space as assets). "Unknown id" is a normal answer.
class CollectionRegistry {
public:
    static constexpr size_t kMaxCollections = 32;

    bool add(const char* name, ItemCollection* collection) {
        if (count_ >= kMaxCollections) return false;
        ids_[count_] = assetIdFromName(name);
        collections_[count_] = collection;
        ++count_;
        return true;
    }

    ItemCollection* find(uint64_t id) const {
        for (size_t i = 0; i < count_; ++i) {
            if (ids_[i] == id) return collections_[i];
        }
        return nullptr;
    }
    ItemCollection* find(const char* name) const { return find(assetIdFromName(name)); }

private:
    uint64_t ids_[kMaxCollections] = {};
    ItemCollection* collections_[kMaxCollections] = {};
    size_t count_ = 0;
};

}  // namespace mge
