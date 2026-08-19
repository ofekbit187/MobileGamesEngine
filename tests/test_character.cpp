// Phase 8 (universal side): characters, factions, mortality/loot, equipment,
// and the basic AI state machine — deterministic, headless.

#include "mge/framework/ai.h"
#include "mge/framework/character.h"
#include "test_framework.h"

using namespace mge;

namespace {

EntityId makeCharacter(World& world, CharacterSystem& characters, Vec3 pos,
                       FactionId faction, float health = 1.0f) {
    const EntityId entity = world.spawn();
    TransformComponent transform;
    transform.position = pos;
    world.setTransform(entity, transform);
    world.setMovement(entity, MovementComponent{{}, 6.0f});
    CharacterComponent* character = characters.attach(entity);
    character->faction = faction;
    character->health = character->maxHealth = health;
    return entity;
}

}  // namespace

MGE_TEST(player_is_just_a_character) {
    // P9: same component set; only the controller differs, and swapping is
    // one assignment.
    World world(32);
    CharacterSystem characters(world);
    const EntityId hero = makeCharacter(world, characters, {0, 0, 0}, 0);
    const EntityId villager = makeCharacter(world, characters, {5, 0, 0}, 0);

    characters.get(hero)->controller = ControllerKind::Player;
    characters.get(villager)->controller = ControllerKind::Ai;
    // Both have the FULL feature set — inventory, equipment slots.
    MGE_CHECK(characters.get(villager)->inventory.add(
        {assetIdFromName("item/apple"), "item.apple", 2, {1, 0, 0, 1}}));
    MGE_CHECK(characters.get(hero)->inventory.add(
        {assetIdFromName("item/apple"), "item.apple", 2, {1, 0, 0, 1}}));

    // Possession: swap who the player controller steers.
    characters.get(hero)->controller = ControllerKind::Ai;
    characters.get(villager)->controller = ControllerKind::Player;
    MGE_CHECK(characters.get(villager)->controller == ControllerKind::Player);
}

MGE_TEST(factions_and_stances) {
    World world(32);
    CharacterSystem characters(world);
    const EntityId villager = makeCharacter(world, characters, {0, 0, 0}, 0);
    const EntityId guard = makeCharacter(world, characters, {1, 0, 0}, 1);
    const EntityId wolf = makeCharacter(world, characters, {2, 0, 0}, 2);

    characters.factions().set(0, 1, Stance::Ally);
    characters.factions().set(1, 2, Stance::Enemy);
    characters.factions().set(0, 2, Stance::Enemy);

    MGE_CHECK(characters.stanceBetween(villager, guard) == Stance::Ally);
    MGE_CHECK(characters.stanceBetween(guard, wolf) == Stance::Enemy);
    MGE_CHECK(characters.stanceBetween(wolf, villager) == Stance::Enemy);  // symmetric
    // Same faction defaults to ally.
    const EntityId villager2 = makeCharacter(world, characters, {3, 0, 0}, 0);
    MGE_CHECK(characters.stanceBetween(villager, villager2) == Stance::Ally);
}

MGE_TEST(mortality_drops_loot) {
    World world(32);
    CharacterSystem characters(world);
    const EntityId bandit = makeCharacter(world, characters, {0, 0, 0}, 3);
    CharacterComponent* character = characters.get(bandit);
    character->inventory.add({assetIdFromName("item/coin"), "item.coin", 12, {1, 1, 0, 1}});
    character->inventory.add({assetIdFromName("item/rope"), "item.rope", 1, {1, 1, 1, 1}});
    // Equip a sword from inventory.
    character->inventory.add({assetIdFromName("item/sword"), "item.sword", 1, {1, 1, 1, 1}});
    MGE_CHECK(characters.equip(bandit, 2, EquipSlot::HeldMain));
    MGE_CHECK(character->equipment[static_cast<size_t>(EquipSlot::HeldMain)].item.count == 1);

    // Wounded but alive.
    MGE_CHECK(!characters.damage(bandit, 0.5f));
    MGE_CHECK(character->alive);

    // Killing blow: loot = inventory + equipment.
    CharacterSystem::DroppedLoot loot;
    MGE_CHECK(characters.damage(bandit, 0.6f, &loot));
    MGE_CHECK(!character->alive);
    MGE_CHECK(loot.count == 3);  // coins, rope, sword
    MGE_CHECK(character->inventory.size() == 0);
    // The dead don't take further damage.
    MGE_CHECK(!characters.damage(bandit, 1.0f));
}

MGE_TEST(equipment_slots_swap_and_sheathe) {
    World world(32);
    CharacterSystem characters(world);
    const EntityId hero = makeCharacter(world, characters, {0, 0, 0}, 0);
    CharacterComponent* character = characters.get(hero);
    character->inventory.add({assetIdFromName("item/sword"), "item.sword", 1, {1, 1, 1, 1}});
    character->inventory.add({assetIdFromName("item/axe"), "item.axe", 1, {1, 1, 1, 1}});
    character->inventory.add({assetIdFromName("wear/tunic"), "wear.tunic", 1, {1, 1, 1, 1}});

    MGE_CHECK(characters.equip(hero, 0, EquipSlot::HeldMain));
    // Equipping the axe swaps the sword back into the inventory.
    MGE_CHECK(characters.equip(hero, 0, EquipSlot::HeldMain));  // axe was index 0 after removal
    MGE_CHECK(character->equipment[static_cast<size_t>(EquipSlot::HeldMain)].item.asset ==
              assetIdFromName("item/axe"));
    bool swordBack = false;
    for (uint32_t i = 0; i < character->inventory.size(); ++i) {
        if (character->inventory.at(i)->asset == assetIdFromName("item/sword")) swordBack = true;
    }
    MGE_CHECK(swordBack);

    // Wearable layer + sheathing.
    for (uint32_t i = 0; i < character->inventory.size(); ++i) {
        if (character->inventory.at(i)->asset == assetIdFromName("wear/tunic")) {
            MGE_CHECK(characters.equip(hero, i, EquipSlot::Torso, 1));
        }
    }
    MGE_CHECK(characters.setSheathed(hero, true));
    MGE_CHECK(character->equipment[static_cast<size_t>(EquipSlot::HeldMain)].sheathed);
    MGE_CHECK(characters.unequip(hero, EquipSlot::HeldMain));
    MGE_CHECK(!characters.setSheathed(hero, true));  // nothing held now
}

// ------------------------------- AI ----------------------------------------

MGE_TEST(ai_wander_stays_near_home) {
    World world(64);
    CharacterSystem characters(world);
    AiSystem ai(world, characters);
    const EntityId deer = makeCharacter(world, characters, {50, 0, 50}, 2);
    AiProfile profile;
    profile.canWander = true;
    profile.homeRadius = 6.0f;
    MGE_CHECK(ai.attach(deer, profile));
    MGE_CHECK(characters.get(deer)->controller == ControllerKind::Ai);

    for (int i = 0; i < 60 * 30; ++i) {  // 30 seconds
        ai.step(1.0f / 60.0f);
        world.step(1.0 / 60.0);
    }
    const Vec3 pos = world.transform(deer)->position;
    MGE_CHECK((pos - Vec3{50, 0, 50}).length() < 10.0f);  // never ran off
}

MGE_TEST(ai_guard_chases_attacks_and_returns) {
    World world(64);
    CharacterSystem characters(world);
    AiSystem ai(world, characters);
    characters.factions().set(1, 3, Stance::Enemy);

    const EntityId guard = makeCharacter(world, characters, {0, 0, 0}, 1);
    const EntityId bandit = makeCharacter(world, characters, {8, 0, 0}, 3, 0.5f);
    AiProfile guardProfile;
    guardProfile.aggressive = true;
    guardProfile.canWander = false;
    MGE_CHECK(ai.attach(guard, guardProfile));

    // Bandit in sight -> chase.
    ai.step(1.0f / 60.0f);
    MGE_CHECK(ai.stateOf(guard) == AiState::Chase);

    // Simulate until the attack lands and the bandit dies.
    bool banditDied = false;
    for (int i = 0; i < 60 * 20 && !banditDied; ++i) {
        ai.step(1.0f / 60.0f);
        world.step(1.0 / 60.0);
        banditDied = !characters.get(bandit)->alive;
    }
    MGE_CHECK(banditDied);

    // Threat gone -> return home, then settle.
    for (int i = 0; i < 60 * 20; ++i) {
        ai.step(1.0f / 60.0f);
        world.step(1.0 / 60.0);
    }
    MGE_CHECK((world.transform(guard)->position).length() < 1.5f);
    MGE_CHECK(ai.stateOf(guard) == AiState::Idle);
}

MGE_TEST(ai_fearful_flees) {
    World world(64);
    CharacterSystem characters(world);
    AiSystem ai(world, characters);
    characters.factions().set(2, 0, Stance::Enemy);

    const EntityId deer = makeCharacter(world, characters, {0, 0, 0}, 2);
    const EntityId hunter = makeCharacter(world, characters, {5, 0, 0}, 0);
    (void)hunter;
    AiProfile deerProfile;
    deerProfile.fearful = true;
    MGE_CHECK(ai.attach(deer, deerProfile));

    for (int i = 0; i < 60 * 3; ++i) {
        ai.step(1.0f / 60.0f);
        world.step(1.0 / 60.0);
    }
    MGE_CHECK(ai.stateOf(deer) == AiState::Flee);
    // Fled AWAY from the hunter (negative x).
    MGE_CHECK(world.transform(deer)->position.x < -2.0f);
}

MGE_TEST(ai_patrol_walks_the_path) {
    World world(64);
    CharacterSystem characters(world);
    AiSystem ai(world, characters);
    const EntityId watchman = makeCharacter(world, characters, {0, 0, 0}, 1);
    AiProfile profile;
    profile.canPatrol = true;
    profile.canWander = false;
    profile.patrolCount = 2;
    profile.patrolPoints[0] = {6, 0, 0};
    profile.patrolPoints[1] = {0, 0, 6};
    MGE_CHECK(ai.attach(watchman, profile));
    MGE_CHECK(ai.stateOf(watchman) == AiState::Patrol);

    bool reachedFirst = false, reachedSecond = false;
    for (int i = 0; i < 60 * 30; ++i) {
        ai.step(1.0f / 60.0f);
        world.step(1.0 / 60.0);
        const Vec3 pos = world.transform(watchman)->position;
        if ((pos - Vec3{6, 0, 0}).length() < 0.8f) reachedFirst = true;
        if (reachedFirst && (pos - Vec3{0, 0, 6}).length() < 0.8f) reachedSecond = true;
    }
    MGE_CHECK(reachedFirst);
    MGE_CHECK(reachedSecond);
}
