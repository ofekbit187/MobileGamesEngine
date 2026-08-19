#pragma once

// The character system (Phase 8, docs/CHARACTERS.md). P9: the player is just
// a character — one component set serves every acting being, and the only
// player/NPC difference is which controller steers it. Universal mechanisms
// live here (health/mortality, factions, inventory, equipment slots);
// humanoid-only machinery (body, variants, wearables) lives in mge/character/.

#include <cstdint>
#include <vector>

#include "mge/framework/items.h"
#include "mge/framework/world.h"

namespace mge {

// ---------------------------------------------------------------- factions --

using FactionId = uint8_t;
constexpr FactionId kMaxFactions = 16;

enum class Stance : uint8_t { Neutral = 0, Ally, Enemy };

// Symmetric-by-default stance table (task 8.5). Same-faction is Ally.
class FactionTable {
public:
    FactionTable() {
        for (auto& row : stances_) {
            for (Stance& s : row) s = Stance::Neutral;
        }
        for (FactionId f = 0; f < kMaxFactions; ++f) stances_[f][f] = Stance::Ally;
    }

    void set(FactionId a, FactionId b, Stance stance) {
        stances_[a][b] = stance;
        stances_[b][a] = stance;
    }
    Stance between(FactionId a, FactionId b) const { return stances_[a][b]; }

private:
    Stance stances_[kMaxFactions][kMaxFactions];
};

// ------------------------------------------------------------- controllers --

enum class ControllerKind : uint8_t {
    None = 0,   // scenery-like character (dialogue statue, etc.)
    Player,     // steered by touch intents via the engine
    Ai,         // steered by the AI system
};

// ------------------------------- status effects (task 9.6, PEOPLE.md §4) ---
// Deliberately abstract: one record shape is the hook a lot of systems hang
// from — buffs, diseases, blessings, "wanted by the guards", literacy — and
// skills/education are the special permanent kind whose magnitude is the
// rank. Ruled split: the engine owns storage, tags, durations, queries, and
// stat-modifier hooks; games define what an effect MEANS in data.

constexpr uint32_t kEffectTagSkill = 1u << 0;   // skills/education (permanent, rank)
constexpr uint32_t kEffectTagSpeed = 1u << 1;   // movement-speed modifier hook
constexpr uint32_t kEffectTagHealth = 1u << 2;  // max-health modifier hook
// bits 8+ are game-defined.

struct StatusEffect {
    uint64_t id = 0;      // FNV name id ("skill/smithing", "effect/blessed")
    uint32_t tags = 0;
    float magnitude = 0;  // rank / strength / modifier value
    float duration = -1;  // seconds remaining; < 0 = permanent
};
constexpr size_t kMaxStatusEffects = 16;

// -------------------------------------------------------------- character ---

// Equipment: named slots per body definition (task 8.7). The humanoid set is
// the shipped default; creatures declare their own subsets.
enum class EquipSlot : uint8_t {
    HeadHair = 0,  // hairstyles are wearables (CHARACTERS.md §5.3)
    HeadTop,
    Torso,
    Legs,
    Feet,
    HeldMain,
    HeldOff,
    Count,
};

struct EquippedItem {
    Item item;            // count 0 = slot empty
    uint8_t layer = 1;    // 0 base / 1 mid / 2 outer (wearable slots)
    bool sheathed = false;  // held slots only
};

struct CharacterComponent {
    float health = 1.0f;
    float maxHealth = 1.0f;
    FactionId faction = 0;
    ControllerKind controller = ControllerKind::None;
    bool alive = true;
    // Perception (task 8.7 hooks): how far this character senses others.
    float sightRange = 15.0f;
    // AI bookkeeping (indices into the AI system's state storage).
    uint16_t aiIndex = UINT16_MAX;
    // Stable game-assigned identity for save files and streaming (task 8.9).
    // 0 = transient: not persisted, state dies with the entity.
    uint32_t persistentId = 0;

    ItemCollection inventory;                      // every character has one (P9)
    EquippedItem equipment[static_cast<size_t>(EquipSlot::Count)];
    // Status effects (task 9.6): every character carries the list.
    StatusEffect effects[kMaxStatusEffects];
    uint8_t effectCount = 0;
};

// ------------------------------------------------------------ intents (8.2) --
// The shared controller contract: PLAYER and AI produce the same intent, one
// applier resolves it against the world. Controllers differ in how the intent
// is produced, never in how it acts (P9).
struct CharacterIntent {
    Vec3 move{};           // world-space movement vector (magnitude = throttle)
    float speed = 0;       // target speed in m/s at full throttle
    float lookDelta = 0;   // yaw change from look input (player camera)
    bool faceMove = false; // face the movement direction (AI); player faces look
};

void applyIntent(World& world, EntityId entity, const CharacterIntent& intent);

// -------------------------------------------------- persistence (task 8.9) --
// The compact on-disk character record. Item name keys are not persisted —
// identity is the asset id; presentation re-derives from the registry.
struct SavedCharacterItem {
    AssetId asset = kInvalidAsset;
    uint32_t count = 0;
    float color[4] = {1, 1, 1, 1};
};

struct SavedEquippedItem {
    SavedCharacterItem item;
    uint8_t layer = 1;
    uint8_t sheathed = 0;
};

struct SavedStatusEffect {
    uint64_t id = 0;
    uint32_t tags = 0;
    float magnitude = 0;
    float duration = -1;
};

struct SavedCharacter {
    uint32_t persistentId = 0;
    float health = 1.0f;
    float maxHealth = 1.0f;
    FactionId faction = 0;
    uint8_t alive = 1;
    uint8_t controller = 0;  // ControllerKind — restored so NPCs resume as AI
    float sightRange = 15.0f;
    std::vector<SavedCharacterItem> items;
    SavedEquippedItem equipment[static_cast<size_t>(EquipSlot::Count)];
    std::vector<SavedStatusEffect> effects;  // save schema v4 (task 9.6)
};

void captureCharacter(const CharacterComponent& component, SavedCharacter& out);
void applyCharacter(const SavedCharacter& saved, CharacterComponent& out);

// Character storage: parallel to World entities, fixed capacity (P1).
class CharacterSystem {
public:
    explicit CharacterSystem(World& world, uint32_t capacity = 256);

    // Makes an existing world entity a character (P9: any entity qualifies).
    CharacterComponent* attach(EntityId entity);
    CharacterComponent* get(EntityId entity);
    const CharacterComponent* get(EntityId entity) const;
    void detach(EntityId entity);

    FactionTable& factions() { return factions_; }
    Stance stanceBetween(EntityId a, EntityId b) const;

    // Mortality (task 8.4): damage; at 0 the character dies — inventory and
    // equipment drop as loot records for the game to spawn, movement stops.
    struct DroppedLoot {
        Item items[ItemCollection::kCapacity + static_cast<size_t>(EquipSlot::Count)];
        uint32_t count = 0;
    };
    // Returns true if this damage killed the character; loot (if any) is
    // written to outLoot.
    bool damage(EntityId entity, float amount, DroppedLoot* outLoot = nullptr);

    // Equipment (task 8.7): move an inventory item into a slot (swapping any
    // occupant back to the inventory) and back.
    bool equip(EntityId entity, uint32_t inventoryIndex, EquipSlot slot, uint8_t layer = 1);
    bool unequip(EntityId entity, EquipSlot slot);
    // Held-item state (CHARACTERS.md §6.1).
    bool setSheathed(EntityId entity, bool sheathed);

    // Status effects (task 9.6). Adding an effect whose id is already
    // present refreshes it (magnitude/duration replaced); a full list
    // refuses (P1). sumMagnitude totals effects matching ANY given tag bit —
    // the stat-modifier query hooks (speed, health, ...) games read from.
    bool addEffect(EntityId entity, const StatusEffect& effect);
    bool removeEffect(EntityId entity, uint64_t effectId);
    const StatusEffect* findEffect(EntityId entity, uint64_t effectId) const;
    float sumMagnitude(EntityId entity, uint32_t tagMask) const;
    // One fixed step for timed effects: durations tick down, expired effects
    // drop. Permanent effects (skills/education) never expire.
    void tickEffects(float dt);

    // Persistence + streaming (task 8.9). Snapshot captures every character
    // with a persistentId; restore re-applies one onto a (new) entity after a
    // load or a chunk reload. pruneDead frees slots whose world entity is gone
    // (chunk evicted / entity despawned), parking persistent state into
    // `parked` first so nothing a player did to an NPC is lost while its
    // chunk is cold — memory stays bounded by character capacity, not world
    // size (P1/P2).
    void snapshot(std::vector<SavedCharacter>& out) const;
    CharacterComponent* restore(EntityId entity, const SavedCharacter& saved);
    void pruneDead(std::vector<SavedCharacter>* parked = nullptr);
    EntityId findByPersistentId(uint32_t persistentId) const;

    template <typename F>
    void forEach(F&& f) {
        for (uint32_t i = 0; i < capacity_; ++i) {
            if (used_[i]) f(entities_[i], components_[i]);
        }
    }

    uint32_t count() const { return liveCount_; }

private:
    int32_t indexOf(EntityId entity) const;

    World& world_;
    uint32_t capacity_;
    uint32_t liveCount_ = 0;
    std::vector<uint8_t> used_;
    std::vector<EntityId> entities_;
    std::vector<CharacterComponent> components_;
    FactionTable factions_;
};

}  // namespace mge
