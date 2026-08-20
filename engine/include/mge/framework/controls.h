#pragma once

// Touch-native control scheme (task 3.5) — logic only, no widgets (those are
// Phase 5 UI). Deterministic mapping from raw touch events to gameplay
// intents:
//   left zone   : virtual stick — drag from the touch-down anchor -> move
//   right zone  : camera drag -> look deltas; a quick small-motion tap -> action
//   buttons     : two round zones in the right zone — jump and use-held
//                 (Phase 12). A touch inside one is a button press, never a
//                 look drag, so aiming the camera never fires an action.
// Coordinates are pixels with y down (Android convention).

#include "mge/core/input.h"
#include "mge/core/math.h"

namespace mge {

struct GameplayIntents {
    float moveX = 0.0f;  // strafe right +
    float moveY = 0.0f;  // forward +
    float lookX = 0.0f;  // accumulated yaw delta (screen fraction)
    float lookY = 0.0f;  // accumulated pitch delta
    bool action = false;   // primary action (tap), latched until consumed
    bool jump = false;     // action/jump  (Phase 12), latched until consumed
    bool useHeld = false;  // action/use_held, latched until consumed
};

// A round on-screen button in pixels. The scheme owns the geometry so the
// control logic and the HUD that draws it can never disagree (task 5.6).
struct TouchButton {
    float x = 0, y = 0, radius = 0;
    bool contains(float px, float py) const {
        const float dx = px - x;
        const float dy = py - y;
        return dx * dx + dy * dy <= radius * radius;
    }
};

class TouchControlScheme {
public:
    void configure(float screenWidthPx, float screenHeightPx) {
        width_ = screenWidthPx;
        height_ = screenHeightPx;
    }

    void handle(const TouchEvent& event);

    // Current move state plus look/action accumulated since the last call
    // (call once per simulation step).
    GameplayIntents consume();

    bool stickActive() const { return stickPointer_ != kNoPointer; }

    // Where the action buttons are, and which one is under a finger now.
    TouchButton jumpButton() const {
        return {width_ - height_ * 0.30f, height_ * 0.74f, height_ * 0.085f};
    }
    TouchButton useButton() const {
        return {width_ - height_ * 0.13f, height_ * 0.80f, height_ * 0.105f};
    }
    bool jumpPressed() const { return buttonHeld_ == kButtonJump; }
    bool usePressed() const { return buttonHeld_ == kButtonUse; }

    // Current stick geometry in screen pixels, for the UI overlay to draw
    // the virtual controls over the scheme's real state (task 5.6).
    bool stickState(float& anchorX, float& anchorY, float& x, float& y) const {
        if (stickPointer_ == kNoPointer) return false;
        anchorX = stickAnchorX;
        anchorY = stickAnchorY;
        x = stickX;
        y = stickY;
        return true;
    }

private:
    static constexpr int32_t kNoPointer = INT32_MIN;
    static constexpr float kStickZoneFraction = 0.45f;  // left 45% of the screen
    static constexpr float kStickRadiusFraction = 0.12f;  // of screen height
    static constexpr int64_t kTapMaxNs = 250'000'000;
    static constexpr float kTapMaxTravelFraction = 0.02f;  // of screen height

    float width_ = 1.0f;
    float height_ = 1.0f;

    static constexpr uint8_t kButtonNone = 0;
    static constexpr uint8_t kButtonJump = 1;
    static constexpr uint8_t kButtonUse = 2;

    int32_t stickPointer_ = kNoPointer;
    float stickAnchorX = 0, stickAnchorY = 0;
    float stickX = 0, stickY = 0;

    int32_t lookPointer_ = kNoPointer;
    float lookLastX = 0, lookLastY = 0;
    float lookAccumX = 0, lookAccumY = 0;
    int64_t lookDownTimeNs = 0;
    float lookDownX = 0, lookDownY = 0;
    float lookTravel = 0;

    int32_t buttonPointer_ = kNoPointer;
    uint8_t buttonHeld_ = kButtonNone;

    bool actionLatched_ = false;
    bool jumpLatched_ = false;
    bool useLatched_ = false;
};

}  // namespace mge
