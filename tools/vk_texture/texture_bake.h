#pragma once

// Test-texture generation and baking, for proving the texture RUNTIME.
//
// This is deliberately NOT engine code and NOT the world material library:
// authored pixels and the shipping baker belong to the textures session
// (docs/TEXTURING.md §1). What lives here is the minimum needed to hand the
// runtime real, deterministic `.mgetex` content so sampling, mips, colour
// space, formats and budgets can be measured rather than asserted.
//
// It does follow the standard's bake rules, because a test texture that
// breaks them would prove the wrong thing:
//   * full mip chain down to 1x1 (§7)
//   * mips generated in LINEAR space, then re-encoded to sRGB (§7)
//   * power-of-two, block-aligned (§4)

#include <cstdint>
#include <functional>

#include "mge/graphics/texture_data.h"

namespace mgetex {

// A source image is RGBA8 in its declared colour space.
struct SourceImage {
    uint32_t width = 0;
    uint32_t height = 0;
    mge::ColorSpace colorSpace = mge::ColorSpace::Srgb;
    std::vector<uint8_t> rgba;

    void resize(uint32_t w, uint32_t h) {
        width = w;
        height = h;
        rgba.assign(static_cast<size_t>(w) * h * 4, 0);
    }
    uint8_t* at(uint32_t x, uint32_t y) {
        return &rgba[(static_cast<size_t>(y) * width + x) * 4];
    }
    const uint8_t* at(uint32_t x, uint32_t y) const {
        return &rgba[(static_cast<size_t>(y) * width + x) * 4];
    }
};

// --- generators (TEXTURING §8: a generator, not a stored PNG) ---

// Encodes each texel's own UV in R and G. Sampling it renders the UV field
// itself, so a readback can be checked against the UV the geometry declared —
// which is how "does the sampler address what I think it does" becomes a
// measurement instead of an opinion.
SourceImage makeUvChart(uint32_t size);

// A checkerboard. Minified, a correct mip chain converges it to the average
// of its two colours; a missing or wrong chain aliases instead. That is the
// mip test.
SourceImage makeChecker(uint32_t size, uint32_t squares, const uint8_t a[4],
                        const uint8_t b[4]);

// Tiling world surfaces, in the standard's medieval earth-pigment range:
// value does the work, no saturated primaries (§2).
SourceImage makePlaster(uint32_t size, uint32_t seed);
SourceImage makePlank(uint32_t size, uint32_t seed);
SourceImage makeGround(uint32_t size, uint32_t seed);

// A packed AO/roughness/mask map in the fixed ORM channel order (§3).
SourceImage makePacked(const SourceImage& albedo, float roughness);

// --- bake ---

// Builds the full mip chain and emits a TextureData in `format`.
// Mip filtering happens in linear space whatever the source colour space is.
bool bakeTexture(const SourceImage& source, mge::TextureFormat format,
                 mge::TextureUsage usage, mge::TextureData& out);

// Peak signal-to-noise ratio of a baked texture's mip 0 against its source,
// in dB. The standard gates compression on this (§4, min 38 dB); reporting it
// is how a smeared normal map fails the bake instead of shipping.
double mip0Psnr(const SourceImage& source, const mge::TextureData& baked);

}  // namespace mgetex
