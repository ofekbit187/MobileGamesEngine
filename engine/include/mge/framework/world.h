#pragma once

// World model (task 3.3): one continuous 3D space partitioned into a chunk
// grid. Component storage is fixed-capacity parallel arrays allocated once at
// world creation (P1) — the per-step path touches no heap. Chunk coordinates
// are the streaming system's addressing unit (P2); today they classify
// entities, in Phase 4 they drive residency.

#include <cstdint>
#include <vector>

#include "mge/core/math.h"
#include "mge/framework/asset_registry.h"
#include "mge/framework/entity.h"

namespace mge {

struct ChunkCoord {
    int32_t x = 0;
    int32_t z = 0;
    bool operator==(const ChunkCoord& r) const { return x == r.x && z == r.z; }
};

struct TransformComponent {
    Vec3 position{};
    float yaw = 0.0f;  // radians around +Y
    // Previous simulation state — rendering interpolates between prev and
    // current with the frame alpha (fixed-step sim, smooth render).
    Vec3 prevPosition{};
    float prevYaw = 0.0f;
};

struct ModelComponent {
    AssetId asset = kInvalidAsset;
    float color[4] = {1, 1, 1, 1};
};

struct MovementComponent {
    Vec3 velocity{};
    float maxSpeed = 4.0f;  // m/s
};

class World {
public:
    static constexpr float kChunkSize = 32.0f;  // meters per chunk side

    explicit World(uint32_t entityCapacity);

    EntityRegistry& entities() { return registry_; }
    const EntityRegistry& entities() const { return registry_; }

    EntityId spawn() { return registry_.create(); }
    void despawn(EntityId id);

    // Component access: setters attach; getters return nullptr when absent
    // or the entity is dead.
    TransformComponent* setTransform(EntityId id, const TransformComponent& value);
    TransformComponent* transform(EntityId id);
    ModelComponent* setModel(EntityId id, const ModelComponent& value);
    ModelComponent* model(EntityId id);
    MovementComponent* setMovement(EntityId id, const MovementComponent& value);
    MovementComponent* movement(EntityId id);

    static ChunkCoord chunkAt(const Vec3& position);
    // Chunk of an entity's current position (entity must have a transform).
    bool chunkOf(EntityId id, ChunkCoord& out);

    // One fixed simulation step: records prev state, integrates movement.
    void step(double dtSeconds);

    // Iterate alive entities that have both transform and model — the render
    // collection pass. Callback: f(EntityId, const TransformComponent&,
    // const ModelComponent&).
    template <typename F>
    void forEachRenderable(F&& f) const {
        for (uint32_t i = 0; i < registry_.capacity(); ++i) {
            if ((flags_[i] & (kHasTransform | kHasModel)) == (kHasTransform | kHasModel)) {
                f(EntityId{i, 0}, transforms_[i], models_[i]);
            }
        }
    }

private:
    static constexpr uint8_t kHasTransform = 1u << 0;
    static constexpr uint8_t kHasModel = 1u << 1;
    static constexpr uint8_t kHasMovement = 1u << 2;

    bool aliveIndex(EntityId id) const { return registry_.isAlive(id); }

    EntityRegistry registry_;
    std::vector<uint8_t> flags_;
    std::vector<TransformComponent> transforms_;
    std::vector<ModelComponent> models_;
    std::vector<MovementComponent> movements_;
};

}  // namespace mge
