#include "mge/framework/ai.h"

#include <cmath>

namespace mge {

AiSystem::AiSystem(World& world, CharacterSystem& characters, uint32_t capacity)
    : world_(world), characters_(characters), agents_(capacity) {}

bool AiSystem::attach(EntityId entity, const AiProfile& profile) {
    CharacterComponent* character = characters_.get(entity);
    const TransformComponent* transform = world_.transform(entity);
    if (character == nullptr || transform == nullptr) return false;
    for (size_t i = 0; i < agents_.size(); ++i) {
        if (agents_[i].used) continue;
        agents_[i].used = true;
        agents_[i].entity = entity;
        agents_[i].profile = profile;
        agents_[i].state = AiAgentState{};
        agents_[i].state.home = transform->position;
        agents_[i].state.wanderTarget = transform->position;
        agents_[i].state.state = profile.canPatrol && profile.patrolCount > 0 ? AiState::Patrol
                                 : profile.canWander                          ? AiState::Wander
                                                                              : AiState::Idle;
        agents_[i].state.seed = static_cast<uint32_t>(entity.index * 2654435761u + 12345u);
        character->controller = ControllerKind::Ai;
        character->aiIndex = static_cast<uint16_t>(i);
        return true;
    }
    return false;  // capacity: refuse
}

void AiSystem::detach(EntityId entity) {
    for (Agent& agent : agents_) {
        if (agent.used && agent.entity == entity) {
            agent.used = false;
            if (CharacterComponent* character = characters_.get(entity)) {
                character->controller = ControllerKind::None;
                character->aiIndex = UINT16_MAX;
            }
        }
    }
}

AiState AiSystem::stateOf(EntityId entity) const {
    for (const Agent& agent : agents_) {
        if (agent.used && agent.entity == entity) return agent.state.state;
    }
    return AiState::Idle;
}

float AiSystem::random01(Agent& agent) const {
    agent.state.seed = agent.state.seed * 1664525u + 1013904223u;
    return static_cast<float>(agent.state.seed >> 8) / 16777216.0f;
}

EntityId AiSystem::findNearestEnemy(EntityId self, const Vec3& position, float range) const {
    EntityId best = kInvalidEntity;
    float bestDistance = range;
    characters_.forEach([&](EntityId other, CharacterComponent& character) {
        if (other == self || !character.alive) return;
        if (characters_.stanceBetween(self, other) != Stance::Enemy) return;
        const TransformComponent* transform = world_.transform(other);
        if (transform == nullptr) return;
        const TransformComponent* selfTransform =
            const_cast<World&>(world_).transform(self);
        (void)selfTransform;
        const float distance = (transform->position - position).length();
        if (distance < bestDistance) {
            bestDistance = distance;
            best = other;
        }
    });
    return best;
}

void AiSystem::moveToward(EntityId entity, const Vec3& target, float speed) {
    const TransformComponent* transform = world_.transform(entity);
    if (transform == nullptr) return;
    Vec3 delta = target - transform->position;
    delta.y = 0;
    const float distance = delta.length();
    // Same intent contract the player controller uses (task 8.2, P9).
    CharacterIntent intent;
    if (distance >= 0.05f) {
        intent.move = delta * (1.0f / distance);
        intent.speed = speed;
        intent.faceMove = true;
    }
    applyIntent(world_, entity, intent);
}

void AiSystem::stop(EntityId entity) {
    applyIntent(world_, entity, CharacterIntent{});
}

void AiSystem::step(float dt) {
    for (Agent& agent : agents_) {
        if (!agent.used) continue;
        if (!world_.entities().isAlive(agent.entity)) {
            agent.used = false;
            continue;
        }
        const CharacterComponent* character = characters_.get(agent.entity);
        if (character == nullptr || !character->alive) {
            agent.used = false;
            continue;
        }
        stepAgent(agent, dt);
    }
}

void AiSystem::stepAgent(Agent& agent, float dt) {
    const AiProfile& profile = agent.profile;
    AiAgentState& state = agent.state;
    CharacterComponent& character = *characters_.get(agent.entity);
    const TransformComponent* transform = world_.transform(agent.entity);
    if (transform == nullptr) return;
    const Vec3 position = transform->position;
    state.stateTime += dt;
    state.attackTimer -= dt;

    // Perception (universal hooks + factions).
    const EntityId enemy = findNearestEnemy(agent.entity, position, character.sightRange);

    // Global transitions first.
    if (enemy != kInvalidEntity) {
        const bool lowHealth = character.health / character.maxHealth < profile.fleeHealthFraction;
        if (profile.fearful || (profile.aggressive && lowHealth)) {
            if (state.state != AiState::Flee) {
                state.state = AiState::Flee;
                state.target = enemy;
                state.stateTime = 0;
            }
        } else if (profile.aggressive &&
                   (state.state == AiState::Idle || state.state == AiState::Wander ||
                    state.state == AiState::Patrol || state.state == AiState::Return)) {
            state.state = AiState::Chase;
            state.target = enemy;
            state.stateTime = 0;
            // Contact: draw the held weapon (CHARACTERS.md §6.1).
            characters_.setSheathed(agent.entity, false);
        }
    }

    // Anything worth picking up nearby? (P9: NPCs use the interaction system,
    // not a private copy of it.) Combat states are never interrupted for it.
    if (profile.gathers && interactions_ != nullptr &&
        (state.state == AiState::Idle || state.state == AiState::Wander ||
         state.state == AiState::Patrol)) {
        const EntityId prize = interactions_->nearestOfKind(position, profile.gatherRange,
                                                            InteractionKind::PickUp);
        if (prize != kInvalidEntity) {
            state.state = AiState::Gather;
            state.target = prize;
            state.stateTime = 0;
        }
    }

    switch (state.state) {
        case AiState::Idle:
            stop(agent.entity);
            if (profile.canWander && state.stateTime > 2.0f) {
                state.state = AiState::Wander;
                state.stateTime = 0;
            }
            break;

        case AiState::Wander: {
            if ((state.wanderTarget - position).length() < 0.3f) {
                const float angle = random01(agent) * 2.0f * kPi;
                const float radius = random01(agent) * profile.homeRadius;
                state.wanderTarget = state.home + Vec3{std::cos(angle) * radius, 0,
                                                       std::sin(angle) * radius};
            }
            moveToward(agent.entity, state.wanderTarget, profile.wanderSpeed);
            if (state.stateTime > 6.0f) {
                state.state = AiState::Idle;
                state.stateTime = 0;
            }
            break;
        }

        case AiState::Patrol: {
            const Vec3 point = profile.patrolPoints[state.patrolIndex % profile.patrolCount];
            moveToward(agent.entity, point, profile.wanderSpeed);
            if ((point - position).length() < 0.5f) {
                state.patrolIndex = static_cast<uint8_t>((state.patrolIndex + 1) %
                                                         profile.patrolCount);
            }
            break;
        }

        case AiState::Chase: {
            const CharacterComponent* target = characters_.get(state.target);
            const TransformComponent* targetTransform = world_.transform(state.target);
            if (target == nullptr || !target->alive || targetTransform == nullptr ||
                (position - state.home).length() > profile.giveUpRange) {
                state.state = AiState::Return;
                state.stateTime = 0;
                characters_.setSheathed(agent.entity, true);  // combat over
                break;
            }
            const float distance = (targetTransform->position - position).length();
            if (distance <= profile.attackRange) {
                state.state = AiState::Attack;
                state.stateTime = 0;
                break;
            }
            moveToward(agent.entity, targetTransform->position, profile.chaseSpeed);
            break;
        }

        case AiState::Attack: {
            const CharacterComponent* target = characters_.get(state.target);
            const TransformComponent* targetTransform = world_.transform(state.target);
            if (target == nullptr || !target->alive || targetTransform == nullptr) {
                state.state = AiState::Return;
                state.stateTime = 0;
                characters_.setSheathed(agent.entity, true);  // combat over
                break;
            }
            const float distance = (targetTransform->position - position).length();
            if (distance > profile.attackRange * 1.3f) {
                state.state = AiState::Chase;
                state.stateTime = 0;
                break;
            }
            stop(agent.entity);
            if (state.attackTimer <= 0.0f) {
                characters_.damage(state.target, profile.attackDamage);
                state.attackTimer = profile.attackCooldown;
            }
            break;
        }

        case AiState::Flee: {
            const TransformComponent* threat = world_.transform(state.target);
            if (threat == nullptr || enemy == kInvalidEntity) {
                if (state.stateTime > 3.0f) {
                    state.state = AiState::Return;
                    state.stateTime = 0;
                }
                break;
            }
            Vec3 away = position - threat->position;
            away.y = 0;
            moveToward(agent.entity, position + away.normalized() * 5.0f, profile.fleeSpeed);
            break;
        }

        case AiState::Gather: {
            const TransformComponent* prize = world_.transform(state.target);
            if (prize == nullptr || interactions_ == nullptr || state.stateTime > 8.0f) {
                state.state = AiState::Return;  // gone, taken by someone else
                state.stateTime = 0;
                break;
            }
            moveToward(agent.entity, prize->position, profile.wanderSpeed);
            if ((prize->position - position).length() < 1.4f) {
                // The same call the player's tap makes — actor, not "player".
                interactions_->interactWith(agent.entity, state.target);
                state.state = AiState::Wander;
                state.stateTime = 0;
            }
            break;
        }

        case AiState::Return: {
            moveToward(agent.entity, state.home, profile.wanderSpeed * 1.5f);
            if ((state.home - position).length() < 0.6f) {
                state.state = profile.canPatrol && profile.patrolCount > 0 ? AiState::Patrol
                              : profile.canWander                          ? AiState::Wander
                                                                           : AiState::Idle;
                state.stateTime = 0;
            }
            break;
        }
    }
}

}  // namespace mge
