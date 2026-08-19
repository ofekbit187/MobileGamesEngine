#include "mge/streaming/streaming_manager.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <new>

#include "mge/core/log.h"
#include "mge/graphics/mesh_io.h"

namespace mge {

namespace {
constexpr const char* kTag = "streaming";

int32_t chebyshev(int32_t ax, int32_t az, int32_t bx, int32_t bz) {
    const int32_t dx = std::abs(ax - bx);
    const int32_t dz = std::abs(az - bz);
    return dx > dz ? dx : dz;
}
}  // namespace

bool StreamingManager::init(const char* worldPath, World& world, AssetRegistry& assets,
                            AsyncIO& io, BudgetRegistry& budgets,
                            const StreamingConfig& config) {
    if (!reader_.open(worldPath)) return false;
    world_ = &world;
    assets_ = &assets;
    io_ = &io;
    budgets_ = &budgets;
    config_ = config;
    budget_ = budgets.registerBudget("streaming", config.streamingBudgetBytes);

    chunkCount_ = reader_.chunks().size();
    chunks_ = std::make_unique<ChunkRuntime[]>(chunkCount_);
    for (size_t i = 0; i < chunkCount_; ++i) {
        // Headroom beyond shipped placements for dynamic spawns.
        chunks_[i].entities.resize(config_.maxPlacementsPerChunk + 64);
        chunks_[i].entitySource.resize(config_.maxPlacementsPerChunk + 64);
    }
    loadedQueue_.reserve(64);

    // Register every asset id up front: virtual models with their declared
    // proportions (always resident placeholders), meshes as known-but-cold
    // records the streaming system fills and drops (P2).
    for (const WorldAssetInfo& info : reader_.assets()) {
        if (info.kind == AssetKind::VirtualModel) {
            assets_->registerVirtualModel(info.name.c_str(), info.virtualDesc);
        } else {
            assets_->registerMesh(info.name.c_str(), LodMesh{});
            assetRuntime_.emplace(std::piecewise_construct, std::forward_as_tuple(info.id),
                                  std::forward_as_tuple());
        }
    }

    MGE_LOGI(kTag, "world open: %zu chunks, %zu assets, chunk size %.0fm",
             reader_.chunks().size(), reader_.assets().size(), reader_.chunkSizeMeters());
    return true;
}

void StreamingManager::shutdown() {
    if (io_ != nullptr) io_->drain();
    for (size_t i = 0; i < chunkCount_; ++i) {
        if (chunks_[i].state == ChunkState::Resident) evictChunk(i);
        if (chunks_[i].staging != nullptr) {
            budgetFree(chunks_[i].staging, chunks_[i].stagingSize);
            chunks_[i].staging = nullptr;
        }
    }
    for (auto& [id, runtime] : assetRuntime_) {
        if (runtime.buffer != nullptr) budgetFree(runtime.buffer, runtime.bufferSize);
        if (runtime.chargedBytes > 0) budgets_->release(budget_, runtime.chargedBytes);
        runtime.buffer = nullptr;
        runtime.bufferSize = 0;
        runtime.chargedBytes = 0;
        runtime.refCount = 0;
        runtime.state = AssetRuntime::State::Cold;
    }
    chunks_.reset();
    chunkCount_ = 0;
}

uint8_t* StreamingManager::budgetAlloc(size_t bytes) {
    if (!budgets_->charge(budget_, bytes)) return nullptr;  // refuse: caller retries later
    return static_cast<uint8_t*>(::operator new(bytes));
}

void StreamingManager::budgetFree(uint8_t* buffer, size_t bytes) {
    if (buffer == nullptr) return;
    ::operator delete(buffer);
    budgets_->release(budget_, bytes);
}

void StreamingManager::update(const Vec3& playerPos) {
    // A teleport (fast travel, spawn) legitimately resets flow continuity —
    // P10's no-waiting rule applies to traversal, not to jumps.
    if ((playerPos - lastPlayerPos_).length() > reader_.chunkSizeMeters()) {
        warmedUp_ = false;
    }
    lastPlayerPos_ = playerPos;
    pollCompletions();
    scanDesired(playerPos);
    instantiateLoaded();

    // Flow-break metric (P10): after warmup the player's chunk must be resident.
    const ChunkCoord pc = World::chunkAt(playerPos);
    const ChunkState state = exteriorState(pc.x, pc.z);
    if (state == ChunkState::Resident) {
        warmedUp_ = true;
    } else if (warmedUp_ && reader_.findExterior(pc.x, pc.z) != nullptr) {
        ++stats_.playerColdUpdates;
    }

    const BudgetStats budget = budgets_->stats(budget_);
    stats_.budgetUsedBytes = budget.usedBytes;
    stats_.budgetCapBytes = budget.capBytes;
}

void StreamingManager::pollCompletions() {
    // Chunk placement reads.
    for (size_t i = 0; i < chunkCount_; ++i) {
        ChunkRuntime& chunk = chunks_[i];
        if (chunk.state != ChunkState::Loading) continue;
        const IoStatus status = chunk.request.status();
        if (status == IoStatus::Pending) continue;
        --inFlight_;
        if (status == IoStatus::Done && chunk.request.bytesRead == chunk.stagingSize) {
            chunk.state = ChunkState::Loaded;
            loadedQueue_.push_back(i);
            stats_.bytesLoaded += chunk.request.bytesRead;
        } else {
            MGE_LOGW(kTag, "chunk read failed (err %d), back to cold", chunk.request.errorCode);
            budgetFree(chunk.staging, chunk.stagingSize);
            chunk.staging = nullptr;
            chunk.state = ChunkState::Cold;
        }
    }
    // Asset payload reads: deserialize + hand to the registry.
    for (auto& [id, runtime] : assetRuntime_) {
        if (runtime.state != AssetRuntime::State::Loading) continue;
        const IoStatus status = runtime.request.status();
        if (status == IoStatus::Pending) continue;
        --inFlight_;
        if (status == IoStatus::Done && runtime.request.bytesRead == runtime.bufferSize) {
            LodMesh mesh;
            if (deserializeMesh(runtime.buffer, runtime.bufferSize, mesh)) {
                const WorldAssetInfo* info = reader_.findAsset(id);
                assets_->registerMesh(info->name.c_str(), std::move(mesh));
                runtime.state = AssetRuntime::State::Resident;
                // The payload buffer becomes the resident charge (the CPU
                // mesh is ~the same size); freed heap, kept charge.
                stats_.bytesLoaded += runtime.bufferSize;
                ::operator delete(runtime.buffer);
                runtime.buffer = nullptr;
                runtime.chargedBytes = runtime.bufferSize;
                runtime.bufferSize = 0;
                ++stats_.residentAssets;
                continue;
            }
        }
        MGE_LOGW(kTag, "asset read failed, back to cold");
        budgetFree(runtime.buffer, runtime.bufferSize);
        runtime.buffer = nullptr;
        runtime.state = AssetRuntime::State::Cold;
    }
}

void StreamingManager::scanDesired(const Vec3& playerPos) {
    const ChunkCoord pc = World::chunkAt(playerPos);
    uint32_t evictedThisUpdate = 0;

    const std::vector<WorldChunkInfo>& infos = reader_.chunks();
    for (size_t i = 0; i < infos.size(); ++i) {
        const WorldChunkInfo& info = infos[i];
        ChunkRuntime& chunk = chunks_[i];

        bool desired = false;
        IoPriority priority = IoPriority::Normal;
        bool evictable = false;

        if (info.kind == CellKind::Exterior) {
            const int32_t ring = chebyshev(info.cx, info.cz, pc.x, pc.z);
            chunk.priorityRing = ring;
            desired = ring <= config_.residentRadius;
            evictable = ring > config_.evictRadius;
            priority = ring == 0   ? IoPriority::Critical
                       : ring == 1 ? IoPriority::High
                                   : IoPriority::Normal;
        } else {
            // Interior cell: approach prediction against the door anchor (P10).
            const Vec3 toDoor = playerPos - info.anchor;
            const float distance = toDoor.length();
            desired = distance <= info.enterRadius * config_.interiorPrefetchFactor;
            evictable = distance > info.exitRadius;
            priority = distance <= info.enterRadius ? IoPriority::Critical : IoPriority::High;
        }

        if (desired && chunk.state == ChunkState::Cold && inFlight_ < config_.maxInFlight) {
            issueChunkLoad(i, priority);
        } else if (evictable && chunk.state == ChunkState::Resident &&
                   evictedThisUpdate < config_.maxEvictPerUpdate) {
            evictChunk(i);
            ++evictedThisUpdate;
        }
    }
}

void StreamingManager::issueChunkLoad(size_t chunkIndex, IoPriority priority) {
    const WorldChunkInfo& info = reader_.chunks()[chunkIndex];
    ChunkRuntime& chunk = chunks_[chunkIndex];
    if (info.placementCount > config_.maxPlacementsPerChunk) {
        MGE_LOGE(kTag, "chunk %zu exceeds placement cap (%u)", chunkIndex, info.placementCount);
        return;
    }
    const size_t bytes = info.placementCount * sizeof(Placement);
    if (bytes == 0) {
        chunk.state = ChunkState::Loaded;
        loadedQueue_.push_back(chunkIndex);
        return;
    }
    uint8_t* staging = budgetAlloc(bytes);
    if (staging == nullptr) return;  // budget refused — retry on a later update

    chunk.staging = staging;
    chunk.stagingSize = bytes;
    chunk.request.reset();
    chunk.request.path = reader_.path().c_str();
    chunk.request.offset = info.placementsOffset;
    chunk.request.dest = staging;
    chunk.request.size = bytes;
    chunk.request.priority = priority;
    if (io_->submit(&chunk.request)) {
        chunk.state = ChunkState::Loading;
        ++inFlight_;
    } else {
        budgetFree(staging, bytes);
        chunk.staging = nullptr;
    }
}

void StreamingManager::instantiateLoaded() {
    uint32_t instantiated = 0;
    while (!loadedQueue_.empty() && instantiated < config_.maxInstantiatePerUpdate) {
        const size_t chunkIndex = loadedQueue_.back();
        loadedQueue_.pop_back();
        if (chunks_[chunkIndex].state != ChunkState::Loaded) continue;
        instantiateChunk(chunkIndex);
        ++instantiated;
    }
}

void StreamingManager::instantiateChunk(size_t chunkIndex) {
    const WorldChunkInfo& info = reader_.chunks()[chunkIndex];
    ChunkRuntime& chunk = chunks_[chunkIndex];
    const Placement* placements = reinterpret_cast<const Placement*>(chunk.staging);

    const uint32_t deltaChunkIndex = static_cast<uint32_t>(chunkIndex);
    const IoPriority assetPriority =
        info.kind == CellKind::Interior ? IoPriority::High : IoPriority::Normal;
    chunk.entityCount = 0;
    for (uint32_t p = 0; p < info.placementCount; ++p) {
        // Save deltas (task 6.4): removed placements never come back.
        if (deltas_ != nullptr && deltas_->isRemoved(deltaChunkIndex, static_cast<uint16_t>(p)))
            continue;
        const Placement& placement = placements[p];
        const EntityId entity = world_->spawn();
        if (entity == kInvalidEntity) {
            MGE_LOGW(kTag, "world entity capacity refused a streamed placement");
            break;
        }
        TransformComponent transform;
        transform.position = {placement.pos[0], placement.pos[1], placement.pos[2]};
        transform.yaw = placement.yaw;
        if (deltas_ != nullptr) {
            if (const MovedPlacement* moved =
                    deltas_->findMoved(deltaChunkIndex, static_cast<uint16_t>(p))) {
                transform.position = moved->position;
                transform.yaw = moved->yaw;
            }
        }
        world_->setTransform(entity, transform);
        ModelComponent model;
        model.asset = placement.asset;
        memcpy(model.color, placement.color, sizeof(model.color));
        world_->setModel(entity, model);
        chunk.entitySource[chunk.entityCount] = static_cast<uint16_t>(p);
        chunk.entities[chunk.entityCount++] = entity;

        addAssetRef(placement.asset, assetPriority);
    }
    // Dynamic spawns recorded in the delta come back with the chunk.
    if (deltas_ != nullptr) {
        if (const ChunkDelta* delta = deltas_->find(deltaChunkIndex)) {
            for (size_t si = 0; si < delta->spawned.size(); ++si) {
                const SpawnedEntity& spawned = delta->spawned[si];
                if (spawned.asset == kInvalidAsset) continue;  // tombstone
                if (chunk.entityCount >= chunk.entities.size()) break;
                const EntityId entity = world_->spawn();
                if (entity == kInvalidEntity) break;
                TransformComponent transform;
                transform.position = spawned.position;
                transform.yaw = spawned.yaw;
                world_->setTransform(entity, transform);
                ModelComponent model;
                model.asset = spawned.asset;
                memcpy(model.color, spawned.color, sizeof(model.color));
                world_->setModel(entity, model);
                chunk.entitySource[chunk.entityCount] =
                    static_cast<uint16_t>(kDynamicFlag | si);
                chunk.entities[chunk.entityCount++] = entity;
                addAssetRef(spawned.asset, assetPriority);
            }
        }
    }

    budgetFree(chunk.staging, chunk.stagingSize);
    chunk.staging = nullptr;
    chunk.stagingSize = 0;
    chunk.state = ChunkState::Resident;
    ++stats_.residentChunks;
    ++stats_.chunkLoadCount;
}

void StreamingManager::evictChunk(size_t chunkIndex) {
    ChunkRuntime& chunk = chunks_[chunkIndex];
    for (uint32_t i = 0; i < chunk.entityCount; ++i) {
        const EntityId entity = chunk.entities[i];
        const ModelComponent* model = world_->model(entity);
        if (model != nullptr) releaseAssetRef(model->asset);
        world_->despawn(entity);
    }
    chunk.entityCount = 0;
    chunk.state = ChunkState::Cold;
    --stats_.residentChunks;
    ++stats_.chunkEvictCount;
}

void StreamingManager::addAssetRef(AssetId id, IoPriority priority) {
    auto it = assetRuntime_.find(id);
    if (it == assetRuntime_.end()) return;  // virtual model: always resident
    AssetRuntime& runtime = it->second;
    ++runtime.refCount;
    if (runtime.state != AssetRuntime::State::Cold || inFlight_ >= config_.maxInFlight) return;

    const WorldAssetInfo* info = reader_.findAsset(id);
    if (info == nullptr || info->payloadSize == 0) return;
    uint8_t* buffer = budgetAlloc(info->payloadSize);
    if (buffer == nullptr) return;  // refused; a later ref or update retries

    runtime.buffer = buffer;
    runtime.bufferSize = info->payloadSize;
    runtime.request.reset();
    runtime.request.path = reader_.path().c_str();
    runtime.request.offset = info->payloadOffset;
    runtime.request.dest = buffer;
    runtime.request.size = info->payloadSize;
    runtime.request.priority = priority;
    if (io_->submit(&runtime.request)) {
        runtime.state = AssetRuntime::State::Loading;
        ++inFlight_;
    } else {
        budgetFree(buffer, info->payloadSize);
        runtime.buffer = nullptr;
    }
}

void StreamingManager::releaseAssetRef(AssetId id) {
    auto it = assetRuntime_.find(id);
    if (it == assetRuntime_.end()) return;
    AssetRuntime& runtime = it->second;
    if (runtime.refCount > 0) --runtime.refCount;
    if (runtime.refCount == 0 && runtime.state == AssetRuntime::State::Resident) {
        assets_->unload(id);
        budgets_->release(budget_, runtime.chargedBytes);
        runtime.chargedBytes = 0;
        runtime.state = AssetRuntime::State::Cold;
        --stats_.residentAssets;
    }
}

bool StreamingManager::findEntity(EntityId entity, size_t& chunkIndex, uint32_t& slot) {
    for (size_t c = 0; c < chunkCount_; ++c) {
        if (chunks_[c].state != ChunkState::Resident) continue;
        for (uint32_t i = 0; i < chunks_[c].entityCount; ++i) {
            if (chunks_[c].entities[i] == entity) {
                chunkIndex = c;
                slot = i;
                return true;
            }
        }
    }
    return false;
}

void StreamingManager::detachEntitySlot(size_t chunkIndex, uint32_t slot) {
    // Swap-remove keeps the arrays dense.
    ChunkRuntime& chunk = chunks_[chunkIndex];
    const uint32_t last = chunk.entityCount - 1;
    chunk.entities[slot] = chunk.entities[last];
    chunk.entitySource[slot] = chunk.entitySource[last];
    --chunk.entityCount;
}

bool StreamingManager::removeStreamedEntity(EntityId entity) {
    size_t chunkIndex;
    uint32_t slot;
    if (deltas_ == nullptr || !findEntity(entity, chunkIndex, slot)) return false;
    ChunkRuntime& chunk = chunks_[chunkIndex];
    const uint16_t source = chunk.entitySource[slot];
    if ((source & kDynamicFlag) != 0) {
        deltas_->tombstoneSpawned(static_cast<uint32_t>(chunkIndex), source & ~kDynamicFlag);
    } else {
        deltas_->recordRemoved(static_cast<uint32_t>(chunkIndex), source);
    }
    const ModelComponent* model = world_->model(entity);
    if (model != nullptr) releaseAssetRef(model->asset);
    world_->despawn(entity);
    detachEntitySlot(chunkIndex, slot);
    return true;
}

bool StreamingManager::moveStreamedEntity(EntityId entity, const Vec3& position, float yaw) {
    size_t chunkIndex;
    uint32_t slot;
    if (deltas_ == nullptr || !findEntity(entity, chunkIndex, slot)) return false;
    ChunkRuntime& chunk = chunks_[chunkIndex];
    const uint16_t source = chunk.entitySource[slot];
    if ((source & kDynamicFlag) != 0) {
        if (ChunkDelta* delta = const_cast<ChunkDelta*>(
                deltas_->find(static_cast<uint32_t>(chunkIndex)))) {
            SpawnedEntity& spawned = delta->spawned[source & ~kDynamicFlag];
            spawned.position = position;
            spawned.yaw = yaw;
        }
    } else {
        deltas_->recordMoved(static_cast<uint32_t>(chunkIndex), source, position, yaw);
    }
    TransformComponent* transform = world_->transform(entity);
    if (transform != nullptr) {
        transform->position = position;
        transform->yaw = yaw;
        transform->prevPosition = position;
        transform->prevYaw = yaw;
    }
    return true;
}

EntityId StreamingManager::spawnDynamic(AssetId asset, const Vec3& position, float yaw,
                                        const float color[4]) {
    if (deltas_ == nullptr) return kInvalidEntity;
    const ChunkCoord coord = World::chunkAt(position);
    const WorldChunkInfo* info = reader_.findExterior(coord.x, coord.z);
    if (info == nullptr) return kInvalidEntity;
    const size_t chunkIndex = static_cast<size_t>(info - reader_.chunks().data());
    ChunkRuntime& chunk = chunks_[chunkIndex];
    if (chunk.state != ChunkState::Resident || chunk.entityCount >= chunk.entities.size()) {
        return kInvalidEntity;
    }
    SpawnedEntity spawned;
    spawned.asset = asset;
    spawned.position = position;
    spawned.yaw = yaw;
    memcpy(spawned.color, color, sizeof(spawned.color));
    const size_t si = deltas_->recordSpawned(static_cast<uint32_t>(chunkIndex), spawned);

    const EntityId entity = world_->spawn();
    if (entity == kInvalidEntity) {
        deltas_->tombstoneSpawned(static_cast<uint32_t>(chunkIndex), si);
        return kInvalidEntity;
    }
    TransformComponent transform;
    transform.position = position;
    transform.yaw = yaw;
    world_->setTransform(entity, transform);
    ModelComponent model;
    model.asset = asset;
    memcpy(model.color, color, sizeof(model.color));
    world_->setModel(entity, model);
    chunk.entitySource[chunk.entityCount] = static_cast<uint16_t>(kDynamicFlag | si);
    chunk.entities[chunk.entityCount++] = entity;
    addAssetRef(asset, IoPriority::Normal);
    return entity;
}

ChunkState StreamingManager::exteriorState(int32_t cx, int32_t cz) const {
    const WorldChunkInfo* info = reader_.findExterior(cx, cz);
    if (info == nullptr) return ChunkState::Cold;
    return chunks_[static_cast<size_t>(info - reader_.chunks().data())].state;
}

ChunkState StreamingManager::chunkState(size_t chunkIndex) const {
    return chunkIndex < chunkCount_ ? chunks_[chunkIndex].state : ChunkState::Cold;
}

void StreamingManager::debugMap(char* out, size_t outSize, int32_t radius) const {
    const ChunkCoord pc = World::chunkAt(lastPlayerPos_);
    size_t cursor = 0;
    for (int32_t dz = -radius; dz <= radius; ++dz) {
        for (int32_t dx = -radius; dx <= radius; ++dx) {
            if (cursor + 2 >= outSize) {
                out[cursor] = '\0';
                return;
            }
            char c = ' ';
            if (dx == 0 && dz == 0) {
                c = 'P';
            } else {
                switch (exteriorState(pc.x + dx, pc.z + dz)) {
                    case ChunkState::Resident: c = '#'; break;
                    case ChunkState::Loading:
                    case ChunkState::Loaded: c = '~'; break;
                    case ChunkState::Cold:
                        c = reader_.findExterior(pc.x + dx, pc.z + dz) != nullptr ? '.' : ' ';
                        break;
                }
            }
            out[cursor++] = c;
        }
        out[cursor++] = '\n';
    }
    out[cursor] = '\0';
}

}  // namespace mge
