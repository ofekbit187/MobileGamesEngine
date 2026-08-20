#include "mge/framework/collision.h"

#include <cmath>

#include "mge/core/log.h"

namespace mge {

namespace {

constexpr const char* kTag = "collision";
// Colliders and characters are separated by a hair so "touching" never reads
// as "overlapping" on the next step (the classic sticky-wall bug).
constexpr float kSkin = 0.001f;

bool overlapsBox(const Aabb& a, const Aabb& b) {
    return a.min.x < b.max.x - kSkin && a.max.x > b.min.x + kSkin &&
           a.min.y < b.max.y - kSkin && a.max.y > b.min.y + kSkin &&
           a.min.z < b.max.z - kSkin && a.max.z > b.min.z + kSkin;
}

}  // namespace

CollisionWorld::CollisionWorld(uint32_t capacity) : boxes_(capacity) {}

Aabb CollisionWorld::characterBounds(const Vec3& position, const CharacterShape& shape) {
    return Aabb{{position.x - shape.radius, position.y, position.z - shape.radius},
                {position.x + shape.radius, position.y + shape.height,
                 position.z + shape.radius}};
}

int32_t CollisionWorld::addBox(const Aabb& bounds, EntityId owner) {
    for (uint32_t i = 0; i < boxes_.size(); ++i) {
        if (boxes_[i].used) continue;
        boxes_[i].bounds = bounds;
        boxes_[i].owner = owner;
        boxes_[i].used = true;
        ++liveCount_;
        return static_cast<int32_t>(i);
    }
    MGE_LOGW(kTag, "collider capacity %zu refused an add", boxes_.size());
    return kInvalidCollider;  // refuse, never grow (P1)
}

void CollisionWorld::removeBox(int32_t handle) {
    if (handle < 0 || static_cast<size_t>(handle) >= boxes_.size()) return;
    if (!boxes_[handle].used) return;
    boxes_[handle].used = false;
    --liveCount_;
}

void CollisionWorld::removeByEntity(EntityId entity) {
    for (uint32_t i = 0; i < boxes_.size(); ++i) {
        if (boxes_[i].used && boxes_[i].owner == entity) {
            boxes_[i].used = false;
            --liveCount_;
        }
    }
}

void CollisionWorld::clear() {
    for (Entry& entry : boxes_) entry.used = false;
    liveCount_ = 0;
}

bool CollisionWorld::overlaps(const Aabb& box, EntityId ignore) const {
    for (const Entry& entry : boxes_) {
        if (!entry.used) continue;
        if (ignore != kInvalidEntity && entry.owner == ignore) continue;
        if (overlapsBox(box, entry.bounds)) return true;
    }
    return false;
}

bool CollisionWorld::blocked(const Vec3& position, const CharacterShape& shape) const {
    return overlaps(characterBounds(position, shape));
}

RayHit CollisionWorld::raycast(const Vec3& origin, const Vec3& direction, float maxDistance,
                               EntityId ignore) const {
    RayHit best;
    best.distance = maxDistance;
    const Vec3 dir = direction.normalized();
    if (dir.lengthSq() < 0.5f) return RayHit{};  // degenerate direction

    for (const Entry& entry : boxes_) {
        if (!entry.used) continue;
        if (ignore != kInvalidEntity && entry.owner == ignore) continue;
        // Slab test.
        float tMin = 0.0f;
        float tMax = maxDistance;
        int axisHit = -1;
        float signHit = 1.0f;
        bool miss = false;
        const float originAxis[3] = {origin.x, origin.y, origin.z};
        const float dirAxis[3] = {dir.x, dir.y, dir.z};
        const float boxMin[3] = {entry.bounds.min.x, entry.bounds.min.y, entry.bounds.min.z};
        const float boxMax[3] = {entry.bounds.max.x, entry.bounds.max.y, entry.bounds.max.z};
        for (int axis = 0; axis < 3 && !miss; ++axis) {
            if (std::fabs(dirAxis[axis]) < 1e-6f) {
                if (originAxis[axis] < boxMin[axis] || originAxis[axis] > boxMax[axis]) {
                    miss = true;
                }
                continue;
            }
            const float inverse = 1.0f / dirAxis[axis];
            float near = (boxMin[axis] - originAxis[axis]) * inverse;
            float far = (boxMax[axis] - originAxis[axis]) * inverse;
            float sign = -1.0f;
            if (near > far) {
                const float swap = near;
                near = far;
                far = swap;
                sign = 1.0f;
            }
            if (near > tMin) {
                tMin = near;
                axisHit = axis;
                signHit = sign;
            }
            if (far < tMax) tMax = far;
            if (tMin > tMax) miss = true;
        }
        if (miss || tMin >= best.distance) continue;

        best.hit = true;
        best.distance = tMin;
        best.point = origin + dir * tMin;
        best.normal = {0, 0, 0};
        if (axisHit == 0) best.normal.x = signHit;
        if (axisHit == 1) best.normal.y = signHit;
        if (axisHit == 2) best.normal.z = signHit;
        best.entity = entry.owner;
    }
    return best.hit ? best : RayHit{};
}

// The highest surface under a character standing at `position` that is close
// enough to stand on, else the ground plane. Shared by step-up and landing.
float CollisionWorld::supportUnder(const Vec3& position, const CharacterShape& shape) const {
    float support = groundY_;
    const Aabb footprint = characterBounds(position, shape);
    for (const Entry& entry : boxes_) {
        if (!entry.used) continue;
        const bool overlapsXZ = footprint.min.x < entry.bounds.max.x &&
                                footprint.max.x > entry.bounds.min.x &&
                                footprint.min.z < entry.bounds.max.z &&
                                footprint.max.z > entry.bounds.min.z;
        if (!overlapsXZ) continue;
        // Only surfaces at or below stepping height count as ground; a roof
        // overhead must not teleport the character onto it.
        if (entry.bounds.max.y <= position.y + shape.stepHeight &&
            entry.bounds.max.y > support) {
            support = entry.bounds.max.y;
        }
    }
    return support;
}

MoveResult CollisionWorld::moveCharacter(const Vec3& position, const CharacterShape& shape,
                                         const Vec3& delta) const {
    constexpr float kLandEpsilon = 1e-4f;
    MoveResult result;
    Vec3 current = position;

    // Were the feet down before any of this? A character in mid-air may not
    // climb ledges — stepping up is something you do while standing.
    const float startSupport = supportUnder(position, shape);
    const bool wasStanding = position.y <= startSupport + kLandEpsilon;

    // Horizontal axes are resolved one at a time so a blocked X still allows
    // the Z component through: that IS sliding along a wall.
    const float horizontal[2] = {delta.x, delta.z};
    for (int axis = 0; axis < 2; ++axis) {
        if (horizontal[axis] == 0.0f) continue;
        Vec3 candidate = current;
        if (axis == 0) {
            candidate.x += horizontal[0];
        } else {
            candidate.z += horizontal[1];
        }
        if (!blocked(candidate, shape)) {
            current = candidate;
            continue;
        }
        // Blocked — but for someone on their feet a low ledge is a step, not
        // a wall. Try again lifted.
        Vec3 stepped = candidate;
        stepped.y += shape.stepHeight;
        if (wasStanding && !blocked(stepped, shape)) {
            current = stepped;
            result.steppedUp = true;
            continue;
        }
        if (axis == 0) {
            result.blockedX = true;
        } else {
            result.blockedZ = true;
        }
    }

    // What is under the feet where the character ended up horizontally. The
    // scan uses the pre-fall height, so a fast fall lands on the platform it
    // was above rather than tunnelling through it.
    const float support = supportUnder(current, shape);

    if (delta.y > 0.0f) {
        Vec3 candidate = current;
        candidate.y += delta.y;
        if (blocked(candidate, shape)) {
            result.hitCeiling = true;  // the rise stops; the caller kills the
                                       // upward velocity and the fall begins
        } else {
            current.y = candidate.y;
        }
        result.grounded = current.y <= support + kLandEpsilon;
    } else if (delta.y < 0.0f) {
        current.y += delta.y;
        if (current.y <= support) {
            current.y = support;  // landed
            result.grounded = true;
        }
    } else {
        // The Phase 11 walker: no vertical intent at all, so the feet simply
        // follow the surface under them. Callers that do gravity never take
        // this path.
        current.y = support;
        result.grounded = true;
    }

    result.position = current;
    return result;
}

}  // namespace mge
