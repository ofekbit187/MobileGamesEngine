#include "mge/framework/world.h"

#include <cmath>

namespace mge {

World::World(uint32_t entityCapacity)
    : registry_(entityCapacity),
      flags_(entityCapacity, 0),
      transforms_(entityCapacity),
      models_(entityCapacity),
      movements_(entityCapacity) {}

void World::despawn(EntityId id) {
    if (!registry_.isAlive(id)) return;
    flags_[id.index] = 0;
    registry_.destroy(id);
}

TransformComponent* World::setTransform(EntityId id, const TransformComponent& value) {
    if (!registry_.isAlive(id)) return nullptr;
    transforms_[id.index] = value;
    transforms_[id.index].prevPosition = value.position;
    transforms_[id.index].prevYaw = value.yaw;
    flags_[id.index] |= kHasTransform;
    return &transforms_[id.index];
}

TransformComponent* World::transform(EntityId id) {
    if (!registry_.isAlive(id) || (flags_[id.index] & kHasTransform) == 0) return nullptr;
    return &transforms_[id.index];
}

ModelComponent* World::setModel(EntityId id, const ModelComponent& value) {
    if (!registry_.isAlive(id)) return nullptr;
    models_[id.index] = value;
    flags_[id.index] |= kHasModel;
    return &models_[id.index];
}

ModelComponent* World::model(EntityId id) {
    if (!registry_.isAlive(id) || (flags_[id.index] & kHasModel) == 0) return nullptr;
    return &models_[id.index];
}

MovementComponent* World::setMovement(EntityId id, const MovementComponent& value) {
    if (!registry_.isAlive(id)) return nullptr;
    movements_[id.index] = value;
    flags_[id.index] |= kHasMovement;
    return &movements_[id.index];
}

MovementComponent* World::movement(EntityId id) {
    if (!registry_.isAlive(id) || (flags_[id.index] & kHasMovement) == 0) return nullptr;
    return &movements_[id.index];
}

ChunkCoord World::chunkAt(const Vec3& position) {
    return {static_cast<int32_t>(std::floor(position.x / kChunkSize)),
            static_cast<int32_t>(std::floor(position.z / kChunkSize))};
}

bool World::chunkOf(EntityId id, ChunkCoord& out) {
    const TransformComponent* t = transform(id);
    if (t == nullptr) return false;
    out = chunkAt(t->position);
    return true;
}

void World::step(double dtSeconds) {
    const float dt = static_cast<float>(dtSeconds);
    for (uint32_t i = 0; i < registry_.capacity(); ++i) {
        if ((flags_[i] & kHasTransform) == 0) continue;
        TransformComponent& t = transforms_[i];
        t.prevPosition = t.position;
        t.prevYaw = t.yaw;
        if ((flags_[i] & kHasMovement) != 0) {
            MovementComponent& m = movements_[i];
            const float speed = m.velocity.length();
            if (speed > m.maxSpeed && speed > 0.0f) {
                m.velocity *= m.maxSpeed / speed;
            }
            t.position += m.velocity * dt;
        }
    }
}

}  // namespace mge
