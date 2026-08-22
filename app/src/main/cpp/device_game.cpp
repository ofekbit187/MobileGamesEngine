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
#include "mge/character/held_items.h"
#include "mge/character/use_archetypes.h"
#include "mge/character/wearable_catalogue.h"
#include "mge/core/log.h"
#include "mge/framework/ai.h"
#include "mge/framework/asset_registry.h"
#include "mge/framework/character_render.h"
#include "practice_yard.h"
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

// The practice yard (19.4): where the quintain stands, and its resting colour.
constexpr Vec3 kQuintainPos{5.5f, 0, -4.0f};
constexpr float kQuintainColor[3] = {0.52f, 0.40f, 0.26f};
constexpr float kStrikeStandoff = 1.5f;   // where the guard plants his feet
constexpr int kBlowsPerPass = 3;          // before he steps back and comes in again

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
    // A held item on this character: the mesh is shared and uploaded once,
    // and only a matrix moves per frame. Baking geometry every frame (what
    // buildPosedCharacter does for the offline previews) would allocate on the
    // frame path, which P1 forbids — attachPointTransform/gripTransform exist
    // separately from placeHeldItem precisely so this path can stay free of it.
    struct Held {
        const GpuLodMesh* mesh = nullptr;
        AttachPoint anchor = AttachPoint::HandR;
        HeldItemDef def{};
        float color[4] = {1, 1, 1, 1};
        bool sheathed = false;
    };
    struct Actor {
        EntityId entity = kInvalidEntity;
        SkinnedCharacter character;
        LocomotionAnimator anim;
        std::vector<Held> held;
        // The use motion currently playing, if any (14.2/14.3). This is what
        // makes the action button move the body instead of only printing a
        // line: the archetype is sampled as an OVERLAY over locomotion, so
        // the legs keep walking through the swing.
        UsePlayer use;
        UseMotion useMotion{};
        JointMask useMask{};      // cached at start; grip decides it, not us
    };
    // Shared across every character in the world (the P1 claim of the
    // template-body design): one body mesh, one mesh per garment kind+layer.
    GpuSkinnedMesh bodyMesh;
    std::vector<MeshPart> bodyParts;
    std::unordered_map<uint32_t, GpuSkinnedMesh> garmentMeshes;
    std::unordered_map<uint32_t, GpuLodMesh> heldMeshes;  // one per catalogue row
    std::vector<SkinnedDrawItem> skinnedItems;
    // The static draw list. It lived INSIDE frame() until 19.6, which meant
    // the device render path heap-allocated every single frame — a P1
    // violation on the shipped path that survived because the host runner
    // enforcing the allocation gate does not compile this file. Reserved once
    // and cleared per frame, exactly like skinnedItems above.
    std::vector<DrawItem> drawItems;
    size_t drawItemsCapacity = 0;   // what was reserved; growth past it is a P1 regression
    bool drawGrowthReported = false;
    Actor player, guard, villager;
    // A strike whose outcome is already decided but whose MOMENT has not
    // arrived: the action model resolves damage on the button press, the
    // archetype says when the blow lands. Until gameplay hangs damage on
    // `strike` itself (14.3, and it is gameplay's call, not this file's),
    // this keeps what the player READS in step with what the arm does.
    bool pendingStrike = false;
    bool pendingStrikeHit = false;

    // --- The practice yard (19.4) ---
    //
    // A scripted beat, deliberately in the GAME layer rather than the AI: a
    // scene is policy, and the AI is mechanism. It also has to be here to be
    // honest — the AI's attack calls damage() directly, so an AI-driven drill
    // would show a guard dealing damage without ever swinging.
    //
    // Walk up, draw, swing, connect, recover; step back every few blows and
    // come in again so it reads as practice rather than a loop.
    EntityId quintain = kInvalidEntity;
    PracticeYard yard;
    PracticeYardConfig yardConfig;
    float quintainFlash = 0;  // 1 at the moment of impact, decaying
    CharacterSystem* characters = nullptr;
    AiSystem* ai = nullptr;
    CollisionWorld collision;
    InteractionSystem* interactions = nullptr;
    ItemCollection chestContents;
    ItemUseRegistry itemUses;   // what using each item MEANS (Phase 12)
    uint32_t heldIndex = 0;     // which inventory entry is in the hand
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
    // The practice yard's quintain (19.4). It is a PROP to look at and a
    // CHARACTER to hit: `strikeTarget` only considers characters, so a post
    // that can be struck has to be one. It never moves, holds nothing and
    // decides nothing — being a character is what makes it hittable, which is
    // the owner's principle that every character can be interacted with,
    // arriving from the other side.
    const AssetId quintainId =
        s.assets.registerMesh("prop/quintain", meshOf(makeCylinder(0.28f, 1.9f, 12)));

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
    // A humanoid body brings its own actions (CHARACTERS.md §3.1). The
    // universal one (interact) was already there by being a character.
    grantHumanoidActions(*s.characters, s.player.entity);
    {
        CharacterComponent* pc = s.characters->get(s.player.entity);
        pc->inventory.add(
            {assetIdFromName("item/torch"), "item.torch", 1, {0.95f, 0.72f, 0.30f, 1}});
        pc->inventory.add(
            {assetIdFromName("item/sword"), "item.sword", 1, {0.72f, 0.75f, 0.79f, 1}});
        s.characters->equip(s.player.entity, 0, EquipSlot::HeldMain);  // torch first
    }

    s.guard.entity = spawnCharacter({3.0f, 0, -2.0f}, 1, 2);
    s.characters->get(s.guard.entity)->inventory.add(
        {assetIdFromName("item/sword"), "item.sword", 1, {0.72f, 0.75f, 0.79f, 1}});
    s.characters->equip(s.guard.entity, 0, EquipSlot::HeldMain);
    s.characters->setSheathed(s.guard.entity, true);
    // No AI profile for the guard: the drill (19.4) is a SCRIPT, and an idle
    // agent would zero his velocity out from under it every step. The AI's own
    // attack also bypasses the action model entirely — it calls damage()
    // directly, so it never draws, never uses the held item and never plays an
    // archetype. A scripted scene wants the real path, which is the one the
    // player's button takes.
    // The villager keeps its AI, so the yard still has life that nobody wrote.

    // The quintain stands where the guard drills, in the player's view from
    // spawn. Same faction as the guard so nothing reads it as an enemy.
    s.quintain = world.spawn();
    {
        TransformComponent t;
        t.position = kQuintainPos;
        t.prevPosition = t.position;
        world.setTransform(s.quintain, t);
        ModelComponent m;
        m.asset = quintainId;
        m.color[0] = kQuintainColor[0];
        m.color[1] = kQuintainColor[1];
        m.color[2] = kQuintainColor[2];
        world.setModel(s.quintain, m);
        CharacterComponent* c = s.characters->attach(s.quintain);
        c->faction = 1;              // the guard's, so it is nobody's enemy
        c->persistentId = 9;
        c->maxHealth = 40.0f;        // a post absorbs a great many blows
        c->health = c->maxHealth;
        solid(s.quintain, kQuintainPos + Vec3{0, 0.95f, 0}, {0.3f, 0.95f, 0.3f});
    }

    s.villager.entity = spawnCharacter({-4.0f, 0, -3.0f}, 0, 3);
    s.yardConfig.standoff = kStrikeStandoff;
    s.yardConfig.blowsPerPass = kBlowsPerPass;
    s.yard.reset(s.guard.entity, s.quintain);
    AiProfile villagerProfile;
    villagerProfile.canWander = true;
    villagerProfile.homeRadius = 4.0f;
    s.ai->attach(s.villager.entity, villagerProfile);
    // The NPCs are humanoids too — same vocabulary, different decider (P9).
    grantHumanoidActions(*s.characters, s.guard.entity);
    grantHumanoidActions(*s.characters, s.villager.entity);

    // --- Things to act on (Phase 11): an apple to take, a chest to open,
    //     and a villager to talk to. All three are ordinary world entities
    //     with an InteractableComponent — nothing here is special-cased.
    s.interactions = new InteractionSystem(world, *s.characters);
    s.interactions->setCollisionWorld(&s.collision);
    // Interaction is a character capability (owner ruling, P9): the game
    // wires the world of interactables once, onto the character system, and
    // every character has the verbs — the tap below is the player using them.
    s.characters->setInteractions(s.interactions);
    // Phase 12: bodies fall and land inside the fixed step, and using the
    // held item means whatever the ITEM says it means.
    s.characters->setCollision(&s.collision);
    s.characters->setItemUses(&s.itemUses);
    engine.setCharacters(s.characters);

    ItemUse swordUse;
    swordUse.kind = ItemUseKind::Strike;
    swordUse.range = 2.3f;
    swordUse.power = 0.25f;
    swordUse.cooldown = 0.7f;
    swordUse.animKey = "anim/swing";
    swordUse.archetype = UseArchetype::Swing;
    swordUse.reach = 1.0f;   // a longsword, and the arc radius follows from it
    swordUse.weight = 1.4f;  // drives wind-up/strike/recovery, in seconds
    s.itemUses.define("item/sword", swordUse);

    ItemUse appleUse;
    appleUse.kind = ItemUseKind::Consume;
    appleUse.power = 0.3f;           // a bite heals
    appleUse.cooldown = 0.4f;
    appleUse.effectId = assetIdFromName("effect/fed");
    appleUse.effectDuration = 60.0f;
    // Now that the archetype actually animates, these two fields stop being
    // decoration: without them an apple is eaten with a swordsman's swing,
    // because Swing is the default.
    appleUse.archetype = UseArchetype::Consume;
    appleUse.reach = 0.25f;
    appleUse.weight = 0.2f;
    s.itemUses.define("item/apple", appleUse);

    ItemUse torchUse;
    torchUse.kind = ItemUseKind::Toggle;
    torchUse.cooldown = 0.3f;
    torchUse.archetype = UseArchetype::Raise;
    torchUse.reach = 0.5f;
    torchUse.weight = 0.8f;
    s.itemUses.define("item/torch", torchUse);

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
            actor.held.clear();
            for (size_t i = 0; i < count && s.vulkanOk; ++i) {
                // Held items are rigid props on a socket, not garments: they
                // mask nothing, have no .mgefit, and ride a matrix rather than
                // the skinning palette. Until now this loop dropped them and
                // NO CHARACTER IN THE SHIPPED ENGINE HAS EVER HELD ANYTHING.
                const size_t row = wearableCatalogue().indexOf(outfit[i].kind);
                if (isHeldItem(row)) {
                    const HeldItemDef* def = heldItemDef(row);
                    const MeshData& itemMesh = sharedHeldItemMesh(row);
                    if (def == nullptr || itemMesh.vertices.empty()) continue;
                    const uint32_t key = static_cast<uint32_t>(row);
                    auto it = s.heldMeshes.find(key);
                    if (it == s.heldMeshes.end()) {
                        // A held item is one small rigid prop — a single LOD
                        // is the whole model, so wrap rather than author a
                        // chain for a sword that is 400 triangles at most.
                        LodMesh single;
                        single.lods.push_back(itemMesh);
                        single.computeBounds();
                        GpuLodMesh gpu;
                        if (!s.renderer.uploadLodMesh(single, gpu)) {
                            s.vulkanOk = false;
                            break;
                        }
                        it = s.heldMeshes.emplace(key, gpu).first;
                    }
                    Impl::Held h;
                    h.mesh = &it->second;
                    h.def = *def;
                    h.anchor = outfit[i].sheathed ? def->sheathed : def->drawn;
                    h.sheathed = outfit[i].sheathed;
                    memcpy(h.color, outfit[i].color, sizeof(h.color));
                    actor.held.push_back(h);
                    continue;
                }
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
                    if (mesh.vertices.empty()) continue;  // no garment mesh for this row
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
            // Reserved to the EXACT bound, not a generous guess: a draw item
            // is either a live renderable entity — capped by entity capacity —
            // or one held item on one of the three actors, and they are
            // already dressed by the time this runs. An exact bound is what
            // makes "never grows" a property rather than a hope.
            s.drawItems.reserve(engine.world().entities().capacity() +
                                s.player.held.size() + s.guard.held.size() +
                                s.villager.held.size());
            s.drawItemsCapacity = s.drawItems.capacity();
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
            s.strings.set(Language::English, "hud.jump", "Leap");
            s.strings.set(Language::Hebrew, "hud.jump", "\xd7\x9c\xd7\xa7\xd7\xa4\xd7\x95\xd7\xa5");
            s.strings.set(Language::English, "hud.use", "Use");
            s.strings.set(Language::Hebrew, "hud.use", "\xd7\x9c\xd7\x94\xd7\xa9\xd7\xaa\xd7\x9e\xd7\xa9");
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

    // Whatever an actor just used, it swings it. The motion comes from the
    // ITEM's own four numbers, so this is the same call for the player's
    // button and for the guard's drill — the NPC and the player take the
    // identical path, which is the only reason a scripted scene proves
    // anything about the real one.
    const auto beginUseMotion = [](Impl::Actor& actor, const ItemUse* use) {
        if (use == nullptr) return;
        actor.useMotion = motionFromItemUse(*use, /*leftHanded=*/false);
        actor.useMask = useArchetypeMask(buildSkeleton(actor.character.variant), actor.useMotion);
        actor.use.start(actor.useMotion);
    };

    // --- Simulation: AI intents, then the engine's fixed-step tick ---
    World& world = s.engine->world();
    s.ai->step(static_cast<float>(dt));
    // The world is solid (Phase 11) and bodies have weight (Phase 12): the
    // engine resolves every character's locomotion INSIDE its fixed step —
    // player and NPCs alike, through the same call — so falling and landing
    // don't change with frame rate.
    s.engine->tick(dt);
    for (Impl::Actor* actor : {&s.player, &s.guard, &s.villager}) {
        const MovementComponent* m = world.movement(actor->entity);
        actor->anim.update(static_cast<float>(dt),
                           m != nullptr ? m->velocity.length() : 0.0f);
        // Advance any use motion. `update` returns true on the ONE frame the
        // strike moment is crossed — the edge 14.3 exists to give gameplay —
        // and it fires exactly once even if a long frame steps over it.
        const bool struck = actor->use.update(static_cast<float>(dt));
        if (struck && actor == &s.player && s.pendingStrike) {
            if (s.pendingStrikeHit) {
                s.strings.set(Language::English, "hud.flash", "Your blade lands");
                s.strings.set(Language::Hebrew, "hud.flash",
                              "\xd7\x94\xd7\x9c\xd7\x94\xd7\x91 \xd7\xa4\xd7\x95\xd7\x92\xd7\xa2");
            } else {
                s.strings.set(Language::English, "hud.flash", "You swing at nothing");
            }
            s.promptFlash = 1.6;
            s.pendingStrike = false;
        }
    }

    // --- The practice yard (19.4): the guard at his drill ---
    //
    // The beats live in practice_yard.h so a host test can drive them against
    // a real World and CharacterSystem. This file cannot be compiled by any
    // host tool, and a scene verified only by "it compiled" is how you find
    // out on the owner's phone that the guard is swinging at thin air.
    if (s.quintain != kInvalidEntity) {
        const PracticeYard::Tick tick =
            s.yard.update(world, *s.characters, static_cast<float>(dt), s.yardConfig);
        if (tick.swung) beginUseMotion(s.guard, s.itemUses.find(tick.item));
        if (tick.connected) s.quintainFlash = 1.0f;

        if (s.quintainFlash > 0) {
            s.quintainFlash -= static_cast<float>(dt) * 2.2f;
            if (s.quintainFlash < 0) s.quintainFlash = 0;
        }
        if (ModelComponent* postModel = world.model(s.quintain)) {
            // Impact reads as a flash of pale wood — the only feedback this
            // scene needs, and driven by the real hit result rather than by
            // the animation's timing.
            for (int c = 0; c < 3; ++c) {
                postModel->color[c] =
                    kQuintainColor[c] + (1.0f - kQuintainColor[c]) * s.quintainFlash;
            }
        }
    }

    // --- The player's actions (Phase 12). Two buttons, and everything they
    //     do is a CHARACTER action — the same calls an NPC would make. ---
    const GameplayIntents& intents = s.engine->lastIntents();
    if (intents.jump) s.characters->perform(s.player.entity, actionJump());
    if (intents.useHeld) {
        const ActionResult used = s.characters->perform(s.player.entity, actionUseHeld());
        // The body moves for ANY successful use, whatever the item meant: the
        // archetype comes from the item's own four numbers, so a torch raises
        // and an apple goes to the mouth without a line of code per item
        // (14.2). This is the whole of "a new weapon is not new animation
        // work" arriving on the phone.
        if (used.performed) {
            // A new use supersedes any strike still waiting to be announced,
            // or eating an apple would print the blade message the swing it
            // interrupted never got to.
            s.pendingStrike = false;
            beginUseMotion(s.player, s.itemUses.find(used.item.asset));
        }
        switch (used.useKind) {
            case ItemUseKind::Strike:
                // Held back until the strike instant rather than printed on
                // the button press. The blow is now something you WATCH land,
                // and the words have to agree with the arm or the swing reads
                // as decoration played after the fact.
                if (used.target != kInvalidEntity) {
                    s.pendingStrikeHit = true;
                    s.pendingStrike = true;
                } else {
                    s.pendingStrikeHit = false;
                    s.pendingStrike = true;
                }
                break;
            case ItemUseKind::Consume:
                s.strings.set(Language::English, "hud.flash", "You eat, and feel better");
                s.promptFlash = 1.8;
                break;
            case ItemUseKind::Toggle:
                s.strings.set(Language::English, "hud.flash",
                              used.toggledOn ? "The torch catches" : "The torch is out");
                s.promptFlash = 1.6;
                break;
            default:
                if (used.refusal == ActionRefusal::NothingHeld) {
                    s.strings.set(Language::English, "hud.flash", "Your hands are empty");
                    s.promptFlash = 1.4;
                }
                break;
        }
    }

    // --- What the player is about to act on, and what a tap does to it ---
    s.focused = s.characters->focus(s.player.entity);
    s.promptFlash -= dt;
    if (intents.action) {
        if (s.openCollectionId != 0) {
            s.openCollectionId = 0;  // a tap closes the open container
        } else if (s.focused == kInvalidEntity) {
            // Facing nothing usable: the tap swaps what is in your hand, so
            // every item's meaning of "use" is reachable on the phone.
            CharacterComponent* pc = s.characters->get(s.player.entity);
            if (pc != nullptr && pc->inventory.size() > 0) {
                s.characters->unequip(s.player.entity, EquipSlot::HeldMain);
                s.heldIndex = (s.heldIndex + 1) % pc->inventory.size();
                if (s.characters->equip(s.player.entity, s.heldIndex, EquipSlot::HeldMain)) {
                    const EquippedItem& slot =
                        pc->equipment[static_cast<size_t>(EquipSlot::HeldMain)];
                    s.strings.set(Language::English, "hud.flash",
                                  slot.item.nameKey != nullptr ? "Now in hand" : "Now in hand");
                    s.promptFlash = 1.4;
                }
            }
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

        std::vector<DrawItem>& items = s.drawItems;
        items.clear();  // capacity survives; nothing is freed and nothing regrows
        // Shared with the gate (19.7): the transform interpolation, culling
        // bounds and push all live in emitWorldRenderables, and the resolver
        // below holds every GPU-typed decision — which is the only part that
        // cannot follow it into mge_core.
        emitWorldRenderables<DrawItem>(
            world, alpha, items,
            [&](AssetId asset, DrawItem& item, ResolvedRenderable& mesh) {
                auto it = s.resident.find(asset);
                if (it == s.resident.end()) return false;  // still streaming
                item.mesh = &it->second;
                mesh.boundsCenter = it->second.bounds.center();
                mesh.boundsRadius = it->second.bounds.extents().length();
                const AssetRecord* record = s.assets.find(asset);
                item.material = (record != nullptr && record->kind == AssetKind::VirtualModel)
                                    ? MaterialKind::Placeholder
                                    : MaterialKind::Lit;
                if (item.material == MaterialKind::Placeholder) item.params[0] = 6.0f;
                return true;
            });
        // Characters: the shared skinned body + garments, deformed on the
        // GPU by each character's palette (task 8.10). Per character per
        // frame the CPU produces 17 matrices — no geometry work at all.
        // The per-character work lives in mge/framework/character_render.h so
        // that host_runner can run it inside the P1 allocation counter (19.7).
        // Before that move this loop was the shipped frame path no gate ever
        // compiled, which is exactly how 19.6's per-frame allocation survived.
        s.skinnedItems.clear();
        for (Impl::Actor* actor : {&s.player, &s.guard, &s.villager}) {
            const TransformComponent* t = world.transform(actor->entity);
            if (t == nullptr || !s.bodyMesh.valid()) continue;
            SkinnedCharacter& character = actor->character;

            CharacterFrame frame;
            composeCharacterFrame(*t, alpha, character.variant, actor->anim, actor->use,
                                  actor->useMotion, actor->useMask,
                                  /*needJointWorld=*/!actor->held.empty(), character.palette,
                                  frame);

            SkinnedDrawItem proto;
            proto.mesh = &s.bodyMesh;
            proto.palette = character.palette;
            proto.model = frame.model;
            proto.worldBounds = frame.bounds;
            proto.lodReference = frame.lodReference;
            memcpy(proto.baseColor, character.variant.skin, sizeof(proto.baseColor));

            emitBodyParts(proto, s.bodyParts.data(), s.bodyParts.size(),
                          character.visibleRegions, s.skinnedItems);
            emitGarments(proto, character.garments.data(), character.garments.size(),
                         s.skinnedItems);
            emitHeldItems<DrawItem>(frame, character.variant, actor->held.data(),
                                    actor->held.size(), items);
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
            // The action buttons (Phase 12), drawn from the control scheme's
            // own geometry so the seal you press and the circle that answers
            // are the same circle. Touch is in device pixels; the HUD is in
            // render pixels, hence the scale.
            {
                const TouchControlScheme& scheme = s.engine->controls();
                const TouchButton jumpZone = scheme.jumpButton();
                const TouchButton useZone = scheme.useButton();
                s.ui.actionSeal(jumpZone.x * s.touchScaleX, jumpZone.y * s.touchScaleY,
                                jumpZone.radius * s.touchScaleY, scheme.jumpPressed(),
                                "hud.jump");
                s.ui.actionSeal(useZone.x * s.touchScaleX, useZone.y * s.touchScaleY,
                                useZone.radius * s.touchScaleY, scheme.usePressed(),
                                "hud.use");
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
        // The device build's only allocation check. The host runner enforces
        // the P1 gate for the engine core but never compiles this file, which
        // is exactly how a per-frame allocation lived here unnoticed. A vector
        // that outgrew its reservation has allocated on the frame path, so say
        // so loudly and once, rather than letting the next one hide as well.
        if (!s.drawGrowthReported && items.capacity() > s.drawItemsCapacity) {
            MGE_LOGE(kTag, "P1: draw list grew past its reservation (%zu > %zu) — the frame path allocated",
                     items.capacity(), s.drawItemsCapacity);
            s.drawGrowthReported = true;
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
