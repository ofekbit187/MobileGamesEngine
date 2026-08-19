#pragma once

// 3D math for the engine (task 1.6). Plain structs, value semantics,
// shaped for the data layouts the entity and rendering systems will use.

#include <cmath>

namespace mge {

constexpr float kPi = 3.14159265358979323846f;

struct Vec3 {
    float x = 0.0f, y = 0.0f, z = 0.0f;

    constexpr Vec3() = default;
    constexpr Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}

    constexpr Vec3 operator+(const Vec3& r) const { return {x + r.x, y + r.y, z + r.z}; }
    constexpr Vec3 operator-(const Vec3& r) const { return {x - r.x, y - r.y, z - r.z}; }
    constexpr Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    constexpr Vec3 operator-() const { return {-x, -y, -z}; }
    Vec3& operator+=(const Vec3& r) { x += r.x; y += r.y; z += r.z; return *this; }
    Vec3& operator-=(const Vec3& r) { x -= r.x; y -= r.y; z -= r.z; return *this; }
    Vec3& operator*=(float s) { x *= s; y *= s; z *= s; return *this; }

    constexpr float dot(const Vec3& r) const { return x * r.x + y * r.y + z * r.z; }
    constexpr Vec3 cross(const Vec3& r) const {
        return {y * r.z - z * r.y, z * r.x - x * r.z, x * r.y - y * r.x};
    }
    float length() const { return std::sqrt(dot(*this)); }
    constexpr float lengthSq() const { return dot(*this); }
    Vec3 normalized() const {
        const float len = length();
        return len > 0.0f ? Vec3{x / len, y / len, z / len} : Vec3{};
    }
};

inline constexpr Vec3 operator*(float s, const Vec3& v) { return v * s; }

inline Vec3 lerp(const Vec3& a, const Vec3& b, float t) { return a + (b - a) * t; }

struct Quat {
    float x = 0.0f, y = 0.0f, z = 0.0f, w = 1.0f;

    constexpr Quat() = default;
    constexpr Quat(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}

    static Quat fromAxisAngle(const Vec3& axis, float radians) {
        const Vec3 n = axis.normalized();
        const float half = radians * 0.5f;
        const float s = std::sin(half);
        return {n.x * s, n.y * s, n.z * s, std::cos(half)};
    }

    constexpr Quat operator*(const Quat& r) const {
        return {
            w * r.x + x * r.w + y * r.z - z * r.y,
            w * r.y - x * r.z + y * r.w + z * r.x,
            w * r.z + x * r.y - y * r.x + z * r.w,
            w * r.w - x * r.x - y * r.y - z * r.z,
        };
    }

    Quat normalized() const {
        const float len = std::sqrt(x * x + y * y + z * z + w * w);
        return len > 0.0f ? Quat{x / len, y / len, z / len, w / len} : Quat{};
    }

    // Rotate a vector by this (unit) quaternion.
    Vec3 rotate(const Vec3& v) const {
        const Vec3 q{x, y, z};
        const Vec3 t = 2.0f * q.cross(v);
        return v + w * t + q.cross(t);
    }
};

// Column-major 4x4 matrix: m[column * 4 + row], matching GPU conventions.
struct Mat4 {
    float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

    static constexpr Mat4 identity() { return {}; }

    static Mat4 translation(const Vec3& t) {
        Mat4 r;
        r.m[12] = t.x;
        r.m[13] = t.y;
        r.m[14] = t.z;
        return r;
    }

    static Mat4 scale(const Vec3& s) {
        Mat4 r;
        r.m[0] = s.x;
        r.m[5] = s.y;
        r.m[10] = s.z;
        return r;
    }

    static Mat4 rotation(const Quat& q) {
        Mat4 r;
        const float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
        const float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
        const float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
        r.m[0] = 1 - 2 * (yy + zz);
        r.m[1] = 2 * (xy + wz);
        r.m[2] = 2 * (xz - wy);
        r.m[4] = 2 * (xy - wz);
        r.m[5] = 1 - 2 * (xx + zz);
        r.m[6] = 2 * (yz + wx);
        r.m[8] = 2 * (xz + wy);
        r.m[9] = 2 * (yz - wx);
        r.m[10] = 1 - 2 * (xx + yy);
        return r;
    }

    // Vulkan-style perspective: right-handed, depth 0..1, Y negated for
    // Vulkan's downward clip-space Y (keeps CCW front faces correct).
    static Mat4 perspective(float fovYRadians, float aspect, float nearZ, float farZ) {
        Mat4 r;
        const float f = 1.0f / std::tan(fovYRadians * 0.5f);
        r.m[0] = f / aspect;
        r.m[5] = -f;
        r.m[10] = farZ / (nearZ - farZ);
        r.m[11] = -1.0f;
        r.m[14] = (nearZ * farZ) / (nearZ - farZ);
        r.m[15] = 0.0f;
        return r;
    }

    // Right-handed view matrix looking from eye toward target.
    static Mat4 lookAt(const Vec3& eye, const Vec3& target, const Vec3& up) {
        const Vec3 f = (target - eye).normalized();   // forward
        const Vec3 s = f.cross(up).normalized();      // right
        const Vec3 u = s.cross(f);                    // corrected up
        Mat4 r;
        r.m[0] = s.x;  r.m[4] = s.y;  r.m[8] = s.z;
        r.m[1] = u.x;  r.m[5] = u.y;  r.m[9] = u.z;
        r.m[2] = -f.x; r.m[6] = -f.y; r.m[10] = -f.z;
        r.m[12] = -s.dot(eye);
        r.m[13] = -u.dot(eye);
        r.m[14] = f.dot(eye);
        return r;
    }

    Mat4 operator*(const Mat4& r) const {
        Mat4 out;
        for (int c = 0; c < 4; ++c) {
            for (int row = 0; row < 4; ++row) {
                float sum = 0.0f;
                for (int k = 0; k < 4; ++k) sum += m[k * 4 + row] * r.m[c * 4 + k];
                out.m[c * 4 + row] = sum;
            }
        }
        return out;
    }

    Vec3 transformPoint(const Vec3& v) const {
        return {
            m[0] * v.x + m[4] * v.y + m[8] * v.z + m[12],
            m[1] * v.x + m[5] * v.y + m[9] * v.z + m[13],
            m[2] * v.x + m[6] * v.y + m[10] * v.z + m[14],
        };
    }
};

struct Aabb {
    Vec3 min{0, 0, 0};
    Vec3 max{0, 0, 0};

    static Aabb fromCenterExtents(const Vec3& center, const Vec3& halfExtents) {
        return {center - halfExtents, center + halfExtents};
    }

    Vec3 center() const { return (min + max) * 0.5f; }
    Vec3 extents() const { return (max - min) * 0.5f; }

    void extend(const Vec3& p) {
        min = {std::fmin(min.x, p.x), std::fmin(min.y, p.y), std::fmin(min.z, p.z)};
        max = {std::fmax(max.x, p.x), std::fmax(max.y, p.y), std::fmax(max.z, p.z)};
    }

    bool contains(const Vec3& p) const {
        return p.x >= min.x && p.x <= max.x && p.y >= min.y && p.y <= max.y && p.z >= min.z &&
               p.z <= max.z;
    }

    bool intersects(const Aabb& o) const {
        return min.x <= o.max.x && max.x >= o.min.x && min.y <= o.max.y && max.y >= o.min.y &&
               min.z <= o.max.z && max.z >= o.min.z;
    }
};

struct Transform {
    Vec3 position{};
    Quat rotation{};
    Vec3 scale{1, 1, 1};

    Mat4 toMatrix() const {
        return Mat4::translation(position) * Mat4::rotation(rotation) * Mat4::scale(scale);
    }
};

}  // namespace mge
