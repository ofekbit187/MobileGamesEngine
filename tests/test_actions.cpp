// Phase 12: a character's actions — what it CAN do. These test the three
// things the owner dictated: the action set comes from what you are, a
// humanoid can leave the ground, and one "use" action means completely
// different things depending on what is in the hand.

#include <cmath>

#include "mge/character/humanoid.h"
#include "mge/character/use_archetypes.h"
#include "mge/framework/action.h"
#include "mge/framework/character.h"
#include "mge/framework/collision.h"
#include "mge/framework/interaction.h"
#include "test_framework.h"

using namespace mge;

namespace {

EntityId spawnCharacterAt(World& world, CharacterSystem& characters, Vec3 position,
                          float yaw = 0.0f) {
    const EntityId entity = world.spawn();
    TransformComponent transform;
    transform.position = position;
    transform.prevPosition = position;
    transform.yaw = yaw;
    world.setTransform(entity, transform);
    world.setMovement(entity, MovementComponent{{}, 4.0f});
    characters.attach(entity);
    return entity;
}

// One fixed step of the loop a game runs: integrate, then resolve bodies.
void stepWorld(World& world, CharacterSystem& characters, float dt) {
    world.step(dt);
    characters.stepLocomotion(dt);
    characters.tickEffects(dt);
}

}  // namespace

// --- the vocabulary comes from what you are ---------------------------------

MGE_TEST(actions_universal_tier_needs_no_flag) {
    World world(64);
    CharacterSystem characters(world);
    const EntityId anything = spawnCharacterAt(world, characters, {0, 0, 0});

    // Every character can interact, by existing — nobody granted this.
    MGE_CHECK(characters.can(anything, actionInteract()));
    // And nothing else comes for free: a bare character is not a humanoid.
    MGE_CHECK(!characters.can(anything, actionJump()));
    MGE_CHECK(!characters.can(anything, actionWalk()));
    MGE_CHECK(!characters.can(anything, actionUseHeld()));
}

MGE_TEST(actions_body_tier_is_granted_by_the_body) {
    World world(64);
    CharacterSystem characters(world);
    const EntityId person = spawnCharacterAt(world, characters, {0, 0, 0});
    const EntityId wolf = spawnCharacterAt(world, characters, {5, 0, 0});

    grantHumanoidActions(characters, person);
    MGE_CHECK(characters.can(person, actionWalk()));
    MGE_CHECK(characters.can(person, actionJump()));
    MGE_CHECK(characters.can(person, actionUseHeld()));

    // A creature declares its own set; it inherits no humanoid assumption.
    characters.grant(wolf, actionWalk());
    MGE_CHECK(characters.can(wolf, actionWalk()));
    MGE_CHECK(!characters.can(wolf, actionJump()));
    MGE_CHECK(!characters.can(wolf, actionUseHeld()));

    // Performing what you don't have fails on WHAT YOU ARE, distinctly.
    const ActionResult refused = characters.perform(wolf, actionJump());
    MGE_CHECK(!refused.performed);
    MGE_CHECK(refused.refusal == ActionRefusal::NotGranted);
}

MGE_TEST(actions_revoke_and_capacity) {
    World world(64);
    CharacterSystem characters(world);
    const EntityId person = spawnCharacterAt(world, characters, {0, 0, 0});
    grantHumanoidActions(characters, person);

    characters.revoke(person, actionJump());
    MGE_CHECK(!characters.can(person, actionJump()));
    MGE_CHECK(characters.can(person, actionWalk()));  // only the one named

    // The set refuses past its cap like every other budget (P1).
    ActionSet set;
    for (uint32_t i = 0; i < ActionSet::kCapacity; ++i) {
        char name[32];
        name[0] = 'a';
        name[1] = static_cast<char>('0' + (i % 10));
        name[2] = static_cast<char>('A' + (i / 10));
        name[3] = 0;
        MGE_CHECK(set.grant(actionId(name)));
    }
    MGE_CHECK(!set.grant(actionId("one/too/many")));
    MGE_CHECK(set.size() == ActionSet::kCapacity);
}

// --- jump: the character leaves the ground -----------------------------------

MGE_TEST(jump_rises_and_gravity_brings_you_back) {
    World world(64);
    CharacterSystem characters(world);
    CollisionWorld collision(64);
    characters.setCollision(&collision);

    const EntityId person = spawnCharacterAt(world, characters, {0, 0, 0});
    grantHumanoidActions(characters, person);

    const float dt = 1.0f / 60.0f;
    stepWorld(world, characters, dt);
    MGE_CHECK(characters.get(person)->grounded);

    const ActionResult jumped = characters.perform(person, actionJump());
    MGE_CHECK(jumped.performed);

    // Refused in mid-air — granted, but not right now.
    stepWorld(world, characters, dt);
    const ActionResult again = characters.perform(person, actionJump());
    MGE_CHECK(!again.performed);
    MGE_CHECK(again.refusal == ActionRefusal::NotGrounded);

    float peak = 0;
    bool landed = false;
    for (int i = 0; i < 240 && !landed; ++i) {
        stepWorld(world, characters, dt);
        const float y = world.transform(person)->position.y;
        if (y > peak) peak = y;
        if (i > 10 && characters.get(person)->grounded) landed = true;
    }
    MGE_CHECK(peak > 0.4f);   // it actually left the floor
    MGE_CHECK(landed);        // and gravity brought it down
    MGE_CHECK(std::fabs(world.transform(person)->position.y) < 0.01f);
    MGE_CHECK(characters.perform(person, actionJump()).performed);  // ready again
}

MGE_TEST(jump_hits_a_ceiling_and_falls) {
    World world(64);
    CharacterSystem characters(world);
    CollisionWorld collision(64);
    characters.setCollision(&collision);
    // A low roof: a 1.8 m character jumping under it hits its head.
    collision.addBox(Aabb::fromCenterExtents({0, 2.1f, 0}, {2.0f, 0.1f, 2.0f}));

    const EntityId person = spawnCharacterAt(world, characters, {0, 0, 0});
    grantHumanoidActions(characters, person);

    const float dt = 1.0f / 60.0f;
    stepWorld(world, characters, dt);
    MGE_CHECK(characters.perform(person, actionJump()).performed);

    float peak = 0;
    for (int i = 0; i < 240; ++i) {
        stepWorld(world, characters, dt);
        peak = world.transform(person)->position.y > peak ? world.transform(person)->position.y
                                                          : peak;
    }
    MGE_CHECK(peak < 0.25f);                       // the roof stopped the rise
    MGE_CHECK(characters.get(person)->grounded);   // and it came back down
}

MGE_TEST(jump_falls_off_a_ledge_instead_of_hovering) {
    World world(64);
    CharacterSystem characters(world);
    CollisionWorld collision(64);
    characters.setCollision(&collision);
    // A platform to walk off the edge of.
    collision.addBox(Aabb::fromCenterExtents({0, 0.5f, 0}, {1.0f, 0.5f, 1.0f}));

    const EntityId person = spawnCharacterAt(world, characters, {0, 1.0f, 0});
    grantHumanoidActions(characters, person);
    const float dt = 1.0f / 60.0f;
    stepWorld(world, characters, dt);
    MGE_CHECK(characters.get(person)->grounded);
    MGE_CHECK(std::fabs(world.transform(person)->position.y - 1.0f) < 0.01f);

    // Walk east off the platform.
    world.movement(person)->velocity = {3.0f, 0, 0};
    for (int i = 0; i < 120; ++i) stepWorld(world, characters, dt);

    MGE_CHECK(world.transform(person)->position.x > 1.5f);              // off the edge
    MGE_CHECK(std::fabs(world.transform(person)->position.y) < 0.01f);  // down on the ground
    MGE_CHECK(characters.get(person)->grounded);
}

// --- one action, whatever the item means -------------------------------------

MGE_TEST(use_held_does_what_the_item_says) {
    World world(64);
    CharacterSystem characters(world);
    ItemUseRegistry itemUses;
    characters.setItemUses(&itemUses);

    ItemUse sword;
    sword.kind = ItemUseKind::Strike;
    sword.range = 2.0f;
    sword.power = 0.3f;
    sword.cooldown = 0.6f;
    sword.animKey = "anim/swing";
    itemUses.define("item/sword", sword);

    ItemUse apple;
    apple.kind = ItemUseKind::Consume;
    apple.power = 0.25f;   // heals
    apple.cooldown = 0.2f;
    apple.effectId = assetIdFromName("effect/fed");
    apple.effectMagnitude = 1.0f;
    apple.effectDuration = 30.0f;
    itemUses.define("item/apple", apple);

    ItemUse torch;
    torch.kind = ItemUseKind::Toggle;
    torch.cooldown = 0.1f;
    itemUses.define("item/torch", torch);

    const EntityId knight = spawnCharacterAt(world, characters, {0, 0, 0}, kPi);
    const EntityId victim = spawnCharacterAt(world, characters, {0, 0, 1.2f});
    grantHumanoidActions(characters, knight);
    characters.factions().set(0, 1, Stance::Enemy);
    characters.get(victim)->faction = 1;

    // --- the same action, holding a sword: a strike that hurts someone.
    CharacterComponent* character = characters.get(knight);
    character->inventory.add({assetIdFromName("item/sword"), "item.sword", 1, {1, 1, 1, 1}});
    MGE_CHECK(characters.equip(knight, 0, EquipSlot::HeldMain));
    characters.setSheathed(knight, true);

    const float victimHealthBefore = characters.get(victim)->health;
    const ActionResult swing = characters.perform(knight, actionUseHeld());
    MGE_CHECK(swing.performed);
    MGE_CHECK(swing.useKind == ItemUseKind::Strike);
    MGE_CHECK(swing.target == victim);
    MGE_CHECK(characters.get(victim)->health < victimHealthBefore - 0.2f);
    // Using a sheathed weapon draws it first.
    MGE_CHECK(!character->equipment[static_cast<size_t>(EquipSlot::HeldMain)].sheathed);

    // The cooldown refuses a second swing this instant.
    const ActionResult tooSoon = characters.perform(knight, actionUseHeld());
    MGE_CHECK(!tooSoon.performed);
    MGE_CHECK(tooSoon.refusal == ActionRefusal::OnCooldown);
    characters.tickEffects(1.0f);

    // --- the same action, holding an apple: eaten, healing, and gone.
    characters.unequip(knight, EquipSlot::HeldMain);
    character->inventory = ItemCollection{};
    character->inventory.add({assetIdFromName("item/apple"), "item.apple", 1, {1, 0, 0, 1}});
    MGE_CHECK(characters.equip(knight, 0, EquipSlot::HeldMain));
    characters.damage(knight, 0.5f);
    const float healthBefore = character->health;

    const ActionResult bite = characters.perform(knight, actionUseHeld());
    MGE_CHECK(bite.performed);
    MGE_CHECK(bite.useKind == ItemUseKind::Consume);
    MGE_CHECK(character->health > healthBefore);
    MGE_CHECK(characters.findEffect(knight, assetIdFromName("effect/fed")) != nullptr);
    // Spent: the hand is empty now.
    MGE_CHECK(character->equipment[static_cast<size_t>(EquipSlot::HeldMain)].item.count == 0);
    characters.tickEffects(1.0f);

    // --- the same action, holding a torch: it lights, and lights off again.
    character->inventory = ItemCollection{};
    character->inventory.add({assetIdFromName("item/torch"), "item.torch", 1, {1, 1, 0, 1}});
    MGE_CHECK(characters.equip(knight, 0, EquipSlot::HeldMain));

    const ActionResult lit = characters.perform(knight, actionUseHeld());
    MGE_CHECK(lit.performed);
    MGE_CHECK(lit.useKind == ItemUseKind::Toggle);
    MGE_CHECK(lit.toggledOn);
    characters.tickEffects(1.0f);
    MGE_CHECK(!characters.perform(knight, actionUseHeld()).toggledOn);
}

MGE_TEST(use_held_with_an_empty_or_unknown_hand_does_nothing) {
    World world(64);
    CharacterSystem characters(world);
    ItemUseRegistry itemUses;
    characters.setItemUses(&itemUses);

    const EntityId person = spawnCharacterAt(world, characters, {0, 0, 0});
    grantHumanoidActions(characters, person);

    const ActionResult empty = characters.perform(person, actionUseHeld());
    MGE_CHECK(!empty.performed);
    MGE_CHECK(empty.refusal == ActionRefusal::NothingHeld);

    // Holding something the game never described: still nothing, cleanly.
    CharacterComponent* character = characters.get(person);
    character->inventory.add({assetIdFromName("item/pebble"), "item.pebble", 1, {1, 1, 1, 1}});
    MGE_CHECK(characters.equip(person, 0, EquipSlot::HeldMain));
    MGE_CHECK(characters.perform(person, actionUseHeld()).refusal == ActionRefusal::NothingHeld);
}

MGE_TEST(use_held_reports_what_the_engine_will_not_pretend_to_do) {
    World world(64);
    CharacterSystem characters(world);
    ItemUseRegistry itemUses;
    characters.setItemUses(&itemUses);

    ItemUse bow;
    bow.kind = ItemUseKind::Launch;   // there are no projectiles yet
    bow.range = 30.0f;
    bow.payload = 7;
    itemUses.define("item/bow", bow);

    const EntityId archer = spawnCharacterAt(world, characters, {0, 0, 0});
    grantHumanoidActions(characters, archer);
    characters.get(archer)->inventory.add(
        {assetIdFromName("item/bow"), "item.bow", 1, {1, 1, 1, 1}});
    MGE_CHECK(characters.equip(archer, 0, EquipSlot::HeldMain));

    const ActionResult shot = characters.perform(archer, actionUseHeld());
    MGE_CHECK(!shot.performed);                                 // honest: not done
    MGE_CHECK(shot.refusal == ActionRefusal::NotPerformed);      // reported instead
    MGE_CHECK(shot.useKind == ItemUseKind::Launch);
    MGE_CHECK(shot.payload == 7);
    MGE_CHECK(shot.amount > 29.0f);                             // the range to fire over
}

// --- P9: an action is an action, whoever performs it -------------------------

MGE_TEST(actions_are_not_player_only) {
    World world(64);
    CharacterSystem characters(world);
    CollisionWorld collision(64);
    ItemUseRegistry itemUses;
    characters.setCollision(&collision);
    characters.setItemUses(&itemUses);

    ItemUse club;
    club.kind = ItemUseKind::Strike;
    club.range = 2.0f;
    club.power = 0.2f;
    itemUses.define("item/club", club);

    // Nobody here is the player. A bandit jumps, and clubs a traveller.
    const EntityId bandit = spawnCharacterAt(world, characters, {0, 0, 0}, kPi);
    const EntityId traveller = spawnCharacterAt(world, characters, {0, 0, 1.2f});
    grantHumanoidActions(characters, bandit);
    characters.get(bandit)->controller = ControllerKind::Ai;
    characters.get(bandit)->inventory.add(
        {assetIdFromName("item/club"), "item.club", 1, {1, 1, 1, 1}});
    MGE_CHECK(characters.equip(bandit, 0, EquipSlot::HeldMain));

    stepWorld(world, characters, 1.0f / 60.0f);
    MGE_CHECK(characters.perform(bandit, actionJump()).performed);
    const ActionResult hit = characters.perform(bandit, actionUseHeld());
    MGE_CHECK(hit.performed);
    MGE_CHECK(hit.target == traveller);
    MGE_CHECK(characters.get(traveller)->health < 1.0f);

    // And the dead perform nothing at all.
    characters.damage(bandit, 99.0f);
    MGE_CHECK(characters.perform(bandit, actionJump()).refusal == ActionRefusal::NoActor);
}

// ---------------------------------------------- the damage moment (14.3) ----
//
// ADR 0021: a Strike lands at the motion's strike moment, not on the button
// press. Before this, `perform` picked a victim and applied damage in the same
// call that started the swing — correct when nothing moved, visibly wrong once
// the character actually swung, and worst for heavy weapons, whose longer
// wind-up put the damage furthest from the moment it looked like it should
// arrive.

namespace {

// A swordsman facing a victim two metres away, with the timing installed.
struct Duel {
    World world{32};
    CharacterSystem characters{world};
    ItemUseRegistry itemUses;
    EntityId attacker = kInvalidEntity;
    EntityId victim = kInvalidEntity;
    float strikeDelay = 0;

    explicit Duel(bool withTiming = true, float weight = 1.4f) {
        ItemUse sword;
        sword.kind = ItemUseKind::Strike;
        sword.range = 2.3f;
        sword.power = 0.25f;
        sword.cooldown = 0.7f;
        sword.archetype = UseArchetype::Swing;
        sword.weight = weight;
        itemUses.define("item/sword", sword);
        characters.setItemUses(&itemUses);
        if (withTiming) characters.setStrikeTiming(&strikeDelaySeconds);
        strikeDelay = strikeDelaySeconds(sword);

        // The attacker faces -Z, which is where yaw 0 looks.
        attacker = spawnCharacterAt(world, characters, {0, 0, 0}, 0.0f);
        characters.get(attacker)->inventory.add(
            {assetIdFromName("item/sword"), "item.sword", 1, {1, 1, 1, 1}});
        characters.equip(attacker, 0, EquipSlot::HeldMain);
        grantHumanoidActions(characters, attacker);

        victim = spawnCharacterAt(world, characters, {0, 0, -2.0f});
        characters.get(victim)->maxHealth = 10.0f;
        characters.get(victim)->health = 10.0f;
    }

    float victimHealth() { return characters.get(victim)->health; }
    void step(float dt) { stepWorld(world, characters, dt); }
};

}  // namespace

MGE_TEST(a_strike_does_not_land_on_the_button_press) {
    // The defect this ADR removes: damage arriving while the arm is still
    // winding up.
    Duel duel;
    const float before = duel.victimHealth();
    const ActionResult swing = duel.characters.perform(duel.attacker, actionUseHeld());

    MGE_CHECK(swing.performed);                     // the swing started
    MGE_CHECK(swing.pending);                       // ...and has not landed
    MGE_CHECK(swing.target == kInvalidEntity);      // nobody hit YET
    MGE_CHECK(swing.amount == 0.0f);
    MGE_CHECK(duel.victimHealth() == before);       // and nothing has happened
}

MGE_TEST(the_blow_lands_at_the_motions_strike_moment) {
    Duel duel;
    const float before = duel.victimHealth();
    MGE_CHECK(duel.characters.perform(duel.attacker, actionUseHeld()).performed);

    // Nothing until the moment arrives.
    float elapsed = 0;
    const float dt = 1.0f / 240.0f;  // fine enough to time the edge
    while (elapsed < duel.strikeDelay - dt) {
        duel.step(dt);
        elapsed += dt;
        MGE_CHECK(duel.victimHealth() == before);
    }
    // ...and it lands within a step of it.
    duel.step(dt);
    duel.step(dt);
    MGE_CHECK(duel.victimHealth() < before);

    CharacterSystem::StrikeOutcome blow;
    MGE_CHECK(duel.characters.consumeStrike(blow));
    MGE_CHECK(blow.actor == duel.attacker);
    MGE_CHECK(blow.target == duel.victim);
    MGE_CHECK(blow.amount > 0.0f);
    MGE_CHECK(!duel.characters.consumeStrike(blow));  // reported exactly once
}

MGE_TEST(a_heavier_weapon_lands_its_blow_later_through_the_action_model) {
    // The timing is the motion's, so weight reaches the damage moment without
    // anybody tuning a delay to agree with an animation.
    Duel light(true, 0.4f);
    Duel heavy(true, 7.0f);
    MGE_CHECK(heavy.strikeDelay > light.strikeDelay * 1.5f);
}

MGE_TEST(a_victim_who_steps_out_of_reach_during_the_wind_up_is_missed) {
    // Hit detection happens when the blade is there. Under the old model both
    // this and its opposite were decided before the arm moved.
    Duel duel;
    MGE_CHECK(duel.characters.perform(duel.attacker, actionUseHeld()).performed);

    // He backs off well beyond the sword's reach while it is still winding up.
    TransformComponent* fleeing = duel.world.transform(duel.victim);
    fleeing->position = {0, 0, -9.0f};
    fleeing->prevPosition = fleeing->position;

    const float before = duel.victimHealth();
    for (float t = 0; t < duel.strikeDelay + 0.2f; t += 1.0f / 120.0f) duel.step(1.0f / 120.0f);

    MGE_CHECK(duel.victimHealth() == before);  // the blow found nothing

    // The miss is still REPORTED — "you swing at nothing" is a thing the game
    // says, and it is not the same as never having swung.
    CharacterSystem::StrikeOutcome blow;
    MGE_CHECK(duel.characters.consumeStrike(blow));
    MGE_CHECK(blow.actor == duel.attacker);
    MGE_CHECK(blow.target == kInvalidEntity);
}

MGE_TEST(an_interrupted_swing_never_lands) {
    Duel duel;
    MGE_CHECK(duel.characters.perform(duel.attacker, actionUseHeld()).performed);
    duel.characters.cancelStrike(duel.attacker);

    const float before = duel.victimHealth();
    for (float t = 0; t < duel.strikeDelay + 0.5f; t += 1.0f / 120.0f) duel.step(1.0f / 120.0f);

    MGE_CHECK(duel.victimHealth() == before);
    CharacterSystem::StrikeOutcome blow;
    MGE_CHECK(!duel.characters.consumeStrike(blow));  // no blow, not even a miss
}

MGE_TEST(without_a_strike_timing_installed_damage_still_resolves_at_once) {
    // What keeps framework/ standing on its own: a game that never brings the
    // character pillar gets the pre-ADR-0021 action model, unchanged.
    Duel duel(/*withTiming=*/false);
    const float before = duel.victimHealth();
    const ActionResult swing = duel.characters.perform(duel.attacker, actionUseHeld());

    MGE_CHECK(swing.performed);
    MGE_CHECK(!swing.pending);
    MGE_CHECK(swing.target == duel.victim);
    MGE_CHECK(swing.amount > 0.0f);
    MGE_CHECK(duel.victimHealth() < before);
}
