#include "device_game.h"

#define VK_USE_PLATFORM_ANDROID_KHR
#include <vulkan/vulkan.h>

#include <cmath>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#include "mge/character/body_mesh.h"
#include "mge/character/humanoid.h"
#include "mge/core/log.h"
#include "mge/framework/ai.h"
#include "mge/framework/asset_registry.h"
#include "mge/framework/camera_controller.h"
#include "mge/framework/character.h"
#include "mge/framework/collision.h"
#include "mge/framework/interaction.h"
#include "mge/graphics/primitives.h"
#include "mge/graphics/renderer.h"
#include "mge/graphics/swapchain.h"
#include "mge/graphics/vulkan_device.h"
#include "mge/ui/font.h"
#include "mge/ui/localization.h"
#include "mge/ui/ui.h"

namespace mge {

namespace {

constexpr const char* kTag = "device";

// ---- characters: the v2 skinned template body on the GPU (task 8.10) ------
// ONE template mesh + one garment mesh per (kind, layer) serve every
// character; a character is 17 matrices. Masking a covered body region is a
// draw-range decision on the shared mesh, so a dressed body draws FEWER
// triangles than a bare one and can never clip through its clothes.

struct SkinnedCharacter {
    HumanoidVariant variant;
    uint32_t visibleRegions = kAllRegions;
    struct Garment {
        const GpuSkinnedMesh* mesh = nullptr;
        float color[4] = {1, 1, 1, 1};
    };
    std::vector<Garment> garments;
    Mat4 palette[kJointCount];
};

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
    Swapchain swapchain;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    bool vulkanOk = false;
    bool presenting = false;   // swapchain path (never mix with window locks)
    uint32_t renderWidth = 1280, renderHeight = 720;
    std::vector<uint8_t> pixels;  // readback fallback only
    double heartbeat = 0;

    // Scene
    AssetRegistry assets;
    std::unordered_map<AssetId, GpuLodMesh> resident;
    struct Actor {
        EntityId entity = kInvalidEntity;
        SkinnedCharacter character;
        LocomotionAnimator anim;
    };
    // Shared across every character in the world (the P1 claim of the
    // template-body design): one body mesh, one mesh per garment kind+layer.
    GpuSkinnedMesh bodyMesh;
    std::vector<MeshPart> bodyParts;
    std::unordered_map<uint32_t, GpuSkinnedMesh> garmentMeshes;
    std::vector<SkinnedDrawItem> skinnedItems;
    Actor player, guard, villager;
    CharacterSystem* characters = nullptr;
    AiSystem* ai = nullptr;
    CollisionWorld collision;
    InteractionSystem* interactions = nullptr;
    CharacterShape playerShape;
    ItemCollection chestContents;
    CollectionRegistry collections;
    EntityId focused = kInvalidEntity;
    uint64_t openCollectionId = 0;
    double promptFlash = 0;   // "picked up X" / "the chest holds..." feedback
    std::string flashText;
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
        delete interactions;
        delete ai;
        delete characters;
    }
};

DeviceGame::~DeviceGame() { stop(); }

bool DeviceGame::start(Engine& engine, AudioMixer& mixer, ANativeWindow* window,
                       uint32_t surfaceWidth, uint32_t surfaceHeight) {
    if (impl_ != nullptr) return true;
    impl_ = new Impl();
    Impl& s = *impl_;
    s.engine = &engine;
    s.mixer = &mixer;

    // Render size: 720p at the surface's aspect; the presentation blit scales
    // it to the panel in the driver, so render cost stays independent of a
    // 1440p display.
    if (surfaceWidth > 0 && surfaceHeight > 0) {
        s.renderHeight = 720;
        s.renderWidth =
            ((surfaceWidth * s.renderHeight / surfaceHeight) + 3u) & ~3u;
        if (s.renderWidth > 1600) s.renderWidth = 1600;
        if (s.renderWidth < 320) s.renderWidth = 320;
    }

    // --- Graphics: the engine's pass on the device GPU, presented through a
    //     real swapchain (task 2.1). The surface extensions are named here,
    //     in platform code — the engine core never mentions Android.
    const char* const kSurfaceExtensions[] = {VK_KHR_SURFACE_EXTENSION_NAME,
                                              VK_KHR_ANDROID_SURFACE_EXTENSION_NAME};
    VulkanDeviceConfig deviceConfig;
    deviceConfig.instanceExtensions = kSurfaceExtensions;
    deviceConfig.instanceExtensionCount = 2;
    deviceConfig.enableSwapchain = true;
    s.vulkanOk = s.device.init(deviceConfig);
    if (!s.vulkanOk) {
        // Presentation-capable init failed: try plain headless-style init so
        // the readback path can still show the world.
        MGE_LOGW(kTag, "surface-capable Vulkan init failed — trying windowless");
        s.vulkanOk = s.device.init(VulkanDeviceConfig{});
    }
    if (s.vulkanOk) {
        RendererConfig config;
        config.width = s.renderWidth;
        config.height = s.renderHeight;
        config.lightDir[0] = -0.25f;
        config.lightDir[1] = 0.80f;
        config.lightDir[2] = 0.55f;
        s.vulkanOk = s.renderer.init(config);
    }
    if (s.vulkanOk && s.device.swapchainEnabled() && window != nullptr) {
        VkAndroidSurfaceCreateInfoKHR surfaceInfo{};
        surfaceInfo.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
        surfaceInfo.window = window;
        if (vkCreateAndroidSurfaceKHR(s.device.instance(), &surfaceInfo, nullptr, &s.surface) ==
                VK_SUCCESS &&
            s.swapchain.init(s.device, s.surface)) {
            s.presenting = true;
        } else {
            MGE_LOGW(kTag, "no swapchain — falling back to readback + window blit");
            if (s.surface != VK_NULL_HANDLE) {
                vkDestroySurfaceKHR(s.device.instance(), s.surface, nullptr);
                s.surface = VK_NULL_HANDLE;
            }
        }
    }
    if (s.vulkanOk) {
        MGE_LOGI(kTag, "vulkan up: %s, render %ux%u, present=%d", s.device.deviceName(),
                 s.renderWidth, s.renderHeight, s.presenting ? 1 : 0);
    } else {
        MGE_LOGE(kTag, "vulkan unavailable — parchment fallback active");
    }
    if (!s.presenting) {
        s.pixels.resize(static_cast<size_t>(s.renderWidth) * s.renderHeight * 4);
    }

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
    VirtualModelDesc crateDesc;
    crateDesc.proportions = {1.2f, 0.4f, 1.2f};
    crateDesc.description = "low wooden crate, planks and iron banding";
    const AssetId crateId = s.assets.registerVirtualModel("prop/crate", crateDesc);
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
    // Props are placed AND made solid: the same call registers a collider,
    // so the hamlet you see is the hamlet you bump into (task 11.4 — a
    // streaming world does this per chunk instead).
    const auto solid = [&](EntityId entity, Vec3 center, Vec3 halfExtents) {
        s.collision.addBox(Aabb::fromCenterExtents(center, halfExtents), entity);
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
    solid(place(stallId, {-2.5f, 1.05f, -9.0f}, 0.4f, 0.85f, 0.55f, 0.18f),
          {-2.5f, 1.05f, -9.0f}, {1.2f, 1.05f, 0.9f});
    solid(place(wellId, {3.0f, 0.7f, -4.5f}, 0, 0.85f, 0.55f, 0.18f),
          {3.0f, 0.7f, -4.5f}, {0.9f, 0.7f, 0.9f});
    // A low crate you can step onto, proving step-up on real hardware.
    solid(place(crateId, {-1.6f, 0.2f, -3.2f}, 0.3f, 0.55f, 0.42f, 0.26f),
          {-1.6f, 0.2f, -3.2f}, {0.6f, 0.2f, 0.6f});

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

    // --- Things to act on (Phase 11): an apple to take, a chest to open,
    //     and a villager to talk to. All three are ordinary world entities
    //     with an InteractableComponent — nothing here is special-cased.
    s.interactions = new InteractionSystem(world, *s.characters);
    s.interactions->setCollisionWorld(&s.collision);
    // Interaction is a character capability (owner ruling, P9): the game
    // wires the world of interactables once, onto the character system, and
    // every character has the verbs — the tap below is the player using them.
    s.characters->setInteractions(s.interactions);

    const auto apple = [&](Vec3 position) {
        const EntityId entity = place(crateId, position, 0.0f, 0.85f, 0.25f, 0.20f);
        InteractableComponent pick;
        pick.kind = InteractionKind::PickUp;
        pick.promptKey = "prompt.take";
        pick.range = 2.0f;
        pick.item = {assetIdFromName("item/apple"), "item.apple", 1, {0.80f, 0.22f, 0.16f, 1}};
        s.interactions->attach(entity, pick);
        return entity;
    };
    apple({1.2f, 0.2f, 1.0f});
    apple({-2.6f, 0.2f, -1.4f});

    // The chest binds a registered collection — the UI shows whatever the
    // game put in it (the Phase 5 data binding, now reachable in play).
    s.chestContents.add({assetIdFromName("item/rope"), "item.rope", 2, {0.72f, 0.62f, 0.42f, 1}});
    s.chestContents.add({assetIdFromName("item/coin"), "item.coin", 17, {0.85f, 0.72f, 0.28f, 1}});
    s.chestContents.add({assetIdFromName("item/bread"), "item.bread", 3, {0.76f, 0.58f, 0.32f, 1}});
    s.collections.add("chest.hamlet", &s.chestContents);
    {
        const EntityId chest = place(crateId, {2.2f, 0.2f, -2.6f}, 0.2f, 0.45f, 0.34f, 0.20f);
        s.collision.addBox(Aabb::fromCenterExtents({2.2f, 0.2f, -2.6f}, {0.6f, 0.2f, 0.6f}),
                           chest);
        InteractableComponent container;
        container.kind = InteractionKind::Container;
        container.promptKey = "prompt.open";
        container.range = 2.2f;
        container.collectionId = assetIdFromName("chest.hamlet");
        s.interactions->attach(chest, container);
    }
    {
        InteractableComponent talk;
        talk.kind = InteractionKind::Talk;
        talk.promptKey = "prompt.talk";
        talk.range = 2.6f;
        s.interactions->attach(s.villager.entity, talk);
    }

    if (s.vulkanOk) {
        for (AssetId id : {groundId, houseId, towerId, stallId, wellId, crateId}) {
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
        // The shared template body (LOD0) — uploaded ONCE for everyone.
        const std::vector<SkinnedMeshData>& lods = sharedTemplateLods();
        if (lods.empty() || !s.renderer.uploadSkinnedMesh(lods[0], s.bodyMesh)) {
            MGE_LOGE(kTag, "template body upload failed");
            s.vulkanOk = false;
        } else {
            s.bodyParts = lods[0].parts;
            MGE_LOGI(kTag, "template body: %zu vertices, %zu triangles, %zu regions",
                     lods[0].vertices.size(), lods[0].triangleCount(), s.bodyParts.size());
        }

        // One garment mesh per (kind, layer), shared by every wearer.
        const auto garmentKey = [](WearableKind kind, uint8_t layer) {
            return (static_cast<uint32_t>(kind) << 8) | layer;
        };
        const auto dress = [&](Impl::Actor& actor, const HumanoidVariant& variant,
                               const WearableInstance* outfit, size_t count) {
            actor.character.variant = variant;
            actor.character.visibleRegions = kAllRegions;
            actor.character.garments.clear();
            for (size_t i = 0; i < count && s.vulkanOk; ++i) {
                actor.character.visibleRegions &= ~garmentCoverage(outfit[i].kind);
                const uint32_t key = garmentKey(outfit[i].kind, outfit[i].layer);
                auto it = s.garmentMeshes.find(key);
                if (it == s.garmentMeshes.end()) {
                    GarmentBuildDesc desc;
                    desc.kind = outfit[i].kind;
                    desc.layer = outfit[i].layer;
                    desc.lod = BodyLod::Lod0;
                    SkinnedMeshData mesh;
                    buildGarmentMesh(desc, mesh);
                    if (mesh.vertices.empty()) continue;  // held items aren't garments
                    GpuSkinnedMesh gpu;
                    if (!s.renderer.uploadSkinnedMesh(mesh, gpu)) {
                        s.vulkanOk = false;
                        break;
                    }
                    it = s.garmentMeshes.emplace(key, gpu).first;
                }
                SkinnedCharacter::Garment garment;
                garment.mesh = &it->second;
                memcpy(garment.color, outfit[i].color, sizeof(garment.color));
                actor.character.garments.push_back(garment);
            }
        };
        if (s.vulkanOk) {
            dress(s.player, playerVariant, playerOutfit, 4);
            dress(s.guard, guardVariant, guardOutfit, 5);
            dress(s.villager, villagerVariant, villagerOutfit, 4);
            s.skinnedItems.reserve(Renderer::kMaxSkinnedDraws);
        }
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
            s.strings.set(Language::English, "prompt.take", "Take");
            s.strings.set(Language::Hebrew, "prompt.take", "\xd7\x9c\xd7\xa7\xd7\x97\xd7\xaa");
            s.strings.set(Language::English, "prompt.open", "Open");
            s.strings.set(Language::Hebrew, "prompt.open", "\xd7\x9c\xd7\xa4\xd7\xaa\xd7\x95\xd7\x97");
            s.strings.set(Language::English, "prompt.talk", "Speak");
            s.strings.set(Language::Hebrew, "prompt.talk", "\xd7\x9c\xd7\x93\xd7\x91\xd7\xa8");
            s.strings.set(Language::English, "hud.flash", "");
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
        s.renderer.destroySkinnedMesh(s.bodyMesh);
        for (auto& [key, mesh] : s.garmentMeshes) s.renderer.destroySkinnedMesh(mesh);
        s.garmentMeshes.clear();
        for (auto& [id, mesh] : s.resident) s.renderer.destroyLodMesh(mesh);
        s.resident.clear();
        s.renderer.shutdown();
    }
    s.swapchain.shutdown();
    if (s.surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(s.device.instance(), s.surface, nullptr);
        s.surface = VK_NULL_HANDLE;
    }
    s.device.shutdown();
    delete impl_;
    impl_ = nullptr;
}

void DeviceGame::onSurfaceResized(uint32_t width, uint32_t height) {
    if (impl_ == nullptr) return;
    Impl& s = *impl_;
    if (width > 0 && height > 0) {
        s.touchScaleX = static_cast<float>(s.renderWidth) / width;
        s.touchScaleY = static_cast<float>(s.renderHeight) / height;
    }
    if (s.presenting) s.swapchain.recreate();
}

void DeviceGame::frame(double dtSeconds, ANativeWindow* window) {
    if (impl_ == nullptr) return;
    Impl& s = *impl_;
    double dt = dtSeconds;
    if (dt <= 0.0 || dt > 0.05) dt = 1.0 / 60.0;

    // --- Simulation: AI intents, then the engine's fixed-step tick ---
    World& world = s.engine->world();
    Vec3 before[3] = {};
    Impl::Actor* actors[3] = {&s.player, &s.guard, &s.villager};
    for (int i = 0; i < 3; ++i) {
        if (const TransformComponent* t = world.transform(actors[i]->entity)) {
            before[i] = t->position;
        }
    }
    s.ai->step(static_cast<float>(dt));
    s.engine->tick(dt);

    // The world is solid (Phase 11): whatever the controls and the AI asked
    // for, the collision world decides where bodies actually end up — for
    // the player and the NPCs alike, through the same call.
    for (int i = 0; i < 3; ++i) {
        TransformComponent* t = world.transform(actors[i]->entity);
        if (t == nullptr) continue;
        const MoveResult resolved =
            s.collision.moveCharacter(before[i], s.playerShape, t->position - before[i]);
        t->position = resolved.position;
    }
    for (Impl::Actor* actor : {&s.player, &s.guard, &s.villager}) {
        const MovementComponent* m = world.movement(actor->entity);
        actor->anim.update(static_cast<float>(dt),
                           m != nullptr ? m->velocity.length() : 0.0f);
    }

    // --- What the player is about to act on, and what a tap does to it ---
    s.focused = s.characters->focus(s.player.entity);
    s.promptFlash -= dt;
    if (s.engine->lastIntents().action) {
        if (s.openCollectionId != 0) {
            s.openCollectionId = 0;  // a tap closes the open container
        } else {
            InteractionResult acted;
            s.characters->interact(s.player.entity, &acted);
            switch (acted.kind) {
                case InteractionKind::PickUp:
                    if (acted.handled) {
                        s.strings.set(Language::English, "hud.flash", "Took an apple");
                        s.strings.set(Language::Hebrew, "hud.flash",
                                      "\xd7\x9c\xd7\xa7\xd7\x97\xd7\xaa \xd7\xaa\xd7\xa4\xd7\x95\xd7\x97");
                    } else {
                        s.strings.set(Language::English, "hud.flash", "Your pack is full");
                    }
                    s.promptFlash = 2.0;
                    break;
                case InteractionKind::Container:
                    s.openCollectionId = acted.collectionId;
                    break;
                case InteractionKind::Talk: {
                    const TransformComponent* speaker = world.transform(acted.target);
                    if (s.speech.loaded() && speaker != nullptr) {
                        AudioPlayParams line;
                        line.bus = AudioBus::Voice;
                        line.positional = true;
                        line.position = speaker->position + Vec3{0, 1.5f, 0};
                        line.refDistance = 2.0f;
                        line.maxDistance = 20.0f;
                        s.mixer->play(s.speech, line);
                        s.subtitleTimer = 3.2;
                        s.speechCooldown = 14.0;  // don't repeat it unprompted
                    }
                    break;
                }
                default:
                    break;
            }
        }
    }
    // Walking away closes the container panel — no modal traps.
    if (s.openCollectionId != 0 && s.focused == kInvalidEntity) s.openCollectionId = 0;

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

    if (window == nullptr && !s.presenting) return;
    if (!s.presenting) {
        // Fallback path only: the swapchain owns the window when presenting,
        // and locking it behind Vulkan's back is undefined.
        ANativeWindow_setBuffersGeometry(window, static_cast<int32_t>(s.renderWidth),
                                         static_cast<int32_t>(s.renderHeight),
                                         WINDOW_FORMAT_RGBA_8888);
    }

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
        // Characters: the shared skinned body + garments, deformed on the
        // GPU by each character's palette (task 8.10). Per character per
        // frame the CPU produces 17 matrices — no geometry work at all.
        s.skinnedItems.clear();
        for (Impl::Actor* actor : {&s.player, &s.guard, &s.villager}) {
            const TransformComponent* t = world.transform(actor->entity);
            if (t == nullptr || !s.bodyMesh.valid()) continue;
            const Vec3 pos = lerp(t->prevPosition, t->position, alpha);
            const float yaw = t->prevYaw + (t->yaw - t->prevYaw) * alpha;
            Pose pose;
            actor->anim.samplePose(pose);
            SkinnedCharacter& character = actor->character;
            buildSkinPalette(character.variant, pose, character.palette);

            const Mat4 model = Mat4::translation(pos) *
                               Mat4::rotation(Quat::fromAxisAngle({0, 1, 0}, -yaw));
            const Aabb bounds = Aabb::fromCenterExtents(pos + Vec3{0, 1.0f, 0}, {1.4f, 1.4f, 1.4f});

            SkinnedDrawItem item;
            item.mesh = &s.bodyMesh;
            item.palette = character.palette;
            item.model = model;
            item.worldBounds = bounds;
            item.lodReference = pos;
            memcpy(item.baseColor, character.variant.skin, sizeof(item.baseColor));
            // Uncovered body regions only — masking is a draw range here.
            for (const MeshPart& part : s.bodyParts) {
                if ((character.visibleRegions & regionBit(part.region)) == 0) continue;
                item.firstIndex = part.firstIndex;
                item.indexCount = part.indexCount;
                s.skinnedItems.push_back(item);
            }
            for (const SkinnedCharacter::Garment& garment : character.garments) {
                SkinnedDrawItem worn = item;
                worn.mesh = garment.mesh;
                worn.firstIndex = 0;
                worn.indexCount = 0;  // whole garment
                memcpy(worn.baseColor, garment.color, sizeof(worn.baseColor));
                s.skinnedItems.push_back(worn);
            }
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
            // What you are about to act on (Phase 11): the prompt is the
            // interactable's own localized key, so a Hebrew game reads
            // right-to-left with no extra work (P11).
            if (s.focused != kInvalidEntity && s.openCollectionId == 0) {
                const InteractableComponent* target = s.interactions->get(s.focused);
                if (target != nullptr && target->promptKey[0] != '\0') {
                    s.ui.panel({w * 0.5f - 90, h * 0.62f, 180, 44});
                    s.ui.label({w * 0.5f - 80, h * 0.62f + 8, 160, 28}, target->promptKey, 0.72f,
                               TextAlign::Center);
                }
            }
            if (s.promptFlash > 0) {
                s.ui.label({w * 0.5f - w * 0.3f, h * 0.54f, w * 0.6f, 28}, "hud.flash", 0.66f,
                           TextAlign::Center);
            }
            // An opened container shows its BOUND collection — the Phase 5
            // data binding, now reachable in play.
            if (s.openCollectionId != 0) {
                if (ItemCollection* contents = s.collections.find(s.openCollectionId)) {
                    static int selected = -1;
                    s.ui.collectionView(900, {w * 0.5f - 190, h * 0.16f, 380, h * 0.6f},
                                        "inv.chest", *contents, 4, selected);
                }
            }
            if (s.subtitleTimer > 0) {
                s.ui.label({w * 0.5f - w * 0.35f, h - 54, w * 0.7f, 30}, "hud.subtitle",
                           0.7f, TextAlign::Center);
            }
            uiList = &s.ui.drawList();
        }
        haveFrame = s.renderer.renderFrame(s.camera, items.data(), items.size(), nullptr,
                                           uiList, nullptr, 0, s.skinnedItems.data(),
                                           s.skinnedItems.size());
        if (haveFrame && s.presenting) {
            // The frame never leaves the GPU: blit + present (task 2.1).
            if (!s.swapchain.present(s.renderer)) {
                // Stale swapchain (rotation, resize, surface change) is a
                // normal answer — rebuild and show the next frame.
                if (!s.swapchain.recreate()) {
                    MGE_LOGW(kTag, "swapchain lost — readback fallback");
                    s.presenting = false;
                    s.pixels.resize(static_cast<size_t>(s.renderWidth) * s.renderHeight * 4);
                }
            }
            return;  // presented (or recovering); nothing to blit by hand
        }
        if (haveFrame) haveFrame = s.renderer.readback(s.pixels.data(), s.pixels.size());
        if (!haveFrame) {
            MGE_LOGE(kTag, "render/readback failed — falling back");
            s.vulkanOk = false;
        }
    }
    if (!haveFrame) {
        if (s.presenting || window == nullptr) return;  // never lock behind Vulkan
        // Parchment heartbeat: visible proof of life when Vulkan is absent.
        s.pixels.resize(static_cast<size_t>(s.renderWidth) * s.renderHeight * 4);
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
