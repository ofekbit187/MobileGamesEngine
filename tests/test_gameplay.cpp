// Phase 11: the world is solid and can be acted on. These test the
// behaviours a player notices — walls stop you, corners let you slide,
// doorsteps don't, a chest through a wall isn't usable, a tap takes the
// apple — headlessly and deterministically.

#include <cmath>

#include "mge/framework/ai.h"
#include "mge/framework/collision.h"
#include "mge/framework/interaction.h"
#include "test_framework.h"

using namespace mge;

namespace {

Aabb boxAt(Vec3 center, Vec3 halfExtents) {
    return Aabb::fromCenterExtents(center, halfExtents);
}

EntityId spawnAt(World& world, Vec3 position, float yaw = 0.0f) {
    const EntityId entity = world.spawn();
    TransformComponent transform;
    transform.position = position;
    transform.yaw = yaw;
    world.setTransform(entity, transform);
    return entity;
}

}  // namespace

MGE_TEST(collision_wall_blocks_and_lets_you_slide) {
    CollisionWorld collision;
    CharacterShape shape;
    // A wall across x, 4 m wide, standing at z = -2.
    collision.addBox(boxAt({0, 1.25f, -2}, {2.0f, 1.25f, 0.25f}));

    // Walking straight into it stops the character short of the wall.
    const MoveResult intoWall =
        collision.moveCharacter({0, 0, -1.0f}, shape, {0, 0, -0.5f});
    MGE_CHECK(intoWall.blockedZ);
    MGE_CHECK(intoWall.position.z > -1.5f);          // did not pass through
    MGE_CHECK(!collision.overlaps(CollisionWorld::characterBounds(intoWall.position, shape)));

    // Pushing diagonally into it still slides sideways — the wall guides you.
    const MoveResult slide =
        collision.moveCharacter({0, 0, -1.0f}, shape, {0.5f, 0, -0.5f});
    MGE_CHECK(slide.blockedZ);
    MGE_CHECK_NEAR(slide.position.x, 0.5f, 1e-4f);   // the X part went through

    // Walking away from the wall is never blocked.
    const MoveResult away = collision.moveCharacter({0, 0, -1.0f}, shape, {0, 0, 0.5f});
    MGE_CHECK(!away.blockedZ);
    MGE_CHECK_NEAR(away.position.z, -0.5f, 1e-4f);

    // A gap in the wall is walkable: nothing at x = 5.
    const MoveResult gap = collision.moveCharacter({5, 0, -1.0f}, shape, {0, 0, -1.0f});
    MGE_CHECK(!gap.blockedZ);
    MGE_CHECK_NEAR(gap.position.z, -2.0f, 1e-4f);
}

MGE_TEST(collision_steps_over_ledges_and_stands_on_them) {
    CollisionWorld collision;
    CharacterShape shape;
    shape.stepHeight = 0.4f;

    // A doorstep (0.25 m) and a crate too tall to step onto (1.0 m).
    collision.addBox(boxAt({0, 0.125f, -2}, {1.0f, 0.125f, 0.5f}));   // step
    collision.addBox(boxAt({5, 0.5f, -2}, {0.5f, 0.5f, 0.5f}));       // crate

    // The doorstep is walked over, and the character ends up standing ON it.
    const MoveResult onStep = collision.moveCharacter({0, 0, -1.0f}, shape, {0, 0, -1.0f});
    MGE_CHECK(!onStep.blockedZ);
    MGE_CHECK(onStep.grounded);
    MGE_CHECK_NEAR(onStep.position.y, 0.25f, 1e-3f);

    // The crate is a wall, not a step.
    const MoveResult intoCrate = collision.moveCharacter({5, 0, -1.0f}, shape, {0, 0, -1.0f});
    MGE_CHECK(intoCrate.blockedZ);
    MGE_CHECK(intoCrate.position.z > -1.5f);

    // Off the step, the character settles back to the ground plane.
    const MoveResult offStep =
        collision.moveCharacter({0, 0.25f, -2.0f}, shape, {0, 0, 1.5f});
    MGE_CHECK_NEAR(offStep.position.y, 0.0f, 1e-3f);
    MGE_CHECK(offStep.grounded);
}

MGE_TEST(collision_raycast_hits_nearest_and_reports_surface) {
    CollisionWorld collision;
    const EntityId near = EntityId{1, 1};
    const EntityId far = EntityId{2, 1};
    collision.addBox(boxAt({0, 1, -3}, {1, 1, 0.5f}), near);
    collision.addBox(boxAt({0, 1, -8}, {1, 1, 0.5f}), far);

    const RayHit hit = collision.raycast({0, 1, 0}, {0, 0, -1}, 20.0f);
    MGE_CHECK(hit.hit);
    MGE_CHECK(hit.entity == near);                    // nearest wins
    MGE_CHECK_NEAR(hit.distance, 2.5f, 1e-3f);
    MGE_CHECK_NEAR(hit.normal.z, 1.0f, 1e-3f);        // faces the ray
    MGE_CHECK_NEAR(hit.point.z, -2.5f, 1e-3f);

    // Short rays and misses are normal answers, not errors.
    MGE_CHECK(!collision.raycast({0, 1, 0}, {0, 0, -1}, 1.0f).hit);
    MGE_CHECK(!collision.raycast({20, 1, 0}, {0, 0, -1}, 50.0f).hit);
}

MGE_TEST(collision_capacity_refuses_and_releases) {
    CollisionWorld collision(4);
    for (int i = 0; i < 4; ++i) {
        MGE_CHECK(collision.addBox(boxAt({static_cast<float>(i) * 3, 1, 0}, {1, 1, 1})) >= 0);
    }
    MGE_CHECK(collision.count() == 4);
    MGE_CHECK(collision.addBox(boxAt({99, 1, 0}, {1, 1, 1})) == kInvalidCollider);  // refuse

    // Streaming releases by owner when a chunk unloads.
    CollisionWorld byOwner(8);
    const EntityId chunkProp = EntityId{7, 1};
    byOwner.addBox(boxAt({0, 1, 0}, {1, 1, 1}), chunkProp);
    byOwner.addBox(boxAt({4, 1, 0}, {1, 1, 1}), chunkProp);
    byOwner.addBox(boxAt({8, 1, 0}, {1, 1, 1}), EntityId{8, 1});
    MGE_CHECK(byOwner.count() == 3);
    byOwner.removeByEntity(chunkProp);
    MGE_CHECK(byOwner.count() == 1);
    byOwner.clear();
    MGE_CHECK(byOwner.count() == 0);
}

// ------------------------------ interaction --------------------------------

MGE_TEST(interaction_focuses_what_you_face_and_in_reach) {
    World world(32);
    CharacterSystem characters(world);
    InteractionSystem interactions(world, characters);

    // The player stands at the origin facing -Z (yaw 0).
    const EntityId player = spawnAt(world, {0, 0, 0});
    characters.attach(player);

    InteractableComponent chest;
    chest.kind = InteractionKind::Container;
    chest.promptKey = "prompt.open";
    chest.range = 2.5f;
    const EntityId inFront = spawnAt(world, {0, 0, -1.5f});
    MGE_CHECK(interactions.attach(inFront, chest) != nullptr);

    const EntityId behind = spawnAt(world, {0, 0, 1.5f});
    interactions.attach(behind, chest);
    const EntityId tooFar = spawnAt(world, {0, 0, -6.0f});
    interactions.attach(tooFar, chest);

    MGE_CHECK(interactions.focus(player) == inFront);

    // Turning away drops the focus entirely.
    world.transform(player)->yaw = kPi;
    MGE_CHECK(interactions.focus(player) == behind);
    world.transform(player)->yaw = kPi * 0.5f;  // facing +X: nothing there
    MGE_CHECK(interactions.focus(player) == kInvalidEntity);

    // Disabled interactables are invisible to focus.
    world.transform(player)->yaw = 0.0f;
    interactions.get(inFront)->enabled = false;
    MGE_CHECK(interactions.focus(player) == kInvalidEntity);
}

MGE_TEST(interaction_respects_walls) {
    World world(32);
    CharacterSystem characters(world);
    CollisionWorld collision;
    InteractionSystem interactions(world, characters);
    interactions.setCollisionWorld(&collision);

    const EntityId player = spawnAt(world, {0, 0, 0});
    characters.attach(player);
    InteractableComponent chest;
    chest.kind = InteractionKind::Container;
    chest.range = 3.0f;
    const EntityId target = spawnAt(world, {0, 0, -2.0f});
    interactions.attach(target, chest);
    MGE_CHECK(interactions.focus(player) == target);

    // Drop a wall between them: the chest is no longer usable.
    const int32_t wall =
        collision.addBox(Aabb::fromCenterExtents({0, 1.25f, -1.0f}, {2, 1.25f, 0.2f}));
    MGE_CHECK(interactions.focus(player) == kInvalidEntity);

    // Remove the wall and it comes back.
    collision.removeBox(wall);
    MGE_CHECK(interactions.focus(player) == target);
}

MGE_TEST(interaction_verbs_pick_up_open_and_talk) {
    World world(32);
    CharacterSystem characters(world);
    InteractionSystem interactions(world, characters);

    const EntityId player = spawnAt(world, {0, 0, 0});
    CharacterComponent* character = characters.attach(player);

    // An apple on the ground in front of the player.
    InteractableComponent apple;
    apple.kind = InteractionKind::PickUp;
    apple.promptKey = "prompt.take";
    apple.item = {assetIdFromName("item/apple"), "item.apple", 1, {0.8f, 0.2f, 0.2f, 1}};
    const EntityId appleEntity = spawnAt(world, {0, 0, -1.2f});
    interactions.attach(appleEntity, apple);

    MGE_CHECK(character->inventory.size() == 0);
    const InteractionSystem::Result taken = interactions.interact(player);
    MGE_CHECK(taken.handled);
    MGE_CHECK(taken.kind == InteractionKind::PickUp);
    MGE_CHECK(taken.item.asset == assetIdFromName("item/apple"));
    MGE_CHECK(character->inventory.size() == 1);
    // Taken means gone from the world — and not focusable again.
    MGE_CHECK(!world.entities().isAlive(appleEntity));
    MGE_CHECK(interactions.focus(player) == kInvalidEntity);
    MGE_CHECK(!interactions.interact(player).handled);  // nothing to act on

    // A chest reports its bound collection for the game to present.
    InteractableComponent chest;
    chest.kind = InteractionKind::Container;
    chest.collectionId = assetIdFromName("chest.tavern");
    const EntityId chestEntity = spawnAt(world, {0, 0, -1.5f});
    interactions.attach(chestEntity, chest);
    const InteractionSystem::Result opened = interactions.interact(player);
    MGE_CHECK(opened.handled);
    MGE_CHECK(opened.kind == InteractionKind::Container);
    MGE_CHECK(opened.collectionId == assetIdFromName("chest.tavern"));
    MGE_CHECK(world.entities().isAlive(chestEntity));  // chests stay put
    interactions.detach(chestEntity);

    // A villager reports which line to speak.
    InteractableComponent villager;
    villager.kind = InteractionKind::Talk;
    villager.payload = 3;
    const EntityId villagerEntity = spawnAt(world, {0, 0, -1.4f});
    interactions.attach(villagerEntity, villager);
    const InteractionSystem::Result spoke = interactions.interact(player);
    MGE_CHECK(spoke.kind == InteractionKind::Talk);
    MGE_CHECK(spoke.payload == 3);

    // A full inventory refuses the pick-up and leaves the item in the world.
    World world2(64);
    CharacterSystem characters2(world2);
    InteractionSystem interactions2(world2, characters2);
    const EntityId hoarder = spawnAt(world2, {0, 0, 0});
    CharacterComponent* full = characters2.attach(hoarder);
    for (uint32_t i = 0; i < ItemCollection::kCapacity; ++i) {
        full->inventory.add({static_cast<AssetId>(1000 + i), "item.junk", 1, {1, 1, 1, 1}});
    }
    const EntityId ground = spawnAt(world2, {0, 0, -1.0f});
    interactions2.attach(ground, apple);
    MGE_CHECK(!interactions2.interact(hoarder).handled);
    MGE_CHECK(world2.entities().isAlive(ground));  // still there to take later
}

// --- P9: every character can interact, not just the player ------------------

MGE_TEST(interaction_is_not_player_only) {
    // The owner's ruling: interaction is a universal character mechanism.
    // An NPC takes an apple through the exact call a tap makes, and the
    // PLAYER is a legal target for someone else's interaction.
    World world(64);
    CharacterSystem characters(world);
    InteractionSystem interactions(world, characters);

    const EntityId villager = spawnAt(world, {0, 0, 0});
    CharacterComponent* villagerCharacter = characters.attach(villager);
    villagerCharacter->controller = ControllerKind::Ai;   // NOT the player

    InteractableComponent apple;
    apple.kind = InteractionKind::PickUp;
    apple.item = {assetIdFromName("item/apple"), "item.apple", 1, {1, 0, 0, 1}};
    const EntityId onGround = spawnAt(world, {0, 0, -1.2f});
    interactions.attach(onGround, apple);

    MGE_CHECK(interactions.focus(villager) == onGround);
    const InteractionSystem::Result took = interactions.interact(villager);
    MGE_CHECK(took.handled);
    MGE_CHECK(villagerCharacter->inventory.size() == 1);
    MGE_CHECK(!world.entities().isAlive(onGround));

    // The player can be the one acted upon.
    const EntityId player = spawnAt(world, {0, 0, -1.0f});
    CharacterComponent* playerCharacter = characters.attach(player);
    playerCharacter->controller = ControllerKind::Player;
    InteractableComponent talk;
    talk.kind = InteractionKind::Talk;
    talk.payload = 9;
    interactions.attach(player, talk);
    MGE_CHECK(interactions.focus(villager) == player);
    const InteractionSystem::Result spoke = interactions.interact(villager);
    MGE_CHECK(spoke.kind == InteractionKind::Talk);
    MGE_CHECK(spoke.payload == 9);

    // The dead act on nothing (mortality is universal too).
    characters.damage(villager, 99.0f);
    MGE_CHECK(!interactions.interact(villager).handled);
}

MGE_TEST(ai_gatherer_picks_up_what_it_finds) {
    // The behaviour proof: a wandering villager notices an apple, walks to
    // it, and takes it — no player involved anywhere in the loop.
    World world(64);
    CharacterSystem characters(world);
    InteractionSystem interactions(world, characters);
    AiSystem ai(world, characters);
    ai.setInteractions(&interactions);

    const EntityId villager = spawnAt(world, {0, 0, 0});
    CharacterComponent* character = characters.attach(villager);
    world.setMovement(villager, MovementComponent{{}, 4.0f});
    AiProfile profile;
    profile.canWander = true;
    profile.homeRadius = 3.0f;
    profile.gathers = true;
    profile.gatherRange = 8.0f;
    MGE_CHECK(ai.attach(villager, profile));

    InteractableComponent apple;
    apple.kind = InteractionKind::PickUp;
    apple.range = 2.0f;
    apple.item = {assetIdFromName("item/apple"), "item.apple", 1, {1, 0, 0, 1}};
    const EntityId prize = spawnAt(world, {5.0f, 0, -3.0f});
    interactions.attach(prize, apple);

    bool sawGathering = false;
    for (int i = 0; i < 60 * 20 && character->inventory.size() == 0; ++i) {
        ai.step(1.0f / 60.0f);
        world.step(1.0 / 60.0);
        if (ai.stateOf(villager) == AiState::Gather) sawGathering = true;
    }
    MGE_CHECK(sawGathering);                       // it decided to go get it
    MGE_CHECK(character->inventory.size() == 1);   // and it has the apple
    MGE_CHECK(!world.entities().isAlive(prize));

    // With nothing left to gather it goes back to its ordinary life.
    for (int i = 0; i < 60 * 3; ++i) {
        ai.step(1.0f / 60.0f);
        world.step(1.0 / 60.0);
    }
    MGE_CHECK(ai.stateOf(villager) != AiState::Gather);
}
