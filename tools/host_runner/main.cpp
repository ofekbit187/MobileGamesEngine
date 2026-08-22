// Headless host runner: boots the engine without Android and demonstrates the
// Phase 1 exit criteria that don't need a device — a stable fixed-step loop
// through lifecycle events, a live memory-budget dashboard, and zero
// steady-state heap allocations in the frame loop (P1).

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

#include <cmath>
#include <vector>

#include "mge/audio/mixer.h"
#include "mge/character/humanoid.h"
#include "mge/framework/action.h"
#include "mge/framework/character.h"
#include "mge/framework/character_render.h"
#include "mge/framework/collision.h"
#include "mge/core/log.h"
#include "mge/framework/engine.h"

// The device build's draw items, minus Vulkan.
//
// `DrawItem`/`SkinnedDrawItem` live in renderer.h behind <vulkan/vulkan.h>,
// and this runner is deliberately graphics-free: it is built for arm64 under
// QEMU, where mge_graphics does not exist. So the gate instantiates the
// device's emission templates on these stand-ins, which carry the same fields
// with an opaque mesh handle. The CODE under the counter is the device's; only
// the type of the pointer it copies differs.
struct HostDrawItem {
    const void* mesh = nullptr;
    mge::Mat4 model;
    mge::Aabb worldBounds{};
    mge::Vec3 lodReference{};
    float baseColor[4] = {1, 1, 1, 1};
};

struct HostSkinnedItem {
    const void* mesh = nullptr;
    const mge::Mat4* palette = nullptr;
    mge::Mat4 model;
    mge::Aabb worldBounds{};
    mge::Vec3 lodReference{};
    float baseColor[4] = {1, 1, 1, 1};
    uint32_t firstIndex = 0;
    uint32_t indexCount = 0;
};

struct HostGarment {
    const void* mesh = nullptr;
    float color[4] = {1, 1, 1, 1};
};

struct HostHeld {
    const void* mesh = nullptr;
    mge::AttachPoint anchor = mge::AttachPoint::HandR;
    mge::HeldItemDef def{};
    float color[4] = {1, 1, 1, 1};
};

// Global allocation counter: proves the frame loop is allocation-free in
// steady state. Engine-internal arenas/pools charge budgets instead of
// hitting the heap per frame.
static std::atomic<long> gAllocCount{0};

void* operator new(std::size_t size) {
    gAllocCount.fetch_add(1, std::memory_order_relaxed);
    if (void* p = std::malloc(size)) return p;
    throw std::bad_alloc();
}

void* operator new[](std::size_t size) {
    gAllocCount.fetch_add(1, std::memory_order_relaxed);
    if (void* p = std::malloc(size)) return p;
    throw std::bad_alloc();
}

void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

int main() {
    mge::Engine engine;
    mge::EngineConfig config;
    if (!engine.init(config)) {
        MGE_LOGE("host", "engine init failed");
        return 1;
    }

    engine.onSurfaceCreated(1080, 2400);
    engine.onResume();

    // The audio mixer joins the P1 gate (task 10.5): a looping positional
    // voice mixes every frame inside the allocation counter's window.
    mge::BudgetRegistry audioBudgets;
    mge::AudioMixer mixer(audioBudgets);
    mge::AudioClip hum;
    {
        mge::WavData data;
        data.sampleRate = 22050;
        data.channels = 1;
        data.samples.resize(2205);
        for (size_t i = 0; i < data.samples.size(); ++i) {
            data.samples[i] = static_cast<int16_t>(
                6000.0f * std::sin(2.0f * mge::kPi * 110.0f * i / 22050.0f));
        }
        if (!hum.adopt(std::move(data), mixer)) return 1;
    }
    mge::AudioPlayParams humParams;
    humParams.loop = true;
    humParams.positional = true;
    humParams.position = {3, 0, -2};
    if (mixer.play(hum, humParams) == mge::kInvalidAudioVoice) return 1;
    static int16_t mixBuffer[800 * 2];  // one 60 Hz frame of 48 kHz stereo

    // A body that walks, falls, lands and acts — inside the gate.
    mge::CollisionWorld collision(64);
    collision.addBox(mge::Aabb::fromCenterExtents({0, 0.2f, -3.0f}, {1.0f, 0.2f, 1.0f}));
    mge::CharacterSystem characters(engine.world());
    characters.setCollision(&collision);
    mge::ItemUseRegistry itemUses;
    mge::ItemUse torch;
    torch.kind = mge::ItemUseKind::Toggle;
    torch.cooldown = 0.05f;
    itemUses.define("item/torch", torch);
    characters.setItemUses(&itemUses);
    engine.setCharacters(&characters);

    const mge::EntityId walker = engine.world().spawn();
    mge::TransformComponent walkerTransform;
    walkerTransform.position = {0, 0, 0};
    engine.world().setTransform(walker, walkerTransform);
    engine.world().setMovement(walker, mge::MovementComponent{{0, 0, -1.0f}, 4.0f});
    if (mge::CharacterComponent* c = characters.attach(walker)) {
        c->inventory.add({mge::assetIdFromName("item/torch"), "item.torch", 1, {1, 1, 1, 1}});
        characters.equip(walker, 0, mge::EquipSlot::HeldMain);
    }
    mge::grantHumanoidActions(characters, walker);

    // --- The device build's render composition, under the gate (19.7) ---
    //
    // Three dressed characters, one of them carrying a sword, composed every
    // frame exactly as device_game.cpp composes them. This is the part that
    // matters: until now the SHIPPED app's frame path was the only one no
    // runner compiled, and a per-frame heap allocation lived in it unnoticed
    // for months (19.6). Everything below runs inside the counter.
    struct GateActor {
        mge::EntityId entity = mge::kInvalidEntity;
        mge::HumanoidVariant variant;
        mge::LocomotionAnimator anim;
        mge::UsePlayer use;
        mge::UseMotion motion;
        mge::JointMask useMask;
        uint32_t visibleRegions = mge::kAllRegions;
        std::vector<HostGarment> garments;
        std::vector<HostHeld> held;
        mge::Mat4 palette[mge::kJointCount];
    };
    GateActor gateActors[3];
    // Stand-in mesh handles: the emission code only ever copies the pointer,
    // never dereferences it, which is what lets the gate run without a GPU.
    const int bodyMeshToken = 0;
    const int garmentMeshToken = 0;
    const int swordMeshToken = 0;

    // The body's draw ranges, one per region — the real thing, from the real
    // body mesh description, so the emitted row count matches the device's.
    std::vector<mge::MeshPart> gateBodyParts;
    for (size_t r = 0; r < mge::kBodyRegionCount; ++r) {
        mge::MeshPart part;
        part.region = static_cast<mge::BodyRegion>(r);
        part.firstIndex = static_cast<uint32_t>(r * 96);
        part.indexCount = 96;
        gateBodyParts.push_back(part);
    }

    for (int a = 0; a < 3; ++a) {
        GateActor& actor = gateActors[a];
        actor.entity = engine.world().spawn();
        mge::TransformComponent xf;
        xf.position = {static_cast<float>(a) * 2.0f, 0, -4.0f};
        xf.prevPosition = xf.position;
        engine.world().setTransform(actor.entity, xf);
        engine.world().setMovement(actor.entity, mge::MovementComponent{{0, 0, -1.0f}, 3.0f});
        // Two garments each, and the covered regions dropped from the body —
        // the same "masking is a draw range" arrangement the device uses.
        actor.garments.push_back(HostGarment{&garmentMeshToken, {0.5f, 0.4f, 0.3f, 1}});
        actor.garments.push_back(HostGarment{&garmentMeshToken, {0.3f, 0.3f, 0.3f, 1}});
        actor.visibleRegions &= ~(mge::regionBit(mge::BodyRegion::Torso));
        actor.anim.update(0.0f, 1.6f);
    }
    // One of them is armed, so emitHeldItems and the joint-transform
    // evaluation it needs are both inside the counter.
    {
        HostHeld sword;
        sword.mesh = &swordMeshToken;
        sword.anchor = mge::AttachPoint::HandR;
        sword.def.grip = mge::GripType::OneHanded;
        gateActors[0].held.push_back(sword);

        mge::ItemUse swordUse;
        swordUse.kind = mge::ItemUseKind::Strike;
        swordUse.archetype = mge::UseArchetype::Swing;
        swordUse.reach = 1.0f;
        swordUse.weight = 1.4f;
        gateActors[0].motion = mge::motionFromItemUse(swordUse, false);
        gateActors[0].useMask =
            mge::useArchetypeMask(mge::buildSkeleton(gateActors[0].variant), gateActors[0].motion);
    }

    // Scenery, so the static half of the draw list actually has work to do.
    // Without renderable entities emitWorldRenderables walks the registry and
    // pushes nothing, which would exercise the loop without exercising the
    // push — the thing that allocates.
    constexpr int kSceneryCount = 24;
    for (int i = 0; i < kSceneryCount; ++i) {
        const mge::EntityId prop = engine.world().spawn();
        mge::TransformComponent xf;
        xf.position = {static_cast<float>(i % 6) * 3.0f, 0, static_cast<float>(i / 6) * 3.0f};
        xf.prevPosition = xf.position;
        engine.world().setTransform(prop, xf);
        mge::ModelComponent model;
        model.asset = mge::assetIdFromName("model/house");
        engine.world().setModel(prop, model);
    }

    // The draw lists, reserved once and cleared per frame — the shape 19.6
    // established. If either ever outgrows its reservation the counter sees
    // the allocation and the gate fails, which is the whole point.
    std::vector<HostDrawItem> gateDrawItems;
    std::vector<HostSkinnedItem> gateSkinnedItems;
    gateDrawItems.reserve(engine.world().entities().capacity() + 8);
    gateSkinnedItems.reserve(3 * (mge::kBodyRegionCount + 2));

    constexpr int kWarmupFrames = 60;
    constexpr int kSteadyFrames = 600;
    constexpr double kFrameDt = 1.0 / 60.0;

    for (int i = 0; i < kWarmupFrames; ++i) engine.tick(kFrameDt);

    // Exercise the Android lifecycle mid-run: pause/resume + surface loss.
    engine.onPause();
    engine.tick(kFrameDt);
    engine.onResume();
    engine.onSurfaceLost();
    engine.tick(kFrameDt);
    engine.onSurfaceCreated(1080, 2400);

    // Characters join the P1 gate (task 12.3/12.6): a walking, falling body
    // resolving against colliders and performing actions every frame must
    // not touch the heap either.
    const long allocsBefore = gAllocCount.load();
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < kSteadyFrames; ++i) {
        engine.tick(kFrameDt);
        mixer.mix(mixBuffer, 800);  // the audio pull rides the same gate
        // ... and so does a character acting: jump when it can, use what it
        // holds, every single frame.
        characters.perform(walker, mge::actionJump());
        characters.perform(walker, mge::actionUseHeld());

        // --- and the device's render composition, every frame (19.7) ---
        const float alpha = engine.renderAlpha();
        gateDrawItems.clear();
        gateSkinnedItems.clear();
        // The static half of the draw list, through the same template the
        // device uses. The resolver stands in for the residency map: every
        // entity here is "resident", which is the busiest case.
        mge::emitWorldRenderables<HostDrawItem>(
            engine.world(), alpha, gateDrawItems,
            [&](mge::AssetId, HostDrawItem& item, mge::ResolvedRenderable& mesh) {
                item.mesh = &bodyMeshToken;
                mesh.boundsRadius = 1.0f;
                return true;
            });
        // Restart the swing whenever it finishes, so the archetype overlay,
        // its pose blend and the joint evaluation are live in most frames
        // rather than only at the start.
        if (!gateActors[0].use.active()) gateActors[0].use.start(gateActors[0].motion);
        for (GateActor& actor : gateActors) {
            const mge::TransformComponent* t = engine.world().transform(actor.entity);
            if (t == nullptr) continue;
            actor.anim.update(static_cast<float>(kFrameDt), 1.6f);
            actor.use.update(static_cast<float>(kFrameDt));

            mge::CharacterFrame frame;
            mge::composeCharacterFrame(*t, alpha, actor.variant, actor.anim, actor.use,
                                       actor.motion, actor.useMask,
                                       /*needJointWorld=*/!actor.held.empty(), actor.palette,
                                       frame);

            HostSkinnedItem proto;
            proto.mesh = &bodyMeshToken;
            proto.palette = actor.palette;
            proto.model = frame.model;
            proto.worldBounds = frame.bounds;
            proto.lodReference = frame.lodReference;
            std::memcpy(proto.baseColor, actor.variant.skin, sizeof(proto.baseColor));

            mge::emitBodyParts(proto, gateBodyParts.data(), gateBodyParts.size(),
                               actor.visibleRegions, gateSkinnedItems);
            mge::emitGarments(proto, actor.garments.data(), actor.garments.size(),
                              gateSkinnedItems);
            mge::emitHeldItems<HostDrawItem>(frame, actor.variant, actor.held.data(),
                                             actor.held.size(), gateDrawItems);
        }
    }
    const auto end = std::chrono::steady_clock::now();
    const long steadyAllocs = gAllocCount.load() - allocsBefore;

    const double totalMs = std::chrono::duration<double, std::milli>(end - start).count();

    printf("\n=== MobileGamesEngine host prototype ===\n");
    printf("frames: %llu  sim steps: %llu\n",
           static_cast<unsigned long long>(engine.stats().frameCount),
           static_cast<unsigned long long>(engine.stats().simStepCount));
    printf("steady-state: %d frames in %.2f ms (%.1f us/frame)\n", kSteadyFrames, totalMs,
           totalMs * 1000.0 / kSteadyFrames);
    printf("device render composition: %zu skinned rows + %zu props per frame "
           "(3 characters, 1 armed)\n",
           gateSkinnedItems.size(), gateDrawItems.size());
    printf("steady-state heap allocations: %ld  (target: 0)\n", steadyAllocs);
    printf("frame arena high water: %zu bytes\n", engine.stats().frameArenaHighWaterBytes);
    printf("\n--- memory budgets ---\n%s\n", engine.memoryReport().c_str());

    engine.shutdown();

    if (steadyAllocs != 0) {
        printf("FAIL: steady-state frame loop allocated on the heap\n");
        return 1;
    }
    printf("OK\n");
    return 0;
}
