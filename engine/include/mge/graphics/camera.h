#pragma once

// Camera + view frustum (task 2.7). Pure CPU math, header-only — usable by
// tests and tools without a Vulkan device. Culling decides both what to draw
// and (later) what to stream: distance drives LOD selection here and chunk
// priority in the streaming system.

#include <cstddef>

#include "mge/core/math.h"

namespace mge {

struct Camera {
    Vec3 eye{0, 2, 6};
    Vec3 target{0, 0, 0};
    Vec3 up{0, 1, 0};
    float fovYRadians = 60.0f * kPi / 180.0f;
    float aspect = 16.0f / 9.0f;
    float nearZ = 0.1f;
    float farZ = 500.0f;

    Mat4 view() const { return Mat4::lookAt(eye, target, up); }
    Mat4 projection() const { return Mat4::perspective(fovYRadians, aspect, nearZ, farZ); }
    Mat4 viewProj() const { return projection() * view(); }
};

// Six-plane frustum extracted from a view-projection matrix
// (Gribb/Hartmann), for AABB visibility tests.
struct Frustum {
    // Planes as (nx, ny, nz, d): dot(n, p) + d >= 0 means inside.
    float planes[6][4];

    static Frustum fromViewProj(const Mat4& vp) {
        Frustum f;
        const float* m = vp.m;
        auto row = [&](int i) {
            // Row i of the column-major matrix.
            return Vec3{m[0 * 4 + i], m[1 * 4 + i], m[2 * 4 + i]};
        };
        auto roww = [&](int i) { return m[3 * 4 + i]; };
        const Vec3 r0 = row(0), r1 = row(1), r2 = row(2), r3 = row(3);
        const float w0 = roww(0), w1 = roww(1), w2 = roww(2), w3 = roww(3);

        const Vec3 n[6] = {
            r3 + r0,  // left
            r3 - r0,  // right
            r3 + r1,  // bottom
            r3 - r1,  // top
            r2,       // near (Vulkan depth 0..1)
            r3 - r2,  // far
        };
        const float d[6] = {w3 + w0, w3 - w0, w3 + w1, w3 - w1, w2, w3 - w2};
        for (int i = 0; i < 6; ++i) {
            const float len = n[i].length();
            f.planes[i][0] = n[i].x / len;
            f.planes[i][1] = n[i].y / len;
            f.planes[i][2] = n[i].z / len;
            f.planes[i][3] = d[i] / len;
        }
        return f;
    }

    bool intersects(const Aabb& box) const {
        for (const auto& p : planes) {
            // Most-positive vertex along the plane normal.
            const Vec3 v{
                p[0] >= 0 ? box.max.x : box.min.x,
                p[1] >= 0 ? box.max.y : box.min.y,
                p[2] >= 0 ? box.max.z : box.min.z,
            };
            if (p[0] * v.x + p[1] * v.y + p[2] * v.z + p[3] < 0) return false;
        }
        return true;
    }
};

// Distance-based LOD selection (task 2.7): switchDistances is ascending;
// returns the LOD index to use at `distance` (0 = finest).
inline size_t selectLod(float distance, const float* switchDistances, size_t lodCount) {
    if (lodCount == 0) return 0;
    for (size_t i = 0; i + 1 < lodCount; ++i) {
        if (distance < switchDistances[i]) return i;
    }
    return lodCount - 1;
}

}  // namespace mge
