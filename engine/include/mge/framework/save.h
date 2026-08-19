#pragma once

// Saving & loading (tasks 6.3/6.5/6.6, P7). Saves are snapshots taken at a
// simulation boundary, serialized, and written ATOMICALLY: tmp file, fsync,
// rename. Process death at any moment leaves the previous save intact — the
// kill-test suite enforces this. Loads validate a payload checksum before
// anything is applied, and a schema-version migration chain upgrades old
// saves forward.

#include <cstdint>
#include <string>
#include <vector>

#include "mge/core/math.h"
#include "mge/framework/delta_log.h"
#include "mge/framework/items.h"

namespace mge {

constexpr uint32_t kSaveSchemaVersion = 2;  // v2: player health added

struct PlayerState {
    Vec3 position{};
    float yaw = 0;
    float health = 1.0f;
};

struct SavedItem {
    AssetId asset = kInvalidAsset;
    uint32_t count = 0;
    float color[4] = {1, 1, 1, 1};
};

struct SavedCollection {
    uint64_t id = 0;  // CollectionRegistry id
    std::vector<SavedItem> items;
};

// Everything a save persists. Games extend by adding sections (with a schema
// version bump + migration).
struct SaveSnapshot {
    PlayerState player;
    double playtimeSeconds = 0;
    WorldDeltaLog deltas;
    std::vector<SavedCollection> collections;
};

struct SaveSlotInfo {
    std::string name;       // slot name (file stem)
    uint32_t schemaVersion = 0;
    uint64_t timestampUnix = 0;
    double playtimeSeconds = 0;
    bool valid = false;     // header + checksum passed
};

class SaveManager {
public:
    // Directory must exist (app-private storage on Android).
    explicit SaveManager(const char* directory) : directory_(directory) {}

    bool save(const char* slotName, const SaveSnapshot& snapshot);
    // Fails cleanly (returns false) on missing slot, checksum mismatch, or
    // future schema version. Old versions run the migration chain.
    bool load(const char* slotName, SaveSnapshot& out);

    void listSlots(std::vector<SaveSlotInfo>& out) const;
    bool slotInfo(const char* slotName, SaveSlotInfo& out) const;
    bool removeSlot(const char* slotName);

    // --- test hooks (kill-test suite) ---
    // Truncate the write after N bytes and stop before the rename —
    // simulating process death mid-save. -1 disables.
    void testKillDuringWrite(long afterBytes) { killAfterBytes_ = afterBytes; }

private:
    std::string slotPath(const char* slotName) const;
    std::string directory_;
    long killAfterBytes_ = -1;
};

// FNV-1a 64 over arbitrary bytes (checksums, shared with asset ids' scheme).
uint64_t fnv1a(const uint8_t* data, size_t size);

}  // namespace mge
