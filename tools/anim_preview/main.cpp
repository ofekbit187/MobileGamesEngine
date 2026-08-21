// anim_preview: the proof for task 14.1 — layered poses with masks.
//
// Everything this writes is REAL engine output: the engine's rig, the
// engine's shipped locomotion, the engine's layer composition, rendered by
// the engine's Vulkan renderer.
//
//   anim_walk_vs_layered.ppm — the same walk phase, with and without an
//                              upper-body action layer. The legs are
//                              identical; only the torso and arms differ.
//   anim_swing_strip.ppm     — one action across its timeline while walking:
//                              the stride keeps running underneath it.
//   anim_mask_scope.ppm      — what each mask actually claims, by driving a
//                              pose through it alone.
//
// It also MEASURES the two claims that are worth nothing unstated:
// composition allocates nothing on the frame path, and it costs what it
// costs.
//
// Usage: mge_anim_preview [outputDir]

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <vector>

#include "mge/character/animation.h"
#include "mge/character/humanoid.h"
#include "mge/core/memory.h"
#include "mge/graphics/primitives.h"
#include "mge/graphics/renderer.h"
#include "mge/graphics/vulkan_device.h"

using namespace mge;

// Allocation counter, same instrument the host runner uses for the P1 gate.
// Here it is pointed at the layered-pose path specifically.
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

namespace {

constexpr size_t idx(Joint j) { return static_cast<size_t>(j); }

Quat rotX(float a) { return Quat::fromAxisAngle({1, 0, 0}, a); }
Quat rotY(float a) { return Quat::fromAxisAngle({0, 1, 0}, a); }
Quat rotZ(float a) { return Quat::fromAxisAngle({0, 0, 1}, a); }

float mix(float a, float b, float t) { return a + (b - a) * t; }
float smooth(float t) { return t * t * (3.0f - 2.0f * t); }

// A STAND-IN for a use archetype, not the archetype library — task 14.2
// builds that, in the engine, parameterized by grip/reach/weight. This lives
// in the demo on purpose: 14.1 is the layering mechanism, and it needs
// something recognisable to carry in order to be seen.
//
// One right-handed overhead swing over t in [0, 1]: wind-up, strike,
// recovery. The phase split is hard-coded here; making it addressable is
// exactly what task 14.3 is for.
Pose standInSwing(float t) {
    Pose pose;
    float arm = 0.0f, elbow = 0.15f, twist = 0.0f;
    if (t < 0.35f) {  // wind-up: arm goes up and back, torso loads
        const float u = smooth(t / 0.35f);
        arm = mix(0.0f, -2.5f, u);
        elbow = mix(0.15f, 1.30f, u);
        twist = mix(0.0f, -0.50f, u);
    } else if (t < 0.55f) {  // strike: fast, so it reads as a blow
        const float u = (t - 0.35f) / 0.20f;
        const float a = u * u;
        arm = mix(-2.5f, 0.60f, a);
        elbow = mix(1.30f, 0.10f, a);
        twist = mix(-0.50f, 0.45f, a);
    } else {  // recovery: back to guard
        const float u = smooth((t - 0.55f) / 0.45f);
        arm = mix(0.60f, 0.0f, u);
        elbow = mix(0.10f, 0.15f, u);
        twist = mix(0.45f, 0.0f, u);
    }
    pose.rotation[idx(Joint::UpperArmR)] = rotX(arm);
    pose.rotation[idx(Joint::ForearmR)] = rotX(elbow);
    pose.rotation[idx(Joint::Chest)] = rotY(twist);
    pose.rotation[idx(Joint::Spine)] = rotY(twist * 0.5f);
    pose.rotation[idx(Joint::Head)] = rotY(-twist * 0.3f);
    // The off hand braces rather than hanging — grip (§6.1) will decide this
    // properly once archetypes land.
    pose.rotation[idx(Joint::UpperArmL)] = rotX(-0.35f) * rotZ(0.30f);
    pose.rotation[idx(Joint::ForearmL)] = rotX(0.85f);
    return pose;
}

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
        std::memcpy(item.baseColor, rig.parts[i].color, sizeof item.baseColor);
        items.push_back(item);
    }
}

DrawItem prop(const GpuLodMesh* mesh, const Vec3& position, float r, float g, float b) {
    DrawItem item;
    item.mesh = mesh;
    item.model = Mat4::translation(position);
    const Vec3 e = mesh->bounds.extents();
    const float radius = e.length();
    item.worldBounds =
        Aabb::fromCenterExtents(position + mesh->bounds.center(), {radius, radius, radius});
    item.lodReference = position;
    item.baseColor[0] = r;
    item.baseColor[1] = g;
    item.baseColor[2] = b;
    return item;
}

bool capture(Renderer& renderer, const Camera& camera, const std::vector<DrawItem>& items,
             const std::string& path) {
    RenderStats stats;
    if (!renderer.renderFrame(camera, items.data(), items.size(), &stats)) {
        fprintf(stderr, "FAIL: renderFrame for %s\n", path.c_str());
        return false;
    }
    const size_t pixelBytes = static_cast<size_t>(renderer.width()) * renderer.height() * 4;
    std::vector<uint8_t> pixels(pixelBytes);
    if (!renderer.readback(pixels.data(), pixels.size())) return false;
    size_t nonSky = 0;
    const uint8_t skyR = 135, skyG = 168, skyB = 214;
    for (size_t i = 0; i < pixelBytes; i += 4) {
        if (pixels[i] != skyR || pixels[i + 1] != skyG || pixels[i + 2] != skyB) ++nonSky;
    }
    FILE* f = fopen(path.c_str(), "wb");
    if (f == nullptr) return false;
    fprintf(f, "P6\n%u %u\n255\n", renderer.width(), renderer.height());
    for (size_t i = 0; i < pixelBytes; i += 4) fwrite(&pixels[i], 1, 3, f);
    fclose(f);
    const double share = static_cast<double>(nonSky) / (pixelBytes / 4);
    printf("%s: drawn %u, coverage %.1f%%\n", path.c_str(), stats.drawn, share * 100.0);
    return share > 0.05;
}

// The walk pose at a given moment, from the engine's shipped locomotion.
Pose walkPose(LocomotionAnimator& anim, float seconds, float speed) {
    for (int f = 0; f < static_cast<int>(seconds * 60.0f); ++f) anim.update(1.0f / 60.0f, speed);
    Pose pose;
    anim.samplePose(pose);
    return pose;
}

// Angle between two rotations, in degrees — used to put a number on how far
// a joint has been moved rather than describing it.
float angleBetween(const Quat& a, const Quat& b) {
    float dot = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    if (dot < 0.0f) dot = -dot;
    if (dot > 1.0f) dot = 1.0f;
    return 2.0f * std::acos(dot) * 180.0f / kPi;
}

}  // namespace

int main(int argc, char** argv) {
    const std::string outDir = argc > 1 ? argv[1] : ".";

    // ---------------------------------------------------------------------
    // Measurement 1: what layering actually changes, joint by joint.
    // The claim under test is the task's own sentence — locomotion drives the
    // lower body while an action drives the upper body.
    // ---------------------------------------------------------------------
    const HumanoidVariant variant;
    const Skeleton skeleton = buildSkeleton(variant);
    const JointMask upper = maskUpperBody(skeleton, 0.5f);

    LocomotionAnimator probe;
    const Pose walk = walkPose(probe, 3.0f, 1.6f);
    const Pose action = standInSwing(0.42f);  // mid-strike
    LayeredPose layered;
    layered.reset(walk);
    layered.addLayer(action, upper, 1.0f);

    static const char* kJointNames[kJointCount] = {
        "Hips",      "Spine",  "Chest",     "Neck",     "Head",  "UpperArmL",
        "ForearmL",  "HandL",  "UpperArmR", "ForearmR", "HandR", "ThighL",
        "ShinL",     "FootL",  "ThighR",    "ShinR",    "FootR"};
    printf("\n--- 14.1: what the upper-body mask moves (degrees from locomotion) ---\n");
    printf("%-11s %6s  %8s\n", "joint", "mask", "moved");
    for (size_t j = 0; j < kJointCount; ++j) {
        printf("%-11s %6.2f  %7.1f%s\n", kJointNames[j], upper.weight[j],
               angleBetween(walk.rotation[j], layered.result().rotation[j]),
               upper.weight[j] == 0.0f ? "   <- locomotion keeps it" : "");
    }

    // ---------------------------------------------------------------------
    // Measurement 2: the frame path allocates nothing (P1), and costs what.
    // A crowd's worth of characters, composed the way a frame would.
    // ---------------------------------------------------------------------
    constexpr int kCharacters = 64;
    constexpr int kFrames = 600;
    LocomotionAnimator crowd[kCharacters];
    for (int c = 0; c < kCharacters; ++c) {
        for (int f = 0; f < 60; ++f) crowd[c].update(1.0f / 60.0f, 1.2f + 0.02f * c);
    }
    // One composer for the whole crowd: it is scratch, not per-character state.
    LayeredPose composer;
    Pose scratch;
    Mat4 palette[kJointCount];

    // Warm up outside the counted window, then count.
    for (int c = 0; c < kCharacters; ++c) {
        crowd[c].samplePose(scratch);
        composer.reset(scratch);
        composer.addLayer(action, upper, 0.5f);
        evaluatePose(skeleton, composer.result(), palette);
    }

    const long before = gAllocCount.load(std::memory_order_relaxed);
    const auto t0 = std::chrono::steady_clock::now();
    for (int f = 0; f < kFrames; ++f) {
        const float t = static_cast<float>(f % 90) / 90.0f;
        const Pose swing = standInSwing(t);
        for (int c = 0; c < kCharacters; ++c) {
            crowd[c].update(1.0f / 60.0f, 1.2f + 0.02f * c);
            crowd[c].samplePose(scratch);
            composer.reset(scratch);
            composer.addLayer(swing, upper, 1.0f);
            evaluatePose(skeleton, composer.result(), palette);
        }
    }
    const auto t1 = std::chrono::steady_clock::now();
    const long allocs = gAllocCount.load(std::memory_order_relaxed) - before;
    const double us = std::chrono::duration<double, std::micro>(t1 - t0).count();
    const double poses = static_cast<double>(kFrames) * kCharacters;

    printf("\n--- 14.1: cost of layering (%d characters x %d frames) ---\n", kCharacters,
           kFrames);
    printf("layered poses composed: %.0f\n", poses);
    printf("steady-state heap allocations: %ld  (target: 0)\n", allocs);
    printf("compose + evaluate: %.3f us per character-frame (%.2f ms total)\n", us / poses,
           us / 1000.0);
    printf("LayeredPose size: %zu bytes (scratch, shared by the whole crowd)\n",
           sizeof(LayeredPose));
    printf("JointMask size: %zu bytes\n", sizeof(JointMask));
    printf("palettes built: %.0f  (one per character-frame, same as unlayered)\n", poses);
    if (allocs != 0) {
        fprintf(stderr, "FAIL: layered pose path allocated %ld times\n", allocs);
        return 1;
    }

    // ---------------------------------------------------------------------
    // The captures.
    // ---------------------------------------------------------------------
    BudgetRegistry budgets;
    VulkanDevice device(budgets);
    if (!device.init(VulkanDeviceConfig{})) {
        printf("\nno Vulkan device — measurements above stand, captures skipped\n");
        return 0;
    }
    printf("\ndevice: %s\n", device.deviceName());

    Renderer renderer(device);
    RendererConfig config;
    config.width = 1280;
    config.height = 720;
    config.lightDir[0] = -0.35f;
    config.lightDir[1] = 0.80f;
    config.lightDir[2] = 0.50f;
    if (!renderer.init(config)) return 1;

    GpuLodMesh ground;
    {
        LodMesh m;
        m.lods.push_back(makePlane(60, 60));
        m.computeBounds();
        if (!renderer.uploadLodMesh(m, ground)) return 1;
    }

    // A guard with a drawn sword: the sword is rigidly attached to the hand
    // joint, so it follows the layered pose with no animation of its own —
    // which is the whole point (CHARACTERS.md §6.1).
    WearableInstance kit[5];
    kit[0].kind = WearableKind::Tunic;
    kit[0].color[0] = 0.42f; kit[0].color[1] = 0.32f; kit[0].color[2] = 0.20f;
    kit[1].kind = WearableKind::Armor;
    kit[1].layer = 2;
    kit[1].color[0] = 0.55f; kit[1].color[1] = 0.57f; kit[1].color[2] = 0.62f;
    kit[2].kind = WearableKind::Pants;
    kit[2].color[0] = 0.25f; kit[2].color[1] = 0.22f; kit[2].color[2] = 0.18f;
    kit[3].kind = WearableKind::Boots;
    kit[3].color[0] = 0.18f; kit[3].color[1] = 0.14f; kit[3].color[2] = 0.10f;
    kit[4].kind = WearableKind::Sword;
    kit[4].sheathed = false;
    kit[4].color[0] = 0.72f; kit[4].color[1] = 0.75f; kit[4].color[2] = 0.79f;

    RigInstance rig;
    if (!uploadRig(renderer, variant, kit, 5, rig)) return 1;

    bool ok = true;
    const float aspect = static_cast<float>(config.width) / config.height;

    // ---- 1. The same stride, with and without the action layer ------------
    // Both characters are at the IDENTICAL walk phase. Every difference on
    // screen is the layer, and the legs are provably untouched.
    {
        std::vector<DrawItem> items;
        items.push_back(prop(&ground, {0, 0, 0}, 0.44f, 0.48f, 0.37f));

        // yaw = pi/2 faces +X (screen right); pulling back to 0.38*pi turns
        // them three-quarters toward the camera so the body reads as well as
        // the arc. The swing is sagittal, so a near-side view is the only one
        // that shows it — the first framing of this capture looked at their
        // backs and the raised sword foreshortened into a sliver.
        const float kYaw = kPi * 0.38f;
        LocomotionAnimator a;
        const Pose stride = walkPose(a, 3.0f, 1.6f);
        emitRig(items, rig, stride, {-0.95f, 0, 0}, kYaw);

        LayeredPose both;
        both.reset(stride);
        both.addLayer(standInSwing(0.35f), upper, 1.0f);  // top of the wind-up
        emitRig(items, rig, both.result(), {0.95f, 0, 0}, kYaw);

        Camera camera;
        camera.eye = {0.0f, 1.35f, 3.1f};
        camera.target = {0.0f, 1.05f, 0.0f};
        camera.aspect = aspect;
        ok = capture(renderer, camera, items, outDir + "/anim_walk_vs_layered.ppm") && ok;
    }

    // ---- 2. The action across its timeline, walking throughout ------------
    // The stride advances between frames as well, so the legs are visibly
    // mid-cycle while the arm goes through wind-up, strike and recovery.
    {
        std::vector<DrawItem> items;
        items.push_back(prop(&ground, {0, 0, 0}, 0.44f, 0.48f, 0.37f));
        // Sampled at the moments that carry the motion, not at even spacing:
        // an even sample lands mid-strike where the arm passes through the
        // neutral hang, and the frame reads as "arm down" rather than "mid
        // blow". The times are printed so the sheet describes itself.
        const float kTimes[] = {0.00f, 0.35f, 0.46f, 0.55f, 0.80f};
        const char* kLabels[] = {"guard", "wind-up top", "mid-strike", "impact", "recovery"};
        const int kSteps = 5;
        printf("  swing strip sampled at t =");
        for (int i = 0; i < kSteps; ++i) printf(" %.2f (%s)", kTimes[i], kLabels[i]);
        printf("\n");
        for (int i = 0; i < kSteps; ++i) {
            const float t = kTimes[i];
            LocomotionAnimator a;
            // The stride advances between frames too, so the legs are at a
            // DIFFERENT point of the walk cycle in each one — the two
            // timelines are genuinely independent.
            const float strideSeconds = 3.0f + static_cast<float>(i) * 0.16f;
            const Pose stride = walkPose(a, strideSeconds, 1.6f);
            LayeredPose both;
            both.reset(stride);
            both.addLayer(standInSwing(t), upper, 1.0f);
            // Pure side (yaw = pi/2, facing +X): the swing arc lies in this
            // plane, so nothing about it is foreshortened.
            emitRig(items, rig, both.result(), {-2.4f + 1.2f * i, 0, 0}, kPi * 0.5f);
        }
        Camera camera;
        camera.eye = {0.0f, 1.35f, 5.0f};
        camera.target = {0.0f, 1.05f, 0.0f};
        camera.aspect = aspect;
        ok = capture(renderer, camera, items, outDir + "/anim_swing_strip.ppm") && ok;
    }

    // ---- 3. What each mask claims -----------------------------------------
    // The same action pose driven through three masks, over the same stride:
    // nothing, upper body, everything. The middle one is 14.1.
    {
        std::vector<DrawItem> items;
        items.push_back(prop(&ground, {0, 0, 0}, 0.44f, 0.48f, 0.37f));
        LocomotionAnimator a;
        const Pose stride = walkPose(a, 3.0f, 1.6f);
        const Pose swing = standInSwing(0.42f);
        const JointMask masks[3] = {maskNone(), upper, maskAll()};
        for (int i = 0; i < 3; ++i) {
            LayeredPose composed;
            composed.reset(stride);
            composed.addLayer(swing, masks[i], 1.0f);
            emitRig(items, rig, composed.result(), {-1.35f + 1.35f * i, 0, 0}, kPi * 0.42f);
        }
        Camera camera;
        camera.eye = {0.0f, 1.35f, 4.0f};
        camera.target = {0.0f, 1.05f, 0.0f};
        camera.aspect = aspect;
        ok = capture(renderer, camera, items, outDir + "/anim_mask_scope.ppm") && ok;
    }

    for (GpuLodMesh& mesh : rig.gpu) renderer.destroyLodMesh(mesh);
    renderer.destroyLodMesh(ground);

    printf(ok ? "OK\n" : "FAIL\n");
    return ok ? 0 : 1;
}
