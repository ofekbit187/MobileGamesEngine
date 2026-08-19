#include "mge/ui/draw_list.h"

namespace mge {

void UiDrawList::pushQuad(float x0, float y0, float x1, float y1, float u0, float v0, float u1,
                          float v1, const float color[4]) {
    if (quadCount_ >= kMaxQuads) return;  // full: drop, never grow (P1)
    UiVertex* v = &vertices_[quadCount_ * 4];
    v[0] = {x0, y0, u0, v0, color[0], color[1], color[2], color[3]};
    v[1] = {x1, y0, u1, v0, color[0], color[1], color[2], color[3]};
    v[2] = {x1, y1, u1, v1, color[0], color[1], color[2], color[3]};
    v[3] = {x0, y1, u0, v1, color[0], color[1], color[2], color[3]};
    ++quadCount_;
}

void UiDrawList::rect(const UiRect& r, const float color[4]) {
    float u, v;
    font_->whiteUv(u, v);
    pushQuad(r.x, r.y, r.x + r.w, r.y + r.h, u, v, u, v, color);
}

void UiDrawList::border(const UiRect& r, float t, const float color[4]) {
    rect({r.x, r.y, r.w, t}, color);
    rect({r.x, r.y + r.h - t, r.w, t}, color);
    rect({r.x, r.y + t, t, r.h - 2 * t}, color);
    rect({r.x + r.w - t, r.y + t, t, r.h - 2 * t}, color);
}

void UiDrawList::doubleBorder(const UiRect& r, const float color[4]) {
    border(r, 2.0f, color);
    border({r.x + 5, r.y + 5, r.w - 10, r.h - 10}, 1.0f, color);
}

float UiDrawList::measureText(const char* utf8, float scale, bool rtlBase) const {
    uint32_t codepoints[512];
    const size_t count = bidiReorder(utf8, rtlBase, codepoints, 512);
    return font_->measure(codepoints, count, scale);
}

float UiDrawList::text(const UiRect& r, const char* utf8, float scale, const float color[4],
                       TextAlign align, bool rtlBase) {
    uint32_t codepoints[512];
    const size_t count = bidiReorder(utf8, rtlBase, codepoints, 512);
    const float width = font_->measure(codepoints, count, scale);

    float penX = r.x;
    if (align == TextAlign::Center) penX = r.x + (r.w - width) * 0.5f;
    if (align == TextAlign::Right) penX = r.x + r.w - width;
    // Vertical centering on the baseline.
    const float baseline = r.y + (r.h + font_->ascent() * scale) * 0.5f - scale * 2.0f;

    for (size_t i = 0; i < count; ++i) {
        const Glyph* g = font_->glyph(codepoints[i]);
        if (g == nullptr) continue;
        if (g->width > 0) {
            const float x0 = penX + g->offsetX * scale;
            const float y0 = baseline + g->offsetY * scale;
            pushQuad(x0, y0, x0 + g->width * scale, y0 + g->height * scale, g->u0, g->v0, g->u1,
                     g->v1, color);
        }
        penX += g->advance * scale;
    }
    return width;
}

}  // namespace mge
