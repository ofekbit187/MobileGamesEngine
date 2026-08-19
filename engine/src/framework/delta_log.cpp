#include "mge/framework/delta_log.h"

#include <cstring>

namespace mge {

namespace {
void append(std::vector<uint8_t>& out, const void* data, size_t size) {
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    out.insert(out.end(), bytes, bytes + size);
}
bool read(const uint8_t*& cursor, size_t& remaining, void* dest, size_t size) {
    if (remaining < size) return false;
    memcpy(dest, cursor, size);
    cursor += size;
    remaining -= size;
    return true;
}
}  // namespace

void WorldDeltaLog::recordRemoved(uint32_t chunkIndex, uint16_t placementIndex) {
    ChunkDelta& delta = chunk(chunkIndex);
    for (uint16_t existing : delta.removed) {
        if (existing == placementIndex) return;
    }
    delta.removed.push_back(placementIndex);
    // A removed placement's move record is moot.
    for (size_t i = 0; i < delta.moved.size(); ++i) {
        if (delta.moved[i].placementIndex == placementIndex) {
            delta.moved.erase(delta.moved.begin() + static_cast<long>(i));
            break;
        }
    }
}

void WorldDeltaLog::recordMoved(uint32_t chunkIndex, uint16_t placementIndex,
                                const Vec3& position, float yaw) {
    ChunkDelta& delta = chunk(chunkIndex);
    for (MovedPlacement& moved : delta.moved) {
        if (moved.placementIndex == placementIndex) {
            moved.position = position;
            moved.yaw = yaw;
            return;
        }
    }
    delta.moved.push_back({placementIndex, position, yaw});
}

size_t WorldDeltaLog::recordSpawned(uint32_t chunkIndex, const SpawnedEntity& entity) {
    ChunkDelta& delta = chunk(chunkIndex);
    delta.spawned.push_back(entity);
    return delta.spawned.size() - 1;
}

bool WorldDeltaLog::tombstoneSpawned(uint32_t chunkIndex, size_t spawnedIndex) {
    auto it = chunks_.find(chunkIndex);
    if (it == chunks_.end() || spawnedIndex >= it->second.spawned.size()) return false;
    it->second.spawned[spawnedIndex].asset = kInvalidAsset;  // skipped on instantiate
    return true;
}

const ChunkDelta* WorldDeltaLog::find(uint32_t chunkIndex) const {
    auto it = chunks_.find(chunkIndex);
    return it != chunks_.end() ? &it->second : nullptr;
}

bool WorldDeltaLog::isRemoved(uint32_t chunkIndex, uint16_t placementIndex) const {
    const ChunkDelta* delta = find(chunkIndex);
    if (delta == nullptr) return false;
    for (uint16_t removed : delta->removed) {
        if (removed == placementIndex) return true;
    }
    return false;
}

const MovedPlacement* WorldDeltaLog::findMoved(uint32_t chunkIndex,
                                               uint16_t placementIndex) const {
    const ChunkDelta* delta = find(chunkIndex);
    if (delta == nullptr) return nullptr;
    for (const MovedPlacement& moved : delta->moved) {
        if (moved.placementIndex == placementIndex) return &moved;
    }
    return nullptr;
}

void WorldDeltaLog::serialize(std::vector<uint8_t>& out) const {
    const uint32_t count = static_cast<uint32_t>(chunks_.size());
    append(out, &count, sizeof(count));
    for (const auto& [chunkIndex, delta] : chunks_) {
        append(out, &chunkIndex, sizeof(chunkIndex));
        const uint32_t removedCount = static_cast<uint32_t>(delta.removed.size());
        const uint32_t movedCount = static_cast<uint32_t>(delta.moved.size());
        const uint32_t spawnedCount = static_cast<uint32_t>(delta.spawned.size());
        append(out, &removedCount, sizeof(removedCount));
        append(out, &movedCount, sizeof(movedCount));
        append(out, &spawnedCount, sizeof(spawnedCount));
        append(out, delta.removed.data(), removedCount * sizeof(uint16_t));
        append(out, delta.moved.data(), movedCount * sizeof(MovedPlacement));
        append(out, delta.spawned.data(), spawnedCount * sizeof(SpawnedEntity));
    }
}

bool WorldDeltaLog::deserialize(const uint8_t*& cursor, size_t& remaining) {
    clear();
    uint32_t count = 0;
    if (!read(cursor, remaining, &count, sizeof(count)) || count > 100000) return false;
    for (uint32_t c = 0; c < count; ++c) {
        uint32_t chunkIndex = 0, removedCount = 0, movedCount = 0, spawnedCount = 0;
        if (!read(cursor, remaining, &chunkIndex, sizeof(chunkIndex)) ||
            !read(cursor, remaining, &removedCount, sizeof(removedCount)) ||
            !read(cursor, remaining, &movedCount, sizeof(movedCount)) ||
            !read(cursor, remaining, &spawnedCount, sizeof(spawnedCount)) ||
            removedCount > 4096 || movedCount > 4096 || spawnedCount > 4096) {
            clear();
            return false;
        }
        ChunkDelta& delta = chunk(chunkIndex);
        delta.removed.resize(removedCount);
        delta.moved.resize(movedCount);
        delta.spawned.resize(spawnedCount);
        if (!read(cursor, remaining, delta.removed.data(), removedCount * sizeof(uint16_t)) ||
            !read(cursor, remaining, delta.moved.data(),
                  movedCount * sizeof(MovedPlacement)) ||
            !read(cursor, remaining, delta.spawned.data(),
                  spawnedCount * sizeof(SpawnedEntity))) {
            clear();
            return false;
        }
    }
    return true;
}

}  // namespace mge
