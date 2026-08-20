#pragma once

// Collision v1 (Phase 11, tasks 11.1–11.4): the world becomes solid.
//
// Scope is deliberately small and honest: STATIC axis-aligned boxes for world
// geometry, and characters resolved as upright boxes that slide along
// blockers, step over low ledges, and stand on what's under them. That is
// what "you cannot walk through a house" needs. Dynamics (falling, pushing,
// ragdolls, projectiles) are a later phase and are not pretended here.
//
// P1: fixed capacity, no allocation after construction, refuse at the cap.
// P2: colliders are registered and released with their chunk, so the cost of
// a query is bounded by the RESIDENT set — never by world size.

#include <cstdint>
#include <vector>

#include "mge/core/math.h"
#include "mge/framework/entity.h"

namespace mge {

// The character's collision shape: an upright box of `radius` half-extent in
// XZ with its feet at the transform's position.
struct CharacterShape {
    float radius = 0.35f;
    float height = 1.8f;
    // Ledges no taller than this are walked over instead of blocking — the
    // difference between a doorstep and a wall.
    float stepHeight = 0.4f;
};

struct RayHit {
    bool hit = false;
    float distance = 0;
    Vec3 point{};
    Vec3 normal{};
    EntityId entity = kInvalidEntity;
};

struct MoveResult {
    Vec3 position{};
    bool blockedX = false;
    bool blockedZ = false;
    bool grounded = false;   // standing on ground or on a collider
    bool steppedUp = false;
};

constexpr int32_t kInvalidCollider = -1;

class CollisionWorld {
public:
    explicit CollisionWorld(uint32_t capacity = 2048);

    // --- static geometry (the streaming layer owns the lifetime) ---
    int32_t addBox(const Aabb& bounds, EntityId owner = kInvalidEntity);
    void removeBox(int32_t handle);
    void removeByEntity(EntityId entity);
    void clear();
    uint32_t count() const { return liveCount_; }
    uint32_t capacity() const { return static_cast<uint32_t>(boxes_.size()); }

    // The flat ground plane characters stand on until terrain height exists.
    void setGroundHeight(float y) { groundY_ = y; }
    float groundHeight() const { return groundY_; }

    // --- queries ---
    bool overlaps(const Aabb& box, EntityId ignore = kInvalidEntity) const;
    RayHit raycast(const Vec3& origin, const Vec3& direction, float maxDistance,
                   EntityId ignore = kInvalidEntity) const;

    // Resolve a desired movement: slide along what blocks, step over low
    // ledges, and settle onto whatever supports the feet. Allocation-free.
    MoveResult moveCharacter(const Vec3& position, const CharacterShape& shape,
                             const Vec3& delta) const;

    // The box a character occupies standing at `position` (feet at y).
    static Aabb characterBounds(const Vec3& position, const CharacterShape& shape);

private:
    struct Entry {
        Aabb bounds;
        EntityId owner = kInvalidEntity;
        bool used = false;
    };

    // True when the character box at `position` intersects any collider.
    // `supportOut` receives the highest surface under the feet that is close
    // enough to stand on (step-up and support share this scan).
    bool blocked(const Vec3& position, const CharacterShape& shape) const;

    std::vector<Entry> boxes_;
    uint32_t liveCount_ = 0;
    float groundY_ = 0.0f;
};

}  // namespace mge
