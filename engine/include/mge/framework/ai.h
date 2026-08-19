#pragma once

// Basic AI v1 (task 8.8, CHARACTERS.md §7): a data-defined state machine over
// the universal mechanisms — perception + factions decide transitions, the
// movement component carries the result. One profile format serves humanoids,
// animals, and monsters. Later behavior models replace these INTERNALS; the
// controller contract (an entity handed to the AI system) does not change.

#include <cstdint>

#include "mge/framework/character.h"

namespace mge {

enum class AiState : uint8_t {
    Idle = 0,
    Wander,   // roam within homeRadius of home
    Patrol,   // walk a fixed point path
    Chase,    // close on a hostile
    Attack,   // in range: deal damage on a cooldown
    Flee,     // run from the threat
    Return,   // go back home / to the path
};

// ai_profile (data): which behaviors are enabled and their tuning.
struct AiProfile {
    bool canWander = true;
    bool canPatrol = false;
    bool aggressive = false;    // chase/attack enemies on sight
    bool fearful = false;       // flee from enemies on sight
    float wanderSpeed = 1.2f;
    float chaseSpeed = 3.5f;
    float fleeSpeed = 4.0f;
    float homeRadius = 8.0f;
    float attackRange = 1.6f;
    float attackDamage = 0.15f;
    float attackCooldown = 1.2f;  // seconds
    float fleeHealthFraction = 0.25f;  // aggressive characters flee below this
    float giveUpRange = 25.0f;    // chase abandoned beyond this from home
    // Patrol path (world positions); used when canPatrol.
    Vec3 patrolPoints[8];
    uint8_t patrolCount = 0;
};

struct AiAgentState {
    AiState state = AiState::Idle;
    Vec3 home{};
    Vec3 wanderTarget{};
    float stateTime = 0;
    float attackTimer = 0;
    uint8_t patrolIndex = 0;
    EntityId target = kInvalidEntity;
    uint32_t seed = 1;  // deterministic per-agent randomness
};

class AiSystem {
public:
    AiSystem(World& world, CharacterSystem& characters, uint32_t capacity = 256);

    // Registers an entity (must be a character) with a profile; home is its
    // current position. Sets the character's controller to Ai (P9: swapping
    // controllers is one call).
    bool attach(EntityId entity, const AiProfile& profile);
    void detach(EntityId entity);

    // One fixed step for all agents. LOD ticking (distant agents thinking
    // less) hooks in here when streaming-driven activity lands.
    void step(float dt);

    AiState stateOf(EntityId entity) const;

private:
    struct Agent {
        bool used = false;
        EntityId entity = kInvalidEntity;
        AiProfile profile;
        AiAgentState state;
    };

    void stepAgent(Agent& agent, float dt);
    EntityId findNearestEnemy(EntityId self, const Vec3& position, float range) const;
    void moveToward(EntityId entity, const Vec3& target, float speed);
    void stop(EntityId entity);
    float random01(Agent& agent) const;

    World& world_;
    CharacterSystem& characters_;
    std::vector<Agent> agents_;
};

}  // namespace mge
