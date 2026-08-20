// Texture container and format rules (docs/TEXTURING.md, task 2.3/2.4).
// Everything here is CPU-side, so it runs in the arm64/QEMU tier too — the
// GPU half is measured by tools/vk_texture.

#include <cstdlib>
#include <cstring>
#include <string>

#include "mge/graphics/primitives.h"
#include "mge/graphics/texture_io.h"
#include "test_framework.h"

using namespace mge;

namespace {

std::string tmpPath(const char* name) {
    std::string dir = "/tmp";
    if (const char* t = getenv("TMPDIR")) dir = t;
    return dir + "/" + name;
}

// A texture with a full, correctly-sized mip chain in the given format.
TextureData makeChain(uint32_t size, TextureFormat format, ColorSpace space) {
    TextureData texture;
    texture.width = size;
    texture.height = size;
    texture.format = format;
    texture.colorSpace = space;
    texture.usage = TextureUsage::Albedo;
    uint32_t w = size, h = size;
    const uint32_t levels = fullMipCount(size, size);
    for (uint32_t i = 0; i < levels; ++i) {
        TextureMip mip;
        mip.width = w;
        mip.height = h;
        mip.offset = texture.pixels.size();
        mip.size = mipByteSize(format, w, h);
        texture.pixels.resize(texture.pixels.size() + mip.size,
                              static_cast<uint8_t>(0x40 + i));
        texture.mips.push_back(mip);
        w = w > 1 ? w / 2 : 1;
        h = h > 1 ? h / 2 : 1;
    }
    return texture;
}

}  // namespace

MGE_TEST(mip_chain_reaches_one_by_one) {
    // "Every texture ships a full mip chain, down to 1x1. No exceptions, not
    // even UI." A 1024 texture is 11 levels, not 10.
    MGE_CHECK(fullMipCount(1, 1) == 1);
    MGE_CHECK(fullMipCount(512, 512) == 10);
    MGE_CHECK(fullMipCount(1024, 1024) == 11);
    // Non-square chains keep halving until BOTH sides bottom out.
    MGE_CHECK(fullMipCount(256, 64) == 9);
}

MGE_TEST(block_footprints_match_the_standard) {
    // The bpp figures docs/TEXTURING.md §4 budgets against. A 6x6 ASTC block
    // is 16 bytes over 36 texels = 3.56 bpp.
    MGE_CHECK(mipByteSize(TextureFormat::Rgba8, 4, 4) == 64);
    MGE_CHECK(mipByteSize(TextureFormat::Astc6x6, 6, 6) == 16);
    MGE_CHECK(mipByteSize(TextureFormat::Astc4x4, 4, 4) == 16);
    MGE_CHECK(mipByteSize(TextureFormat::Etc2Rgb8, 4, 4) == 8);
    // A partial block still costs a whole block — which is why small mips of
    // a large-block format stop shrinking.
    MGE_CHECK(mipByteSize(TextureFormat::Astc6x6, 1, 1) == 16);
    MGE_CHECK(mipByteSize(TextureFormat::Astc8x8, 9, 9) == 4 * 16);
}

MGE_TEST(astc_sheet_costs_what_the_budget_says) {
    // The standard budgets a 1024² skin sheet at 0.59 MiB in ASTC with mips.
    size_t total = 0;
    uint32_t w = 1024, h = 1024;
    for (uint32_t i = 0; i < fullMipCount(1024, 1024); ++i) {
        total += mipByteSize(TextureFormat::Astc6x6, w, h);
        w = w > 1 ? w / 2 : 1;
        h = h > 1 ? h / 2 : 1;
    }
    const double mib = static_cast<double>(total) / (1024 * 1024);
    MGE_CHECK(mib > 0.55 && mib < 0.63);
    // And the same sheet uncompressed is the 5.3 MiB the standard rejects.
    size_t raw = 0;
    w = 1024;
    h = 1024;
    for (uint32_t i = 0; i < fullMipCount(1024, 1024); ++i) {
        raw += mipByteSize(TextureFormat::Rgba8, w, h);
        w = w > 1 ? w / 2 : 1;
        h = h > 1 ? h / 2 : 1;
    }
    MGE_CHECK(static_cast<double>(raw) / (1024 * 1024) > 5.0);
}

MGE_TEST(texture_file_roundtrip_preserves_every_mip) {
    const TextureData original = makeChain(64, TextureFormat::Rgba8, ColorSpace::Srgb);
    const std::string path = tmpPath("mge_roundtrip.mgetex");
    MGE_CHECK(writeTextureFile(path.c_str(), original));

    TextureData loaded;
    MGE_CHECK(readTextureFile(path.c_str(), loaded));
    MGE_CHECK(loaded.width == original.width);
    MGE_CHECK(loaded.format == original.format);
    // Colour space is stored, never inferred: a data map that came back as
    // sRGB would be the defect the standard calls the most common of all.
    MGE_CHECK(loaded.colorSpace == ColorSpace::Srgb);
    MGE_CHECK(loaded.mips.size() == original.mips.size());
    MGE_CHECK(loaded.pixels == original.pixels);
    for (size_t i = 0; i < loaded.mips.size(); ++i) {
        MGE_CHECK(loaded.mips[i].width == original.mips[i].width);
        MGE_CHECK(loaded.mips[i].offset == original.mips[i].offset);
        MGE_CHECK(loaded.mips[i].size == original.mips[i].size);
    }
}

MGE_TEST(one_mip_reads_without_the_rest) {
    // Streaming is BY MIP, not by texture: a distant chunk holds mip 4 and
    // pulls finer levels on approach. That requires reading one range.
    const TextureData original = makeChain(256, TextureFormat::Rgba8, ColorSpace::Srgb);
    const std::string path = tmpPath("mge_permip.mgetex");
    MGE_CHECK(writeTextureFile(path.c_str(), original));

    TextureInfo info;
    MGE_CHECK(readTextureInfo(path.c_str(), info));
    MGE_CHECK(info.width == 256);
    MGE_CHECK(info.mips.size() == fullMipCount(256, 256));

    std::vector<uint8_t> level;
    MGE_CHECK(readTextureMip(path.c_str(), info, 4, level));
    MGE_CHECK(level.size() == original.mips[4].size);
    // The bytes are that level's and no other's.
    MGE_CHECK(memcmp(level.data(), &original.pixels[original.mips[4].offset], level.size()) == 0);
    // And it is a small fraction of the file — the point of the layout.
    MGE_CHECK(level.size() * 20 < original.pixels.size());
}

MGE_TEST(malformed_textures_are_refused_not_clamped) {
    const char* reason = "";

    // A chain that stops short of 1x1 aliases into shimmer at distance.
    TextureData partial = makeChain(64, TextureFormat::Rgba8, ColorSpace::Srgb);
    partial.mips.pop_back();
    MGE_CHECK(!validateTexture(partial, &reason));

    // A mip whose declared size does not match its block footprint would have
    // the GPU read past the end of the payload.
    TextureData wrongSize = makeChain(64, TextureFormat::Rgba8, ColorSpace::Srgb);
    wrongSize.mips[2].size += 4;
    MGE_CHECK(!validateTexture(wrongSize, &reason));

    // A range that runs past the payload.
    TextureData truncated = makeChain(64, TextureFormat::Rgba8, ColorSpace::Srgb);
    truncated.pixels.resize(truncated.pixels.size() - 16);
    MGE_CHECK(!validateTexture(truncated, &reason));

    // A chain that is not a halving sequence.
    TextureData notHalving = makeChain(64, TextureFormat::Rgba8, ColorSpace::Srgb);
    notHalving.mips[1].width = 40;
    MGE_CHECK(!validateTexture(notHalving, &reason));

    // The writer refuses too, so a bad texture never becomes a file.
    MGE_CHECK(!writeTextureFile(tmpPath("mge_bad.mgetex").c_str(), partial));
}

MGE_TEST(formats_map_to_their_packs) {
    // One `.mgetex` holds one pack; Play's texture targeting ships the single
    // pack a device can sample.
    MGE_CHECK(packOf(TextureFormat::Astc6x6) == TexturePack::Astc);
    MGE_CHECK(packOf(TextureFormat::Etc2Rgb8) == TexturePack::Etc2);
    MGE_CHECK(packOf(TextureFormat::EacRg11) == TexturePack::Etc2);
    MGE_CHECK(packOf(TextureFormat::Bc7Rgba) == TexturePack::Bc);
    MGE_CHECK(packOf(TextureFormat::Rgba8) == TexturePack::Uncompressed);
}

MGE_TEST(primitives_carry_texture_space) {
    // Every primitive the world is built from must have UVs, or a textured
    // world is impossible before the first authored mesh arrives. UVs are in
    // METRES, so a 4x3 wall spans 4 by 3 UV units and the material sets the
    // tiling rate.
    const MeshData box = makeBox({4, 3, 5});
    float minU = 1e9f, maxU = -1e9f, minV = 1e9f, maxV = -1e9f;
    for (const Vertex& v : box.vertices) {
        minU = v.uv[0] < minU ? v.uv[0] : minU;
        maxU = v.uv[0] > maxU ? v.uv[0] : maxU;
        minV = v.uv[1] < minV ? v.uv[1] : minV;
        maxV = v.uv[1] > maxV ? v.uv[1] : maxV;
    }
    MGE_CHECK_NEAR(maxU - minU, 5.0f, 1e-4);  // widest face spans the 5 m depth
    MGE_CHECK_NEAR(maxV - minV, 5.0f, 1e-4);

    // Texel density is uniform: a plane's UV span equals its size in metres,
    // so a wall and the floor beside it are never four times apart (§5.1).
    const MeshData plane = makePlane(60.0f, 60.0f);
    float planeSpan = 0;
    for (const Vertex& v : plane.vertices) {
        planeSpan = v.uv[0] > planeSpan ? v.uv[0] : planeSpan;
    }
    MGE_CHECK_NEAR(planeSpan, 30.0f, 1e-4);  // half-extent from a centred plane

    // A mesh merged from parts keeps its texture space.
    MeshData merged;
    appendMesh(merged, box, {10, 0, 0});
    MGE_CHECK(merged.vertices.size() == box.vertices.size());
    MGE_CHECK_NEAR(merged.vertices[0].uv[0], box.vertices[0].uv[0], 1e-6);
}
