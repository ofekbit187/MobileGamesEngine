#pragma once

// Font atlas (task 5.2): bakes a TTF into a single-channel coverage atlas at
// load time (stb_truetype), covering Latin + Hebrew (P11). Per-script
// fallback chains arrive with more languages; the atlas API already takes
// explicit codepoint ranges.

#include <cstdint>
#include <vector>

namespace mge {

struct Glyph {
    // Atlas UVs.
    float u0 = 0, v0 = 0, u1 = 0, v1 = 0;
    // Placement relative to the pen position (pixels, y down).
    float offsetX = 0, offsetY = 0;
    float width = 0, height = 0;
    float advance = 0;
};

class FontAtlas {
public:
    static constexpr int kAtlasSize = 512;

    // Bakes at the given pixel height. Covers ASCII (32..126) and the Hebrew
    // block (0x5D0..0x5EA) plus punctuation used by both.
    bool bakeFromFile(const char* ttfPath, float pixelHeight);
    bool bakeFromMemory(const uint8_t* ttf, size_t size, float pixelHeight);
    // The engine-embedded default face (Liberation Serif, OFL — covers Latin
    // and Hebrew): no host font path needed, on device or off (P11).
    bool bakeEmbedded(float pixelHeight);

    const Glyph* glyph(uint32_t codepoint) const;
    float lineHeight() const { return lineHeight_; }
    float ascent() const { return ascent_; }

    // Width in pixels of a visual-order codepoint sequence.
    float measure(const uint32_t* codepoints, size_t count, float scale = 1.0f) const;

    const uint8_t* pixels() const { return pixels_.data(); }  // R8, kAtlasSize^2
    // A guaranteed-opaque texel for drawing untextured quads through the
    // same pipeline.
    void whiteUv(float& u, float& v) const;

private:
    std::vector<uint8_t> pixels_;
    std::vector<Glyph> glyphs_;
    std::vector<uint32_t> codepoints_;  // parallel to glyphs_
    float lineHeight_ = 0;
    float ascent_ = 0;
};

}  // namespace mge
