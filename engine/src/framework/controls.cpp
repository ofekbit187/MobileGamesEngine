#include "mge/framework/controls.h"

#include <cmath>

namespace mge {

void TouchControlScheme::handle(const TouchEvent& event) {
    const bool inStickZone = event.x < width_ * kStickZoneFraction;

    switch (event.action) {
        case TouchAction::Down:
            if (inStickZone && stickPointer_ == kNoPointer) {
                stickPointer_ = event.pointerId;
                stickAnchorX = stickX = event.x;
                stickAnchorY = stickY = event.y;
            } else if (!inStickZone && lookPointer_ == kNoPointer) {
                lookPointer_ = event.pointerId;
                lookLastX = lookDownX = event.x;
                lookLastY = lookDownY = event.y;
                lookDownTimeNs = event.timestampNs;
                lookTravel = 0;
            }
            break;

        case TouchAction::Move:
            if (event.pointerId == stickPointer_) {
                stickX = event.x;
                stickY = event.y;
            } else if (event.pointerId == lookPointer_) {
                lookAccumX += (event.x - lookLastX) / height_;
                lookAccumY += (event.y - lookLastY) / height_;
                lookTravel += std::fabs(event.x - lookLastX) + std::fabs(event.y - lookLastY);
                lookLastX = event.x;
                lookLastY = event.y;
            }
            break;

        case TouchAction::Up:
        case TouchAction::Cancel:
            if (event.pointerId == stickPointer_) {
                stickPointer_ = kNoPointer;
            } else if (event.pointerId == lookPointer_) {
                if (event.action == TouchAction::Up &&
                    event.timestampNs - lookDownTimeNs <= kTapMaxNs &&
                    lookTravel <= height_ * kTapMaxTravelFraction) {
                    actionLatched_ = true;  // quick, small-motion touch = tap
                }
                lookPointer_ = kNoPointer;
            }
            break;
    }
}

GameplayIntents TouchControlScheme::consume() {
    GameplayIntents intents;
    if (stickPointer_ != kNoPointer) {
        const float radius = height_ * kStickRadiusFraction;
        float dx = (stickX - stickAnchorX) / radius;
        float dy = (stickY - stickAnchorY) / radius;
        const float len = std::sqrt(dx * dx + dy * dy);
        if (len > 1.0f) {
            dx /= len;
            dy /= len;
        }
        intents.moveX = dx;
        intents.moveY = -dy;  // screen y down -> forward up
    }
    intents.lookX = lookAccumX;
    intents.lookY = lookAccumY;
    intents.action = actionLatched_;
    lookAccumX = lookAccumY = 0;
    actionLatched_ = false;
    return intents;
}

}  // namespace mge
