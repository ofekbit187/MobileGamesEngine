// Task 15.1/15.2 — an IMPORTED skin on the shipped body.
//
// ADR 0014's reversal: base maps are sourced, never originated. Task 15.0
// measured that the only route to a FACE is a mesh-to-mesh transfer, because
// every CC0 skin worth having is authored against somebody else's mesh.
//
//   mge_skin_import --mesh <source.obj> --texture <source.png> --out <dir>
//
// The source is a build-time input and is not committed, exactly as the body's
// own Blender bundle is not (MODELING.md §0). Provenance, licence and the
// measurements that chose it: docs/research/skin-sourceability.md.
//
// Everything printed is a measurement. The one that matters most is the mean
// distance between our surface and the source's ACROSS THE FACE: a few
// millimetres there is the difference between an eye landing on an eye and an
// eye landing on a cheek.

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "mge/character/body_mesh.h"
#include "mge/character/humanoid.h"
#include "mge/core/memory.h"
#include "mge/graphics/camera.h"
#include "mge/graphics/renderer.h"
#include "mge/graphics/vulkan_device.h"

#include "../skin_preview/skin_generator.h"
#include "skin_transfer.h"

using namespace mge;

namespace {

bool writePpm(const std::string& path, const std::vector<uint8_t>& rgba, uint32_t w, uint32_t h) {
    FILE* f = fopen(path.c_str(), "wb");
    if (f == nullptr) return false;
    fprintf(f, "P6\n%u %u\n255\n", w, h);
    std::vector<uint8_t> rgb(size_t(w) * h * 3);
    for (size_t i = 0; i < size_t(w) * h; ++i) {
        rgb[i * 3 + 0] = rgba[i * 4 + 0];
        rgb[i * 3 + 1] = rgba[i * 4 + 1];
        rgb[i * 3 + 2] = rgba[i * 4 + 2];
    }
    fwrite(rgb.data(), 1, rgb.size(), f);
    fclose(f);
    return true;
}

bool writeSheetRgb(const std::string& path, const std::vector<uint8_t>& rgb, uint32_t sheet) {
    std::vector<uint8_t> rgba(size_t(sheet) * sheet * 4, 255);
    for (size_t i = 0; i < size_t(sheet) * sheet; ++i) {
        rgba[i * 4 + 0] = rgb[i * 3 + 0];
        rgba[i * 4 + 1] = rgb[i * 3 + 1];
        rgba[i * 4 + 2] = rgb[i * 3 + 2];
    }
    return writePpm(path, rgba, sheet, sheet);
}

bool capture(Renderer& renderer, const Camera& camera, const std::vector<DrawItem>& items,
             const std::string& path) {
    RenderStats stats;
    if (!renderer.renderFrame(camera, items.data(), items.size(), &stats)) return false;
    const size_t bytes = size_t(renderer.width()) * renderer.height() * 4;
    std::vector<uint8_t> pixels(bytes);
    if (!renderer.readback(pixels.data(), pixels.size())) return false;
    if (!writePpm(path, pixels, renderer.width(), renderer.height())) return false;
    printf("  wrote %s\n", path.c_str());
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    std::string meshPath, texturePath, outDir = ".";
    uint32_t sheet = 1024;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--mesh") == 0 && i + 1 < argc) meshPath = argv[++i];
        else if (std::strcmp(argv[i], "--texture") == 0 && i + 1 < argc) texturePath = argv[++i];
        else if (std::strcmp(argv[i], "--out") == 0 && i + 1 < argc) outDir = argv[++i];
        else if (std::strcmp(argv[i], "--sheet") == 0 && i + 1 < argc) sheet = uint32_t(atoi(argv[++i]));
    }
    if (meshPath.empty() || texturePath.empty()) {
        fprintf(stderr,
                "usage: mge_skin_import --mesh <source.obj> --texture <source.png> "
                "[--out <dir>] [--sheet N]\n"
                "The source is a build-time input; see docs/research/skin-sourceability.md.\n");
        return 2;
    }

    // ------------------------------------------------------------ inputs ---
    skin::SourceMesh source;
    std::string error;
    if (!skin::loadObj(meshPath, source, error)) {
        fprintf(stderr, "FAIL: %s\n", error.c_str());
        return 1;
    }
    skin::SourceImage image;
    if (!skin::loadImage(texturePath, image, error)) {
        fprintf(stderr, "FAIL: %s\n", error.c_str());
        return 1;
    }
    printf("source mesh    %zu vertices, %zu triangles, %zu texcoords\n", source.positions.size(),
           source.triangleCount(), source.uvs.size() / 2);
    printf("source texture %d x %d\n", image.width, image.height);

    SkinnedMeshData body;
    BodyBuildDesc desc;
    desc.lod = BodyLod::Lod0;
    desc.regions = kAllRegions;
    buildTemplateBody(desc, body);
    printf("our body       %zu vertices, %zu triangles\n", body.vertices.size(),
           body.triangleCount());

    // ------------------------------------------------------------- align ---
    printf("aligning (measuring both facings rather than assuming one)...\n");
    const skin::Alignment alignment = skin::alignToBody(source, body);
    printf("  scale %.4f, flip %s, translate (%.3f %.3f %.3f)\n", alignment.scale,
           alignment.flipped ? "180 deg about Y" : "none", alignment.translation.x,
           alignment.translation.y, alignment.translation.z);
    printf("  surface separation: mean %.1f mm, median %.1f mm  (rejected facing: %.1f mm)\n",
           alignment.meanDistance * 1000.0, alignment.medianDistance * 1000.0,
           alignment.meanDistanceFlipped * 1000.0);
    if (alignment.meanDistance * 2.0f > alignment.meanDistanceFlipped) {
        printf("  WARNING: the two facings score within 2x of each other — the choice is not\n"
               "           clearly measured, so treat the orientation as unverified.\n");
    }

    // ---------------------------------------------------- per-region fit ---
    // One rigid transform cannot fit two humans in different poses. Each region
    // finds where it actually corresponds.
    printf("fitting each region separately (the two bodies are not in the same pose)...\n");
    const skin::RegionFit fit = skin::fitRegions(source, body, alignment, 6);
    {
        static const char* kNames[] = {"Scalp", "Face",  "Neck",  "Torso", "ArmL",  "ArmR",
                                       "HandL", "HandR", "LegL",  "LegR",  "FootL", "FootR"};
        printf("  %-7s %9s %9s %9s   %s\n", "region", "before", "after", "moved", "");
        for (size_t r = 0; r < kBodyRegionCount; ++r) {
            if (fit.samples[r] == 0) continue;
            const Vec3& o = fit.offset[r];
            const float moved = std::sqrt(o.x * o.x + o.y * o.y + o.z * o.z);
            printf("  %-7s %6.1f mm %6.1f mm %6.1f mm\n", kNames[r], fit.before[r] * 1000.0,
                   fit.residual[r] * 1000.0, moved * 1000.0);
        }
    }

    // ----------------------------------------------------------- surface ---
    std::vector<skin::SurfaceTexel> surface;
    skin::buildSurfaceMap(body, sheet, surface);
    size_t covered = 0, faceTexels = 0;
    for (const skin::SurfaceTexel& t : surface) {
        if (t.valid()) covered++;
        if (t.region == uint8_t(BodyRegion::Face)) faceTexels++;
    }
    printf("chart %u^2: %zu texels carry surface (%.1f%%), Face island %zu\n", sheet, covered,
           100.0 * double(covered) / double(surface.size()), faceTexels);

    // ---------------------------------------------------------- transfer ---
    std::vector<uint8_t> rgb;
    std::vector<uint8_t> filled;
    skin::TransferStats stats;
    if (!skin::transfer(source, image, alignment, fit, surface.data(), sheet, rgb, filled, stats)) {
        fprintf(stderr, "FAIL: transfer wrote nothing\n");
        return 1;
    }
    printf("  non-skin samples rejected: %zu (%zu on the face) — the source models eyeballs,\n"
           "  teeth and tongue as separate meshes, so its sheet carries saturated interiors\n"
           "  for surfaces our closed face shell does not have. Filled by dilation from the\n"
           "  surrounding skin.\n",
           stats.texelsRejected, stats.faceRejected);
    printf("transfer: %zu texels written, %zu missed (%.2f%%)\n", stats.texelsWritten,
           stats.texelsMissed,
           100.0 * double(stats.texelsMissed) / double(stats.texelsWritten + stats.texelsMissed));
    printf("  correspondence distance: mean %.1f mm, max %.1f mm\n", stats.meanDistance * 1000.0,
           stats.maxDistance * 1000.0);
    printf("  ACROSS THE FACE: mean %.1f mm  <- the number that decides whether an eye lands\n"
           "                                    on an eye\n",
           stats.faceMeanDistance * 1000.0);

    writeSheetRgb(outDir + "/imported_sheet.ppm", rgb, sheet);

    // ------------------------------------------- into the engine's format --
    // The transfer wrote sRGB-encoded bytes (the source is an sRGB image), so
    // the texture is tagged sRGB and the hardware linearises on fetch. Mips
    // must still be built in LINEAR light, which packSkinMaps does.
    TextureData albedo;
    if (!skin::packImportedAlbedo(rgb, filled, sheet, albedo)) {
        fprintf(stderr, "FAIL: packing the imported albedo\n");
        return 1;
    }
    const char* reason = nullptr;
    if (!validateTexture(albedo, &reason)) {
        fprintf(stderr, "FAIL: imported albedo is malformed: %s\n", reason);
        return 1;
    }
    printf("albedo: %u^2, %zu mips, %.2f MiB uncompressed, sRGB\n", albedo.width,
           albedo.mips.size(), double(albedo.byteSize()) / (1024.0 * 1024.0));

    // ---------------------------------------------------------- rendering --
    BudgetRegistry budgets;
    VulkanDevice device(budgets);
    if (!device.init(VulkanDeviceConfig{})) {
        fprintf(stderr, "SKIP: no Vulkan device\n");
        return 0;
    }
    Renderer renderer(device);
    RendererConfig config;
    config.width = 900;
    config.height = 900;
    config.lightDir[0] = -0.45f;
    config.lightDir[1] = 0.65f;
    config.lightDir[2] = -0.62f;
    if (!renderer.init(config)) return 1;

    GpuTexture albedoGpu{};
    if (!renderer.uploadTexture(albedo, albedoGpu)) {
        fprintf(stderr, "FAIL: texture upload refused\n");
        return 1;
    }
    GpuMaterial material;
    if (!renderer.createMaterial(&albedoGpu, nullptr, material)) return 1;

    Pose pose;
    Mat4 palette[kJointCount];
    float shape[kMorphCount] = {0};
    buildSkinPalette(templateVariant(), pose, palette);

    MeshData posed;
    // skinMesh now carries the chart UVs (task 16.5) — the local workaround
    // this tool's sibling needed is gone.
    skinMesh(body, palette, shape, posed);
    LodMesh lod;
    lod.lods.push_back(posed);
    lod.computeBounds();
    GpuLodMesh gpuBody;
    if (!renderer.uploadLodMesh(lod, gpuBody)) return 1;

    DrawItem figure;
    figure.mesh = &gpuBody;
    figure.model = Mat4::translation({0, 0, 0});
    figure.worldBounds = Aabb::fromCenterExtents({0, 0.9f, 0}, {1.5f, 1.5f, 1.5f});
    figure.surface = &material;
    std::vector<DrawItem> items{figure};

    const skin::HeadLandmarks head = skin::measureHead(body);
    bool ok = true;
    {
        Camera c;
        const Vec3 target{0, head.eyeY - head.headHeight() * 0.10f, 0};
        c.fovYRadians = 35.0f * kPi / 180.0f;
        c.aspect = 1.0f;
        c.nearZ = 0.02f;
        c.farZ = 20.0f;
        c.target = target;
        c.eye = target + Vec3{0, 0, -0.42f};
        ok = capture(renderer, c, items, outDir + "/imported_portrait.ppm") && ok;
        c.eye = target + Vec3{-0.32f, 0.05f, -0.44f};
        ok = capture(renderer, c, items, outDir + "/imported_threequarter.ppm") && ok;
    }
    {
        Camera c;
        const Vec3 target{0, 0.95f, 0};
        c.fovYRadians = 40.0f * kPi / 180.0f;
        c.aspect = 1.0f;
        c.nearZ = 0.05f;
        c.farZ = 40.0f;
        c.target = target;
        c.eye = target + Vec3{0.5f, 0.15f, -2.4f};
        ok = capture(renderer, c, items, outDir + "/imported_body.ppm") && ok;
    }

    renderer.destroyTexture(albedoGpu);
    printf("%s\n", ok ? "OK" : "FAILED");
    return ok ? 0 : 1;
}
