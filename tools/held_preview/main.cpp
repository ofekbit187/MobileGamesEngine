// mge_held_preview — look at what a character is actually holding (task 14.7).
//
// MODELING.md §6 makes "looked at" a gate, and this task earned it twice: the
// numbers said a sheathed sword was fine when it was lying flat across the
// shoulder blades, and said it was fine again when its point was above the
// character's head. Both were obvious in one glance and invisible in a
// centroid.
//
// The engine's other previews render through Vulkan, which is not available in
// every environment this runs in, so this one rasterizes on the CPU: an
// orthographic camera, a depth buffer, flat shading. It is not pretty and does
// not need to be — it answers "is the sword in the hand, and is it through the
// leg?", which is precisely what the numbers could not.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "mge/character/body_mesh.h"
#include "mge/character/held_items.h"
#include "mge/character/animation.h"
#include "mge/character/humanoid.h"
#include "mge/character/use_archetypes.h"
#include "mge/character/wearable_catalogue.h"

using namespace mge;

namespace {

constexpr int kW = 260;
constexpr int kH = 420;

struct Image {
    std::vector<unsigned char> rgb = std::vector<unsigned char>(kW * kH * 3, 24);
    std::vector<float> depth = std::vector<float>(kW * kH, 1e30f);
};

// Orthographic: the character stands in a 2.1 m tall box, seen down an axis.
struct View {
    const char* name;
    int axisRight;  // 0=x 1=y 2=z
    int axisUp;
    int axisDepth;
    float rightSign;
    float depthSign;
};

float component(const Vec3& v, int axis) {
    return axis == 0 ? v.x : (axis == 1 ? v.y : v.z);
}

void raster(Image& img, const View& view, const MeshData& mesh, const float color[4]) {
    const float height = 2.10f;
    const float scale = static_cast<float>(kH) / height;
    for (size_t t = 0; t + 2 < mesh.indices.size(); t += 3) {
        Vec3 p[3];
        for (int k = 0; k < 3; ++k) p[k] = mesh.vertices[mesh.indices[t + k]].position;

        // Flat shade from the triangle's own normal, so form reads without
        // lighting rig or materials.
        const Vec3 n = (p[1] - p[0]).cross(p[2] - p[0]);
        const float area = n.length();
        if (area < 1e-12f) continue;
        const Vec3 unit = n * (1.0f / area);
        const float lit = 0.35f + 0.65f * std::fabs(unit.dot(Vec3{0.35f, 0.5f, -0.79f}));

        float sx[3], sy[3], sd[3];
        for (int k = 0; k < 3; ++k) {
            const float r = component(p[k], view.axisRight) * view.rightSign;
            const float u = component(p[k], view.axisUp);
            sx[k] = static_cast<float>(kW) * 0.5f + r * scale;
            sy[k] = static_cast<float>(kH) - (u + 0.05f) * scale;
            sd[k] = component(p[k], view.axisDepth) * view.depthSign;
        }
        const int minX = std::max(0, static_cast<int>(std::floor(std::min({sx[0], sx[1], sx[2]}))));
        const int maxX = std::min(kW - 1, static_cast<int>(std::ceil(std::max({sx[0], sx[1], sx[2]}))));
        const int minY = std::max(0, static_cast<int>(std::floor(std::min({sy[0], sy[1], sy[2]}))));
        const int maxY = std::min(kH - 1, static_cast<int>(std::ceil(std::max({sy[0], sy[1], sy[2]}))));
        const float den = (sy[1] - sy[2]) * (sx[0] - sx[2]) + (sx[2] - sx[1]) * (sy[0] - sy[2]);
        if (std::fabs(den) < 1e-9f) continue;

        for (int y = minY; y <= maxY; ++y) {
            for (int x = minX; x <= maxX; ++x) {
                const float px = static_cast<float>(x) + 0.5f;
                const float py = static_cast<float>(y) + 0.5f;
                const float a = ((sy[1] - sy[2]) * (px - sx[2]) + (sx[2] - sx[1]) * (py - sy[2])) / den;
                const float b = ((sy[2] - sy[0]) * (px - sx[2]) + (sx[0] - sx[2]) * (py - sy[2])) / den;
                const float c = 1.0f - a - b;
                if (a < 0 || b < 0 || c < 0) continue;
                const float d = a * sd[0] + b * sd[1] + c * sd[2];
                const size_t idx = static_cast<size_t>(y) * kW + static_cast<size_t>(x);
                if (d >= img.depth[idx]) continue;
                img.depth[idx] = d;
                for (int ch = 0; ch < 3; ++ch) {
                    const float v = color[ch] * lit * 255.0f;
                    img.rgb[idx * 3 + static_cast<size_t>(ch)] =
                        static_cast<unsigned char>(std::clamp(v, 0.0f, 255.0f));
                }
            }
        }
    }
}

bool writePpm(const std::string& path, const std::vector<Image>& panels) {
    const int w = kW * static_cast<int>(panels.size());
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (f == nullptr) return false;
    std::fprintf(f, "P6\n%d %d\n255\n", w, kH);
    std::vector<unsigned char> row(static_cast<size_t>(w) * 3);
    for (int y = 0; y < kH; ++y) {
        for (size_t p = 0; p < panels.size(); ++p) {
            std::memcpy(&row[p * kW * 3], &panels[p].rgb[static_cast<size_t>(y) * kW * 3],
                        kW * 3);
        }
        std::fwrite(row.data(), 1, row.size(), f);
    }
    std::fclose(f);
    return true;
}

}  // namespace

// --swing: the sequence the DEVICE build now plays (task 19.3).
//
// Everything the phone does per frame is reproduced here in the same order,
// because the point is to prove the wiring rather than to redraw the
// archetype: locomotion samples a walk, the use archetype is layered over it
// under its own mask, and the held item is placed on the resulting pose. If
// the sword leaves the hand mid-swing, or the legs stop walking when the arm
// starts, it shows up in this strip and nowhere else.
int renderSwing(const std::string& outDir) {
    HumanoidVariant variant;
    const Skeleton skeleton = buildSkeleton(variant);

    // The sword the device declares, field for field (device_game.cpp).
    ItemUse swordUse;
    swordUse.kind = ItemUseKind::Strike;
    swordUse.archetype = UseArchetype::Swing;
    swordUse.reach = 1.0f;
    swordUse.weight = 1.4f;
    const UseMotion motion = motionFromItemUse(swordUse, /*leftHanded=*/false);
    const JointMask mask = useArchetypeMask(skeleton, motion);
    const UsePhases phases = usePhases(motion);

    LocomotionAnimator walk;

    constexpr int kFrames = 8;
    std::vector<Image> panels;
    std::printf("  swing: %.2fs total, wind-up %.0f%% / strike %.0f%% / recovery %.0f%%\n",
                motion.reach > 0 ? phases.duration : phases.duration, phases.windUp * 100.0f,
                phases.strike * 100.0f, phases.recovery * 100.0f);
    // The walk has to ADVANCE across the strip, or the legs are frozen and the
    // whole point — that locomotion survives the overlay — goes unproven.
    const float frameDt = phases.duration / static_cast<float>(kFrames - 1);
    for (int f = 0; f < kFrames; ++f) {
        const float t = static_cast<float>(f) / static_cast<float>(kFrames - 1);
        if (f > 0) walk.update(frameDt, 1.6f);
        Pose base;
        walk.samplePose(base);
        Pose overlay;
        sampleUseArchetype(motion, t, overlay);
        LayeredPose layered;
        layered.reset(base);
        layered.addLayer(overlay, mask, 1.0f);
        const Pose pose = layered.result();

        const WearableInstance outfit[] = {
            {WearableKind::Tunic, 1, false, {0.55f, 0.42f, 0.28f, 1}},
            {WearableKind::Pants, 1, false, {0.34f, 0.30f, 0.26f, 1}},
            {WearableKind::Sword, 1, false, {0.72f, 0.73f, 0.78f, 1}},
        };
        std::vector<CharacterPiece> pieces;
        buildPosedCharacter(variant, outfit, 3, pose, BodyLod::Lod0, pieces);

        // FRONT view on purpose. A Swing travels across the body in x, and a
        // side view puts x down the depth axis: the first capture of this
        // strip showed a sword that appeared not to move at all, because the
        // whole 0.7 m arc was pointing at the camera.
        const View view = {"front", 0, 1, 2, 1.0f, 1.0f};
        Image img;
        for (const CharacterPiece& piece : pieces) raster(img, view, piece.mesh, piece.color);
        panels.push_back(std::move(img));

        const char* phaseName = t < phases.windUp
                                    ? "wind-up"
                                    : (t < phases.windUp + phases.strike ? "STRIKE" : "recovery");
        std::printf("  t=%.2f  %-8s  %zu pieces\n", t, phaseName, pieces.size());
    }

    const std::string path = outDir + "/held_swing.ppm";
    if (!writePpm(path, panels)) {
        std::printf("could not write %s\n", path.c_str());
        return 1;
    }
    std::printf("\nwrote %s (%d frames, wind-up through recovery)\n", path.c_str(), kFrames);
    return 0;
}

int main(int argc, char** argv) {
    std::string outDir = "build";
    bool swing = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--swing") == 0) {
            swing = true;
        } else {
            outDir = argv[i];
        }
    }
    if (swing) return renderSwing(outDir);

    const size_t sword = wearableCatalogue().find("sword");
    if (sword == WearableCatalogue::npos) {
        std::printf("no sword in the wearable catalogue\n");
        return 1;
    }

    HumanoidVariant variant;
    Pose pose;
    for (size_t j = 0; j < kJointCount; ++j) pose.rotation[j] = Quat{0, 0, 0, 1};

    const View views[] = {
        {"front", 0, 1, 2, 1.0f, 1.0f},
        {"side", 2, 1, 0, 1.0f, 1.0f},
        {"back", 0, 1, 2, -1.0f, -1.0f},
    };

    std::vector<Image> panels;
    for (int sheathed = 0; sheathed < 2; ++sheathed) {
        for (const View& view : views) {
            Image img;
            const WearableInstance outfit[] = {
                {WearableKind::Tunic, 1, false, {0.55f, 0.42f, 0.28f, 1}},
                {WearableKind::Pants, 1, false, {0.34f, 0.30f, 0.26f, 1}},
                {WearableKind::Sword, 1, sheathed != 0, {0.72f, 0.73f, 0.78f, 1}},
            };
            std::vector<CharacterPiece> pieces;
            buildPosedCharacter(variant, outfit, 3, pose, BodyLod::Lod0, pieces);
            for (const CharacterPiece& piece : pieces) raster(img, view, piece.mesh, piece.color);
            panels.push_back(std::move(img));
            std::printf("  %-8s %-8s %zu pieces\n", sheathed ? "sheathed" : "drawn",
                        view.name, pieces.size());
        }
    }

    const std::string path = outDir + "/held_sword.ppm";
    if (!writePpm(path, panels)) {
        std::printf("could not write %s\n", path.c_str());
        return 1;
    }
    std::printf("\nwrote %s (drawn front/side/back, then sheathed)\n", path.c_str());
    return 0;
}
