#pragma once

// The practice yard (task 19.4) — the scripted scene.
//
// A guard walks up to a quintain, draws, swings, connects, recovers, steps
// back and comes in again. It is the scene that answers "can I see it in
// action?" with something that reads as a game rather than a feature list.
//
// WHY IT IS A SCRIPT AND NOT AN AI BEHAVIOUR. A scene is policy; the AI is
// mechanism, and the two must not be confused. There is also a concrete
// reason: `AiSystem`'s attack calls `CharacterSystem::damage` directly, so an
// AI-driven drill would show a guard dealing damage without ever drawing,
// without using the item he holds, and without playing an archetype. This
// script goes through the ordinary character calls instead — the SAME ones the
// player's button takes — which is the only reason watching the guard tells
// you anything true about the game.
//
// WHY IT IS IN A HEADER OF ITS OWN. `device_game.cpp` cannot be compiled by
// any host tool: it includes Android and Vulkan. Everything below is portable
// C++ over the engine's own types, so it lives here where a test can drive it
// against a real World and CharacterSystem and check that the beats actually
// happen. Shipping a scene verified only by "it compiled" is how you find out
// on the owner's phone that the guard swings at thin air.

#include <cmath>

#include "mge/framework/action.h"
#include "mge/framework/character.h"
#include "mge/framework/items.h"
#include "mge/framework/world.h"

namespace mge {

// How the yard is laid out and paced. Content, not mechanism — every number
// here is a scene decision.
struct PracticeYardConfig {
    float standoff = 1.5f;       // where the guard plants his feet to swing
    float approachSpeed = 1.5f;  // m/s, a walk rather than a charge
    float stepBackSpeed = 1.1f;
    float drawSeconds = 0.45f;   // the draw is its own beat, so it is seen
    float recoverSeconds = 0.85f;
    float stepBackSeconds = 1.4f;
    float refusedRetrySeconds = 0.25f;  // cooldown refused the swing; wait it out
    int blowsPerPass = 3;        // before stepping back and coming in again
};

class PracticeYard {
public:
    enum class Beat : uint8_t { Approach, Draw, Strike, Recover, StepBack };

    // What one update did, so a caller can react without re-deriving it: the
    // device build starts the swing animation and flashes the post, a test
    // asserts the beats happened at all.
    struct Tick {
        bool swung = false;      // a use_held was performed this update
        bool connected = false;  // ...and it landed on the quintain
        AssetId item = kInvalidAsset;  // what was swung, for the motion lookup
    };

    void reset(EntityId guard, EntityId quintain) {
        guard_ = guard;
        quintain_ = quintain;
        beat_ = Beat::Approach;
        timer_ = 0;
        blows_ = 0;
    }

    Beat beat() const { return beat_; }
    int blowsLanded() const { return landed_; }

    Tick update(World& world, CharacterSystem& characters, float dt,
                const PracticeYardConfig& cfg = {}) {
        Tick tick;
        TransformComponent* guardT = world.transform(guard_);
        const TransformComponent* postT = world.transform(quintain_);
        MovementComponent* guardM = world.movement(guard_);
        CharacterComponent* post = characters.get(quintain_);
        if (guardT == nullptr || postT == nullptr || guardM == nullptr || post == nullptr) {
            return tick;
        }

        Vec3 toPost = postT->position - guardT->position;
        toPost.y = 0;
        const float distance = toPost.length();
        const Vec3 heading = distance > 0.001f ? toPost * (1.0f / distance) : Vec3{0, 0, -1};

        // Face the post. `strikeTarget` rejects anything outside a forward
        // cone, so a guard who has not turned swings at nothing all day and
        // looks, convincingly, like he is hitting it.
        guardT->yaw = std::atan2(heading.x, -heading.z);

        timer_ -= dt;
        switch (beat_) {
            case Beat::Approach:
                if (distance > cfg.standoff) {
                    guardM->velocity = heading * cfg.approachSpeed;
                } else {
                    guardM->velocity = {};
                    // The draw is its own beat. `use_held` unsheathes on its
                    // own, but then the sword would arrive in his hand on the
                    // same frame it started moving and the draw would never be
                    // seen.
                    characters.setSheathed(guard_, false);
                    beat_ = Beat::Draw;
                    timer_ = cfg.drawSeconds;
                }
                break;

            case Beat::Draw:
                guardM->velocity = {};
                if (timer_ <= 0) beat_ = Beat::Strike;
                break;

            case Beat::Strike: {
                guardM->velocity = {};
                const ActionResult swing = characters.perform(guard_, actionUseHeld());
                if (swing.performed) {
                    tick.swung = true;
                    tick.item = swing.item.asset;
                    tick.connected = swing.target == quintain_;
                    if (tick.connected) ++landed_;
                    ++blows_;
                    beat_ = Beat::Recover;
                    timer_ = cfg.recoverSeconds;
                } else {
                    // Refused — on cooldown, or nothing in hand. Wait rather
                    // than hammering the call every frame.
                    beat_ = Beat::Recover;
                    timer_ = cfg.refusedRetrySeconds;
                }
                break;
            }

            case Beat::Recover:
                guardM->velocity = {};
                if (timer_ <= 0) {
                    if (blows_ >= cfg.blowsPerPass) {
                        blows_ = 0;
                        beat_ = Beat::StepBack;
                        timer_ = cfg.stepBackSeconds;
                    } else {
                        beat_ = Beat::Strike;
                    }
                }
                break;

            case Beat::StepBack:
                // Back off with the sword still drawn, then come in again.
                // Without this the drill is three seconds of animation on a
                // loop; with it, it is someone practising.
                guardM->velocity = heading * -cfg.stepBackSpeed;
                if (timer_ <= 0) {
                    guardM->velocity = {};
                    beat_ = Beat::Approach;
                }
                break;
        }

        // The post takes it and stays standing. A quintain that fell over
        // after twenty blows would end the scene a minute in.
        if (post->health < post->maxHealth * 0.35f) post->health = post->maxHealth;
        return tick;
    }

private:
    EntityId guard_ = kInvalidEntity;
    EntityId quintain_ = kInvalidEntity;
    Beat beat_ = Beat::Approach;
    float timer_ = 0;
    int blows_ = 0;
    int landed_ = 0;
};

}  // namespace mge
