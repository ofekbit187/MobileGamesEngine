#pragma once

// Streaming residency manager (tasks 4.3/4.5/4.6/4.7).
//
// Realizes P2 and P10: the world file stays on disk; chunks around the player
// (and interiors whose doors the player approaches) stream in through the
// priority AsyncIO system, instantiate amortized across updates, and evict
// when left behind or when the streaming budget refuses. update() never
// blocks on I/O — it polls completions, issues async reads, and spawns or
// despawns a bounded amount of work per call.

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include "mge/core/io.h"
#include "mge/core/memory.h"
#include "mge/framework/asset_registry.h"
#include "mge/framework/world.h"
#include "mge/streaming/world_file.h"

namespace mge {

struct StreamingConfig {
    int32_t residentRadius = 2;  // chunks (Chebyshev) around the player
    int32_t evictRadius = 3;     // beyond this, resident chunks unload
    uint32_t maxInFlight = 8;    // concurrent async reads
    uint32_t maxInstantiatePerUpdate = 2;
    uint32_t maxEvictPerUpdate = 4;
    uint32_t maxPlacementsPerChunk = 256;
    size_t streamingBudgetBytes = 64u * 1024u * 1024u;
    // Approach prediction (P10): prefetch an interior when the player is
    // within enterRadius * this factor of its door.
    float interiorPrefetchFactor = 2.0f;
};

enum class ChunkState : uint8_t { Cold = 0, Loading, Loaded, Resident };

struct StreamingStats {
    uint32_t residentChunks = 0;
    uint32_t loadingChunks = 0;
    uint32_t residentAssets = 0;
    uint64_t chunkLoadCount = 0;
    uint64_t chunkEvictCount = 0;
    uint64_t bytesLoaded = 0;
    // Updates (after the first residency) where the player stood on a
    // non-resident exterior chunk — the flow-break metric (P10): must stay 0.
    uint64_t playerColdUpdates = 0;
    size_t budgetUsedBytes = 0;
    size_t budgetCapBytes = 0;
};

class StreamingManager {
public:
    bool init(const char* worldPath, World& world, AssetRegistry& assets, AsyncIO& io,
              BudgetRegistry& budgets, const StreamingConfig& config);
    void shutdown();

    // Non-blocking. Call once per simulation step (or frame).
    void update(const Vec3& playerPos);

    const StreamingStats& stats() const { return stats_; }
    const WorldFileReader& reader() const { return reader_; }

    ChunkState exteriorState(int32_t cx, int32_t cz) const;
    ChunkState chunkState(size_t chunkIndex) const;

    // ASCII residency map around the player: 'P' player, '#' resident,
    // '~' loading, '.' cold, ' ' outside the world. Rows are z, cols x.
    void debugMap(char* out, size_t outSize, int32_t radius) const;

private:
    struct ChunkRuntime {
        ChunkState state = ChunkState::Cold;
        IoRequest request;
        uint8_t* staging = nullptr;   // placement bytes while Loading/Loaded
        size_t stagingSize = 0;
        std::vector<EntityId> entities;  // sized maxPlacementsPerChunk at init
        uint32_t entityCount = 0;
        int32_t priorityRing = 0;
    };

    struct AssetRuntime {
        enum class State : uint8_t { Cold, Loading, Resident } state = State::Cold;
        uint32_t refCount = 0;
        IoRequest request;
        uint8_t* buffer = nullptr;  // payload while Loading
        size_t bufferSize = 0;
        size_t chargedBytes = 0;    // resident charge against the budget
    };

    void pollCompletions();
    void scanDesired(const Vec3& playerPos);
    void issueChunkLoad(size_t chunkIndex, IoPriority priority);
    void instantiateLoaded();
    void instantiateChunk(size_t chunkIndex);
    void evictChunk(size_t chunkIndex);
    void addAssetRef(AssetId id, IoPriority priority);
    void releaseAssetRef(AssetId id);
    uint8_t* budgetAlloc(size_t bytes);
    void budgetFree(uint8_t* buffer, size_t bytes);

    World* world_ = nullptr;
    AssetRegistry* assets_ = nullptr;
    AsyncIO* io_ = nullptr;
    BudgetRegistry* budgets_ = nullptr;
    BudgetId budget_ = kInvalidBudget;
    StreamingConfig config_{};
    WorldFileReader reader_;

    // Heap array (not vector): ChunkRuntime holds a non-movable IoRequest.
    std::unique_ptr<ChunkRuntime[]> chunks_;
    size_t chunkCount_ = 0;
    std::unordered_map<AssetId, AssetRuntime> assetRuntime_;
    std::vector<size_t> loadedQueue_;  // chunks with placements read, awaiting spawn
    uint32_t inFlight_ = 0;
    Vec3 lastPlayerPos_{};
    bool warmedUp_ = false;
    StreamingStats stats_;
};

}  // namespace mge
