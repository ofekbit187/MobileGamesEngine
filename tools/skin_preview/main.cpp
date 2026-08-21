// Task 15.1 — one generated face, rendered, for the owner's eye.
//
// ADR 0014 puts this before any system on purpose: a generator can pass every
// numeric gate in `assets/standards/skin_texture.mgestd` and still look like
// painted plastic, and there is no metric for "reads as skin". So the cheapest
// possible test comes first — one skin on the shipped body, rendered, shown.
//
// What this writes to the output directory:
//   skin_portrait.ppm    the face, close, front — the capture the ruling needs
//   skin_threequarter.ppm the same head at 3/4, where a flat texture betrays itself
//   skin_body.ppm        the whole figure, so the skin is judged at play distance
//   skin_sheet.ppm       the generated albedo sheet itself, flat
//   skin_packed.ppm      the AO/roughness sheet, flat
//
// It also prints the measurable properties of what it generated — the ITA
// angle and its Fitzpatrick band, sheet coverage, mip count, budget — because
// those are the parts a session is allowed to have an opinion about.
//
// NOTE ON THE RENDER PATH. This tool CPU-skins the body and draws it through
// the static textured pipeline, and it copies the chart UVs across itself,
// because `skinMesh()` does not carry them and `SkinnedDrawItem` has no
// material. Both are outside this session's area; see docs/status/textures.md
// for the two seam requests. The pixels are real either way: the same mesh,
// the same frozen chart, the same lit shader every character uses.

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "mge/character/body_mesh.h"
#include "mge/character/humanoid.h"
#include "mge/graphics/camera.h"
#include "mge/graphics/renderer.h"
#include "mge/core/memory.h"
#include "mge/graphics/vulkan_device.h"

#include "skin_generator.h"

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

// The chart UVs, carried onto the skinned result. `skinMesh` writes position
// and normal only, so without this the whole body samples one texel.
void carryUvs(const SkinnedMeshData& src, MeshData& dst) {
    const size_t n = std::min(src.vertices.size(), dst.vertices.size());
    for (size_t i = 0; i < n; ++i) {
        dst.vertices[i].uv[0] = src.vertices[i].uv[0] / 65535.0f;
        dst.vertices[i].uv[1] = src.vertices[i].uv[1] / 65535.0f;
    }
}

bool capture(Renderer& renderer, const Camera& camera, const std::vector<DrawItem>& items,
             const std::string& path) {
    RenderStats stats;
    if (!renderer.renderFrame(camera, items.data(), items.size(), &stats)) {
        fprintf(stderr, "FAIL: renderFrame for %s\n", path.c_str());
        return false;
    }
    const size_t bytes = size_t(renderer.width()) * renderer.height() * 4;
    std::vector<uint8_t> pixels(bytes);
    if (!renderer.readback(pixels.data(), pixels.size())) return false;
    if (!writePpm(path, pixels, renderer.width(), renderer.height())) return false;
    printf("  wrote %s (%u x %u, %u drawn)\n", path.c_str(), renderer.width(), renderer.height(),
           stats.drawn);
    return true;
}

// The generated sheet, written flat so the maps can be inspected as images
// rather than only as a render.
bool writeSheet(const std::string& path, const TextureData& tex) {
    if (tex.mips.empty()) return false;
    const TextureMip& mip = tex.mips[0];
    std::vector<uint8_t> rgba(size_t(mip.width) * mip.height * 4);
    std::memcpy(rgba.data(), tex.pixels.data() + mip.offset, rgba.size());
    return writePpm(path, rgba, mip.width, mip.height);
}

}  // namespace

int main(int argc, char** argv) {
    std::string outDir = ".";
    uint32_t sheet = 1024;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--out") == 0 && i + 1 < argc) outDir = argv[++i];
        else if (std::strcmp(argv[i], "--sheet") == 0 && i + 1 < argc) sheet = uint32_t(atoi(argv[++i]));
    }

    // ---------------------------------------------------------- the body ---
    SkinnedMeshData body;
    BodyBuildDesc desc;
    desc.lod = BodyLod::Lod0;
    desc.regions = kAllRegions;
    buildTemplateBody(desc, body);
    if (body.vertices.empty()) {
        fprintf(stderr, "FAIL: template body is empty\n");
        return 1;
    }

    const skin::HeadLandmarks head = skin::measureHead(body);
    printf("head measured from the delivered body:\n");
    printf("  chin %.4f m  crown %.4f m  height %.4f m (%.2f heads tall)\n", head.chinY,
           head.crownY, head.headHeight(), 1.751f / head.headHeight());
    printf("  eye line %.4f  brow %.4f  nose base %.4f  mouth %.4f\n", head.eyeY, head.browY,
           head.noseBaseY, head.mouthY);

    // ------------------------------------------------------- the surface ---
    printf("rasterizing the body into chart space (%u^2)...\n", sheet);
    std::vector<skin::SurfaceTexel> surface;
    skin::buildSurfaceMap(body, sheet, surface);
    size_t covered = 0;
    for (const skin::SurfaceTexel& t : surface) {
        if (t.valid()) covered++;
    }
    printf("  %zu of %zu texels carry surface (%.1f%% of the sheet)\n", covered, surface.size(),
           100.0 * double(covered) / double(surface.size()));

    // What the face actually gets, in texels. The features a face needs are
    // small in millimetres, so this number decides whether they can be drawn
    // at all — and it is the evidence behind the density question in
    // docs/status/textures.md, not an opinion about the render.
    size_t faceTexels = 0;
    for (const skin::SurfaceTexel& t : surface) {
        if (t.region == uint8_t(BodyRegion::Face)) faceTexels++;
    }
    {
        const float across = 2.0f * head.halfWidth;
        const float density = std::sqrt(float(faceTexels) / 0.0674f);  // Face is 0.0674 m2
        printf("  Face island: %zu texels for %.4f m2 -> %.0f px/m\n", faceTexels, 0.0674, density);
        printf("  the face is %.0f mm across = %.0f texels; at that density an eye opening\n"
               "  (30 mm) is %.1f texels wide, an iris (11.7 mm) %.1f, a pupil (4 mm) %.1f\n",
               across * 1000.0f, across * density, 0.030f * density, 0.0117f * density,
               0.004f * density);
    }

    // ----------------------------------------------------- the generator ---
    skin::SkinParams params;
    // One person. Task 15.3 binds these to the Genome's phenotype; for the
    // owner's first look they are a single set, chosen to land mid-range so
    // the ruling is not made on an extreme.
    params.melanin = 0.38f;
    params.haemoglobin = 0.45f;
    params.weathering = 0.40f;
    params.freckles = 0.20f;
    params.seed = 7u;

    double L, a, b;
    skin::baseLab(params, L, a, b);
    const double ita = skin::itaDegrees(L, b);
    printf("generating (melanin %.2f, haemoglobin %.2f, weathering %.2f, freckles %.2f, seed %u)\n",
           params.melanin, params.haemoglobin, params.weathering, params.freckles, params.seed);
    printf("  base colour CIELAB L*=%.1f a*=%.1f b*=%.1f\n", L, a, b);
    printf("  ITA %.1f deg -> Fitzpatrick %s\n", ita, skin::fitzpatrickBand(ita));

    skin::SkinMaps maps;
    skin::generate(body, surface.data(), sheet, head, params, maps);

    const char* reason = nullptr;
    if (!validateTexture(maps.albedo, &reason)) {
        fprintf(stderr, "FAIL: generated albedo is malformed: %s\n", reason);
        return 1;
    }
    if (!validateTexture(maps.packed, &reason)) {
        fprintf(stderr, "FAIL: generated packed map is malformed: %s\n", reason);
        return 1;
    }
    printf("  albedo: %u^2, %zu mips, %.2f MiB uncompressed (sRGB)\n", maps.albedo.width,
           maps.albedo.mips.size(), double(maps.albedo.byteSize()) / (1024.0 * 1024.0));
    printf("  packed: %u^2, %zu mips, %.2f MiB uncompressed (linear)\n", maps.packed.width,
           maps.packed.mips.size(), double(maps.packed.byteSize()) / (1024.0 * 1024.0));

    writeSheet(outDir + "/skin_sheet.ppm", maps.albedo);
    writeSheet(outDir + "/skin_packed.ppm", maps.packed);

    // ---------------------------------------------------------- rendering --
    BudgetRegistry budgets;
    VulkanDevice device(budgets);
    if (!device.init(VulkanDeviceConfig{})) {
        fprintf(stderr, "SKIP: no Vulkan device (install libvulkan-dev mesa-vulkan-drivers)\n");
        return 0;
    }
    printf("device: %s\n", device.deviceName());

    Renderer renderer(device);
    RendererConfig config;
    config.width = 900;
    config.height = 900;
    // A single key light from the front-left and above: the lighting a portrait
    // is judged under, and the one that shows whether a surface has any life.
    config.lightDir[0] = -0.45f;
    config.lightDir[1] = 0.65f;
    config.lightDir[2] = -0.62f;
    if (!renderer.init(config)) {
        fprintf(stderr, "FAIL: renderer\n");
        return 1;
    }

    GpuTexture albedoGpu{}, packedGpu{};
    if (!renderer.uploadTexture(maps.albedo, albedoGpu) ||
        !renderer.uploadTexture(maps.packed, packedGpu)) {
        fprintf(stderr, "FAIL: texture upload refused (GPU texture budget)\n");
        return 1;
    }
    GpuMaterial skinMaterial;
    if (!renderer.createMaterial(&albedoGpu, &packedGpu, skinMaterial)) {
        fprintf(stderr, "FAIL: createMaterial\n");
        return 1;
    }

    // Bind pose, template proportions: the body as delivered.
    Pose pose;
    Mat4 palette[kJointCount];
    float shape[kMorphCount] = {0};
    buildSkinPalette(templateVariant(), pose, palette);

    MeshData posed;
    skinMesh(body, palette, shape, posed);
    carryUvs(body, posed);
    LodMesh lod;
    lod.lods.push_back(posed);
    lod.computeBounds();

    GpuLodMesh gpuBody;
    if (!renderer.uploadLodMesh(lod, gpuBody)) {
        fprintf(stderr, "FAIL: mesh upload\n");
        return 1;
    }

    DrawItem figure;
    figure.mesh = &gpuBody;
    figure.model = Mat4::translation({0, 0, 0});
    figure.worldBounds = Aabb::fromCenterExtents({0, 0.9f, 0}, {1.5f, 1.5f, 1.5f});
    figure.lodReference = {0, 0, 0};
    figure.surface = &skinMaterial;
    std::vector<DrawItem> items{figure};

    bool ok = true;
    // The face looks toward -Z, so the camera stands in front of it there.
    {
        Camera camera;
        const Vec3 target{0, head.eyeY - head.headHeight() * 0.10f, 0};
        camera.fovYRadians = 35.0f * kPi / 180.0f;
        camera.aspect = 1.0f;
        camera.nearZ = 0.02f;
        camera.farZ = 20.0f;
        camera.target = target;
        camera.eye = target + Vec3{0, 0, -0.42f};
        ok = capture(renderer, camera, items, outDir + "/skin_portrait.ppm") && ok;
    }
    {
        Camera camera;
        const Vec3 target{0, head.eyeY - head.headHeight() * 0.10f, 0};
        camera.fovYRadians = 35.0f * kPi / 180.0f;
        camera.aspect = 1.0f;
        camera.nearZ = 0.02f;
        camera.farZ = 20.0f;
        camera.target = target;
        camera.eye = target + Vec3{-0.32f, 0.05f, -0.44f};  // ~36 degrees off front
        ok = capture(renderer, camera, items, outDir + "/skin_threequarter.ppm") && ok;
    }
    {
        Camera camera;
        const Vec3 target{0, 0.95f, 0};
        camera.fovYRadians = 40.0f * kPi / 180.0f;
        camera.aspect = 1.0f;
        camera.nearZ = 0.05f;
        camera.farZ = 40.0f;
        camera.target = target;
        camera.eye = target + Vec3{0.5f, 0.15f, -2.4f};
        ok = capture(renderer, camera, items, outDir + "/skin_body.ppm") && ok;
    }

    renderer.destroyTexture(albedoGpu);
    renderer.destroyTexture(packedGpu);
    printf("%s\n", ok ? "OK" : "FAILED");
    return ok ? 0 : 1;
}
