#include "mge/framework/interaction.h"

#include <cmath>

#include "mge/core/log.h"
#include "mge/framework/camera_controller.h"

namespace mge {

namespace {
constexpr const char* kTag = "interaction";
// Eye height for the line-of-sight probe: a chest on the floor should be
// visible over a low wall the character can see across.
constexpr float kEyeHeight = 1.5f;
}  // namespace

InteractionSystem::InteractionSystem(World& world, CharacterSystem& characters,
                                     uint32_t capacity)
    : world_(world),
      characters_(characters),
      capacity_(capacity),
      used_(capacity, 0),
      entities_(capacity),
      components_(capacity) {}

int32_t InteractionSystem::indexOf(EntityId entity) const {
    for (uint32_t i = 0; i < capacity_; ++i) {
        if (used_[i] && entities_[i] == entity) return static_cast<int32_t>(i);
    }
    return -1;
}

InteractableComponent* InteractionSystem::attach(EntityId entity,
                                                 const InteractableComponent& interactable) {
    if (!world_.entities().isAlive(entity)) return nullptr;
    if (InteractableComponent* existing = get(entity)) {
        *existing = interactable;
        return existing;
    }
    for (uint32_t i = 0; i < capacity_; ++i) {
        if (used_[i]) continue;
        used_[i] = 1;
        entities_[i] = entity;
        components_[i] = interactable;
        ++liveCount_;
        return &components_[i];
    }
    MGE_LOGW(kTag, "interactable capacity refused an attach");  // refuse (P1)
    return nullptr;
}

InteractableComponent* InteractionSystem::get(EntityId entity) {
    const int32_t index = indexOf(entity);
    return index >= 0 ? &components_[index] : nullptr;
}

const InteractableComponent* InteractionSystem::get(EntityId entity) const {
    const int32_t index = indexOf(entity);
    return index >= 0 ? &components_[index] : nullptr;
}

void InteractionSystem::detach(EntityId entity) {
    const int32_t index = indexOf(entity);
    if (index < 0) return;
    used_[index] = 0;
    --liveCount_;
}

EntityId InteractionSystem::focus(EntityId actor) const {
    const TransformComponent* actorTransform =
        const_cast<World&>(world_).transform(actor);
    if (actorTransform == nullptr) return kInvalidEntity;
    const Vec3 eye = actorTransform->position + Vec3{0, kEyeHeight, 0};
    const Vec3 forward = yawForward(actorTransform->yaw);

    EntityId best = kInvalidEntity;
    float bestScore = -1.0f;
    for (uint32_t i = 0; i < capacity_; ++i) {
        if (!used_[i] || !components_[i].enabled) continue;
        if (components_[i].kind == InteractionKind::None) continue;
        const EntityId candidate = entities_[i];
        if (candidate == actor) continue;
        if (!world_.entities().isAlive(candidate)) continue;
        const TransformComponent* transform =
            const_cast<World&>(world_).transform(candidate);
        if (transform == nullptr) continue;

        Vec3 delta = transform->position - actorTransform->position;
        delta.y = 0;
        const float distance = delta.length();
        if (distance > components_[i].range) continue;

        // In front of the actor, not merely nearby.
        const float facing = distance > 0.001f ? delta.normalized().dot(forward) : 1.0f;
        if (facing < components_[i].facingDot) continue;

        // Not through a wall.
        if (collision_ != nullptr && distance > 0.001f) {
            const Vec3 target = transform->position + Vec3{0, 0.6f, 0};
            Vec3 toTarget = target - eye;
            const float reach = toTarget.length();
            const RayHit hit = collision_->raycast(eye, toTarget, reach, actor);
            if (hit.hit && hit.entity != candidate && hit.distance < reach - 0.05f) continue;
        }

        // Prefer what the actor is most directly facing, then what is closest.
        const float score = facing * 2.0f - distance * 0.1f;
        if (score > bestScore) {
            bestScore = score;
            best = candidate;
        }
    }
    return best;
}

InteractionResult InteractionSystem::interact(EntityId actor) {
    return interactWith(actor, focus(actor));
}

InteractionResult InteractionSystem::interactWith(EntityId actor, EntityId target) {
    InteractionResult result;
    if (target == kInvalidEntity) return result;
    // Any character may act — but not a dead one (mortality is universal too).
    if (const CharacterComponent* character = characters_.get(actor)) {
        if (!character->alive) return result;
    }
    InteractableComponent* interactable = get(target);
    if (interactable == nullptr || !interactable->enabled) return result;

    result.kind = interactable->kind;
    result.target = target;
    result.collectionId = interactable->collectionId;
    result.payload = interactable->payload;

    switch (interactable->kind) {
        case InteractionKind::PickUp: {
            CharacterComponent* character = characters_.get(actor);
            if (character == nullptr) return result;
            if (!character->inventory.add(interactable->item)) {
                MGE_LOGI(kTag, "inventory full — pick up refused");
                return result;  // full inventory refuses, nothing is lost
            }
            result.item = interactable->item;
            result.handled = true;
            // The item is now carried: it leaves the world.
            interactable->enabled = false;
            detach(target);
            world_.despawn(target);
            break;
        }
        case InteractionKind::Container:
        case InteractionKind::Talk:
        case InteractionKind::Custom:
            // The engine reports; the game presents (a screen, a voice line,
            // whatever it defines). Nothing here assumes a genre.
            result.handled = true;
            break;
        case InteractionKind::None:
            break;
    }
    return result;
}

}  // namespace mge
