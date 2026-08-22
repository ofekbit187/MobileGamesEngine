// The scripted scene (task 19.4).
//
// The APK's practice yard, driven here against a real World and
// CharacterSystem. device_game.cpp cannot be compiled by any host tool, so
// without this the scene would ship verified by nothing but "it compiled" —
// and the failure mode is not a crash. It is a guard swinging at thin air on
// the owner's phone, which looks exactly like a guard hitting something until
// you notice the post never reacts.

#include "mge/character/humanoid.h"
#include "mge/character/use_archetypes.h"
#include "mge/framework/character.h"
#include "mge/framework/world.h"
#include "practice_yard.h"
#include "test_framework.h"

using namespace mge;

namespace {

constexpr float kDt = 1.0f / 60.0f;

// Is the guard's main hand still on his back?
bool sheathed(const CharacterSystem& characters, EntityId entity) {
    const CharacterComponent* c = const_cast<CharacterSystem&>(characters).get(entity);
    return c != nullptr && c->equipment[static_cast<size_t>(EquipSlot::HeldMain)].sheathed;
}

// The yard as device_game.cpp builds it: a guard with a sheathed sword, a
// quintain three metres off, and the sword's real ItemUse.
struct Yard {
    World world{64};
    CharacterSystem characters{world};
    ItemUseRegistry itemUses;
    EntityId guard = kInvalidEntity;
    EntityId post = kInvalidEntity;
    PracticeYard drill;

    Yard() {
        ItemUse sword;
        sword.kind = ItemUseKind::Strike;
        sword.range = 2.3f;
        sword.power = 0.25f;
        sword.cooldown = 0.7f;
        sword.archetype = UseArchetype::Swing;
        itemUses.define("item/sword", sword);
        characters.setItemUses(&itemUses);
        // The damage moment lands on the motion's strike (ADR 0021), as it
        // does on the device.
        characters.setStrikeTiming(&strikeDelaySeconds);

        guard = spawn({3.0f, 0, -2.0f});
        characters.get(guard)->inventory.add(
            {assetIdFromName("item/sword"), "item.sword", 1, {1, 1, 1, 1}});
        characters.equip(guard, 0, EquipSlot::HeldMain);
        characters.setSheathed(guard, true);
        grantHumanoidActions(characters, guard);

        post = spawn({5.5f, 0, -4.0f});
        CharacterComponent* c = characters.get(post);
        c->faction = 1;
        c->maxHealth = 40.0f;
        c->health = c->maxHealth;

        drill.reset(guard, post);
    }

    EntityId spawn(Vec3 at) {
        const EntityId e = world.spawn();
        TransformComponent t;
        t.position = at;
        t.prevPosition = at;
        world.setTransform(e, t);
        world.setMovement(e, MovementComponent{{}, 4.0f});
        characters.attach(e);
        return e;
    }

    // One frame, in the order Engine::tick runs it on the device: the drill
    // decides, the world moves what it steered, and the character systems
    // tick. tickEffects counts the use cooldown down AND lands blows whose
    // moment has come — leaving it out is why the first run of this test
    // showed a guard who swung once and then stood there.
    struct Frame {
        PracticeYard::Tick tick;
        int landedOnPost = 0;  // blows that connected with the quintain
        int missed = 0;
    };
    Frame step() {
        Frame frame;
        frame.tick = drill.update(world, characters, kDt);
        world.step(kDt);
        characters.stepLocomotion(kDt);
        characters.tickEffects(kDt);
        // The landing arrives here, frames after the swing was requested.
        CharacterSystem::StrikeOutcome blow;
        while (characters.consumeStrike(blow)) {
            if (blow.target == post) {
                ++frame.landedOnPost;
            } else {
                ++frame.missed;
            }
        }
        return frame;
    }
};

}  // namespace

MGE_TEST(the_guard_walks_up_to_the_quintain_before_swinging) {
    Yard yard;
    const float startDistance =
        (yard.world.transform(yard.post)->position - yard.world.transform(yard.guard)->position)
            .length();
    MGE_CHECK(startDistance > 2.5f);  // he starts out of reach

    // He must close the distance before the script lets him swing.
    bool swungWhileFar = false;
    for (int i = 0; i < 240; ++i) {
        const float distance =
            (yard.world.transform(yard.post)->position - yard.world.transform(yard.guard)->position)
                .length();
        if (yard.step().tick.swung && distance > 2.4f) swungWhileFar = true;
        if (yard.drill.beat() != PracticeYard::Beat::Approach) break;
    }
    MGE_CHECK(!swungWhileFar);
    const float endDistance =
        (yard.world.transform(yard.post)->position - yard.world.transform(yard.guard)->position)
            .length();
    MGE_CHECK(endDistance < startDistance);
}

MGE_TEST(he_draws_before_he_swings) {
    // The draw is its own beat so it is actually seen. If use_held were left
    // to unsheathe on its own, the sword would appear in his hand on the same
    // frame it started moving.
    Yard yard;
    MGE_CHECK(sheathed(yard.characters, yard.guard));

    bool sawDrawBeat = false;
    for (int i = 0; i < 600; ++i) {
        const Yard::Frame frame = yard.step();
        if (yard.drill.beat() == PracticeYard::Beat::Draw) sawDrawBeat = true;
        if (frame.tick.swung) {
            MGE_CHECK(sawDrawBeat);                        // drawn first
            MGE_CHECK(!sheathed(yard.characters, yard.guard));  // and still in hand
            return;
        }
    }
    MGE_CHECK(false);  // never swung at all
}

MGE_TEST(the_blow_connects_with_the_quintain) {
    // The whole point of the scene. A swing that misses looks identical to a
    // swing that lands until you watch the post.
    Yard yard;
    int swings = 0;
    int connections = 0;
    int misses = 0;
    for (int i = 0; i < 1800; ++i) {  // 30 seconds
        const Yard::Frame frame = yard.step();
        if (frame.tick.swung) ++swings;
        connections += frame.landedOnPost;
        misses += frame.missed;
    }
    MGE_CHECK(swings >= 4);
    MGE_CHECK(connections >= 4);
    MGE_CHECK(misses == 0);  // he does not miss what he is standing at
    // At most one swing can still be in flight when the window closes, since
    // the blow now lands after the motion rather than on the press.
    MGE_CHECK(swings - connections <= 1);
    MGE_CHECK(yard.drill.blowsSwung() == swings);
}

MGE_TEST(the_drill_keeps_going_and_the_post_stays_standing) {
    // A quintain that fell over after twenty blows would end the scene a
    // minute in, and a drill that stalled in one beat would too.
    Yard yard;
    bool sawStepBack = false;
    bool sawSecondApproach = false;
    int connections = 0;
    for (int i = 0; i < 3600; ++i) {  // a full minute
        connections += yard.step().landedOnPost;
        if (yard.drill.beat() == PracticeYard::Beat::StepBack) sawStepBack = true;
        if (sawStepBack && yard.drill.beat() == PracticeYard::Beat::Approach) {
            sawSecondApproach = true;
        }
    }
    MGE_CHECK(sawStepBack);        // he backs off between passes
    MGE_CHECK(sawSecondApproach);  // and comes in again
    MGE_CHECK(connections >= 20);
    const CharacterComponent* post = yard.characters.get(yard.post);
    MGE_CHECK(post->alive);
    MGE_CHECK(post->health > 0.0f);
}
