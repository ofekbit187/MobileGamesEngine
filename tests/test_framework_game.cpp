// Phase 3 framework tests: entities, world, controls, cameras, asset registry.

#include <cmath>

#include "mge/framework/asset_registry.h"
#include "mge/framework/camera_controller.h"
#include "mge/framework/controls.h"
#include "mge/framework/engine.h"
#include "mge/framework/world.h"
#include "mge/graphics/primitives.h"
#include "test_framework.h"

using namespace mge;

MGE_TEST(entity_ids_are_stable_and_generational) {
    EntityRegistry registry(4);
    const EntityId a = registry.create();
    const EntityId b = registry.create();
    MGE_CHECK(registry.isAlive(a) && registry.isAlive(b));
    MGE_CHECK(a.index != b.index);

    registry.destroy(a);
    MGE_CHECK(!registry.isAlive(a));

    // Index reuse must not resurrect the old id.
    const EntityId c = registry.create();
    MGE_CHECK(c.index == a.index && c.generation != a.generation);
    MGE_CHECK(!registry.isAlive(a));
    MGE_CHECK(registry.isAlive(c));
}

MGE_TEST(entity_capacity_refuses) {
    EntityRegistry registry(2);
    MGE_CHECK(registry.create() != kInvalidEntity);
    MGE_CHECK(registry.create() != kInvalidEntity);
    MGE_CHECK(registry.create() == kInvalidEntity);  // full: refuse, never grow
    MGE_CHECK(registry.liveCount() == 2);
}

MGE_TEST(world_components_and_chunks) {
    World world(16);
    const EntityId e = world.spawn();
    TransformComponent t;
    t.position = {100.0f, 0, -40.0f};
    world.setTransform(e, t);
    world.setModel(e, ModelComponent{assetIdFromName("test"), {1, 1, 1, 1}});

    ChunkCoord chunk;
    MGE_CHECK(world.chunkOf(e, chunk));
    MGE_CHECK(chunk.x == 3 && chunk.z == -2);  // 100/32=3.1, -40/32=-1.25 -> floor

    int renderables = 0;
    world.forEachRenderable([&](EntityId, const TransformComponent&, const ModelComponent&) {
        ++renderables;
    });
    MGE_CHECK(renderables == 1);

    world.despawn(e);
    MGE_CHECK(world.transform(e) == nullptr);
    renderables = 0;
    world.forEachRenderable([&](EntityId, const TransformComponent&, const ModelComponent&) {
        ++renderables;
    });
    MGE_CHECK(renderables == 0);
}

MGE_TEST(world_step_integrates_and_records_prev) {
    World world(4);
    const EntityId e = world.spawn();
    world.setTransform(e, TransformComponent{});
    MovementComponent movement;
    movement.velocity = {2.0f, 0, 0};
    movement.maxSpeed = 4.0f;
    world.setMovement(e, movement);

    world.step(0.5);
    TransformComponent* t = world.transform(e);
    MGE_CHECK_NEAR(t->position.x, 1.0f, 1e-5);
    MGE_CHECK_NEAR(t->prevPosition.x, 0.0f, 1e-5);  // render interpolates prev->curr

    // Velocity beyond maxSpeed is clamped.
    world.movement(e)->velocity = {100.0f, 0, 0};
    world.step(1.0);
    MGE_CHECK_NEAR(world.transform(e)->position.x, 1.0f + 4.0f, 1e-4);
}

namespace {
TouchEvent touch(int32_t id, TouchAction action, float x, float y, int64_t timeNs = 0) {
    TouchEvent e;
    e.pointerId = id;
    e.action = action;
    e.x = x;
    e.y = y;
    e.timestampNs = timeNs;
    return e;
}
}  // namespace

MGE_TEST(controls_virtual_stick) {
    TouchControlScheme controls;
    controls.configure(2000, 1000);

    // Left-zone touch anchors the stick; dragging up-right moves forward-right.
    controls.handle(touch(0, TouchAction::Down, 400, 700));
    controls.handle(touch(0, TouchAction::Move, 400 + 120, 700 - 120));  // radius=120px
    GameplayIntents intents = controls.consume();
    MGE_CHECK_NEAR(intents.moveX, 1.0f / std::sqrt(2.0f), 0.01);
    MGE_CHECK_NEAR(intents.moveY, 1.0f / std::sqrt(2.0f), 0.01);

    // Over-radius drags clamp to unit length.
    controls.handle(touch(0, TouchAction::Move, 400 + 500, 700));
    intents = controls.consume();
    MGE_CHECK_NEAR(intents.moveX, 1.0f, 1e-4);

    // Release stops movement.
    controls.handle(touch(0, TouchAction::Up, 900, 700));
    intents = controls.consume();
    MGE_CHECK_NEAR(intents.moveX, 0.0f, 1e-6);
}

MGE_TEST(controls_look_and_tap) {
    TouchControlScheme controls;
    controls.configure(2000, 1000);

    // Right-zone drag accumulates look deltas (fractions of screen height).
    controls.handle(touch(1, TouchAction::Down, 1500, 500, 0));
    controls.handle(touch(1, TouchAction::Move, 1600, 550, 50'000'000));
    GameplayIntents intents = controls.consume();
    MGE_CHECK_NEAR(intents.lookX, 0.1f, 1e-4);
    MGE_CHECK_NEAR(intents.lookY, 0.05f, 1e-4);
    MGE_CHECK(!intents.action);  // moved too far to be a tap
    controls.handle(touch(1, TouchAction::Up, 1600, 550, 100'000'000));
    MGE_CHECK(!controls.consume().action);

    // Quick small-motion touch = tap = action.
    controls.handle(touch(2, TouchAction::Down, 1500, 500, 1'000'000'000));
    controls.handle(touch(2, TouchAction::Up, 1502, 501, 1'100'000'000));
    intents = controls.consume();
    MGE_CHECK(intents.action);
    MGE_CHECK(!controls.consume().action);  // latched once
}

MGE_TEST(controls_two_pointers_independent) {
    TouchControlScheme controls;
    controls.configure(2000, 1000);
    controls.handle(touch(0, TouchAction::Down, 300, 800));       // stick
    controls.handle(touch(1, TouchAction::Down, 1500, 500));      // look
    controls.handle(touch(0, TouchAction::Move, 300, 680));       // forward
    controls.handle(touch(1, TouchAction::Move, 1700, 500));      // look right
    const GameplayIntents intents = controls.consume();
    MGE_CHECK(intents.moveY > 0.9f);
    MGE_CHECK_NEAR(intents.lookX, 0.2f, 1e-4);
}

MGE_TEST(third_person_camera_follows_behind) {
    Camera camera;
    ThirdPersonCamera controller;
    // Character at origin facing -Z (yaw 0): camera sits behind (+Z side).
    controller.update(camera, {0, 0, 0}, 0.0f);
    MGE_CHECK(camera.eye.z > 0.0f);
    MGE_CHECK(camera.eye.y > 0.0f);
    // Facing +X (yaw pi/2): camera sits on -X side.
    controller.update(camera, {0, 0, 0}, kPi * 0.5f);
    MGE_CHECK(camera.eye.x < 0.0f);
}

MGE_TEST(asset_registry_and_virtual_models) {
    AssetRegistry assets;
    const AssetId houseId = assets.registerMesh("prop/house", [] {
        LodMesh m;
        m.lods.push_back(makeBox({3, 2, 3}));
        m.computeBounds();
        return m;
    }());
    MGE_CHECK(houseId != kInvalidAsset);
    MGE_CHECK(assetIdFromName("prop/house") == houseId);  // stable ids

    VirtualModelDesc desc;
    desc.proportions = {2.4f, 2.1f, 1.8f};
    desc.shape = PlaceholderShape::Box;
    desc.description = "wooden market stall, striped canvas roof";
    const AssetId stallId = assets.registerVirtualModel("prop/market_stall", desc);

    // Placeholder volume generated at declared proportions.
    const AssetRecord* stall = assets.find(stallId);
    MGE_CHECK(stall != nullptr && stall->kind == AssetKind::VirtualModel);
    const Vec3 size = stall->mesh.bounds.max - stall->mesh.bounds.min;
    MGE_CHECK_NEAR(size.x, 2.4f, 1e-4);
    MGE_CHECK_NEAR(size.y, 2.1f, 1e-4);

    // Manifest lists it as unfulfilled.
    std::vector<const AssetRecord*> pending;
    assets.unfulfilled(pending);
    MGE_CHECK(pending.size() == 1 && pending[0]->id == stallId);

    // Fulfillment: real mesh arrives under the SAME id, kind flips, manifest empties.
    LodMesh real;
    real.lods.push_back(makeBox({2.4f, 2.1f, 1.8f}));
    real.computeBounds();
    MGE_CHECK(assets.fulfill(stallId, std::move(real)));
    MGE_CHECK(assets.find(stallId)->kind == AssetKind::Mesh);
    pending.clear();
    assets.unfulfilled(pending);
    MGE_CHECK(pending.empty());

    // Unknown id resolves to null — "not resident/unknown" is a normal answer.
    MGE_CHECK(assets.find(assetIdFromName("prop/nonexistent")) == nullptr);
    // Only virtual models can be fulfilled.
    MGE_CHECK(!assets.fulfill(houseId, LodMesh{}));
}

MGE_TEST(engine_drives_player_from_touch) {
    Engine engine;
    EngineConfig config;
    MGE_CHECK(engine.init(config));
    engine.onSurfaceCreated(2000, 1000);

    World& world = engine.world();
    const EntityId player = world.spawn();
    world.setTransform(player, TransformComponent{});
    world.setMovement(player, MovementComponent{{}, 4.0f});
    engine.setPlayerEntity(player);

    // Hold the stick fully forward and run one second of simulation.
    engine.pushTouchEvent(touch(0, TouchAction::Down, 300, 800));
    engine.pushTouchEvent(touch(0, TouchAction::Move, 300, 800 - 200));
    for (int i = 0; i < 60; ++i) engine.tick(1.0 / 60.0);

    const TransformComponent* t = world.transform(player);
    // Facing -Z at full speed 4 m/s for ~1 s.
    MGE_CHECK(t->position.z < -3.5f && t->position.z > -4.5f);
    MGE_CHECK_NEAR(t->position.x, 0.0f, 1e-3);

    // Look drag turns the player.
    engine.pushTouchEvent(touch(1, TouchAction::Down, 1500, 500));
    engine.pushTouchEvent(touch(1, TouchAction::Move, 1750, 500));
    engine.tick(1.0 / 60.0);
    MGE_CHECK(world.transform(player)->yaw > 0.5f);  // 0.25 * 3.5 rad

    engine.shutdown();
}
