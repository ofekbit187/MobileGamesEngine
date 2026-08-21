// anim_preview: the proof for Phase 14 — layered poses, use archetypes,
// phase timing and interruption.
//
// Everything this writes is REAL engine output: the engine's rig, the
// engine's shipped locomotion, the engine's layer composition, the IMPORTED
// ARTIST BODY, skinned by the engine and rendered by the engine's Vulkan
// renderer.
//
// It renders through `buildPosedCharacter` — the shipped path, the one
// `device_game.cpp` runs on the phone. Its first version used
// `buildHumanoidVisual`, the deprecated v1 box rig, and that was not a
// cosmetic mistake: separated boxes have no surface between them, so a joint
// mask that snaps 0 -> 1 across a joint looks PERFECT on boxes and shears on
// continuous skinned geometry, where the skin weights blend across that same
// joint. A preview that cannot fail is not evidence. The shear measurement
// below is the gate; the pictures are for the owner.
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

#include <deque>
#include <map>
#include <set>
#include <algorithm>

#include "mge/character/animation.h"
#include "mge/character/body_mesh.h"
#include "mge/character/humanoid.h"
#include "mge/character/use_archetypes.h"
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

// One CPU-skinned piece of one character, on the GPU. Held in a deque
// because DrawItems point at these, so their addresses must stay put.
struct Piece {
    GpuLodMesh gpu;
    float color[4] = {1, 1, 1, 1};
};
using PieceList = std::deque<Piece>;

// The shipped path: the imported artist body (plus its garments), skinned by
// the engine's own reference skinning for this pose. CPU-skinning here is
// deliberate — this tool writes stills, and the CPU path is the definition
// the GPU path is checked against.
bool addPosedCharacter(Renderer& renderer, const HumanoidVariant& variant,
                       const WearableInstance* wearables, size_t wearableCount,
                       const Pose& pose, PieceList& pieces, size_t& first) {
    first = pieces.size();
    std::vector<CharacterPiece> parts;
    buildPosedCharacter(variant, wearables, wearableCount, pose, BodyLod::Lod0, parts);
    for (const CharacterPiece& part : parts) {
        LodMesh lod;
        lod.lods.push_back(part.mesh);
        lod.computeBounds();
        Piece piece;
        if (!renderer.uploadLodMesh(lod, piece.gpu)) return false;
        std::memcpy(piece.color, part.color, sizeof piece.color);
        pieces.push_back(piece);
    }
    return true;
}

// buildPosedCharacter returns meshes already posed in character-local space,
// so the model matrix is only where the character stands.
void emitPosed(std::vector<DrawItem>& items, const PieceList& pieces, size_t first,
               const Vec3& position, float yaw) {
    const Mat4 root =
        Mat4::translation(position) * Mat4::rotation(Quat::fromAxisAngle({0, 1, 0}, -yaw));
    for (size_t i = first; i < pieces.size(); ++i) {
        DrawItem item;
        item.mesh = &pieces[i].gpu;
        item.model = root;
        item.worldBounds = Aabb::fromCenterExtents(position + Vec3{0, 1.0f, 0}, {2.5f, 2.5f, 2.5f});
        item.lodReference = position;
        std::memcpy(item.baseColor, pieces[i].color, sizeof item.baseColor);
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

// ---------------------------------------------------- skin shear (16.2) ----
// Per-edge strain — how far each mesh edge's length moves from bind. This is
// what tearing and pinching physically ARE, and it is computable on the CPU
// with no picture involved, which is the point: the render is for the owner,
// this is the gate.
struct EdgeSet {
    std::vector<std::pair<uint32_t, uint32_t>> edges;
    std::vector<float> bindLength;
};

EdgeSet edgesOf(const SkinnedMeshData& mesh) {
    std::set<std::pair<uint32_t, uint32_t>> unique;
    const auto add = [&](uint32_t a, uint32_t b) {
        unique.insert({std::min(a, b), std::max(a, b)});
    };
    for (size_t t = 0; t + 2 < mesh.indices.size(); t += 3) {
        add(mesh.indices[t], mesh.indices[t + 1]);
        add(mesh.indices[t + 1], mesh.indices[t + 2]);
        add(mesh.indices[t + 2], mesh.indices[t]);
    }
    EdgeSet out;
    out.edges.assign(unique.begin(), unique.end());
    out.bindLength.resize(out.edges.size());
    for (size_t i = 0; i < out.edges.size(); ++i) {
        out.bindLength[i] = (mesh.vertices[out.edges[i].first].position -
                             mesh.vertices[out.edges[i].second].position)
                                .length();
    }
    return out;
}

std::vector<float> edgeStrain(const SkinnedMeshData& mesh, const EdgeSet& es,
                              const HumanoidVariant& variant, const Pose& pose) {
    Mat4 palette[kJointCount];
    buildSkinPalette(variant, pose, palette);
    MeshData posed;
    skinMesh(mesh, palette, posed);
    std::vector<float> out(es.edges.size(), 0.0f);
    for (size_t i = 0; i < es.edges.size(); ++i) {
        if (es.bindLength[i] < 1e-6f) continue;
        const float len = (posed.vertices[es.edges[i].first].position -
                           posed.vertices[es.edges[i].second].position)
                              .length();
        out[i] = std::fabs(len / es.bindLength[i] - 1.0f);
    }
    return out;
}

float worstOf(const std::vector<float>& v) {
    float w = 0.0f;
    for (float x : v) w = std::max(w, x);
    return w;
}

int countOver(const std::vector<float>& v, float threshold) {
    int n = 0;
    for (float x : v) {
        if (x > threshold) ++n;
    }
    return n;
}

// The walk pose at a given moment, from the engine's shipped locomotion.
Pose walkPose(LocomotionAnimator& anim, float seconds, float speed) {
    for (int f = 0; f < static_cast<int>(seconds * 60.0f); ++f) anim.update(1.0f / 60.0f, speed);
    Pose pose;
    anim.samplePose(pose);
    return pose;
}

// Angle between two rotations, in degrees — a number instead of an adjective.
//
// NOT 2*acos(dot): acos has an infinite derivative at 1, so two BIT-IDENTICAL
// quaternions come back 0.06 degrees apart on float rounding alone. That is
// small enough to hide behind %.1f and large enough to make an exact claim
// wrong. 4*atan2(|a-b|, |a+b|) is exact in the same neighbourhood.
float angleBetween(const Quat& a, const Quat& b) {
    const float dot = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    const float s = dot < 0.0f ? -1.0f : 1.0f;
    const float dx = a.x - s * b.x, dy = a.y - s * b.y, dz = a.z - s * b.z, dw = a.w - s * b.w;
    const float sx = a.x + s * b.x, sy = a.y + s * b.y, sz = a.z + s * b.z, sw = a.w + s * b.w;
    const float dLen = std::sqrt(dx * dx + dy * dy + dz * dz + dw * dw);
    const float sLen = std::sqrt(sx * sx + sy * sy + sz * sz + sw * sw);
    return 4.0f * std::atan2(dLen, sLen) * 180.0f / kPi;
}

// ------------------------------------------------- the 14.6 catalog --------
// Six items. Every one is an archetype and four numbers. There is no clip, no
// per-item code and no animation authoring anywhere in this table — which is
// the whole claim P12 makes.
struct CatalogItem {
    const char* name;
    UseArchetype archetype;
    ItemGrip grip;
    float reach, weight;
};
const CatalogItem kCatalog[] = {
    {"sword",  UseArchetype::Swing,   ItemGrip::Versatile, 1.05f, 1.40f},
    {"spear",  UseArchetype::Thrust,  ItemGrip::TwoHanded, 2.40f, 2.20f},
    {"axe",    UseArchetype::Chop,    ItemGrip::OneHanded, 0.85f, 2.60f},
    {"hammer", UseArchetype::Work,    ItemGrip::OneHanded, 0.45f, 3.20f},
    {"torch",  UseArchetype::Raise,   ItemGrip::OneHanded, 0.55f, 0.70f},
    {"apple",  UseArchetype::Consume, ItemGrip::OneHanded, 0.10f, 0.20f},
};
constexpr size_t kCatalogCount = sizeof(kCatalog) / sizeof(kCatalog[0]);

UseMotion catalogMotion(size_t i) {
    UseMotion m;
    m.archetype = kCatalog[i].archetype;
    m.grip = kCatalog[i].grip;
    m.reach = kCatalog[i].reach;
    m.weight = kCatalog[i].weight;
    return m;
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
    // Measurement 3 (task 14.6): the catalog. Six items, zero per-item
    // animation authoring. Each row below IS the entire animation cost of
    // that item, and the phase timing on the right is derived from its
    // weight rather than tuned by hand.
    // ---------------------------------------------------------------------
    printf("\n--- 14.6: six items, zero per-item animation authoring ---\n");
    printf("%-8s %-9s %-10s %6s %7s | %8s %7s %9s %9s\n", "item", "archetype", "grip",
           "reach", "weight", "wind-up", "strike", "recovery", "duration");
    for (size_t i = 0; i < kCatalogCount; ++i) {
        const UseMotion m = catalogMotion(i);
        const UsePhases p = usePhases(m);
        const char* gripName = m.grip == ItemGrip::TwoHanded   ? "two-handed"
                               : m.grip == ItemGrip::Versatile ? "versatile"
                                                               : "one-handed";
        printf("%-8s %-9s %-10s %5.2fm %6.2fkg | %7.2f%% %6.2f%% %8.2f%% %8.3fs\n",
               kCatalog[i].name, useArchetypeName(m.archetype), gripName, m.reach, m.weight,
               p.windUp * 100.0f, p.strike * 100.0f, p.recovery * 100.0f, p.duration);
    }
    // The damage moment gameplay will hang on `strike` (task 14.3), in
    // seconds, so a maul landing late is the motion saying so.
    printf("strike moment (s from start, = full extension): ");
    for (size_t i = 0; i < kCatalogCount; ++i) {
        UsePlayer player;
        player.start(catalogMotion(i));
        printf("%s %.3f  ", kCatalog[i].name, player.strikeMoment());
    }
    printf("\n");

    // How different are the six, measured across their whole timelines? A
    // single frame is the wrong question — a thrust and a chop both end with
    // the arm forward; the path there is what separates them.
    float worstPair = 1e9f;
    const char *wa = "", *wb = "";
    for (size_t i = 0; i < kCatalogCount; ++i) {
        for (size_t j = i + 1; j < kCatalogCount; ++j) {
            float widest = 0.0f;
            for (int step = 0; step <= 20; ++step) {
                const float tt = static_cast<float>(step) / 20.0f;
                Pose pi, pj;
                sampleUseArchetype(catalogMotion(i), tt, pi);
                sampleUseArchetype(catalogMotion(j), tt, pj);
                for (size_t jt = 0; jt < kJointCount; ++jt) {
                    const float d = angleBetween(pi.rotation[jt], pj.rotation[jt]);
                    if (d > widest) widest = d;
                }
            }
            if (widest < worstPair) {
                worstPair = widest;
                wa = kCatalog[i].name;
                wb = kCatalog[j].name;
            }
        }
    }
    printf("closest pair of the six: %s vs %s, %.1f degrees apart at their widest\n", wa, wb,
           worstPair);

    // ---------------------------------------------------------------------
    // Measurement 4 (task 16.2): does any of this SHEAR THE SKIN?
    // Measured on the real imported body, on the CPU. No picture involved.
    // ---------------------------------------------------------------------
    {
        const SkinnedMeshData& body = sharedTemplateLods()[0];
        const EdgeSet es = edgesOf(body);
        printf("\n--- 16.2: skin shear on the real body (LOD0, %zu verts, %zu edges) ---\n",
               body.vertices.size(), es.edges.size());

        LocomotionAnimator walkAnim;
        const Pose walkP = walkPose(walkAnim, 3.0f, 1.6f);
        const std::vector<float> walkStrain = edgeStrain(body, es, variant, walkP);
        LocomotionAnimator runAnim;
        const Pose runP = walkPose(runAnim, 3.0f, 4.5f);
        const std::vector<float> runStrain = edgeStrain(body, es, variant, runP);
        printf("baseline  locomotion walk: worst %.3f, edges over 50%%: %d\n",
               worstOf(walkStrain), countOver(walkStrain, 0.5f));
        printf("baseline  locomotion run : worst %.3f, edges over 50%%: %d\n",
               worstOf(runStrain), countOver(runStrain, 0.5f));

        // What layering ADDS, over and above the two poses it blends. This is
        // the number that answers "does the mask boundary tear".
        float worstExcess = 0.0f;
        const UseArchetype kProbe[] = {UseArchetype::Swing, UseArchetype::Chop,
                                       UseArchetype::Thrust, UseArchetype::Raise};
        for (UseArchetype a : kProbe) {
            UseMotion m;
            m.archetype = a;
            for (int step = 0; step <= 6; ++step) {
                Pose act;
                sampleUseArchetype(m, static_cast<float>(step) / 6.0f, act);
                const std::vector<float> actStrain = edgeStrain(body, es, variant, act);
                LayeredPose L;
                L.reset(walkP);
                L.addLayer(act, useArchetypeMask(skeleton, m), 1.0f);
                const std::vector<float> layStrain = edgeStrain(body, es, variant, L.result());
                for (size_t i = 0; i < es.edges.size(); ++i) {
                    worstExcess =
                        std::max(worstExcess, layStrain[i] - std::max(walkStrain[i], actStrain[i]));
                }
            }
        }
        printf("LAYERING adds at most %.3f excess strain — under locomotion's own %.3f, so the\n"
               "  mask boundary is NOT what breaks the picture.\n",
               worstExcess, worstOf(walkStrain));

        // The shoulder envelope: what the body itself can take, with nothing
        // layered at all. This is the finding that matters.
        printf("\nshoulder envelope (pure rotX on UpperArmR, nothing else posed):\n");
        printf("%8s | %8s %14s %14s\n", "degrees", "worst", "edges >50%", "edges >100%");
        for (float deg : {0.0f, 15.0f, 30.0f, 45.0f, 60.0f, 90.0f, 140.0f}) {
            Pose p;
            p.rotation[idx(Joint::UpperArmR)] = rotX(deg * kPi / 180.0f);
            const std::vector<float> st = edgeStrain(body, es, variant, p);
            printf("%8.0f | %8.3f %14d %14d\n", deg, worstOf(st), countOver(st, 0.5f),
                   countOver(st, 1.0f));
        }
        printf("Locomotion stays inside ~35 degrees of shoulder rotation and never puts one\n"
               "  edge over 50%%. Every use archetype needs 60-140, where the shoulder tears.\n");
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

    bool ok = true;
    // Each distinct pose is skinned and uploaded on its own: buildPosedCharacter
    // bakes the pose into the vertices, which is right for stills and is exactly
    // what the frame path must NOT do (it skins on the GPU from the cached mesh
    // plus the palette).
    const auto place = [&](std::vector<DrawItem>& items, PieceList& pieces, const Pose& pose,
                           size_t wearCount, const Vec3& at, float yaw) {
        size_t first = 0;
        if (!addPosedCharacter(renderer, variant, kit, wearCount, pose, pieces, first)) return false;
        emitPosed(items, pieces, first, at, yaw);
        return true;
    };
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
        PieceList pieces;
        ok = place(items, pieces, stride, 5, {-0.95f, 0, 0}, kYaw) && ok;

        LayeredPose both;
        both.reset(stride);
        UseMotion sword;
        sword.archetype = UseArchetype::Swing;
        sword.reach = 1.05f;
        sword.weight = 1.40f;
        Pose swing;
        const UsePhases sp = usePhases(sword);
        sampleUseArchetype(sword, sp.windUp, swing);  // top of the wind-up
        both.addLayer(swing, useArchetypeMask(skeleton, sword), 1.0f);
        ok = place(items, pieces, both.result(), 5, {0.95f, 0, 0}, kYaw) && ok;

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
        PieceList pieces;
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
            ok = place(items, pieces, both.result(), 5, {-2.4f + 1.2f * i, 0, 0}, kPi * 0.5f) && ok;
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
        PieceList pieces;
        items.push_back(prop(&ground, {0, 0, 0}, 0.44f, 0.48f, 0.37f));
        LocomotionAnimator a;
        const Pose stride = walkPose(a, 3.0f, 1.6f);
        UseMotion swordM;
        swordM.archetype = UseArchetype::Swing;
        Pose swing;
        sampleUseArchetype(swordM, usePhases(swordM).windUp, swing);
        const JointMask masks[3] = {maskNone(), upper, maskAll()};
        for (int i = 0; i < 3; ++i) {
            LayeredPose composed;
            composed.reset(stride);
            composed.addLayer(swing, masks[i], 1.0f);
            ok = place(items, pieces, composed.result(), 5, {-1.35f + 1.35f * i, 0, 0},
                       kPi * 0.42f) && ok;
        }
        Camera camera;
        camera.eye = {0.0f, 1.35f, 4.0f};
        camera.target = {0.0f, 1.05f, 0.0f};
        camera.aspect = aspect;
        ok = capture(renderer, camera, items, outDir + "/anim_mask_scope.ppm") && ok;
    }

    // ---- 4. The catalog (14.6): six items, six motions, one mechanism ----
    // Rendered BARE-HANDED on purpose. Item meshes beyond the parametric
    // sword do not exist — held-item content is the wearables area, not this
    // one — and putting a sword in the apple-eater's hand would claim
    // otherwise. What 14.6 is about is the MOTION, and that is what is here.
    {
        std::vector<DrawItem> items;
        PieceList pieces;
        items.push_back(prop(&ground, {0, 0, 0}, 0.44f, 0.48f, 0.37f));
        for (size_t i = 0; i < kCatalogCount; ++i) {
            const UseMotion m = catalogMotion(i);
            const UsePhases p = usePhases(m);
            LocomotionAnimator a;
            const Pose stride = walkPose(a, 3.0f + 0.13f * static_cast<float>(i), 1.6f);
            Pose act;
            // Each at its own strike moment — the comparable instant, since
            // the six have different timelines.
            sampleUseArchetype(m, p.windUp + p.strike, act);
            LayeredPose both;
            both.reset(stride);
            both.addLayer(act, useArchetypeMask(skeleton, m), 1.0f);
            ok = place(items, pieces, both.result(), 4,  // kit minus the sword
                       {-3.1f + 1.24f * static_cast<float>(i), 0, 0}, kPi * 0.42f) && ok;
        }
        Camera camera;
        camera.eye = {0.0f, 1.40f, 6.0f};
        camera.target = {0.0f, 1.05f, 0.0f};
        camera.aspect = aspect;
        ok = capture(renderer, camera, items, outDir + "/anim_catalog.ppm") && ok;
    }

    // ---- 5. Interruption (14.4): a chop taking a hit mid-strike ----------
    // Five consecutive frames straddling the hit. The arm eases back toward
    // the walk instead of teleporting to it, and the legs never stop.
    {
        std::vector<DrawItem> items;
        PieceList pieces;
        items.push_back(prop(&ground, {0, 0, 0}, 0.44f, 0.48f, 0.37f));

        UseMotion axe;
        axe.archetype = UseArchetype::Chop;
        axe.reach = 0.85f;
        axe.weight = 2.60f;
        const JointMask mask = useArchetypeMask(skeleton, axe);

        LocomotionAnimator walk;
        for (int f = 0; f < 180; ++f) walk.update(1.0f / 60.0f, 1.6f);
        UsePlayer player;
        player.start(axe, 0.08f);
        while (player.phase() != UsePhase::Strike) {
            player.update(1.0f / 60.0f);
            walk.update(1.0f / 60.0f, 1.6f);
        }
        player.interrupt(0.22f);
        printf("  interrupt strip: hit taken during %s, blending out over 0.22s\n",
               usePhaseName(player.phase()));

        // Sample every third frame so five stills span the whole fade.
        for (int i = 0; i < 5; ++i) {
            Pose basePose;
            walk.samplePose(basePose);
            LayeredPose layered;
            layered.reset(basePose);
            if (player.active()) {
                Pose action;
                player.samplePose(action);
                layered.addLayer(action, mask, player.weight());
            }
            printf("    frame %d: weight %.2f\n", i, player.active() ? player.weight() : 0.0f);
            ok = place(items, pieces, layered.result(), 5, {-2.4f + 1.2f * i, 0, 0}, kPi * 0.5f) &&
                 ok;
            for (int k = 0; k < 3; ++k) {
                player.update(1.0f / 60.0f);
                walk.update(1.0f / 60.0f, 1.6f);
            }
        }
        Camera camera;
        camera.eye = {0.0f, 1.35f, 5.0f};
        camera.target = {0.0f, 1.05f, 0.0f};
        camera.aspect = aspect;
        ok = capture(renderer, camera, items, outDir + "/anim_interrupt.ppm") && ok;
    }

    // ---- 6. The shoulder envelope (16.2): where the body gives out --------
    // Nothing is layered here and no archetype is playing. This is the naked
    // body with one joint rotated, which is why it is evidence ABOUT THE BODY
    // rather than about anything this area built.
    {
        std::vector<DrawItem> items;
        PieceList pieces;
        items.push_back(prop(&ground, {0, 0, 0}, 0.44f, 0.48f, 0.37f));
        const float kDegrees[] = {0.0f, 30.0f, 60.0f, 90.0f, 140.0f};
        for (int i = 0; i < 5; ++i) {
            Pose p;
            p.rotation[idx(Joint::UpperArmR)] = rotX(kDegrees[i] * kPi / 180.0f);
            ok = place(items, pieces, p, 0, {-2.2f + 1.1f * i, 0, 0}, kPi * 0.5f) && ok;
        }
        Camera camera;
        camera.eye = {0.0f, 1.45f, 3.6f};
        camera.target = {0.0f, 1.32f, 0.0f};
        camera.aspect = aspect;
        ok = capture(renderer, camera, items, outDir + "/anim_shoulder_envelope.ppm") && ok;
    }

    renderer.destroyLodMesh(ground);

    printf(ok ? "OK\n" : "FAIL\n");
    return ok ? 0 : 1;
}
