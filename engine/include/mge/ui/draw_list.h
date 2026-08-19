#pragma once

// UI draw list (task 5.2): fixed-capacity quad batch the widget layer fills
// each frame and the renderer uploads in one go — allocation-free per frame
// (P1). All coordinates are pixels, y down.

#include <cstddef>
#include <cstdint>

#include "mge/ui/font.h"
#include "mge/ui/text.h"

namespace mge {

struct UiRect {
    float x = 0, y = 0, w = 0, h = 0;
    bool contains(float px, float py) const {
        return px >= x && px < x + w && py >= y && py < y + h;
    }
};

struct UiVertex {
    float x, y;
    float u, v;
    float r, g, b, a;
};

enum class TextAlign : uint8_t { Left, Center, Right };

class UiDrawList {
public:
    static constexpr size_t kMaxQuads = 8192;

    void clear() { quadCount_ = 0; }
    size_t quadCount() const { return quadCount_; }
    const UiVertex* vertices() const { return vertices_; }
    size_t vertexCount() const { return quadCount_ * 4; }

    void setFont(const FontAtlas* font) { font_ = font; }
    const FontAtlas* font() const { return font_; }

    // Solid rectangle.
    void rect(const UiRect& r, const float color[4]);
    // Rectangle outline of the given thickness (four rects).
    void border(const UiRect& r, float thickness, const float color[4]);
    // Double border — the Codex manuscript rule.
    void doubleBorder(const UiRect& r, const float color[4]);

    // Text with v1 bidi (task 5.2/P11). Returns the drawn width in px.
    // `rtlBase` controls run reordering; alignment is resolved within `r`.
    float text(const UiRect& r, const char* utf8, float scale, const float color[4],
               TextAlign align, bool rtlBase);
    float measureText(const char* utf8, float scale, bool rtlBase) const;

private:
    void pushQuad(float x0, float y0, float x1, float y1, float u0, float v0, float u1,
                  float v1, const float color[4]);

    const FontAtlas* font_ = nullptr;
    UiVertex vertices_[kMaxQuads * 4];
    size_t quadCount_ = 0;
};

}  // namespace mge
