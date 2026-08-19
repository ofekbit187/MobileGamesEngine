// humanoid_demo: the Phase 8 proof tool. Renders, headlessly and with real
// engine output (● for the review board):
//   1. variants.ppm — one rig, five data-file variants, undressed
//   2. walk.ppm     — the default walk cycle, six phases of one character
//   3. dressed.ppm  — parametric wearables: fitting, layering, hair, held
//   4. hamlet_*.ppm — AI-driven characters walking a hamlet, bodies animated
//      by their actual movement (universal layer + humanoid layer together)
//
// Usage: mge_humanoid_demo [outputDir]

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "mge/character/humanoid.h"
#include "mge/core/memory.h"
#include "mge/framework/ai.h"
#include "mge/framework/character.h"
#include "mge/graphics/primitives.h"
#include "mge/graphics/renderer.h"
#include "mge/graphics/vulkan_device.h"

using namespace mge;

namespace {

// A humanoid's visual uploaded to the GPU: one mesh per rig part.
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

// Emits one posed character as draw items: model = root * jointWorld.
void emitRig(std::vector<DrawItem>& items, const RigInstance& rig, const Pose& pose,
             const Vec3& position, float yaw) {
    Mat4 world[kJointCount];
    evaluatePose(rig.skeleton, pose, world);
    // yawForward(yaw) = {sin, 0, -cos}: the matrix that takes -Z there is a
    // rotation by -yaw around +Y.
    const Mat4 root =
        Mat4::translation(position) * Mat4::rotation(Quat::fromAxisAngle({0, 1, 0}, -yaw));
    for (size_t i = 0; i < rig.parts.size(); ++i) {
        DrawItem item;
        item.mesh = &rig.gpu[i];
        item.model = root * world[static_cast<size_t>(rig.parts[i].joint)];
        item.worldBounds = Aabb::fromCenterExtents(position + Vec3{0, 1.2f, 0}, {3, 3, 3});
        item.lodReference = position;
        std::memcpy(item.baseColor, rig.parts[i].color, sizeof item.baseColor);
        items.push_back(item);
    }
}

DrawItem prop(const GpuLodMesh* mesh, const Vec3& position, float yawRadians, float r, float g,
              float b) {
    DrawItem item;
    item.mesh = mesh;
    Transform xf;
    xf.position = position;
    xf.rotation = Quat::fromAxisAngle({0, 1, 0}, yawRadians);
    item.model = xf.toMatrix();
    const Vec3 e = mesh->bounds.extents();
    const float radius = e.length();
    item.worldBounds = Aabb::fromCenterExtents(position + mesh->bounds.center(),
                                               {radius, radius, radius});
    item.lodReference = position;
    item.baseColor[0] = r;
    item.baseColor[1] = g;
    item.baseColor[2] = b;
    return item;
}

bool capture(Renderer& renderer, const Camera& camera, std::vector<DrawItem>& items,
             const std::string& path, double minNonSky) {
    RenderStats stats;
    if (!renderer.renderFrame(camera, items.data(), items.size(), &stats)) {
        fprintf(stderr, "FAIL: renderFrame for %s\n", path.c_str());
        return false;
    }
    const size_t pixelBytes = static_cast<size_t>(renderer.width()) * renderer.height() * 4;
    std::vector<uint8_t> pixels(pixelBytes);
    if (!renderer.readback(pixels.data(), pixels.size())) return false;
    size_t nonSky = 0;
    const uint8_t skyR = 135, skyG = 168, skyB = 214;  // default clear color
    for (size_t i = 0; i < pixelBytes; i += 4) {
        if (pixels[i] != skyR || pixels[i + 1] != skyG || pixels[i + 2] != skyB) ++nonSky;
    }
    const double share = static_cast<double>(nonSky) / (pixelBytes / 4);
    FILE* f = fopen(path.c_str(), "wb");
    if (f == nullptr) return false;
    fprintf(f, "P6\n%u %u\n255\n", renderer.width(), renderer.height());
    for (size_t i = 0; i < pixelBytes; i += 4) fwrite(&pixels[i], 1, 3, f);
    fclose(f);
    printf("%s: drawn %u, non-sky %.1f%%\n", path.c_str(), stats.drawn, share * 100.0);
    return share > minNonSky;
}

}  // namespace

int main(int argc, char** argv) {
    const std::string outDir = argc > 1 ? argv[1] : ".";

    BudgetRegistry budgets;
    VulkanDevice device(budgets);
    if (!device.init(VulkanDeviceConfig{})) return 1;
    printf("device: %s\n", device.deviceName());

    Renderer renderer(device);
    RendererConfig config;
    config.width = 1280;
    config.height = 720;
    config.lightDir[0] = -0.30f;
    config.lightDir[1] = 0.85f;
    config.lightDir[2] = 0.45f;
    if (!renderer.init(config)) return 1;

    GpuLodMesh ground, house, well;
    {
        LodMesh m;
        m.lods.push_back(makePlane(70, 70));
        m.computeBounds();
        if (!renderer.uploadLodMesh(m, ground)) return 1;
    }
    {
        LodMesh m;
        m.lods.push_back(makeBox({3.4f, 2.7f, 3.0f}));
        m.computeBounds();
        if (!renderer.uploadLodMesh(m, house)) return 1;
    }
    {
        LodMesh m;
        m.lods.push_back(makeCylinder(0.9f, 1.1f, 16));
        m.computeBounds();
        if (!renderer.uploadLodMesh(m, well)) return 1;
    }

    bool ok = true;

    // ---- 1. Variant lineup (8.12/8.13): one rig, five data files -----------
    {
        HumanoidVariant variants[5];
        // average (defaults)
        variants[1].height = 2.02f; variants[1].shoulderWidth = 0.46f;
        variants[1].legRatio = 0.53f; variants[1].bulk = 0.85f;
        variants[1].skin[0] = 0.55f; variants[1].skin[1] = 0.40f; variants[1].skin[2] = 0.29f;
        variants[2].height = 1.55f; variants[2].shoulderWidth = 0.46f;
        variants[2].hipWidth = 0.36f; variants[2].bulk = 1.35f;
        variants[2].skin[0] = 0.70f; variants[2].skin[1] = 0.52f; variants[2].skin[2] = 0.38f;
        variants[3].height = 2.30f; variants[3].shoulderWidth = 0.62f;
        variants[3].hipWidth = 0.42f; variants[3].bulk = 1.60f; variants[3].headScale = 0.92f;
        variants[3].skin[0] = 0.62f; variants[3].skin[1] = 0.58f; variants[3].skin[2] = 0.50f;
        variants[4].height = 1.62f; variants[4].shoulderWidth = 0.36f;
        variants[4].hipWidth = 0.26f; variants[4].bulk = 0.80f; variants[4].headScale = 1.08f;
        variants[4].skin[0] = 0.86f; variants[4].skin[1] = 0.68f; variants[4].skin[2] = 0.55f;

        std::vector<DrawItem> items;
        items.push_back(prop(&ground, {0, 0, 0}, 0, 0.44f, 0.48f, 0.37f));
        std::vector<RigInstance> rigs(5);
        LocomotionAnimator idle;
        for (int i = 0; i < 90; ++i) idle.update(1.0f / 60.0f, 0.0f);
        Pose pose;
        idle.samplePose(pose);
        const float xs[5] = {-4.4f, -2.2f, 0.0f, 2.4f, 4.6f};
        for (int i = 0; i < 5; ++i) {
            if (!uploadRig(renderer, variants[i], nullptr, 0, rigs[i])) return 1;
            emitRig(items, rigs[i], pose, {xs[i], 0, 0}, kPi);  // facing the camera
        }
        Camera camera;
        camera.eye = {0.0f, 1.9f, 6.8f};
        camera.target = {0.0f, 1.05f, 0.0f};
        camera.aspect = static_cast<float>(config.width) / config.height;
        ok = ok && capture(renderer, camera, items, outDir + "/humanoid_variants.ppm", 0.30);
        for (RigInstance& rig : rigs) destroyRig(renderer, rig);
    }

    // ---- 2. Default walk cycle (8.14): six phases of one stride ------------
    {
        HumanoidVariant villager;
        WearableInstance outfit[4];
        outfit[0].kind = WearableKind::Tunic;
        outfit[0].color[0] = 0.55f; outfit[0].color[1] = 0.42f; outfit[0].color[2] = 0.26f;
        outfit[1].kind = WearableKind::Pants;
        outfit[1].color[0] = 0.30f; outfit[1].color[1] = 0.24f; outfit[1].color[2] = 0.18f;
        outfit[2].kind = WearableKind::Boots;
        outfit[2].color[0] = 0.24f; outfit[2].color[1] = 0.17f; outfit[2].color[2] = 0.11f;
        outfit[3].kind = WearableKind::HairShort;
        outfit[3].color[0] = 0.22f; outfit[3].color[1] = 0.14f; outfit[3].color[2] = 0.08f;

        RigInstance rig;
        if (!uploadRig(renderer, villager, outfit, 4, rig)) return 1;
        std::vector<DrawItem> items;
        items.push_back(prop(&ground, {0, 0, 0}, 0, 0.44f, 0.48f, 0.37f));
        // Six animators, advanced to successive fractions of one stride.
        for (int i = 0; i < 6; ++i) {
            LocomotionAnimator anim;
            for (int f = 0; f < 60 * 3; ++f) anim.update(1.0f / 60.0f, 1.5f);  // settle blend
            const float cycleSeconds = 0.75f;
            const int extra = static_cast<int>(60.0f * cycleSeconds * i / 6.0f);
            for (int f = 0; f < extra; ++f) anim.update(1.0f / 60.0f, 1.5f);
            Pose pose;
            anim.samplePose(pose);
            emitRig(items, rig, pose, {-5.0f + 2.0f * i, 0, 0}, kPi * 0.5f);  // walking +X
        }
        Camera camera;
        camera.eye = {0.0f, 1.8f, 7.2f};
        camera.target = {0.0f, 1.0f, 0.0f};
        camera.aspect = static_cast<float>(config.width) / config.height;
        ok = ok && capture(renderer, camera, items, outDir + "/humanoid_walk.ppm", 0.25);
        destroyRig(renderer, rig);
    }

    // ---- 3. Wearables (8.15–8.19): fitting, layering, hair, held items -----
    {
        HumanoidVariant base;
        HumanoidVariant broad = base;
        broad.bulk = 1.4f;
        broad.shoulderWidth = 0.52f;
        HumanoidVariant tall = base;
        tall.height = 2.0f;
        tall.bulk = 0.9f;

        // The SAME outfit declarations dressed onto different bodies —
        // fitting is by construction, not per-body authoring.
        WearableInstance villager[4];
        villager[0].kind = WearableKind::Tunic;
        villager[0].color[0] = 0.55f; villager[0].color[1] = 0.42f; villager[0].color[2] = 0.26f;
        villager[1].kind = WearableKind::Pants;
        villager[1].color[0] = 0.30f; villager[1].color[1] = 0.24f; villager[1].color[2] = 0.18f;
        villager[2].kind = WearableKind::Boots;
        villager[2].color[0] = 0.24f; villager[2].color[1] = 0.17f; villager[2].color[2] = 0.11f;
        villager[3].kind = WearableKind::HairShort;
        villager[3].color[0] = 0.22f; villager[3].color[1] = 0.14f; villager[3].color[2] = 0.08f;

        WearableInstance noble[4];
        noble[0].kind = WearableKind::Tunic;
        noble[0].color[0] = 0.56f; noble[0].color[1] = 0.18f; noble[0].color[2] = 0.17f;
        noble[1].kind = WearableKind::Pants;
        noble[1].color[0] = 0.16f; noble[1].color[1] = 0.14f; noble[1].color[2] = 0.20f;
        noble[2].kind = WearableKind::Boots;
        noble[2].color[0] = 0.20f; noble[2].color[1] = 0.15f; noble[2].color[2] = 0.10f;
        noble[3].kind = WearableKind::HairLong;
        noble[3].color[0] = 0.65f; noble[3].color[1] = 0.52f; noble[3].color[2] = 0.28f;

        // Layering: armor (outer) OVER a tunic (mid) + a drawn sword.
        WearableInstance guard[5];
        guard[0].kind = WearableKind::Tunic;
        guard[0].layer = 1;
        guard[0].color[0] = 0.42f; guard[0].color[1] = 0.32f; guard[0].color[2] = 0.20f;
        guard[1].kind = WearableKind::Armor;
        guard[1].layer = 2;
        guard[1].color[0] = 0.55f; guard[1].color[1] = 0.57f; guard[1].color[2] = 0.62f;
        guard[2].kind = WearableKind::Pants;
        guard[2].color[0] = 0.25f; guard[2].color[1] = 0.22f; guard[2].color[2] = 0.18f;
        guard[3].kind = WearableKind::Boots;
        guard[3].color[0] = 0.18f; guard[3].color[1] = 0.14f; guard[3].color[2] = 0.10f;
        guard[4].kind = WearableKind::Sword;
        guard[4].sheathed = false;
        guard[4].color[0] = 0.72f; guard[4].color[1] = 0.75f; guard[4].color[2] = 0.79f;

        // Held-item depth: the same sword, sheathed on the back.
        WearableInstance hunter[5];
        hunter[0].kind = WearableKind::Tunic;
        hunter[0].color[0] = 0.26f; hunter[0].color[1] = 0.36f; hunter[0].color[2] = 0.22f;
        hunter[1].kind = WearableKind::Pants;
        hunter[1].color[0] = 0.28f; hunter[1].color[1] = 0.22f; hunter[1].color[2] = 0.16f;
        hunter[2].kind = WearableKind::Boots;
        hunter[2].color[0] = 0.22f; hunter[2].color[1] = 0.16f; hunter[2].color[2] = 0.11f;
        hunter[3].kind = WearableKind::HairShort;
        hunter[3].color[0] = 0.10f; hunter[3].color[1] = 0.08f; hunter[3].color[2] = 0.06f;
        hunter[4].kind = WearableKind::Sword;
        hunter[4].sheathed = true;
        hunter[4].color[0] = 0.72f; hunter[4].color[1] = 0.75f; hunter[4].color[2] = 0.79f;

        std::vector<DrawItem> items;
        items.push_back(prop(&ground, {0, 0, 0}, 0, 0.44f, 0.48f, 0.37f));
        std::vector<RigInstance> rigs(4);
        if (!uploadRig(renderer, base, villager, 4, rigs[0])) return 1;
        if (!uploadRig(renderer, tall, noble, 4, rigs[1])) return 1;
        if (!uploadRig(renderer, broad, guard, 5, rigs[2])) return 1;
        if (!uploadRig(renderer, base, hunter, 5, rigs[3])) return 1;

        LocomotionAnimator idle;
        for (int i = 0; i < 80; ++i) idle.update(1.0f / 60.0f, 0.0f);
        Pose pose;
        idle.samplePose(pose);
        emitRig(items, rigs[0], pose, {-3.6f, 0, 0}, kPi);
        emitRig(items, rigs[1], pose, {-1.2f, 0, 0}, kPi * 0.9f);
        emitRig(items, rigs[2], pose, {1.2f, 0, 0}, kPi);
        emitRig(items, rigs[3], pose, {3.6f, 0, 0.2f}, kPi * 0.62f);  // angled: back visible

        Camera camera;
        camera.eye = {0.0f, 1.9f, 6.0f};
        camera.target = {0.0f, 1.05f, 0.0f};
        camera.aspect = static_cast<float>(config.width) / config.height;
        ok = ok && capture(renderer, camera, items, outDir + "/humanoid_dressed.ppm", 0.30);
        for (RigInstance& rig : rigs) destroyRig(renderer, rig);
    }

    // ---- 4. AI hamlet walkabout (8.22): the layers working together --------
    // Real World + CharacterSystem + AiSystem simulation; bodies animated by
    // their ACTUAL velocities, headings from their transforms.
    {
        World world(64);
        CharacterSystem characters(world);
        AiSystem ai(world, characters);
        characters.factions().set(2, 0, Stance::Enemy);  // deer fear villagers

        struct Actor {
            EntityId entity;
            RigInstance rig;
            LocomotionAnimator anim;
        };
        std::vector<Actor> actors(4);

        const auto spawnActor = [&](int i, Vec3 pos, const HumanoidVariant& variant,
                                    const WearableInstance* outfit, size_t outfitCount,
                                    FactionId faction, const AiProfile& profile) -> bool {
            const EntityId entity = world.spawn();
            TransformComponent transform;
            transform.position = pos;
            world.setTransform(entity, transform);
            world.setMovement(entity, MovementComponent{{}, 6.0f});
            CharacterComponent* c = characters.attach(entity);
            c->faction = faction;
            if (!ai.attach(entity, profile)) return false;
            actors[i].entity = entity;
            return uploadRig(renderer, variant, outfit, outfitCount, actors[i].rig);
        };

        HumanoidVariant v0, v1, v2;
        v1.height = 1.62f; v1.bulk = 0.9f; v1.shoulderWidth = 0.38f;
        v1.skin[0] = 0.62f; v1.skin[1] = 0.45f; v1.skin[2] = 0.33f;
        v2.bulk = 1.3f; v2.shoulderWidth = 0.50f;

        WearableInstance villagerA[4], villagerB[4], guardKit[5];
        villagerA[0].kind = WearableKind::Tunic;
        villagerA[0].color[0] = 0.55f; villagerA[0].color[1] = 0.42f; villagerA[0].color[2] = 0.26f;
        villagerA[1].kind = WearableKind::Pants;
        villagerA[1].color[0] = 0.30f; villagerA[1].color[1] = 0.24f; villagerA[1].color[2] = 0.18f;
        villagerA[2].kind = WearableKind::Boots;
        villagerA[2].color[0] = 0.24f; villagerA[2].color[1] = 0.17f; villagerA[2].color[2] = 0.11f;
        villagerA[3].kind = WearableKind::HairShort;
        villagerA[3].color[0] = 0.22f; villagerA[3].color[1] = 0.14f; villagerA[3].color[2] = 0.08f;
        std::memcpy(villagerB, villagerA, sizeof villagerA);
        villagerB[0].color[0] = 0.30f; villagerB[0].color[1] = 0.38f; villagerB[0].color[2] = 0.45f;
        villagerB[3].kind = WearableKind::HairLong;
        villagerB[3].color[0] = 0.55f; villagerB[3].color[1] = 0.40f; villagerB[3].color[2] = 0.20f;
        std::memcpy(guardKit, villagerA, sizeof villagerA);
        guardKit[0].layer = 1;
        guardKit[1].kind = WearableKind::Armor;
        guardKit[1].layer = 2;
        guardKit[1].color[0] = 0.55f; guardKit[1].color[1] = 0.57f; guardKit[1].color[2] = 0.62f;
        guardKit[4].kind = WearableKind::Sword;
        guardKit[4].sheathed = true;
        guardKit[4].color[0] = 0.72f; guardKit[4].color[1] = 0.75f; guardKit[4].color[2] = 0.79f;

        AiProfile wanderer;
        wanderer.canWander = true;
        wanderer.homeRadius = 5.0f;
        AiProfile patrol;
        patrol.canPatrol = true;
        patrol.canWander = false;
        patrol.patrolCount = 4;
        patrol.patrolPoints[0] = {-6, 0, -6};
        patrol.patrolPoints[1] = {6, 0, -6};
        patrol.patrolPoints[2] = {6, 0, 4};
        patrol.patrolPoints[3] = {-6, 0, 4};
        AiProfile fearful;
        fearful.fearful = true;
        fearful.canWander = true;
        fearful.homeRadius = 3.0f;

        if (!spawnActor(0, {0, 0, -2}, v0, villagerA, 4, 0, wanderer)) return 1;
        if (!spawnActor(1, {3, 0, 2}, v1, villagerB, 4, 0, wanderer)) return 1;
        if (!spawnActor(2, {-6, 0, -6}, v2, guardKit, 5, 1, patrol)) return 1;
        if (!spawnActor(3, {9, 0, -9}, v1, villagerA, 4, 2, fearful)) return 1;

        Camera camera;
        camera.eye = {10.5f, 6.5f, 11.5f};
        camera.target = {0.0f, 0.8f, -2.0f};
        camera.aspect = static_cast<float>(config.width) / config.height;

        const float dt = 1.0f / 60.0f;
        int shot = 0;
        for (int frame = 1; frame <= 60 * 24; ++frame) {
            ai.step(dt);
            world.step(dt);
            for (Actor& actor : actors) {
                const MovementComponent* movement = world.movement(actor.entity);
                actor.anim.update(dt, movement != nullptr ? movement->velocity.length() : 0.0f);
            }
            if (frame % (60 * 8) == 0) {
                std::vector<DrawItem> items;
                items.push_back(prop(&ground, {0, 0, 0}, 0, 0.44f, 0.48f, 0.37f));
                items.push_back(prop(&house, {-6.5f, 1.35f, -1.5f}, 0.25f, 0.62f, 0.55f, 0.45f));
                items.push_back(prop(&house, {5.5f, 1.35f, -7.0f}, -0.4f, 0.58f, 0.50f, 0.42f));
                items.push_back(prop(&well, {0.5f, 0.55f, 3.5f}, 0, 0.52f, 0.51f, 0.50f));
                for (Actor& actor : actors) {
                    const TransformComponent* t = world.transform(actor.entity);
                    Pose pose;
                    actor.anim.samplePose(pose);
                    emitRig(items, actor.rig, pose, t->position, t->yaw);
                }
                char name[64];
                snprintf(name, sizeof name, "/hamlet_t%d.ppm", ++shot);
                ok = ok && capture(renderer, camera, items, outDir + name, 0.30);
            }
        }
        for (Actor& actor : actors) destroyRig(renderer, actor.rig);
        printf("hamlet: villager=%d guard=%d deer=%d (states after 24s)\n",
               static_cast<int>(ai.stateOf(actors[0].entity)),
               static_cast<int>(ai.stateOf(actors[2].entity)),
               static_cast<int>(ai.stateOf(actors[3].entity)));
    }

    // ---- Cleanup: GPU budget must return to zero (P1) ----------------------
    renderer.destroyLodMesh(ground);
    renderer.destroyLodMesh(house);
    renderer.destroyLodMesh(well);
    renderer.shutdown();
    const size_t residual = device.gpuBudgetStats().usedBytes;
    device.shutdown();
    if (residual != 0) {
        fprintf(stderr, "FAIL: residual GPU budget %zu bytes\n", residual);
        return 1;
    }
    if (!ok) {
        fprintf(stderr, "FAIL: a capture missed its content threshold\n");
        return 1;
    }
    printf("OK\n");
    return 0;
}
