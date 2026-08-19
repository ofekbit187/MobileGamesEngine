#pragma once

// World delta log (task 6.2): mutations recorded against shipped content.
// Shipped placements are immutable on disk; play removes them, moves them,
// or adds dynamic entities — this log is the only thing a save persists
// about the world (P7: save size scales with impact, not world size).
// Mutations are gameplay events, not per-frame work, so map growth here is
// off the hot path.

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "mge/core/math.h"
#include "mge/framework/asset_registry.h"

namespace mge {

struct MovedPlacement {
    uint16_t placementIndex = 0;
    Vec3 position{};
    float yaw = 0;
};

struct SpawnedEntity {
    AssetId asset = kInvalidAsset;
    Vec3 position{};
    float yaw = 0;
    float color[4] = {1, 1, 1, 1};
};

struct ChunkDelta {
    std::vector<uint16_t> removed;        // shipped placement indices, sorted-ish
    std::vector<MovedPlacement> moved;    // shipped placements with new transforms
    std::vector<SpawnedEntity> spawned;   // player-added dynamic entities

    bool empty() const { return removed.empty() && moved.empty() && spawned.empty(); }
};

class WorldDeltaLog {
public:
    void clear() { chunks_.clear(); }

    void recordRemoved(uint32_t chunkIndex, uint16_t placementIndex);
    void recordMoved(uint32_t chunkIndex, uint16_t placementIndex, const Vec3& position,
                     float yaw);
    // Returns the spawned entry's index within the chunk delta.
    size_t recordSpawned(uint32_t chunkIndex, const SpawnedEntity& entity);
    // Un-spawns a dynamic entity by tombstoning its entry (indices held by
    // live entities stay stable; instantiation skips tombstones).
    bool tombstoneSpawned(uint32_t chunkIndex, size_t spawnedIndex);

    const ChunkDelta* find(uint32_t chunkIndex) const;
    bool isRemoved(uint32_t chunkIndex, uint16_t placementIndex) const;
    const MovedPlacement* findMoved(uint32_t chunkIndex, uint16_t placementIndex) const;

    size_t dirtyChunkCount() const { return chunks_.size(); }

    // Serialization (payload section only; framing/checksum is SaveManager's).
    void serialize(std::vector<uint8_t>& out) const;
    bool deserialize(const uint8_t*& cursor, size_t& remaining);

private:
    ChunkDelta& chunk(uint32_t chunkIndex) { return chunks_[chunkIndex]; }
    std::unordered_map<uint32_t, ChunkDelta> chunks_;
};

}  // namespace mge
