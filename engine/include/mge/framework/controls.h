#pragma once

// Touch-native control scheme (task 3.5) — logic only, no widgets (those are
// Phase 5 UI). Deterministic mapping from raw touch events to gameplay
// intents:
//   left zone   : virtual stick — drag from the touch-down anchor -> move
//   right zone  : camera drag -> look deltas; a quick small-motion tap -> action
// Coordinates are pixels with y down (Android convention).

#include "mge/core/input.h"
#include "mge/core/math.h"

namespace mge {

struct GameplayIntents {
    float moveX = 0.0f;  // strafe right +
    float moveY = 0.0f;  // forward +
    float lookX = 0.0f;  // accumulated yaw delta (screen fraction)
    float lookY = 0.0f;  // accumulated pitch delta
    bool action = false; // primary action (tap), latched until consumed
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

private:
    static constexpr int32_t kNoPointer = INT32_MIN;
    static constexpr float kStickZoneFraction = 0.45f;  // left 45% of the screen
    static constexpr float kStickRadiusFraction = 0.12f;  // of screen height
    static constexpr int64_t kTapMaxNs = 250'000'000;
    static constexpr float kTapMaxTravelFraction = 0.02f;  // of screen height

    float width_ = 1.0f;
    float height_ = 1.0f;

    int32_t stickPointer_ = kNoPointer;
    float stickAnchorX = 0, stickAnchorY = 0;
    float stickX = 0, stickY = 0;

    int32_t lookPointer_ = kNoPointer;
    float lookLastX = 0, lookLastY = 0;
    float lookAccumX = 0, lookAccumY = 0;
    int64_t lookDownTimeNs = 0;
    float lookDownX = 0, lookDownY = 0;
    float lookTravel = 0;

    bool actionLatched_ = false;
};

}  // namespace mge
