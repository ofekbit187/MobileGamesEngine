#pragma once

// CPU-side texture data and the `.mgetex` runtime container (task 2.3/2.4,
// docs/TEXTURING.md §4). Load/bake-time representation only — the frame path
// sees a GpuTexture and a sampler, never this.
//
// Three rules from the standard shape everything here:
//
//   * A shipped texture is ALREADY block-compressed and ALREADY mipped. The
//     runtime never decodes and never generates mips: those are content, baked
//     and deterministic (TEXTURING §7).
//   * Each mip is its own byte range, so the streaming system can pull mip 4
//     without touching mip 0 — the per-LOD byte ranges ADR 0003 deferred.
//   * Colour space is a property of the TEXTURE, not of the shader. Albedo is
//     sRGB-typed so the hardware linearises for free; every data map is linear.

#include <cstddef>
#include <cstdint>
#include <vector>

namespace mge {

// The GPU formats the engine can carry. One `.mgetex` holds ONE format: the
// bake emits a file per pack and Play's texture targeting ships the single
// pack a device can sample (TEXTURING §4).
//
// Order is a file-format contract: appending is safe, reordering re-tags every
// baked texture in existence.
enum class TextureFormat : uint8_t {
    Rgba8 = 0,     // uncompressed reference/dev pack — 32 bpp
    Bc1Rgb,        // desktop/dev pack — 4 bpp
    Bc3Rgba,       // desktop/dev pack with alpha — 8 bpp
    Bc7Rgba,       // desktop/dev pack, high quality — 8 bpp
    Astc4x4,       // near-field albedo — 8.00 bpp
    Astc5x5,       // normal maps (XXXY swizzle) — 5.12 bpp
    Astc6x6,       // standard albedo / packed — 3.56 bpp
    Astc8x8,       // large distant surfaces — 2.00 bpp
    Etc2Rgb8,      // ETC2 pack albedo — 4.0 bpp
    Etc2Rgba8,     // ETC2 pack with alpha — 8.0 bpp
    EacRg11,       // ETC2 pack normal maps — 8.0 bpp
    Count,
};

// Which pack a format belongs to. A device samples exactly one pack.
enum class TexturePack : uint8_t {
    Uncompressed = 0,  // always available; the verification and fallback pack
    Bc,                // desktop and llvmpipe
    Astc,              // >80% of Play devices — the primary shipping pack
    Etc2,              // >95% of Play devices — the fallback shipping pack
    Count,
};

// sRGB is a property of the texture. Getting this wrong is the single most
// common texture defect in shipping games (TEXTURING §4), so it is stored,
// not inferred.
enum class ColorSpace : uint8_t { Linear = 0, Srgb };

// What the texture is FOR. Decides the block size a baker picks and the
// colour space a loader asserts (TEXTURING §3).
enum class TextureUsage : uint8_t {
    Albedo = 0,  // sRGB, base colour (+ alpha cutout)
    Packed,      // linear, R=AO G=roughness B=mask — the fixed ORM order
    Normal,      // linear, XY tangent-space
    Ui,          // sRGB, the Codex sheet
    Count,
};

const char* textureFormatName(TextureFormat format);
const char* texturePackName(TexturePack pack);
TexturePack packOf(TextureFormat format);

// Block footprint of a format. Uncompressed formats report 1x1.
void blockExtent(TextureFormat format, uint32_t& width, uint32_t& height);
uint32_t blockBytes(TextureFormat format);

// Bytes one mip level occupies: block-aligned, which is why a 6x6 mip of a
// 5x5 image still costs one whole block.
size_t mipByteSize(TextureFormat format, uint32_t width, uint32_t height);

// Levels in a full chain down to 1x1. Every shipped texture has all of them.
uint32_t fullMipCount(uint32_t width, uint32_t height);

// One mip level's slice of the payload.
struct TextureMip {
    uint32_t width = 0;
    uint32_t height = 0;
    uint64_t offset = 0;  // byte offset into TextureData::pixels
    uint64_t size = 0;
};

struct TextureData {
    uint32_t width = 0;
    uint32_t height = 0;
    TextureFormat format = TextureFormat::Rgba8;
    ColorSpace colorSpace = ColorSpace::Linear;
    TextureUsage usage = TextureUsage::Albedo;
    std::vector<TextureMip> mips;   // mips[0] is the largest
    std::vector<uint8_t> pixels;    // all levels, addressed by mips[i]

    size_t byteSize() const { return pixels.size(); }
    bool empty() const { return mips.empty() || pixels.empty(); }
    void clear() {
        *this = TextureData{};
    }
};

// True when the declared mip chain is complete, in descending order, block
// sized, and entirely inside the payload. This is what a loader checks before
// it hands anything to the GPU — a malformed texture is refused, not clamped.
bool validateTexture(const TextureData& texture, const char** reason);

}  // namespace mge
