// Template game v1 (tasks 3.6 + 8.22): the "new game" starting point, run
// headlessly. A world composed through the asset registry (real meshes +
// virtual models), and the Phase 8 exit criteria live: the player and the
// NPCs are the SAME humanoid character — one component set, different
// controllers (P9). The player walks under scripted touch control; a guard
// in a layered outfit patrols with his sword sheathed on his back; a
// villager wanders. Every body animates from its actual velocity. At the
// end the characters' state rides a real save file and comes back (8.9).
//
// Frames are captured at waypoints (● real engine output for the review
// board). On device the exact same stack runs — only the touch events and
// the surface are real instead of scripted.

#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#include "mge/character/humanoid.h"
#include "mge/framework/action.h"
#include "mge/framework/ai.h"
#include "mge/framework/asset_registry.h"
#include "mge/framework/camera_controller.h"
#include "mge/framework/character.h"
#include "mge/framework/collision.h"
#include "mge/framework/interaction.h"
#include "mge/framework/engine.h"
#include "mge/framework/save.h"
#include "mge/graphics/mesh_io.h"
#include "mge/graphics/primitives.h"
#include "mge/graphics/renderer.h"
#include "mge/graphics/vulkan_device.h"

using namespace mge;

namespace {

// GPU residency by asset id: "not resident" is a normal answer (P2); here
// everything is uploaded up front, in Phase 4 the streaming system decides.
struct GpuAssetCache {
    Renderer& renderer;
    std::unordered_map<AssetId, GpuLodMesh> resident;

    explicit GpuAssetCache(Renderer& r) : renderer(r) {}

    bool upload(const AssetRegistry& assets, AssetId id) {
        const AssetRecord* record = assets.find(id);
        if (record == nullptr) return false;
        GpuLodMesh mesh;
        if (!renderer.uploadLodMesh(record->mesh, mesh)) return false;
        resident.emplace(id, std::move(mesh));
        return true;
    }

    const GpuLodMesh* resolve(AssetId id) const {
        auto it = resident.find(id);
        return it != resident.end() ? &it->second : nullptr;
    }

    void destroyAll() {
        for (auto& [id, mesh] : resident) renderer.destroyLodMesh(mesh);
        resident.clear();
    }
};

// A humanoid's visual on the GPU: one mesh per rig part (same pattern as
// tools/humanoid_demo; replaced by skinned rendering with the artist body).
struct RigInstance {
    Skeleton skeleton;
    std::vector<RigPart> parts;
    std::vector<GpuLodMesh> gpu;
};

bool uploadRig(Renderer& renderer, const HumanoidVariant& variant,
               const WearableInstance* wearables, size_t wearableCount, RigInstance& out) {
    out.skeleton = buildSkeleton(variant);
    buildHumanoidVisual(variant, wearables, wearableCount, out.parts);
    out.gpu.resize(out.parts.size());
    for (size_t i = 0; i < out.parts.size(); ++i) {
        LodMesh lod;
        lod.lods.push_back(out.parts[i].mesh);
        lod.computeBounds();
        if (!renderer.uploadLodMesh(lod, out.gpu[i])) return false;
    }
    return true;
}

void destroyRig(Renderer& renderer, RigInstance& rig) {
    for (GpuLodMesh& mesh : rig.gpu) renderer.destroyLodMesh(mesh);
    rig.gpu.clear();
}

void emitRig(std::vector<DrawItem>& items, const RigInstance& rig, const Pose& pose,
             const Vec3& position, float yaw) {
    Mat4 world[kJointCount];
    evaluatePose(rig.skeleton, pose, world);
    const Mat4 root =
        Mat4::translation(position) * Mat4::rotation(Quat::fromAxisAngle({0, 1, 0}, -yaw));
    for (size_t i = 0; i < rig.parts.size(); ++i) {
        DrawItem item;
        item.mesh = &rig.gpu[i];
        item.model = root * world[static_cast<size_t>(rig.parts[i].joint)];
        item.worldBounds = Aabb::fromCenterExtents(position + Vec3{0, 1.2f, 0}, {3, 3, 3});
        item.lodReference = position;
        memcpy(item.baseColor, rig.parts[i].color, sizeof item.baseColor);
        items.push_back(item);
    }
}

TouchEvent touch(int32_t id, TouchAction action, float x, float y, int64_t timeNs) {
    TouchEvent e;
    e.pointerId = id;
    e.action = action;
    e.x = x;
    e.y = y;
    e.timestampNs = timeNs;
    return e;
}

bool savePpm(const char* path, const std::vector<uint8_t>& rgba, uint32_t w, uint32_t h) {
    FILE* f = fopen(path, "wb");
    if (f == nullptr) return false;
    fprintf(f, "P6\n%u %u\n255\n", w, h);
    for (size_t i = 0; i < rgba.size(); i += 4) fwrite(&rgba[i], 1, 3, f);
    fclose(f);
    return true;
}

WearableInstance colored(WearableKind kind, float r, float g, float b, uint8_t layer = 1,
                         bool sheathed = false) {
    WearableInstance w;
    w.kind = kind;
    w.layer = layer;
    w.sheathed = sheathed;
    w.color[0] = r;
    w.color[1] = g;
    w.color[2] = b;
    return w;
}

}  // namespace

int main(int argc, char** argv) {
    const std::string outPrefix = argc > 1 ? argv[1] : "template";
    const char* bakedMeshPath = argc > 2 ? argv[2] : nullptr;

    // --- Engine (simulation side) ---
    Engine engine;
    EngineConfig engineConfig;
    if (!engine.init(engineConfig)) return 1;
    engine.onSurfaceCreated(2000, 1000);  // touch space; controls configured
    engine.onResume();

    // --- Renderer (application side composes the two, mirroring the app) ---
    BudgetRegistry gpuBudgets;
    VulkanDevice device(gpuBudgets);
    if (!device.init(VulkanDeviceConfig{})) return 1;
    Renderer renderer(device);
    RendererConfig renderConfig;
    renderConfig.width = 1280;
    renderConfig.height = 720;
    renderConfig.lightDir[0] = -0.25f;
    renderConfig.lightDir[1] = 0.80f;
    renderConfig.lightDir[2] = 0.55f;
    if (!renderer.init(renderConfig)) return 1;

    // --- Content: registry of real meshes + virtual models (P5) ---
    AssetRegistry assets;
    auto meshOf = [](MeshData data) {
        LodMesh m;
        m.lods.push_back(std::move(data));
        m.computeBounds();
        return m;
    };
    const AssetId groundId = assets.registerMesh("world/ground", meshOf(makePlane(80, 80)));
    const AssetId houseId = assets.registerMesh("prop/house", meshOf(makeBox({3.2f, 2.6f, 2.8f})));
    const AssetId towerId = assets.registerMesh("prop/tower", meshOf(makeCylinder(1.2f, 7.0f, 32)));

    VirtualModelDesc stallDesc;
    stallDesc.proportions = {2.4f, 2.1f, 1.8f};
    stallDesc.description = "wooden market stall, striped canvas roof, worn planks";
    const AssetId stallId = assets.registerVirtualModel("prop/market_stall", stallDesc);
    VirtualModelDesc wellDesc;
    wellDesc.proportions = {1.8f, 1.4f, 1.8f};
    wellDesc.shape = PlaceholderShape::Cylinder;
    wellDesc.description = "stone village well with wooden crank and bucket";
    const AssetId wellId = assets.registerVirtualModel("prop/well", wellDesc);

    // Optionally fulfill a virtual crate with the imported glTF cube (P5 flow).
    VirtualModelDesc crateDesc;
    crateDesc.proportions = {1, 1, 1};
    crateDesc.description = "rough wooden crate";
    const AssetId crateId = assets.registerVirtualModel("prop/crate", crateDesc);
    if (bakedMeshPath != nullptr) {
        LodMesh baked;
        if (readMeshFile(bakedMeshPath, baked) && assets.fulfill(crateId, std::move(baked))) {
            printf("virtual model 'prop/crate' fulfilled by imported asset\n");
        }
    }

    // --- World composition ---
    World& world = engine.world();
    auto place = [&](AssetId asset, Vec3 pos, float yaw, float r, float g, float b) {
        const EntityId e = world.spawn();
        TransformComponent t;
        t.position = pos;
        t.yaw = yaw;
        world.setTransform(e, t);
        ModelComponent m;
        m.asset = asset;
        m.color[0] = r;
        m.color[1] = g;
        m.color[2] = b;
        world.setModel(e, m);
        return e;
    };
    // Phase 11: the same placement call registers a collider, so the world
    // the player sees is the world the player collides with.
    CollisionWorld collision;
    const auto solid = [&](EntityId entity, Vec3 center, Vec3 halfExtents) {
        collision.addBox(Aabb::fromCenterExtents(center, halfExtents), entity);
        return entity;
    };
    place(groundId, {0, 0, 0}, 0, 0.42f, 0.47f, 0.36f);
    solid(place(houseId, {-6.0f, 1.3f, -6.0f}, 0.3f, 0.62f, 0.55f, 0.45f),
          {-6.0f, 1.3f, -6.0f}, {1.9f, 1.3f, 1.7f});
    solid(place(houseId, {6.0f, 1.3f, -8.0f}, -0.4f, 0.58f, 0.50f, 0.42f),
          {6.0f, 1.3f, -8.0f}, {1.9f, 1.3f, 1.7f});
    solid(place(houseId, {4.0f, 1.3f, -17.5f}, 1.2f, 0.55f, 0.52f, 0.47f),
          {4.0f, 1.3f, -17.5f}, {1.9f, 1.3f, 1.7f});
    solid(place(towerId, {10.0f, 3.5f, -14.0f}, 0, 0.52f, 0.50f, 0.55f),
          {10.0f, 3.5f, -14.0f}, {1.2f, 3.5f, 1.2f});
    place(stallId, {-2.5f, 1.05f, -9.0f}, 0.4f, 0.85f, 0.55f, 0.18f);
    place(wellId, {3.0f, 0.7f, -4.5f}, 0, 0.85f, 0.55f, 0.18f);
    place(crateId, {-1.0f, 0.5f, -5.0f}, 0.2f, 0.85f, 0.55f, 0.18f);

    // --- Characters (Phase 8, P9): player and NPCs are the SAME entity kind,
    //     the only difference is which controller steers them. ---
    CharacterSystem characters(world);
    AiSystem ai(world, characters);
    characters.factions().set(1, 3, Stance::Enemy);  // guards vs bandits (none today)

    auto spawnCharacter = [&](Vec3 pos, FactionId faction, uint32_t persistentId) {
        const EntityId e = world.spawn();
        TransformComponent t;
        t.position = pos;
        world.setTransform(e, t);
        world.setMovement(e, MovementComponent{{}, 4.0f});
        CharacterComponent* c = characters.attach(e);
        c->faction = faction;
        c->persistentId = persistentId;
        return e;
    };

    const EntityId player = spawnCharacter({0, 0, 4.0f}, 0, 1);
    characters.get(player)->controller = ControllerKind::Player;
    engine.setPlayerEntity(player);

    const EntityId guard = spawnCharacter({3.0f, 0, -2.0f}, 1, 2);
    characters.get(guard)->inventory.add(
        {assetIdFromName("item/sword"), "item.sword", 1, {0.72f, 0.75f, 0.79f, 1}});
    characters.equip(guard, 0, EquipSlot::HeldMain);
    characters.setSheathed(guard, true);  // on his back until trouble shows
    AiProfile guardProfile;
    guardProfile.canPatrol = true;
    guardProfile.canWander = false;
    guardProfile.aggressive = true;
    guardProfile.patrolCount = 2;
    guardProfile.patrolPoints[0] = {3.0f, 0, -2.0f};
    guardProfile.patrolPoints[1] = {-3.5f, 0, -7.0f};
    ai.attach(guard, guardProfile);
    // Humanoids, so they carry the humanoid vocabulary — and walking is
    // enforced by it (task 12.2): an NPC that never declares walk stays put.
    grantHumanoidActions(characters, guard);

    const EntityId villager = spawnCharacter({-4.0f, 0, -3.0f}, 0, 3);
    AiProfile villagerProfile;
    villagerProfile.canWander = true;
    villagerProfile.homeRadius = 4.0f;
    ai.attach(villager, villagerProfile);
    grantHumanoidActions(characters, villager);

    // --- Humanoid visuals: one template, three variant files, three outfits.
    HumanoidVariant playerVariant;
    HumanoidVariant guardVariant;
    guardVariant.bulk = 1.3f;
    guardVariant.shoulderWidth = 0.50f;
    HumanoidVariant villagerVariant;
    villagerVariant.height = 1.62f;
    villagerVariant.bulk = 0.9f;
    villagerVariant.shoulderWidth = 0.38f;
    villagerVariant.skin[0] = 0.62f;
    villagerVariant.skin[1] = 0.45f;
    villagerVariant.skin[2] = 0.33f;

    const WearableInstance playerOutfit[4] = {
        colored(WearableKind::Tunic, 0.30f, 0.38f, 0.45f),
        colored(WearableKind::Pants, 0.28f, 0.22f, 0.16f),
        colored(WearableKind::Boots, 0.22f, 0.16f, 0.11f),
        colored(WearableKind::HairShort, 0.22f, 0.14f, 0.08f)};
    const WearableInstance guardOutfit[5] = {
        colored(WearableKind::Tunic, 0.42f, 0.32f, 0.20f, 1),
        colored(WearableKind::Armor, 0.55f, 0.57f, 0.62f, 2),  // layered over the tunic
        colored(WearableKind::Pants, 0.25f, 0.22f, 0.18f),
        colored(WearableKind::Boots, 0.18f, 0.14f, 0.10f),
        colored(WearableKind::Sword, 0.72f, 0.75f, 0.79f, 1, true)};  // sheathed
    const WearableInstance villagerOutfit[4] = {
        colored(WearableKind::Tunic, 0.55f, 0.42f, 0.26f),
        colored(WearableKind::Pants, 0.30f, 0.24f, 0.18f),
        colored(WearableKind::Boots, 0.24f, 0.17f, 0.11f),
        colored(WearableKind::HairLong, 0.55f, 0.40f, 0.20f)};

    struct Actor {
        EntityId entity;
        RigInstance rig;
        LocomotionAnimator anim;
    };
    Actor actors[3];
    actors[0].entity = player;
    actors[1].entity = guard;
    actors[2].entity = villager;
    if (!uploadRig(renderer, playerVariant, playerOutfit, 4, actors[0].rig)) return 1;
    if (!uploadRig(renderer, guardVariant, guardOutfit, 5, actors[1].rig)) return 1;
    if (!uploadRig(renderer, villagerVariant, villagerOutfit, 4, actors[2].rig)) return 1;

    // --- GPU residency for props ---
    GpuAssetCache cache(renderer);
    for (AssetId id : {groundId, houseId, towerId, stallId, wellId, crateId}) {
        if (!cache.upload(assets, id)) return 1;
    }

    // --- Scripted touch input: walk forward, turn right toward the stall,
    //     keep walking, stop. Times in seconds -> events.
    struct Scripted {
        double t;
        TouchEvent e;
    };
    auto ns = [](double t) { return static_cast<int64_t>(t * 1e9); };
    const std::vector<Scripted> script = {
        {0.10, touch(0, TouchAction::Down, 400, 800, ns(0.10))},
        {0.15, touch(0, TouchAction::Move, 400, 640, ns(0.15))},   // full forward
        {3.00, touch(1, TouchAction::Down, 1500, 500, ns(3.00))},
        {3.10, touch(1, TouchAction::Move, 1560, 500, ns(3.10))},  // ease right ~0.21 rad
        {3.20, touch(1, TouchAction::Up, 1560, 500, ns(3.20))},
        {4.60, touch(0, TouchAction::Up, 400, 640, ns(4.60))},     // stop walking
        // Turn around to face back at the hamlet (~2.6 rad over a long drag).
        {5.40, touch(1, TouchAction::Down, 1300, 500, ns(5.40))},
        {5.60, touch(1, TouchAction::Move, 1550, 500, ns(5.60))},
        {5.80, touch(1, TouchAction::Move, 1800, 500, ns(5.80))},
        {6.00, touch(1, TouchAction::Move, 1930, 520, ns(6.00))},
        {6.10, touch(1, TouchAction::Up, 1930, 520, ns(6.10))},
    };

    // --- Run: 8 seconds at 60 Hz, captures at waypoints ---
    const double dt = 1.0 / 60.0;
    const std::vector<double> captureTimes = {0.05, 2.9, 7.9};
    size_t scriptCursor = 0;
    size_t captureCursor = 0;
    int captures = 0;
    ThirdPersonCamera cameraController;
    Camera camera;
    camera.aspect = static_cast<float>(renderConfig.width) / renderConfig.height;
    std::vector<uint8_t> pixels(static_cast<size_t>(renderConfig.width) * renderConfig.height * 4);
    const Vec3 startPos = world.transform(player)->position;

    for (int frame = 0; frame < 8 * 60; ++frame) {
        const double now = frame * dt;
        while (scriptCursor < script.size() && script[scriptCursor].t <= now) {
            engine.pushTouchEvent(script[scriptCursor].e);
            ++scriptCursor;
        }
        ai.step(static_cast<float>(dt));  // NPC intents; player intents come from touch
        const Vec3 beforeStep =
            world.transform(player) != nullptr ? world.transform(player)->position : Vec3{};
        engine.tick(dt);
        if (TransformComponent* pt = world.transform(player)) {
            CharacterShape shape;
            pt->position =
                collision.moveCharacter(beforeStep, shape, pt->position - beforeStep).position;
        }
        for (Actor& actor : actors) {
            const MovementComponent* m = world.movement(actor.entity);
            actor.anim.update(static_cast<float>(dt),
                              m != nullptr ? m->velocity.length() : 0.0f);
        }

        if (captureCursor < captureTimes.size() && now >= captureTimes[captureCursor]) {
            const float alpha = engine.renderAlpha();
            const TransformComponent* pt = world.transform(player);
            const Vec3 playerPos = lerp(pt->prevPosition, pt->position, alpha);
            const float playerYaw = pt->prevYaw + (pt->yaw - pt->prevYaw) * alpha;
            cameraController.update(camera, playerPos + Vec3{0, 0.9f, 0}, playerYaw);

            std::vector<DrawItem> items;
            world.forEachRenderable([&](EntityId, const TransformComponent& t,
                                        const ModelComponent& m) {
                const GpuLodMesh* mesh = cache.resolve(m.asset);
                if (mesh == nullptr) return;  // not resident: degrade, don't crash (P2)
                const AssetRecord* record = assets.find(m.asset);
                DrawItem item;
                item.mesh = mesh;
                const Vec3 pos = lerp(t.prevPosition, t.position, alpha);
                const float yaw = t.prevYaw + (t.yaw - t.prevYaw) * alpha;
                Transform xf;
                xf.position = pos;
                xf.rotation = Quat::fromAxisAngle({0, 1, 0}, yaw);
                item.model = xf.toMatrix();
                const float radius = mesh->bounds.extents().length();
                item.worldBounds =
                    Aabb::fromCenterExtents(pos + mesh->bounds.center(), {radius, radius, radius});
                item.lodReference = pos;
                memcpy(item.baseColor, m.color, sizeof(item.baseColor));
                item.material = (record != nullptr && record->kind == AssetKind::VirtualModel)
                                    ? MaterialKind::Placeholder
                                    : MaterialKind::Lit;
                if (item.material == MaterialKind::Placeholder) item.params[0] = 6.0f;
                items.push_back(item);
            });
            // The characters: interpolated like everything else, posed by
            // their animators (speed-driven — real velocities, no scripting).
            for (Actor& actor : actors) {
                const TransformComponent* t = world.transform(actor.entity);
                const Vec3 pos = lerp(t->prevPosition, t->position, alpha);
                const float yaw = t->prevYaw + (t->yaw - t->prevYaw) * alpha;
                Pose pose;
                actor.anim.samplePose(pose);
                emitRig(items, actor.rig, pose, pos, yaw);
            }

            RenderStats stats;
            if (!renderer.renderFrame(camera, items.data(), items.size(), &stats)) return 1;
            renderer.readback(pixels.data(), pixels.size());
            char path[512];
            snprintf(path, sizeof(path), "%s_%d.ppm", outPrefix.c_str(), captures);
            savePpm(path, pixels, renderConfig.width, renderConfig.height);
            printf("capture %d at t=%.2fs: player (%.2f, %.2f), yaw %.2f, drawn %u/%u\n",
                   captures, now, playerPos.x, playerPos.z, playerYaw, stats.drawn,
                   stats.submitted);
            ++captureCursor;
            ++captures;
        }
    }

    // --- Validate the walk ---
    const TransformComponent* finalT = world.transform(player);
    const float traveled = (finalT->position - startPos).length();
    printf("player traveled %.1f m, final yaw %.2f rad\n", traveled, finalT->yaw);
    const float guardTraveled = (world.transform(guard)->position -
                                 Vec3{3.0f, 0, -2.0f}).length();
    printf("guard patrol distance from post: %.1f m (state %d)\n", guardTraveled,
           static_cast<int>(ai.stateOf(guard)));
    printf("engine: %llu frames, %llu sim steps, %llu input events\n",
           static_cast<unsigned long long>(engine.stats().frameCount),
           static_cast<unsigned long long>(engine.stats().simStepCount),
           static_cast<unsigned long long>(engine.stats().inputEventCount));

    // --- Phase 11 mechanisms: the world is solid, and a tap acts on it ---
    // Walk the player straight at a house for two seconds and confirm the
    // wall stops them; then face an apple and tap to take it.
    InteractionSystem interactions(world, characters);
    interactions.setCollisionWorld(&collision);
    // Interaction is a character capability (owner ruling, P9): the game
    // hands the world of interactables to the character system, and every
    // character — player or not — can act through it.
    characters.setInteractions(&interactions);
    bool mechanismsOk = false;
    {
        TransformComponent* pt = world.transform(player);
        pt->position = {-6.0f, 0.0f, -2.6f};   // north of the first house
        pt->yaw = 0.0f;                        // facing -Z, into its wall
        pt->prevPosition = pt->position;
        MovementComponent* pm = world.movement(player);
        CharacterShape shape;
        const Vec3 startedAt = pt->position;
        for (int step = 0; step < 120; ++step) {
            pm->velocity = yawForward(pt->yaw) * pm->maxSpeed;
            const Vec3 before = pt->position;
            world.step(dt);
            pt->position = collision.moveCharacter(before, shape, pt->position - before).position;
        }
        const float traveledIntoHouse = startedAt.z - pt->position.z;
        const bool stoppedByWall =
            pt->position.z > -4.0f &&
            !collision.overlaps(CollisionWorld::characterBounds(pt->position, shape));
        printf("walked into the house: advanced %.2f m in 2 s, stopped clear of the wall: %s\n",
               traveledIntoHouse, stoppedByWall ? "yes" : "NO");

        // Turn away from the house and drop an apple a pace ahead — where
        // the player is now actually looking.
        pt->yaw = kPi;  // facing +Z
        const Vec3 ahead = pt->position + yawForward(pt->yaw) * 1.0f;
        const EntityId apple = place(crateId, {ahead.x, 0.2f, ahead.z}, 0, 0.85f, 0.25f, 0.20f);
        InteractableComponent pick;
        pick.kind = InteractionKind::PickUp;
        pick.promptKey = "prompt.take";
        pick.range = 2.0f;
        pick.item = {assetIdFromName("item/apple"), "item.apple", 1, {0.8f, 0.22f, 0.16f, 1}};
        interactions.attach(apple, pick);
        pm->velocity = {0, 0, 0};

        const bool focusedApple = characters.focus(player) == apple;
        CharacterComponent* playerCharacter = characters.get(player);
        const uint32_t carriedBefore = playerCharacter->inventory.size();
        InteractionResult took;
        characters.interact(player, &took);
        const bool carried = playerCharacter->inventory.size() == carriedBefore + 1;
        printf("apple: focused %s, taken %s, gone from the world %s\n",
               focusedApple ? "yes" : "no", took.handled ? "yes" : "no",
               !world.entities().isAlive(apple) ? "yes" : "no");
        mechanismsOk = stoppedByWall && focusedApple && took.handled && carried &&
                       !world.entities().isAlive(apple);
    }

    // --- Phase 12 actions: a character's vocabulary, on a real body ---
    // The player jumps (leaving the ground for the first time in this engine)
    // and then uses what is in their hands — the SAME action producing three
    // different outcomes because the item decides what it means.
    bool actionsOk = false;
    {
        // The walkabout above is the record for the capture checks; this
        // block teleports the player around to exercise actions, so its
        // transform is put back exactly as it was afterwards.
        const TransformComponent walkaboutEnd = *world.transform(player);
        ItemUseRegistry itemUses;
        characters.setItemUses(&itemUses);
        characters.setCollision(&collision);

        ItemUse sword;
        sword.kind = ItemUseKind::Strike;
        sword.range = 2.2f;
        sword.power = 0.3f;
        sword.cooldown = 0.6f;
        sword.animKey = "anim/swing";
        itemUses.define("item/sword", sword);

        ItemUse apple;
        apple.kind = ItemUseKind::Consume;
        apple.power = 0.25f;
        apple.cooldown = 0.3f;
        apple.effectId = assetIdFromName("effect/fed");
        apple.effectDuration = 60.0f;
        itemUses.define("item/apple", apple);

        ItemUse torch;
        torch.kind = ItemUseKind::Toggle;
        torch.cooldown = 0.2f;
        itemUses.define("item/torch", torch);

        // The vocabulary: universal by existing, humanoid by having a body.
        grantHumanoidActions(characters, player);
        const bool vocabulary = characters.can(player, actionInteract()) &&
                                characters.can(player, actionJump()) &&
                                characters.can(player, actionUseHeld());

        TransformComponent* pt = world.transform(player);
        pt->position = {0.0f, 0.0f, 6.0f};
        pt->prevPosition = pt->position;
        world.movement(player)->velocity = {0, 0, 0};
        const float dt32 = static_cast<float>(dt);
        world.step(dt);
        characters.stepLocomotion(dt32);

        const bool jumped = characters.perform(player, actionJump()).performed;
        const bool refusedMidAir =
            characters.perform(player, actionJump()).refusal == ActionRefusal::NotGrounded;
        float peakHeight = 0;
        bool landed = false;
        for (int step = 0; step < 240 && !landed; ++step) {
            world.step(dt);
            characters.stepLocomotion(dt32);
            characters.tickEffects(dt32);
            const float y = world.transform(player)->position.y;
            if (y > peakHeight) peakHeight = y;
            if (step > 10 && characters.get(player)->grounded) landed = true;
        }
        printf("jump: rose %.2f m, refused in mid-air %s, landed %s\n", peakHeight,
               refusedMidAir ? "yes" : "no", landed ? "yes" : "no");

        // One action, three meanings. Face the villager and swing.
        CharacterComponent* playerCharacter = characters.get(player);
        const TransformComponent* villagerTransform = world.transform(villager);
        pt = world.transform(player);
        pt->position = villagerTransform->position + Vec3{0, 0, 1.4f};
        pt->prevPosition = pt->position;
        pt->yaw = kPi;  // facing -Z... toward the villager at lower Z
        Vec3 toVillager = villagerTransform->position - pt->position;
        pt->yaw = std::atan2(toVillager.x, -toVillager.z);

        playerCharacter->inventory = ItemCollection{};
        playerCharacter->inventory.add(
            {assetIdFromName("item/sword"), "item.sword", 1, {0.72f, 0.75f, 0.79f, 1}});
        characters.equip(player, 0, EquipSlot::HeldMain);
        characters.setSheathed(player, true);
        const float villagerHealthBefore = characters.get(villager)->health;
        const ActionResult swing = characters.perform(player, actionUseHeld());
        const bool drewToStrike =
            !playerCharacter->equipment[static_cast<size_t>(EquipSlot::HeldMain)].sheathed;
        const bool struck = swing.performed && swing.useKind == ItemUseKind::Strike &&
                            characters.get(villager)->health < villagerHealthBefore;
        const bool cooldownHolds =
            characters.perform(player, actionUseHeld()).refusal == ActionRefusal::OnCooldown;
        characters.tickEffects(1.0f);

        // Same button, an apple in hand: eaten, and it heals.
        characters.unequip(player, EquipSlot::HeldMain);
        playerCharacter->inventory = ItemCollection{};
        playerCharacter->inventory.add(
            {assetIdFromName("item/apple"), "item.apple", 1, {0.8f, 0.22f, 0.16f, 1}});
        characters.equip(player, 0, EquipSlot::HeldMain);
        characters.damage(player, 0.4f);
        const float healthBefore = playerCharacter->health;
        const ActionResult bite = characters.perform(player, actionUseHeld());
        const bool ate = bite.performed && bite.useKind == ItemUseKind::Consume &&
                         playerCharacter->health > healthBefore &&
                         playerCharacter->equipment[static_cast<size_t>(EquipSlot::HeldMain)]
                                 .item.count == 0;
        characters.tickEffects(1.0f);

        // Same button, a torch in hand: it lights.
        playerCharacter->inventory = ItemCollection{};
        playerCharacter->inventory.add(
            {assetIdFromName("item/torch"), "item.torch", 1, {0.95f, 0.72f, 0.30f, 1}});
        characters.equip(player, 0, EquipSlot::HeldMain);
        const ActionResult light = characters.perform(player, actionUseHeld());
        const bool lit = light.performed && light.useKind == ItemUseKind::Toggle &&
                         light.toggledOn;

        printf("use held: sword %s, apple %s, torch %s (one action, the item decides)\n",
               struck ? "struck" : "MISSED", ate ? "eaten" : "NOT eaten",
               lit ? "lit" : "NOT lit");

        actionsOk = vocabulary && jumped && refusedMidAir && landed && peakHeight > 0.4f &&
                    drewToStrike && struck && cooldownHolds && ate && lit;
        characters.setCollision(nullptr);  // the walkabout above is a walker
        *world.transform(player) = walkaboutEnd;
    }

    // --- Character persistence (8.9): the walkabout's state rides a real
    //     save file — wound the guard, save, load, verify it came back. ---
    characters.damage(guard, 0.25f);
    bool persistenceOk = false;
    {
        std::string dir = "/tmp";
        if (const char* t = getenv("TMPDIR")) dir = t;
        SaveManager saves(dir.c_str());
        SaveSnapshot snapshot;
        snapshot.player.position = finalT->position;
        snapshot.player.yaw = finalT->yaw;
        characters.snapshot(snapshot.characters);
        if (saves.save("template_walk", snapshot)) {
            SaveSnapshot loaded;
            if (saves.load("template_walk", loaded) && loaded.characters.size() == 3) {
                for (const SavedCharacter& c : loaded.characters) {
                    if (c.persistentId != 2) continue;
                    const auto held = static_cast<size_t>(EquipSlot::HeldMain);
                    persistenceOk = c.health > 0.74f && c.health < 0.76f &&
                                    c.equipment[held].item.asset ==
                                        assetIdFromName("item/sword") &&
                                    c.equipment[held].sheathed == 1;
                }
            }
            saves.removeSlot("template_walk");
        }
        printf("persistence: guard wound + sheathed sword %s the save file\n",
               persistenceOk ? "survived" : "DID NOT survive");
    }

    for (Actor& actor : actors) destroyRig(renderer, actor.rig);
    cache.destroyAll();
    renderer.shutdown();
    const size_t gpuResidual = device.gpuBudgetStats().usedBytes;
    device.shutdown();
    engine.shutdown();

    const bool ok = captures == 3 && traveled > 15.0f && finalT->yaw > 0.5f &&
                    guardTraveled > 0.5f && persistenceOk && mechanismsOk && actionsOk &&
                    gpuResidual == 0;
    if (!ok) {
        fprintf(stderr,
                "FAIL: captures=%d traveled=%.1f yaw=%.2f guard=%.1f persist=%d "
                "mechanisms=%d actions=%d gpuResidual=%zu\n",
                captures, traveled, finalT->yaw, guardTraveled, persistenceOk, mechanismsOk,
                actionsOk, gpuResidual);
        return 1;
    }
    printf("OK\n");
    return 0;
}
