#pragma once

// The engine UI (tasks 5.1/5.4/5.7/5.9): widgets emitted into the draw list
// each frame from fixed-capacity state — allocation-free per frame (P1) —
// with retained interaction state (hot/active/pressed) across frames.
// Layout is direction-aware (P11): widgets are authored in LTR logical
// coordinates and mirror automatically when the language is RTL.
//
// Input routing (task 5.7): feed touches to handleTouch() BEFORE gameplay;
// it returns true when the UI claims the pointer (a press on an interactive
// widget), and gameplay sees nothing. Unclaimed touches fall through.

#include "mge/core/input.h"
#include "mge/framework/items.h"
#include "mge/ui/draw_list.h"
#include "mge/ui/localization.h"
#include "mge/ui/theme.h"

namespace mge {

class Ui {
public:
    bool init(const FontAtlas* font, const Theme& theme, Localization* strings);

    void beginFrame(float screenWidthPx, float screenHeightPx);

    // --- input routing (5.7) ---
    bool handleTouch(const TouchEvent& event);

    // --- widgets (5.4), Codex-styled (5.3) ---
    void panel(const UiRect& r);                        // parchment page + double rule
    void screenDim();                                   // leather backdrop behind menus
    void label(const UiRect& r, const char* key, float scale, TextAlign align);
    void labelInk(const UiRect& r, const char* key, float scale, TextAlign align,
                  const float color[4]);
    bool button(uint32_t id, const UiRect& r, const char* key);
    bool slider(uint32_t id, const UiRect& r, float& value01);
    bool toggle(uint32_t id, const UiRect& r, bool& value);

    // --- HUD pieces (5.5) ---
    void healthBar(const UiRect& r, float fraction);
    void compassStrip(const UiRect& r, float yawRadians);
    void itemSlot(const UiRect& r, const Item* item, bool selected);

    // --- data binding (5.9): grid bound to a registered collection ---
    void collectionView(uint32_t id, const UiRect& r, const char* titleKey,
                        const ItemCollection& collection, int columns, int& selectedIndex);

    // --- virtual gameplay controls (5.6), draw-only over the scheme state ---
    void virtualControls(bool stickActive, float stickAnchorX, float stickAnchorY,
                         float stickX, float stickY);

    // Mirrors a logical-LTR rect for the active direction (P11). All widget
    // calls apply this internally; exposed for custom layouts.
    UiRect place(const UiRect& r) const;

    bool rtl() const { return strings_->rtl(); }
    const UiDrawList& drawList() const { return drawList_; }
    const Theme& theme() const { return theme_; }
    Localization& strings() { return *strings_; }
    float width() const { return width_; }
    float height() const { return height_; }

private:
    static constexpr size_t kMaxInteractive = 128;
    struct Interactive {
        uint32_t id;
        UiRect rect;  // screen-space (already mirrored)
    };

    bool pressed(uint32_t id, const UiRect& screenRect);
    void registerInteractive(uint32_t id, const UiRect& screenRect);

    Theme theme_{};
    const FontAtlas* font_ = nullptr;
    Localization* strings_ = nullptr;
    UiDrawList drawList_;
    float width_ = 1, height_ = 1;

    // Interaction state (retained across frames).
    // The list rebuilt each frame; between frames it holds the last fully
    // built set, which is what touches hit-test against.
    Interactive interactive_[kMaxInteractive];
    size_t interactiveCount_ = 0;
    int32_t uiPointer_ = INT32_MIN;    // pointer claimed by the UI
    uint32_t activeId_ = 0;            // widget under the active pointer
    uint32_t clickedId_ = 0;           // reported once to its widget
    float touchX_ = 0, touchY_ = 0;
    bool touchDown_ = false;
};

}  // namespace mge
