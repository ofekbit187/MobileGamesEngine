#include "mge/ui/font.h"

#include <cstdio>
#include <cstring>

#include "mge/core/log.h"

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "stb_truetype.h"

#include "fonts/liberation_serif_regular.h"

namespace mge {

namespace {
constexpr const char* kTag = "font";

struct Range {
    uint32_t first, last;
};
constexpr Range kRanges[] = {
    {32, 126},        // ASCII
    {0x05D0, 0x05EA}, // Hebrew letters
    {0x2013, 0x2014}, // dashes
};
}  // namespace

bool FontAtlas::bakeFromFile(const char* ttfPath, float pixelHeight) {
    FILE* f = fopen(ttfPath, "rb");
    if (f == nullptr) {
        MGE_LOGE(kTag, "cannot open font: %s", ttfPath);
        return false;
    }
    fseek(f, 0, SEEK_END);
    const long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> data(static_cast<size_t>(size));
    const bool ok = fread(data.data(), 1, data.size(), f) == data.size();
    fclose(f);
    return ok && bakeFromMemory(data.data(), data.size(), pixelHeight);
}

bool FontAtlas::bakeEmbedded(float pixelHeight) {
    return bakeFromMemory(k_font_liberation_serif, k_font_liberation_serif_size, pixelHeight);
}

bool FontAtlas::bakeFromMemory(const uint8_t* ttf, size_t size, float pixelHeight) {
    (void)size;
    stbtt_fontinfo font;
    if (stbtt_InitFont(&font, ttf, stbtt_GetFontOffsetForIndex(ttf, 0)) == 0) {
        MGE_LOGE(kTag, "stbtt_InitFont failed");
        return false;
    }
    const float scale = stbtt_ScaleForPixelHeight(&font, pixelHeight);
    int ascent, descent, lineGap;
    stbtt_GetFontVMetrics(&font, &ascent, &descent, &lineGap);
    ascent_ = ascent * scale;
    lineHeight_ = (ascent - descent + lineGap) * scale;

    pixels_.assign(static_cast<size_t>(kAtlasSize) * kAtlasSize, 0);
    // Reserve texel (0,0) as opaque white for solid quads.
    pixels_[0] = 255;
    pixels_[1] = 255;
    pixels_[kAtlasSize] = 255;
    pixels_[kAtlasSize + 1] = 255;

    int penX = 4, penY = 2, rowHeight = 0;
    for (const Range& range : kRanges) {
        for (uint32_t cp = range.first; cp <= range.last; ++cp) {
            const int glyphIndex = stbtt_FindGlyphIndex(&font, static_cast<int>(cp));
            if (glyphIndex == 0 && cp != ' ') continue;

            int x0, y0, x1, y1;
            stbtt_GetGlyphBitmapBox(&font, glyphIndex, scale, scale, &x0, &y0, &x1, &y1);
            const int w = x1 - x0, h = y1 - y0;
            if (penX + w + 2 >= kAtlasSize) {
                penX = 2;
                penY += rowHeight + 2;
                rowHeight = 0;
            }
            if (penY + h + 2 >= kAtlasSize) {
                MGE_LOGE(kTag, "atlas full at U+%04X", cp);
                return false;
            }
            if (w > 0 && h > 0) {
                stbtt_MakeGlyphBitmap(&font, &pixels_[penY * kAtlasSize + penX], w, h,
                                      kAtlasSize, scale, scale, glyphIndex);
            }

            int advance, leftBearing;
            stbtt_GetGlyphHMetrics(&font, glyphIndex, &advance, &leftBearing);

            Glyph glyph;
            glyph.u0 = static_cast<float>(penX) / kAtlasSize;
            glyph.v0 = static_cast<float>(penY) / kAtlasSize;
            glyph.u1 = static_cast<float>(penX + w) / kAtlasSize;
            glyph.v1 = static_cast<float>(penY + h) / kAtlasSize;
            glyph.offsetX = static_cast<float>(x0);
            glyph.offsetY = static_cast<float>(y0);  // relative to baseline
            glyph.width = static_cast<float>(w);
            glyph.height = static_cast<float>(h);
            glyph.advance = advance * scale;
            glyphs_.push_back(glyph);
            codepoints_.push_back(cp);

            penX += w + 2;
            rowHeight = h > rowHeight ? h : rowHeight;
        }
    }
    MGE_LOGI(kTag, "atlas baked: %zu glyphs at %.0fpx", glyphs_.size(), pixelHeight);
    return true;
}

const Glyph* FontAtlas::glyph(uint32_t codepoint) const {
    for (size_t i = 0; i < codepoints_.size(); ++i) {
        if (codepoints_[i] == codepoint) return &glyphs_[i];
    }
    return nullptr;
}

float FontAtlas::measure(const uint32_t* codepoints, size_t count, float scale) const {
    float width = 0;
    for (size_t i = 0; i < count; ++i) {
        const Glyph* g = glyph(codepoints[i]);
        if (g != nullptr) width += g->advance * scale;
    }
    return width;
}

void FontAtlas::whiteUv(float& u, float& v) const {
    u = 0.5f / kAtlasSize;
    v = 0.5f / kAtlasSize;
}

}  // namespace mge
