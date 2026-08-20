#include "mge/graphics/texture_data.h"

namespace mge {

namespace {

struct FormatInfo {
    const char* name;
    uint32_t blockWidth;
    uint32_t blockHeight;
    uint32_t blockBytes;
    TexturePack pack;
};

// Indexed by TextureFormat. ASTC is always 16 bytes per block whatever the
// footprint — that is the whole point of ASTC: the rate is a per-texture
// choice made by the block size, not by a different payload layout.
constexpr FormatInfo kFormats[] = {
    {"Rgba8", 1, 1, 4, TexturePack::Uncompressed},
    {"Bc1Rgb", 4, 4, 8, TexturePack::Bc},
    {"Bc3Rgba", 4, 4, 16, TexturePack::Bc},
    {"Bc7Rgba", 4, 4, 16, TexturePack::Bc},
    {"Astc4x4", 4, 4, 16, TexturePack::Astc},
    {"Astc5x5", 5, 5, 16, TexturePack::Astc},
    {"Astc6x6", 6, 6, 16, TexturePack::Astc},
    {"Astc8x8", 8, 8, 16, TexturePack::Astc},
    {"Etc2Rgb8", 4, 4, 8, TexturePack::Etc2},
    {"Etc2Rgba8", 4, 4, 16, TexturePack::Etc2},
    {"EacRg11", 4, 4, 16, TexturePack::Etc2},
};
static_assert(sizeof(kFormats) / sizeof(kFormats[0]) ==
                  static_cast<size_t>(TextureFormat::Count),
              "every TextureFormat needs a footprint");

const FormatInfo& info(TextureFormat format) {
    const size_t index = static_cast<size_t>(format);
    return kFormats[index < static_cast<size_t>(TextureFormat::Count) ? index : 0];
}

}  // namespace

const char* textureFormatName(TextureFormat format) { return info(format).name; }

TexturePack packOf(TextureFormat format) { return info(format).pack; }

const char* texturePackName(TexturePack pack) {
    switch (pack) {
        case TexturePack::Uncompressed: return "uncompressed";
        case TexturePack::Bc: return "BC";
        case TexturePack::Astc: return "ASTC";
        case TexturePack::Etc2: return "ETC2";
        default: return "?";
    }
}

void blockExtent(TextureFormat format, uint32_t& width, uint32_t& height) {
    width = info(format).blockWidth;
    height = info(format).blockHeight;
}

uint32_t blockBytes(TextureFormat format) { return info(format).blockBytes; }

size_t mipByteSize(TextureFormat format, uint32_t width, uint32_t height) {
    const FormatInfo& f = info(format);
    const size_t blocksX = (width + f.blockWidth - 1) / f.blockWidth;
    const size_t blocksY = (height + f.blockHeight - 1) / f.blockHeight;
    return blocksX * blocksY * f.blockBytes;
}

uint32_t fullMipCount(uint32_t width, uint32_t height) {
    uint32_t levels = 1;
    while (width > 1 || height > 1) {
        width = width > 1 ? width / 2 : 1;
        height = height > 1 ? height / 2 : 1;
        ++levels;
    }
    return levels;
}

bool validateTexture(const TextureData& texture, const char** reason) {
    auto fail = [&](const char* why) {
        if (reason != nullptr) *reason = why;
        return false;
    };
    if (texture.width == 0 || texture.height == 0) return fail("zero dimension");
    if (static_cast<size_t>(texture.format) >= static_cast<size_t>(TextureFormat::Count))
        return fail("unknown format");
    if (texture.mips.empty()) return fail("no mip levels");
    if (texture.mips[0].width != texture.width || texture.mips[0].height != texture.height)
        return fail("mip 0 does not match the declared size");

    uint32_t expectedWidth = texture.width;
    uint32_t expectedHeight = texture.height;
    for (size_t i = 0; i < texture.mips.size(); ++i) {
        const TextureMip& mip = texture.mips[i];
        if (mip.width != expectedWidth || mip.height != expectedHeight)
            return fail("mip chain is not a halving sequence");
        if (mip.size != mipByteSize(texture.format, mip.width, mip.height))
            return fail("mip size does not match its block footprint");
        if (mip.offset + mip.size > texture.pixels.size())
            return fail("mip range runs past the payload");
        expectedWidth = expectedWidth > 1 ? expectedWidth / 2 : 1;
        expectedHeight = expectedHeight > 1 ? expectedHeight / 2 : 1;
    }
    // The standard is explicit: a full chain to 1x1, no exceptions, not even
    // UI. A partial chain is a texture that will alias into shimmer.
    if (texture.mips.size() != fullMipCount(texture.width, texture.height))
        return fail("mip chain is not full (must reach 1x1)");
    return true;
}

}  // namespace mge
