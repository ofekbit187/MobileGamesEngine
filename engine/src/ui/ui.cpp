#include "mge/ui/ui.h"

#include <cmath>
#include <cstdio>

namespace mge {

bool Ui::init(const FontAtlas* font, const Theme& theme, Localization* strings) {
    if (font == nullptr || strings == nullptr) return false;
    font_ = font;
    theme_ = theme;
    strings_ = strings;
    drawList_.setFont(font);
    return true;
}

void Ui::beginFrame(float screenWidthPx, float screenHeightPx) {
    width_ = screenWidthPx;
    height_ = screenHeightPx;
    drawList_.clear();
    interactiveCount_ = 0;  // widgets re-register as they are built
}

UiRect Ui::place(const UiRect& r) const {
    if (!strings_->rtl()) return r;
    return {width_ - r.x - r.w, r.y, r.w, r.h};
}

void Ui::registerInteractive(uint32_t id, const UiRect& screenRect) {
    if (interactiveCount_ < kMaxInteractive) {
        interactive_[interactiveCount_++] = {id, screenRect};
    }
}

bool Ui::handleTouch(const TouchEvent& event) {
    switch (event.action) {
        case TouchAction::Down: {
            for (size_t i = 0; i < interactiveCount_; ++i) {
                if (interactive_[i].rect.contains(event.x, event.y)) {
                    uiPointer_ = event.pointerId;
                    activeId_ = interactive_[i].id;
                    touchDown_ = true;
                    touchX_ = event.x;
                    touchY_ = event.y;
                    return true;  // UI claims this pointer (5.7)
                }
            }
            return false;  // fall through to gameplay
        }
        case TouchAction::Move:
            if (event.pointerId == uiPointer_) {
                touchX_ = event.x;
                touchY_ = event.y;
                return true;
            }
            return false;
        case TouchAction::Up:
        case TouchAction::Cancel:
            if (event.pointerId == uiPointer_) {
                // A release still inside the active widget is a click.
                for (size_t i = 0; i < interactiveCount_; ++i) {
                    if (interactive_[i].id == activeId_ &&
                        interactive_[i].rect.contains(event.x, event.y) &&
                        event.action == TouchAction::Up) {
                        clickedId_ = activeId_;
                    }
                }
                uiPointer_ = INT32_MIN;
                activeId_ = 0;
                touchDown_ = false;
                return true;
            }
            return false;
    }
    return false;
}

bool Ui::pressed(uint32_t id, const UiRect& screenRect) {
    registerInteractive(id, screenRect);
    if (clickedId_ == id) {
        clickedId_ = 0;
        return true;
    }
    return false;
}

// ---------------------------------------------------------------- widgets --

void Ui::screenDim() { drawList_.rect({0, 0, width_, height_}, theme_.leather); }

void Ui::panel(const UiRect& logical) {
    const UiRect r = place(logical);
    const UiRect shadow{r.x + 5, r.y + 6, r.w, r.h};
    drawList_.rect(shadow, theme_.shadow);
    drawList_.rect(r, theme_.parchment);
    drawList_.doubleBorder(r, theme_.ink);
}

void Ui::label(const UiRect& r, const char* key, float scale, TextAlign align) {
    labelInk(r, key, scale, align, theme_.ink);
}

void Ui::labelInk(const UiRect& logical, const char* key, float scale, TextAlign align,
                  const float color[4]) {
    const UiRect r = place(logical);
    TextAlign resolved = align;
    if (strings_->rtl()) {  // mirror alignment with layout (P11)
        if (align == TextAlign::Left) resolved = TextAlign::Right;
        else if (align == TextAlign::Right) resolved = TextAlign::Left;
    }
    drawList_.text(r, strings_->get(key), scale, color, resolved, strings_->rtl());
}

bool Ui::button(uint32_t id, const UiRect& logical, const char* key) {
    const UiRect r = place(logical);
    const bool held = touchDown_ && activeId_ == id;
    drawList_.rect({r.x + 3, r.y + 4, r.w, r.h}, theme_.shadow);
    drawList_.rect(r, held ? theme_.parchmentDeep : theme_.vellum);
    drawList_.border(r, theme_.borderThick, theme_.ink);
    // Wax-seal accent on the leading edge (mirrors with direction).
    const float sealSize = r.h * 0.32f;
    const float sealX = strings_->rtl() ? r.x + r.w - r.h * 0.5f - sealSize * 0.5f
                                        : r.x + r.h * 0.5f - sealSize * 0.5f;
    drawList_.rect({sealX, r.y + (r.h - sealSize) * 0.5f, sealSize, sealSize}, theme_.crimson);
    drawList_.text(r, strings_->get(key), 1.0f, theme_.ink, TextAlign::Center, strings_->rtl());
    return pressed(id, r);
}

bool Ui::slider(uint32_t id, const UiRect& logical, float& value01) {
    const UiRect r = place(logical);
    registerInteractive(id, r);
    const float trackY = r.y + r.h * 0.5f - 3;
    drawList_.rect({r.x, trackY, r.w, 6}, theme_.parchmentDeep);
    drawList_.border({r.x, trackY, r.w, 6}, 1.0f, theme_.ink);
    bool changed = false;
    if (touchDown_ && activeId_ == id) {
        float v = (touchX_ - r.x) / r.w;
        v = v < 0 ? 0 : (v > 1 ? 1 : v);
        if (strings_->rtl()) v = 1.0f - v;  // slider direction mirrors too
        changed = v != value01;
        value01 = v;
    }
    const float knobV = strings_->rtl() ? 1.0f - value01 : value01;
    const float knobX = r.x + knobV * r.w - 8;
    drawList_.rect({knobX, r.y + r.h * 0.5f - 12, 16, 24}, theme_.crimson);
    drawList_.border({knobX, r.y + r.h * 0.5f - 12, 16, 24}, 1.0f, theme_.ink);
    return changed;
}

bool Ui::toggle(uint32_t id, const UiRect& logical, bool& value) {
    const UiRect r = place(logical);
    drawList_.rect(r, value ? theme_.crimson : theme_.parchmentDeep);
    drawList_.border(r, theme_.borderThick, theme_.ink);
    if (value) {
        drawList_.rect({r.x + r.w * 0.3f, r.y + r.h * 0.3f, r.w * 0.4f, r.h * 0.4f},
                       theme_.vellum);
    }
    if (pressed(id, r)) {
        value = !value;
        return true;
    }
    return false;
}

// -------------------------------------------------------------------- HUD --

void Ui::healthBar(const UiRect& logical, float fraction) {
    const UiRect r = place(logical);
    drawList_.rect(r, theme_.leather);
    drawList_.border(r, 1.5f, theme_.gold);
    const float inset = 3.0f;
    UiRect fill{r.x + inset, r.y + inset, (r.w - 2 * inset) * fraction, r.h - 2 * inset};
    if (strings_->rtl()) fill.x = r.x + r.w - inset - fill.w;  // drains toward the edge
    drawList_.rect(fill, theme_.crimson);
}

void Ui::compassStrip(const UiRect& logical, float yawRadians) {
    const UiRect r = place(logical);
    drawList_.rect(r, theme_.parchment);
    drawList_.border(r, 1.5f, theme_.gold);
    // Cardinal letters slide with yaw; N at yaw 0.
    static const char* kCardinals[4] = {"N", "E", "S", "W"};
    const float degPerPx = 180.0f / r.w;  // half-turn visible
    const float yawDeg = yawRadians * 180.0f / kPi;
    for (int i = 0; i < 4; ++i) {
        float delta = std::fmod(i * 90.0f - yawDeg + 540.0f, 360.0f) - 180.0f;
        const float x = r.x + r.w * 0.5f + delta / degPerPx;
        if (x < r.x + 8 || x > r.x + r.w - 8) continue;
        drawList_.text({x - 8, r.y, 16, r.h}, kCardinals[i], 0.8f, theme_.ink,
                       TextAlign::Center, false);
    }
    // Center needle.
    drawList_.rect({r.x + r.w * 0.5f - 1, r.y + 2, 2, r.h - 4}, theme_.crimson);
}

void Ui::itemSlot(const UiRect& logical, const Item* item, bool selected) {
    const UiRect r = place(logical);
    drawList_.rect(r, theme_.parchmentDeep);
    drawList_.border(r, selected ? 2.5f : 1.0f, selected ? theme_.crimson : theme_.gold);
    if (item != nullptr && item->count > 0) {
        const float inset = r.w * 0.22f;
        drawList_.rect({r.x + inset, r.y + inset, r.w - 2 * inset, r.h - 2 * inset},
                       item->color);
        drawList_.border({r.x + inset, r.y + inset, r.w - 2 * inset, r.h - 2 * inset}, 1.0f,
                         theme_.ink);
        if (item->count > 1) {
            char count[16];
            snprintf(count, sizeof(count), "%u", item->count);
            drawList_.text({r.x, r.y + r.h - 20, r.w - 4, 18}, count, 0.62f, theme_.ink,
                           TextAlign::Right, false);
        }
    }
}

// ---------------------------------------------------- collection binding --

void Ui::collectionView(uint32_t id, const UiRect& logical, const char* titleKey,
                        const ItemCollection& collection, int columns, int& selectedIndex) {
    panel(logical);
    const UiRect r = place(logical);
    label({logical.x + 14, logical.y + 10, logical.w - 28, 30}, titleKey, 0.9f,
          TextAlign::Left);

    const float cell = (logical.w - 28.0f) / columns;
    const float top = logical.y + 46;
    const int rows = (static_cast<int>(collection.capacity()) + columns - 1) / columns;
    for (int i = 0; i < rows * columns; ++i) {
        const int col = i % columns;
        const int row = i / columns;
        const UiRect slotLogical{logical.x + 14 + col * cell, top + row * cell, cell - 6,
                                 cell - 6};
        if (slotLogical.y + slotLogical.h > logical.y + logical.h - 10) break;
        const Item* item = collection.at(static_cast<uint32_t>(i));
        itemSlot(slotLogical, item, selectedIndex == i);
        if (item != nullptr) {
            const UiRect screen = place(slotLogical);
            const uint32_t slotWidgetId = id + 1000 + static_cast<uint32_t>(i);
            registerInteractive(slotWidgetId, screen);
            if (clickedId_ == slotWidgetId) {
                clickedId_ = 0;
                selectedIndex = i;
            }
        }
    }
    (void)r;
}

// ------------------------------------------------- virtual controls (5.6) --

void Ui::virtualControls(bool stickActive, float stickAnchorX, float stickAnchorY,
                         float stickX, float stickY) {
    // Resting positions (drawn faint until touched). Not mirrored: thumbs
    // don't move with reading direction.
    const float ringRadius = height_ * 0.12f;
    float cx = width_ * 0.16f, cy = height_ * 0.76f;
    if (stickActive) {
        cx = stickAnchorX;
        cy = stickAnchorY;
    }
    float ringColor[4] = {theme_.gold[0], theme_.gold[1], theme_.gold[2],
                          stickActive ? 0.9f : 0.35f};
    drawList_.border({cx - ringRadius, cy - ringRadius, ringRadius * 2, ringRadius * 2}, 3.0f,
                     ringColor);
    float knobX = cx, knobY = cy;
    if (stickActive) {
        knobX = stickX;
        knobY = stickY;
    }
    float knobColor[4] = {theme_.crimson[0], theme_.crimson[1], theme_.crimson[2],
                          stickActive ? 0.95f : 0.4f};
    const float knob = ringRadius * 0.38f;
    drawList_.rect({knobX - knob, knobY - knob, knob * 2, knob * 2}, knobColor);

    // Wax-seal action buttons, lower right.
    const float seal = height_ * 0.085f;
    float sealColor[4] = {theme_.crimson[0], theme_.crimson[1], theme_.crimson[2], 0.55f};
    drawList_.rect({width_ * 0.86f, height_ * 0.72f, seal, seal}, sealColor);
    drawList_.border({width_ * 0.86f, height_ * 0.72f, seal, seal}, 2.0f, ringColor);
    drawList_.rect({width_ * 0.79f, height_ * 0.82f, seal * 0.75f, seal * 0.75f}, sealColor);
    drawList_.border({width_ * 0.79f, height_ * 0.82f, seal * 0.75f, seal * 0.75f}, 2.0f,
                     ringColor);
}

}  // namespace mge
