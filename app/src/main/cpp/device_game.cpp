#include "device_game.h"

#include <cmath>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#include "mge/character/humanoid.h"
#include "mge/core/log.h"
#include "mge/framework/ai.h"
#include "mge/framework/asset_registry.h"
#include "mge/framework/camera_controller.h"
#include "mge/framework/character.h"
#include "mge/graphics/primitives.h"
#include "mge/graphics/renderer.h"
#include "mge/graphics/vulkan_device.h"
#include "mge/ui/font.h"
#include "mge/ui/localization.h"
#include "mge/ui/ui.h"

namespace mge {

namespace {

constexpr const char* kTag = "device";

// ---- humanoid rig on the GPU (same pattern the demo tools use) -------------

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

// ---- synthesized sounds (the same voices audio_demo proved headlessly) -----

constexpr uint32_t kAudioRate = 24000;

WavData makeWind(double seconds) {
    WavData data;
    data.sampleRate = kAudioRate;
    data.channels = 1;
    data.samples.resize(static_cast<size_t>(kAudioRate * seconds));
    uint32_t s = 22222;
    float low = 0;
    for (size_t i = 0; i < data.samples.size(); ++i) {
        s = s * 1664525u + 1013904223u;
        const float noise = (static_cast<float>(s >> 8) / 8388608.0f) - 1.0f;
        low += (noise - low) * 0.04f;
        const float swell =
            0.6f + 0.4f * std::sin(2.0f * kPi * 0.13f * static_cast<float>(i) / kAudioRate);
        data.samples[i] = static_cast<int16_t>(low * 9000.0f * swell);
    }
    return data;
}

WavData makeBell(double seconds) {
    WavData data;
    data.sampleRate = kAudioRate;
    data.channels = 1;
    data.samples.resize(static_cast<size_t>(kAudioRate * seconds));
    const float partials[4] = {329.6f, 659.3f, 987.8f, 1318.5f};
    const float weights[4] = {1.0f, 0.55f, 0.30f, 0.18f};
    for (size_t i = 0; i < data.samples.size(); ++i) {
        const float t = static_cast<float>(i) / kAudioRate;
        float v = 0;
        for (int p = 0; p < 4; ++p) {
            v += weights[p] * std::exp(-t * (1.2f + p)) * std::sin(2.0f * kPi * partials[p] * t);
        }
        data.samples[i] = static_cast<int16_t>(v * 9500.0f);
    }
    return data;
}

WavData makeMusicLoop() {
    const float notes[8] = {220.0f, 261.6f, 329.6f, 293.7f, 261.6f, 220.0f, 196.0f, 220.0f};
    const double noteSeconds = 0.5;
    WavData data;
    data.sampleRate = kAudioRate;
    data.channels = 1;
    data.samples.resize(static_cast<size_t>(kAudioRate * noteSeconds * 8));
    for (int n = 0; n < 8; ++n) {
        const size_t start = static_cast<size_t>(kAudioRate * noteSeconds * n);
        const size_t frames = static_cast<size_t>(kAudioRate * noteSeconds);
        for (size_t i = 0; i < frames; ++i) {
            const float t = static_cast<float>(i) / kAudioRate;
            const float pluck = std::exp(-t * 4.0f);
            const float v = pluck * (std::sin(2.0f * kPi * notes[n] * t) +
                                     0.35f * std::sin(2.0f * kPi * notes[n] * 2.0f * t));
            data.samples[start + i] = static_cast<int16_t>(v * 6500.0f);
        }
    }
    return data;
}

WavData makeSpokenTake(double seconds) {
    WavData data;
    data.sampleRate = kAudioRate;
    data.channels = 1;
    data.samples.resize(static_cast<size_t>(kAudioRate * seconds));
    for (size_t i = 0; i < data.samples.size(); ++i) {
        const float t = static_cast<float>(i) / kAudioRate;
        const float syllable = std::fmax(0.0f, std::sin(2.0f * kPi * 3.1f * t));
        const float pitch = 135.0f + 18.0f * std::sin(2.0f * kPi * 0.9f * t);
        const float v = syllable * (std::sin(2.0f * kPi * pitch * t) +
                                    0.45f * std::sin(2.0f * kPi * pitch * 3.1f * t) +
                                    0.20f * std::sin(2.0f * kPi * pitch * 5.2f * t));
        data.samples[i] = static_cast<int16_t>(v * 10500.0f);
    }
    return data;
}

}  // namespace

// ---------------------------------------------------------------------------

struct DeviceGame::Impl {
    Engine* engine = nullptr;
    AudioMixer* mixer = nullptr;

    // Graphics (offscreen render + blit; parchment heartbeat if absent).
    BudgetRegistry gpuBudgets;
    VulkanDevice device{gpuBudgets};
    Renderer renderer{device};
    bool vulkanOk = false;
    uint32_t renderWidth = 960, renderHeight = 540;
    std::vector<uint8_t> pixels;
    double heartbeat = 0;

    // Scene
    AssetRegistry assets;
    std::unordered_map<AssetId, GpuLodMesh> resident;
    struct Actor {
        EntityId entity = kInvalidEntity;
        RigInstance rig;
        LocomotionAnimator anim;
    };
    Actor player, guard, villager;
    CharacterSystem* characters = nullptr;
    AiSystem* ai = nullptr;
    ThirdPersonCamera cameraController;
    Camera camera;

    // Audio
    AudioClip wind, music, bell, speech;
    double bellTimer = 2.0;
    double speechCooldown = 4.0;

    // UI overlay (the Codex HUD, task 5.5): drawn at render resolution;
    // touch state arrives in surface pixels and is scaled down.
    FontAtlas font;
    Ui ui;
    Localization strings;
    bool uiOk = false;
    float touchScaleX = 1.0f, touchScaleY = 1.0f;
    double subtitleTimer = 0;

    ~Impl() {
        delete ai;
        delete characters;
    }
};

DeviceGame::~DeviceGame() { stop(); }

bool DeviceGame::start(Engine& engine, AudioMixer& mixer, uint32_t surfaceWidth,
                       uint32_t surfaceHeight) {
    if (impl_ != nullptr) return true;
    impl_ = new Impl();
    Impl& s = *impl_;
    s.engine = &engine;
    s.mixer = &mixer;

    // Render size: ~540p at the surface's aspect (blit scales to the screen).
    if (surfaceWidth > 0 && surfaceHeight > 0) {
        s.renderHeight = 540;
        s.renderWidth =
            ((surfaceWidth * s.renderHeight / surfaceHeight) + 3u) & ~3u;
        if (s.renderWidth > 1280) s.renderWidth = 1280;
        if (s.renderWidth < 320) s.renderWidth = 320;
    }

    // --- Graphics: the engine's own offscreen pass, on the device GPU ---
    s.vulkanOk = s.device.init(VulkanDeviceConfig{});
    if (s.vulkanOk) {
        RendererConfig config;
        config.width = s.renderWidth;
        config.height = s.renderHeight;
        config.lightDir[0] = -0.25f;
        config.lightDir[1] = 0.80f;
        config.lightDir[2] = 0.55f;
        s.vulkanOk = s.renderer.init(config);
    }
    if (s.vulkanOk) {
        MGE_LOGI(kTag, "vulkan up: %s, offscreen %ux%u", s.device.deviceName(),
                 s.renderWidth, s.renderHeight);
    } else {
        MGE_LOGE(kTag, "vulkan unavailable — parchment fallback active");
    }
    s.pixels.resize(static_cast<size_t>(s.renderWidth) * s.renderHeight * 4);

    // --- World: the template hamlet ---
    World& world = engine.world();
    auto meshOf = [](MeshData data) {
        LodMesh m;
        m.lods.push_back(std::move(data));
        m.computeBounds();
        return m;
    };
    const AssetId groundId = s.assets.registerMesh("world/ground", meshOf(makePlane(80, 80)));
    const AssetId houseId = s.assets.registerMesh("prop/house", meshOf(makeBox({3.2f, 2.6f, 2.8f})));
    const AssetId towerId = s.assets.registerMesh("prop/tower", meshOf(makeCylinder(1.2f, 7.0f, 32)));
    VirtualModelDesc stallDesc;
    stallDesc.proportions = {2.4f, 2.1f, 1.8f};
    stallDesc.description = "wooden market stall, striped canvas roof, worn planks";
    const AssetId stallId = s.assets.registerVirtualModel("prop/market_stall", stallDesc);
    VirtualModelDesc wellDesc;
    wellDesc.proportions = {1.8f, 1.4f, 1.8f};
    wellDesc.shape = PlaceholderShape::Cylinder;
    wellDesc.description = "stone village well with wooden crank and bucket";
    const AssetId wellId = s.assets.registerVirtualModel("prop/well", wellDesc);

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
    place(groundId, {0, 0, 0}, 0, 0.42f, 0.47f, 0.36f);
    place(houseId, {-6.0f, 1.3f, -6.0f}, 0.3f, 0.62f, 0.55f, 0.45f);
    place(houseId, {6.0f, 1.3f, -8.0f}, -0.4f, 0.58f, 0.50f, 0.42f);
    place(houseId, {4.0f, 1.3f, -17.5f}, 1.2f, 0.55f, 0.52f, 0.47f);
    place(towerId, {10.0f, 3.5f, -14.0f}, 0, 0.52f, 0.50f, 0.55f);
    place(stallId, {-2.5f, 1.05f, -9.0f}, 0.4f, 0.85f, 0.55f, 0.18f);
    place(wellId, {3.0f, 0.7f, -4.5f}, 0, 0.85f, 0.55f, 0.18f);

    // --- Characters: player + NPCs, same humanoid, different controllers ---
    s.characters = new CharacterSystem(world);
    s.ai = new AiSystem(world, *s.characters);
    auto spawnCharacter = [&](Vec3 pos, FactionId faction, uint32_t persistentId) {
        const EntityId e = world.spawn();
        TransformComponent t;
        t.position = pos;
        world.setTransform(e, t);
        world.setMovement(e, MovementComponent{{}, 4.0f});
        CharacterComponent* c = s.characters->attach(e);
        c->faction = faction;
        c->persistentId = persistentId;
        return e;
    };
    s.player.entity = spawnCharacter({0, 0, 4.0f}, 0, 1);
    s.characters->get(s.player.entity)->controller = ControllerKind::Player;
    engine.setPlayerEntity(s.player.entity);

    s.guard.entity = spawnCharacter({3.0f, 0, -2.0f}, 1, 2);
    s.characters->get(s.guard.entity)->inventory.add(
        {assetIdFromName("item/sword"), "item.sword", 1, {0.72f, 0.75f, 0.79f, 1}});
    s.characters->equip(s.guard.entity, 0, EquipSlot::HeldMain);
    s.characters->setSheathed(s.guard.entity, true);
    AiProfile guardProfile;
    guardProfile.canPatrol = true;
    guardProfile.canWander = false;
    guardProfile.patrolCount = 2;
    guardProfile.patrolPoints[0] = {3.0f, 0, -2.0f};
    guardProfile.patrolPoints[1] = {-3.5f, 0, -7.0f};
    s.ai->attach(s.guard.entity, guardProfile);

    s.villager.entity = spawnCharacter({-4.0f, 0, -3.0f}, 0, 3);
    AiProfile villagerProfile;
    villagerProfile.canWander = true;
    villagerProfile.homeRadius = 4.0f;
    s.ai->attach(s.villager.entity, villagerProfile);

    if (s.vulkanOk) {
        for (AssetId id : {groundId, houseId, towerId, stallId, wellId}) {
            const AssetRecord* record = s.assets.find(id);
            GpuLodMesh mesh;
            if (record == nullptr || !s.renderer.uploadLodMesh(record->mesh, mesh)) {
                MGE_LOGE(kTag, "mesh upload failed");
                s.vulkanOk = false;
                break;
            }
            s.resident.emplace(id, std::move(mesh));
        }
    }
    if (s.vulkanOk) {
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
            colored(WearableKind::Armor, 0.55f, 0.57f, 0.62f, 2),
            colored(WearableKind::Pants, 0.25f, 0.22f, 0.18f),
            colored(WearableKind::Boots, 0.18f, 0.14f, 0.10f),
            colored(WearableKind::Sword, 0.72f, 0.75f, 0.79f, 1, true)};
        const WearableInstance villagerOutfit[4] = {
            colored(WearableKind::Tunic, 0.55f, 0.42f, 0.26f),
            colored(WearableKind::Pants, 0.30f, 0.24f, 0.18f),
            colored(WearableKind::Boots, 0.24f, 0.17f, 0.11f),
            colored(WearableKind::HairLong, 0.55f, 0.40f, 0.20f)};
        s.vulkanOk = uploadRig(s.renderer, playerVariant, playerOutfit, 4, s.player.rig) &&
                     uploadRig(s.renderer, guardVariant, guardOutfit, 5, s.guard.rig) &&
                     uploadRig(s.renderer, villagerVariant, villagerOutfit, 4, s.villager.rig);
    }

    // --- Audio: live soundscape through the AAudio-pulled mixer ---
    s.mixer->setMusicDucking(0.30f);
    if (s.wind.adopt(makeWind(4.0), *s.mixer)) {
        AudioPlayParams windParams;
        windParams.bus = AudioBus::Sfx;
        windParams.gain = 0.4f;
        windParams.loop = true;
        s.mixer->play(s.wind, windParams);
    }
    if (s.music.adopt(makeMusicLoop(), *s.mixer)) {
        AudioPlayParams musicParams;
        musicParams.bus = AudioBus::Music;
        musicParams.gain = 0.5f;
        musicParams.loop = true;
        s.mixer->play(s.music, musicParams);
    }
    s.bell.adopt(makeBell(3.0), *s.mixer);
    s.speech.adopt(makeSpokenTake(2.6), *s.mixer);

    // --- The Codex HUD (task 5.5) over the scene, engine-embedded font ---
    if (s.vulkanOk) {
        s.uiOk = s.font.bakeEmbedded(30.0f) && s.renderer.setUiFont(s.font);
        if (s.uiOk) {
            s.strings.set(Language::English, "hud.subtitle",
                          "Fine morning, friend. Mind the bell tower.");
            s.strings.set(Language::Hebrew, "hud.subtitle",
                          "\xd7\x91\xd7\x95\xd7\xa7\xd7\xa8 \xd7\x98\xd7\x95\xd7\x91, "
                          "\xd7\x99\xd7\x93\xd7\x99\xd7\x93\xd7\x99");
            s.uiOk = s.ui.init(&s.font, codexTheme(), &s.strings);
        }
        if (!s.uiOk) MGE_LOGW(kTag, "UI overlay unavailable — HUD off");
    }
    if (surfaceWidth > 0 && surfaceHeight > 0) {
        s.touchScaleX = static_cast<float>(s.renderWidth) / surfaceWidth;
        s.touchScaleY = static_cast<float>(s.renderHeight) / surfaceHeight;
    }

    s.camera.aspect = static_cast<float>(s.renderWidth) / s.renderHeight;
    MGE_LOGI(kTag, "device game started (vulkan=%d ui=%d)", s.vulkanOk ? 1 : 0,
             s.uiOk ? 1 : 0);
    return true;
}

void DeviceGame::stop() {
    if (impl_ == nullptr) return;
    Impl& s = *impl_;
    if (s.vulkanOk || s.renderer.initialized()) {
        destroyRig(s.renderer, s.player.rig);
        destroyRig(s.renderer, s.guard.rig);
        destroyRig(s.renderer, s.villager.rig);
        for (auto& [id, mesh] : s.resident) s.renderer.destroyLodMesh(mesh);
        s.resident.clear();
        s.renderer.shutdown();
    }
    s.device.shutdown();
    delete impl_;
    impl_ = nullptr;
}

void DeviceGame::frame(double dtSeconds, ANativeWindow* window) {
    if (impl_ == nullptr) return;
    Impl& s = *impl_;
    double dt = dtSeconds;
    if (dt <= 0.0 || dt > 0.05) dt = 1.0 / 60.0;

    // --- Simulation: AI intents, then the engine's fixed-step tick ---
    s.ai->step(static_cast<float>(dt));
    s.engine->tick(dt);
    World& world = s.engine->world();
    for (Impl::Actor* actor : {&s.player, &s.guard, &s.villager}) {
        const MovementComponent* m = world.movement(actor->entity);
        actor->anim.update(static_cast<float>(dt),
                           m != nullptr ? m->velocity.length() : 0.0f);
    }

    // --- Audio: listener at the player; bell from the tower; the villager
    //     greets you up close (music ducks under speech, live) ---
    const TransformComponent* pt = world.transform(s.player.entity);
    if (pt != nullptr) {
        s.mixer->setListener(pt->position + Vec3{0, 1.6f, 0}, yawForward(pt->yaw));
    }
    s.bellTimer -= dt;
    if (s.bellTimer <= 0.0 && s.bell.loaded()) {
        s.bellTimer = 9.0;
        AudioPlayParams bellParams;
        bellParams.bus = AudioBus::Sfx;
        bellParams.positional = true;
        bellParams.position = {10.0f, 6.0f, -14.0f};  // the tower top
        bellParams.refDistance = 3.0f;
        bellParams.maxDistance = 50.0f;
        s.mixer->play(s.bell, bellParams);
    }
    s.speechCooldown -= dt;
    const TransformComponent* vt = world.transform(s.villager.entity);
    if (s.speechCooldown <= 0.0 && pt != nullptr && vt != nullptr &&
        (vt->position - pt->position).length() < 3.5f && s.speech.loaded()) {
        s.speechCooldown = 14.0;
        s.subtitleTimer = 3.2;  // the line's text, on screen while it plays
        AudioPlayParams speechParams;
        speechParams.bus = AudioBus::Voice;
        speechParams.positional = true;
        speechParams.position = vt->position + Vec3{0, 1.5f, 0};
        speechParams.refDistance = 2.0f;
        speechParams.maxDistance = 20.0f;
        s.mixer->play(s.speech, speechParams);
    }

    if (window == nullptr) return;
    ANativeWindow_setBuffersGeometry(window, static_cast<int32_t>(s.renderWidth),
                                     static_cast<int32_t>(s.renderHeight),
                                     WINDOW_FORMAT_RGBA_8888);

    // --- Render: the engine's offscreen pass, or the parchment heartbeat ---
    bool haveFrame = false;
    if (s.vulkanOk && pt != nullptr) {
        const float alpha = s.engine->renderAlpha();
        const Vec3 playerPos = lerp(pt->prevPosition, pt->position, alpha);
        const float playerYaw = pt->prevYaw + (pt->yaw - pt->prevYaw) * alpha;
        s.cameraController.update(s.camera, playerPos + Vec3{0, 0.9f, 0}, playerYaw);

        std::vector<DrawItem> items;
        world.forEachRenderable([&](EntityId, const TransformComponent& t,
                                    const ModelComponent& m) {
            auto it = s.resident.find(m.asset);
            if (it == s.resident.end()) return;
            const AssetRecord* record = s.assets.find(m.asset);
            DrawItem item;
            item.mesh = &it->second;
            const Vec3 pos = lerp(t.prevPosition, t.position, alpha);
            Transform xf;
            xf.position = pos;
            xf.rotation = Quat::fromAxisAngle({0, 1, 0}, t.yaw);
            item.model = xf.toMatrix();
            const float radius = it->second.bounds.extents().length();
            item.worldBounds =
                Aabb::fromCenterExtents(pos + it->second.bounds.center(),
                                        {radius, radius, radius});
            item.lodReference = pos;
            memcpy(item.baseColor, m.color, sizeof(item.baseColor));
            item.material = (record != nullptr && record->kind == AssetKind::VirtualModel)
                                ? MaterialKind::Placeholder
                                : MaterialKind::Lit;
            if (item.material == MaterialKind::Placeholder) item.params[0] = 6.0f;
            items.push_back(item);
        });
        for (Impl::Actor* actor : {&s.player, &s.guard, &s.villager}) {
            const TransformComponent* t = world.transform(actor->entity);
            if (t == nullptr) continue;
            const Vec3 pos = lerp(t->prevPosition, t->position, alpha);
            const float yaw = t->prevYaw + (t->yaw - t->prevYaw) * alpha;
            Pose pose;
            actor->anim.samplePose(pose);
            emitRig(items, actor->rig, pose, pos, yaw);
        }
        // --- HUD: health, compass, held slot, virtual controls, subtitle ---
        const UiDrawList* uiList = nullptr;
        if (s.uiOk) {
            const float w = static_cast<float>(s.renderWidth);
            const float h = static_cast<float>(s.renderHeight);
            s.subtitleTimer -= dt;
            s.ui.beginFrame(w, h);
            const CharacterComponent* pc = s.characters->get(s.player.entity);
            s.ui.healthBar({18, 14, w * 0.24f, 16}, pc != nullptr ? pc->health : 1.0f);
            s.ui.compassStrip({w * 0.5f - w * 0.13f, 10, w * 0.26f, 24}, pt->yaw);
            const Item* held = nullptr;
            if (pc != nullptr) {
                const EquippedItem& slot =
                    pc->equipment[static_cast<size_t>(EquipSlot::HeldMain)];
                if (slot.item.count > 0) held = &slot.item;
            }
            s.ui.itemSlot({w - 18 - 52, 12, 52, 52}, held, false);
            float ax, ay, sx, sy;
            const bool stickOn =
                s.engine->controls().stickState(ax, ay, sx, sy);
            if (stickOn) {
                s.ui.virtualControls(true, ax * s.touchScaleX, ay * s.touchScaleY,
                                     sx * s.touchScaleX, sy * s.touchScaleY);
            } else {
                // Resting stick hint in the left control zone.
                s.ui.virtualControls(false, w * 0.14f, h * 0.78f, w * 0.14f, h * 0.78f);
            }
            if (s.subtitleTimer > 0) {
                s.ui.label({w * 0.5f - w * 0.35f, h - 54, w * 0.7f, 30}, "hud.subtitle",
                           0.7f, TextAlign::Center);
            }
            uiList = &s.ui.drawList();
        }
        haveFrame = s.renderer.renderFrame(s.camera, items.data(), items.size(), nullptr,
                                           uiList) &&
                    s.renderer.readback(s.pixels.data(), s.pixels.size());
        if (!haveFrame) {
            MGE_LOGE(kTag, "render/readback failed — falling back");
            s.vulkanOk = false;
        }
    }
    if (!haveFrame) {
        // Parchment heartbeat: visible proof of life when Vulkan is absent.
        s.heartbeat += dt;
        const uint32_t bar =
            static_cast<uint32_t>((0.5 + 0.5 * std::sin(s.heartbeat * 2.0)) *
                                  (s.renderWidth - 40));
        for (uint32_t y = 0; y < s.renderHeight; ++y) {
            for (uint32_t x = 0; x < s.renderWidth; ++x) {
                uint8_t* p = &s.pixels[(static_cast<size_t>(y) * s.renderWidth + x) * 4];
                const bool inBar = y > s.renderHeight / 2 - 8 && y < s.renderHeight / 2 + 8 &&
                                   x > bar && x < bar + 40;
                p[0] = inBar ? 0x8E : 0xF2;
                p[1] = inBar ? 0x2F : 0xEA;
                p[2] = inBar ? 0x2B : 0xDA;
                p[3] = 0xFF;
            }
        }
    }

    // --- Blit ---
    ANativeWindow_Buffer buffer;
    if (ANativeWindow_lock(window, &buffer, nullptr) == 0) {
        const uint32_t copyWidth =
            buffer.width < static_cast<int32_t>(s.renderWidth)
                ? static_cast<uint32_t>(buffer.width)
                : s.renderWidth;
        const uint32_t copyHeight =
            buffer.height < static_cast<int32_t>(s.renderHeight)
                ? static_cast<uint32_t>(buffer.height)
                : s.renderHeight;
        auto* dst = static_cast<uint8_t*>(buffer.bits);
        for (uint32_t y = 0; y < copyHeight; ++y) {
            memcpy(dst + static_cast<size_t>(y) * buffer.stride * 4,
                   s.pixels.data() + static_cast<size_t>(y) * s.renderWidth * 4,
                   static_cast<size_t>(copyWidth) * 4);
        }
        ANativeWindow_unlockAndPost(window);
    }
}

}  // namespace mge
