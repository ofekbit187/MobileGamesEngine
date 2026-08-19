#pragma once

// Camera controllers (task 3.7). Pure functions of character state — the
// same controllers serve the template game, tests, and the device app.
// Yaw 0 faces -Z; forward = (sin yaw, 0, -cos yaw) * -1?  Convention:
//   forward(yaw) = ( sin(yaw), 0, -cos(yaw) )  — yaw 0 looks down -Z,
//   positive yaw turns right (clockwise from above).

#include <cmath>

#include "mge/core/math.h"
#include "mge/graphics/camera.h"

namespace mge {

inline Vec3 yawForward(float yaw) { return {std::sin(yaw), 0.0f, -std::cos(yaw)}; }
inline Vec3 yawRight(float yaw) { return {std::cos(yaw), 0.0f, std::sin(yaw)}; }

struct ThirdPersonCamera {
    float distance = 5.5f;
    float height = 2.6f;
    float targetHeight = 1.4f;

    void update(Camera& camera, const Vec3& characterPos, float characterYaw) const {
        const Vec3 back = yawForward(characterYaw) * -distance;
        camera.eye = characterPos + back + Vec3{0, height, 0};
        camera.target = characterPos + Vec3{0, targetHeight, 0};
    }
};

struct FirstPersonCamera {
    float eyeHeight = 1.65f;

    void update(Camera& camera, const Vec3& characterPos, float yaw, float pitch) const {
        camera.eye = characterPos + Vec3{0, eyeHeight, 0};
        const Vec3 forward = yawForward(yaw);
        const float cp = std::cos(pitch);
        camera.target = camera.eye + Vec3{forward.x * cp, std::sin(pitch), forward.z * cp};
    }
};

}  // namespace mge
