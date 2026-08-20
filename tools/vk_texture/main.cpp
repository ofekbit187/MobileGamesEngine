// vk_texture: proves the texture RUNTIME (task 2.3/2.6, docs/TEXTURING.md).
//
// The runtime is independent of any particular UV chart, which is why this
// runs today while the body's chart is refused (docs/research/uv-audit.md):
// it textures WORLD geometry — houses, ground, props — and a synthetic-UV
// test mesh whose UVs are known exactly, so sampling can be measured rather
// than eyeballed.
//
// Four things are measured, each against a number a defect would move:
//   1. ADDRESSING  — a texture that encodes its own UV is sampled at known
//                    points; the pixel read back must BE that UV.
//   2. COLOUR SPACE— an sRGB albedo must linearise on fetch, so a known
//                    mid-grey lands where the sRGB curve says, not 2.2x off.
//   3. MIPS        — a minified checker must converge to its two colours'
//                    average; without a mip chain it aliases to one of them.
//   4. BUDGET      — textures charge their own budget, refuse at the cap, and
//                    return to zero on destroy.
//
// Writes PPM captures for the review board (● real engine output).

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "mge/core/memory.h"
#include "mge/graphics/primitives.h"
#include "mge/graphics/renderer.h"
#include "mge/graphics/texture_io.h"
#include "mge/graphics/vulkan_device.h"

#include "texture_bake.h"

using namespace mge;
using namespace mgetex;

namespace {

bool writePpm(const std::string& path, const std::vector<uint8_t>& rgba, uint32_t w, uint32_t h) {
    FILE* f = fopen(path.c_str(), "wb");
    if (f == nullptr) return false;
    fprintf(f, "P6\n%u %u\n255\n", w, h);
    for (size_t i = 0; i + 3 < rgba.size(); i += 4) fwrite(&rgba[i], 1, 3, f);
    fclose(f);
    return true;
}

const uint8_t* pixelAt(const std::vector<uint8_t>& rgba, uint32_t w, uint32_t x, uint32_t y) {
    return &rgba[(static_cast<size_t>(y) * w + x) * 4];
}

float srgbToLinear(float c) {
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

// An unlit-looking camera setup: the quad faces the light head-on so shading
// is a known constant and the measurement is about the TEXTURE, not lighting.
Camera flatCamera(uint32_t width, uint32_t height, float distance) {
    Camera camera;
    camera.eye = {0.0f, 0.0f, distance};
    camera.target = {0.0f, 0.0f, 0.0f};
    camera.aspect = static_cast<float>(width) / height;
    return camera;
}

// A quad in the XY plane facing +Z, with UV spanning 0..1 exactly once — the
// synthetic-UV test mesh. Its UVs are declared here, so what the sampler
// should return at any point is known in closed form.
MeshData makeUvQuad(float halfSize, float uvSpan) {
    MeshData m;
    const float h = halfSize;
    const Vec3 n{0, 0, 1};
    m.vertices.push_back({{-h, -h, 0}, n, {0, 0}});
    m.vertices.push_back({{h, -h, 0}, n, {uvSpan, 0}});
    m.vertices.push_back({{h, h, 0}, n, {uvSpan, uvSpan}});
    m.vertices.push_back({{-h, h, 0}, n, {0, uvSpan}});
    m.indices = {0, 1, 2, 0, 2, 3};
    m.computeBounds();
    return m;
}

struct Baked {
    TextureData data;
    GpuTexture gpu;
};

bool bakeUploadAndRoundTrip(Renderer& renderer, const SourceImage& source, TextureUsage usage,
                            const std::string& path, Baked& out, double& psnrOut) {
    if (!bakeTexture(source, TextureFormat::Rgba8, usage, out.data)) {
        fprintf(stderr, "FAIL: bake refused %s\n", path.c_str());
        return false;
    }
    psnrOut = mip0Psnr(source, out.data);

    // Through the real container, not straight into the GPU: this is what
    // proves `.mgetex` carries mips as separate byte ranges and reads back
    // byte-identical.
    if (!writeTextureFile(path.c_str(), out.data)) {
        fprintf(stderr, "FAIL: could not write %s\n", path.c_str());
        return false;
    }
    TextureData reloaded;
    if (!readTextureFile(path.c_str(), reloaded)) {
        fprintf(stderr, "FAIL: could not read back %s\n", path.c_str());
        return false;
    }
    if (reloaded.pixels != out.data.pixels || reloaded.mips.size() != out.data.mips.size() ||
        reloaded.colorSpace != out.data.colorSpace) {
        fprintf(stderr, "FAIL: %s did not round-trip\n", path.c_str());
        return false;
    }
    out.data = reloaded;
    return renderer.uploadTexture(out.data, out.gpu);
}

}  // namespace

int main(int argc, char** argv) {
    const std::string outDir = argc > 1 ? argv[1] : ".";

    BudgetRegistry budgets;
    VulkanDevice device(budgets);
    VulkanDeviceConfig deviceConfig;
    if (!device.init(deviceConfig)) return 1;
    printf("device: %s\n", device.deviceName());
    printf("texture pack: %s (ASTC=%d ETC2=%d BC=%d uncompressed=%d)\n",
           texturePackName(device.preferredTexturePack()),
           device.supportsPack(TexturePack::Astc), device.supportsPack(TexturePack::Etc2),
           device.supportsPack(TexturePack::Bc), device.supportsPack(TexturePack::Uncompressed));

    Renderer renderer(device);
    RendererConfig config;
    config.width = 960;
    config.height = 640;
    if (!renderer.init(config)) return 1;
    const size_t pixelBytes = static_cast<size_t>(config.width) * config.height * 4;

    // ---------------------------------------------------------------------
    // Bake the test set through the real container.
    // ---------------------------------------------------------------------
    constexpr uint32_t kSheet = 512;  // the standard's world-material size (§5)
    const uint8_t dark[4] = {26, 26, 26, 255};
    const uint8_t light[4] = {230, 230, 230, 255};

    Baked uvChart, checker, plaster, plank, ground, groundPacked;
    double psnr[6] = {0, 0, 0, 0, 0, 0};
    const bool baked =
        bakeUploadAndRoundTrip(renderer, makeUvChart(256), TextureUsage::Packed,
                               outDir + "/tex_uvchart.mgetex", uvChart, psnr[0]) &&
        bakeUploadAndRoundTrip(renderer, makeChecker(256, 64, dark, light), TextureUsage::Albedo,
                               outDir + "/tex_checker.mgetex", checker, psnr[1]) &&
        bakeUploadAndRoundTrip(renderer, makePlaster(kSheet, 11), TextureUsage::Albedo,
                               outDir + "/tex_plaster.mgetex", plaster, psnr[2]) &&
        bakeUploadAndRoundTrip(renderer, makePlank(kSheet, 23), TextureUsage::Albedo,
                               outDir + "/tex_plank.mgetex", plank, psnr[3]) &&
        bakeUploadAndRoundTrip(renderer, makeGround(kSheet, 7), TextureUsage::Albedo,
                               outDir + "/tex_ground.mgetex", ground, psnr[4]);
    if (!baked) return 1;
    {
        SourceImage groundSource = makeGround(kSheet, 7);
        if (!bakeUploadAndRoundTrip(renderer, makePacked(groundSource, 0.85f),
                                    TextureUsage::Packed, outDir + "/tex_ground_packed.mgetex",
                                    groundPacked, psnr[5])) {
            return 1;
        }
    }

    printf("baked %u textures: %u%s mip 0, full chains to 1x1, PSNR %.1f dB (lossless pack)\n", 6,
           kSheet, "²", psnr[2]);
    printf("  container: %zu mip levels for %ux%u, each its own byte range\n",
           plaster.data.mips.size(), plaster.data.width, plaster.data.height);

    // Per-mip streaming read: pull ONE level without touching the rest — the
    // reason mips are separate byte ranges (TEXTURING §7, ADR 0003 v2).
    TextureInfo info;
    std::vector<uint8_t> mip4;
    if (!readTextureInfo((outDir + "/tex_plaster.mgetex").c_str(), info) ||
        !readTextureMip((outDir + "/tex_plaster.mgetex").c_str(), info, 4, mip4)) {
        fprintf(stderr, "FAIL: per-mip read\n");
        return 1;
    }
    const size_t fullBytes = plaster.data.pixels.size();
    printf("  streamed mip 4 alone: %zu B of %zu B total (%.1f%% of the file)\n", mip4.size(),
           fullBytes, 100.0 * mip4.size() / fullBytes);

    const BudgetStats afterUpload = device.textureBudgetStats();
    printf("texture budget: %zu KiB used of %zu MiB cap (%zu KiB peak)\n",
           afterUpload.usedBytes / 1024, afterUpload.capBytes / (1024 * 1024),
           afterUpload.peakBytes / 1024);

    // ---------------------------------------------------------------------
    // 1. ADDRESSING — sample a texture that encodes its own UV.
    // ---------------------------------------------------------------------
    GpuMaterial uvMaterial;
    if (!renderer.createMaterial(&uvChart.gpu, nullptr, uvMaterial)) return 1;
    GpuLodMesh quadMesh;
    {
        LodMesh lod;
        lod.lods.push_back(makeUvQuad(1.0f, 1.0f));
        lod.computeBounds();
        if (!renderer.uploadLodMesh(lod, quadMesh)) return 1;
    }
    DrawItem quadItem;
    quadItem.mesh = &quadMesh;
    quadItem.worldBounds = Aabb::fromCenterExtents({0, 0, 0}, {2, 2, 2});
    quadItem.surface = &uvMaterial;
    // Light the quad head-on and drop ambient shaping out of the measurement.
    // Head-on light at an intensity that makes the shading factor exactly 1
    // in R and G: the quad's normal is +Z so hemi = 0.5 and ambient is 0.25 in
    // both channels, leaving 0.75 for the direct term. The framebuffer then
    // holds the sampled texture value itself, and the measurement below is
    // about the SAMPLER rather than about the lighting model.
    RendererConfig flatConfig = config;
    flatConfig.lightDir[0] = 0;
    flatConfig.lightDir[1] = 0;
    flatConfig.lightDir[2] = 1;
    flatConfig.lightIntensity = 0.75f;
    Renderer flat(device);
    if (!flat.init(flatConfig)) return 1;
    GpuMaterial flatUvMaterial;
    GpuLodMesh flatQuad;
    {
        LodMesh lod;
        lod.lods.push_back(makeUvQuad(1.0f, 1.0f));
        lod.computeBounds();
        if (!flat.uploadLodMesh(lod, flatQuad)) return 1;
    }
    GpuTexture flatUvTexture;
    if (!flat.uploadTexture(uvChart.data, flatUvTexture)) return 1;
    if (!flat.createMaterial(&flatUvTexture, nullptr, flatUvMaterial)) return 1;
    DrawItem flatItem = quadItem;
    flatItem.mesh = &flatQuad;
    flatItem.surface = &flatUvMaterial;

    // Camera framing the quad exactly: an orthographic-like tight fit via a
    // narrow FOV far away keeps the mapping from pixel to UV simple.
    Camera uvCamera = flatCamera(config.width, config.height, 3.0f);
    uvCamera.fovYRadians = 2.0f * std::atan(1.0f / 3.0f);
    if (!flat.renderFrame(uvCamera, &flatItem, 1)) return 1;
    std::vector<uint8_t> uvPixels(pixelBytes);
    if (!flat.readback(uvPixels.data(), uvPixels.size())) return 1;
    writePpm(outDir + "/tex_uv_addressing.ppm", uvPixels, config.width, config.height);

    // The quad's vertical extent fills the frame height exactly; sample a
    // column of points and compare the decoded UV against the geometric one.
    double worstUvError = 0;
    int uvSamples = 0;
    for (int i = 1; i < 10; ++i) {
        const uint32_t py = static_cast<uint32_t>(config.height * i / 10);
        const uint32_t px = config.width / 2;
        const uint8_t* p = pixelAt(uvPixels, config.width, px, py);
        // v runs bottom-up in the texture, top-down in the framebuffer.
        const double expectedV = 1.0 - static_cast<double>(py) / config.height;
        const double gotV = p[1] / 255.0;
        const double gotU = p[0] / 255.0;
        worstUvError = std::fmax(worstUvError, std::fabs(gotV - expectedV));
        worstUvError = std::fmax(worstUvError, std::fabs(gotU - 0.5));
        ++uvSamples;
    }
    printf("addressing: %d samples across the UV quad, worst error %.4f of a UV unit\n",
           uvSamples, worstUvError);

    // ---------------------------------------------------------------------
    // 2. COLOUR SPACE — an sRGB albedo must linearise on fetch.
    // ---------------------------------------------------------------------
    const uint8_t grey[4] = {188, 188, 188, 255};  // sRGB 0.737 -> linear 0.502
    SourceImage flatGrey = makeChecker(4, 1, grey, grey);
    Baked greySrgb;
    double greyPsnr = 0;
    if (!bakeUploadAndRoundTrip(flat, flatGrey, TextureUsage::Albedo,
                                outDir + "/tex_grey.mgetex", greySrgb, greyPsnr)) {
        return 1;
    }
    GpuMaterial greyMaterial;
    if (!flat.createMaterial(&greySrgb.gpu, nullptr, greyMaterial)) return 1;
    DrawItem greyItem = flatItem;
    greyItem.surface = &greyMaterial;
    if (!flat.renderFrame(uvCamera, &greyItem, 1)) return 1;
    std::vector<uint8_t> greyPixels(pixelBytes);
    flat.readback(greyPixels.data(), greyPixels.size());
    const uint8_t* centre = pixelAt(greyPixels, config.width, config.width / 2, config.height / 2);
    // The shading factor is 1, so the framebuffer holds the LINEARISED albedo.
    // If the hardware were not linearising an sRGB-typed fetch, the same texel
    // would arrive as its raw 188 instead — a 60-count difference no rounding
    // explains, which is what makes this a test rather than a hope.
    const double sampledLinear = srgbToLinear(grey[0] / 255.0f);
    const double expectedByte = sampledLinear * 255.0;
    const double colorSpaceError = std::fabs(centre[0] - expectedByte);
    printf("colour space: sRGB %u fetched as linear %.3f -> framebuffer %u"
           " (expected %.0f, un-linearised would be %u); error %.1f\n",
           grey[0], sampledLinear, centre[0], expectedByte, grey[0], colorSpaceError);

    // ---------------------------------------------------------------------
    // 3. MIPS — a minified checker converges to the average of its colours.
    // ---------------------------------------------------------------------
    GpuTexture flatChecker;
    if (!flat.uploadTexture(checker.data, flatChecker)) return 1;
    GpuMaterial checkerMaterial;
    if (!flat.createMaterial(&flatChecker, nullptr, checkerMaterial)) return 1;
    DrawItem checkerItem = flatItem;
    checkerItem.surface = &checkerMaterial;
    if (!flat.renderFrame(uvCamera, &checkerItem, 1)) return 1;
    std::vector<uint8_t> nearPixels(pixelBytes);
    flat.readback(nearPixels.data(), nearPixels.size());
    writePpm(outDir + "/tex_checker_near.ppm", nearPixels, config.width, config.height);

    // Same quad, pushed far enough that one screen pixel covers many texels.
    // The checker is 4 texels per square, so a correct chain has averaged the
    // pattern away by mip 3 — which is the level a 40-pixel-tall quad selects.
    Camera farCamera = uvCamera;
    farCamera.eye = {0.0f, 0.0f, 48.0f};
    if (!flat.renderFrame(farCamera, &checkerItem, 1)) return 1;
    std::vector<uint8_t> farPixels(pixelBytes);
    flat.readback(farPixels.data(), farPixels.size());
    writePpm(outDir + "/tex_checker_far.ppm", farPixels, config.width, config.height);

    // Measure the spread of the minified quad's pixels: a correct mip chain
    // makes them all near the average; aliasing leaves them at the extremes.
    double minV = 1e9, maxV = -1e9, meanV = 0;
    int counted = 0;
    for (uint32_t y = config.height / 2 - 4; y <= config.height / 2 + 4; ++y) {
        for (uint32_t x = config.width / 2 - 4; x <= config.width / 2 + 4; ++x) {
            const uint8_t* p = pixelAt(farPixels, config.width, x, y);
            const double v = p[0] / 255.0;
            minV = std::fmin(minV, v);
            maxV = std::fmax(maxV, v);
            meanV += v;
            ++counted;
        }
    }
    meanV /= counted;
    printf("mips: %u levels; minified checker spread %.4f (min %.3f max %.3f mean %.3f)\n",
           flatChecker.mipLevels, maxV - minV, minV, maxV, meanV);

    // ---------------------------------------------------------------------
    // 4. The scene: textured world geometry through the real pipeline.
    // ---------------------------------------------------------------------
    GpuMaterial plasterMaterial, plankMaterial, groundMaterial;
    if (!renderer.createMaterial(&plaster.gpu, nullptr, plasterMaterial) ||
        !renderer.createMaterial(&plank.gpu, nullptr, plankMaterial) ||
        !renderer.createMaterial(&ground.gpu, &groundPacked.gpu, groundMaterial)) {
        return 1;
    }
    // One texture per metre is the primitives' UV convention; these scale it
    // to the density the surface wants.
    plasterMaterial.uvScale = 0.5f;   // a 2 m plaster tile
    plankMaterial.uvScale = 1.0f;     // a 1 m plank tile
    groundMaterial.uvScale = 0.25f;   // a 4 m ground tile
    groundMaterial.roughness = 0.9f;

    GpuLodMesh groundMesh, wallMesh, roofMesh, barrelMesh;
    auto upload = [&](const MeshData& data, GpuLodMesh& out) {
        LodMesh lod;
        lod.lods.push_back(data);
        lod.computeBounds();
        return renderer.uploadLodMesh(lod, out);
    };
    if (!upload(makePlane(60.0f, 60.0f), groundMesh) ||
        !upload(makeBox({4.0f, 3.0f, 5.0f}), wallMesh) ||
        !upload(makeBox({4.6f, 0.5f, 5.6f}), roofMesh) ||
        !upload(makeCylinder(0.45f, 1.1f), barrelMesh)) {
        return 1;
    }

    std::vector<DrawItem> scene;
    auto place = [&](GpuLodMesh& mesh, const GpuMaterial& surface, Vec3 position, float r,
                     float g, float b) {
        DrawItem item;
        item.mesh = &mesh;
        Transform xf;
        xf.position = position;
        item.model = xf.toMatrix();
        const Vec3 e = mesh.bounds.extents();
        const float radius = e.length();
        item.worldBounds = Aabb::fromCenterExtents(position + mesh.bounds.center(),
                                                   {radius, radius, radius});
        item.lodReference = position;
        item.baseColor[0] = r;
        item.baseColor[1] = g;
        item.baseColor[2] = b;
        item.surface = &surface;
        scene.push_back(item);
    };
    place(groundMesh, groundMaterial, {0, 0, 0}, 1, 1, 1);
    // A row of cottages: plaster walls, plank roofs — the modular library the
    // standard calls for, not a unique texture per building (§5).
    for (int i = 0; i < 3; ++i) {
        const float x = -6.5f + i * 6.5f;
        place(wallMesh, plasterMaterial, {x, 1.5f, -2.0f}, 1, 1, 1);
        place(roofMesh, plankMaterial, {x, 3.25f, -2.0f}, 1, 1, 1);
    }
    place(barrelMesh, plankMaterial, {-2.2f, 0.55f, 2.4f}, 1, 1, 1);
    place(barrelMesh, plankMaterial, {-1.2f, 0.55f, 2.9f}, 1, 1, 1);

    Camera sceneCamera;
    sceneCamera.eye = {5.5f, 4.2f, 11.0f};
    sceneCamera.target = {0.0f, 1.4f, -1.0f};
    sceneCamera.aspect = static_cast<float>(config.width) / config.height;
    RenderStats stats;
    if (!renderer.renderFrame(sceneCamera, scene.data(), scene.size(), &stats)) return 1;
    std::vector<uint8_t> scenePixels(pixelBytes);
    renderer.readback(scenePixels.data(), scenePixels.size());
    writePpm(outDir + "/tex_scene.ppm", scenePixels, config.width, config.height);

    // How much of the frame is textured surface rather than sky.
    size_t surfacePixels = 0;
    for (size_t i = 0; i + 3 < scenePixels.size(); i += 4) {
        if (scenePixels[i] != 135 || scenePixels[i + 1] != 168 || scenePixels[i + 2] != 214) {
            ++surfacePixels;
        }
    }
    printf("scene: %u draws submitted, %u drawn, %u culled; %.1f%% of the frame is"
           " textured surface\n",
           stats.submitted, stats.drawn, stats.culled,
           100.0 * surfacePixels / (pixelBytes / 4));

    const BudgetStats sceneBudget = device.textureBudgetStats();
    const BudgetStats meshBudget = device.gpuBudgetStats();
    printf("budgets: textures %zu KiB / %zu MiB cap, meshes+buffers %zu KiB\n",
           sceneBudget.usedBytes / 1024, sceneBudget.capBytes / (1024 * 1024),
           meshBudget.usedBytes / 1024);

    // ---------------------------------------------------------------------
    // 5. The cap refuses, and destroy returns to zero.
    // ---------------------------------------------------------------------
    BudgetRegistry tinyBudgets;
    VulkanDevice tinyDevice(tinyBudgets);
    VulkanDeviceConfig tinyConfig;
    tinyConfig.textureBudgetBytes = 64 * 1024;  // 64 KiB: smaller than one sheet
    bool refused = false;
    size_t tinyResidual = 1;
    if (tinyDevice.init(tinyConfig)) {
        Renderer tinyRenderer(tinyDevice);
        RendererConfig tinyRenderConfig;
        tinyRenderConfig.width = 64;
        tinyRenderConfig.height = 64;
        if (tinyRenderer.init(tinyRenderConfig)) {
            GpuTexture tooBig;
            refused = !tinyRenderer.uploadTexture(plaster.data, tooBig);
            if (!refused) tinyRenderer.destroyTexture(tooBig);
            tinyRenderer.shutdown();
        }
        tinyResidual = tinyDevice.textureBudgetStats().usedBytes;
        tinyDevice.shutdown();
    }
    printf("cap: a %ux%u sheet against a 64 KiB texture budget was %s; residual %zu B\n", kSheet,
           kSheet, refused ? "REFUSED" : "ACCEPTED (wrong)", tinyResidual);

    // Tear down and prove the texture budget returns to zero.
    renderer.destroyMaterial(plasterMaterial);
    renderer.destroyMaterial(plankMaterial);
    renderer.destroyMaterial(groundMaterial);
    renderer.destroyMaterial(uvMaterial);
    renderer.destroyTexture(uvChart.gpu);
    renderer.destroyTexture(checker.gpu);
    renderer.destroyTexture(plaster.gpu);
    renderer.destroyTexture(plank.gpu);
    renderer.destroyTexture(ground.gpu);
    renderer.destroyTexture(groundPacked.gpu);
    renderer.destroyLodMesh(quadMesh);
    renderer.destroyLodMesh(groundMesh);
    renderer.destroyLodMesh(wallMesh);
    renderer.destroyLodMesh(roofMesh);
    renderer.destroyLodMesh(barrelMesh);
    renderer.shutdown();

    flat.destroyMaterial(flatUvMaterial);
    flat.destroyMaterial(greyMaterial);
    flat.destroyMaterial(checkerMaterial);
    flat.destroyTexture(flatUvTexture);
    flat.destroyTexture(greySrgb.gpu);
    flat.destroyTexture(flatChecker);
    flat.destroyLodMesh(flatQuad);
    flat.shutdown();

    const size_t textureResidual = device.textureBudgetStats().usedBytes;
    const size_t gpuResidual = device.gpuBudgetStats().usedBytes;
    device.shutdown();
    printf("after teardown: texture budget %zu B, gpu budget %zu B\n", textureResidual,
           gpuResidual);

    // A sampler that addresses correctly is exact to within the 8-bit
    // quantization of the chart plus one texel of filtering.
    const bool ok = worstUvError < 0.01 && (maxV - minV) < 0.05 && colorSpaceError < 3.0 &&
                    surfacePixels > 0 && stats.drawn == scene.size() && refused &&
                    tinyResidual == 0 && textureResidual == 0 && gpuResidual == 0;
    if (!ok) {
        fprintf(stderr,
                "FAIL: uvError=%.4f mipSpread=%.4f colourSpaceError=%.1f surface=%zu"
                " drawn=%u refused=%d residual=%zu/%zu\n",
                worstUvError, maxV - minV, colorSpaceError, surfacePixels, stats.drawn,
                refused ? 1 : 0, textureResidual, gpuResidual);
        return 1;
    }
    printf("OK\n");
    return 0;
}
