#include "mge/graphics/primitives.h"

#include <cmath>

namespace mge {

namespace {

void addQuad(MeshData& mesh, const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& d,
             const Vec3& normal) {
    const uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
    mesh.vertices.push_back({a, normal});
    mesh.vertices.push_back({b, normal});
    mesh.vertices.push_back({c, normal});
    mesh.vertices.push_back({d, normal});
    // CCW winding seen from the normal side.
    mesh.indices.insert(mesh.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
}

}  // namespace

MeshData makeBox(const Vec3& extents) {
    const float x = extents.x * 0.5f, y = extents.y * 0.5f, z = extents.z * 0.5f;
    MeshData m;
    m.vertices.reserve(24);
    m.indices.reserve(36);
    addQuad(m, {-x, -y, z}, {x, -y, z}, {x, y, z}, {-x, y, z}, {0, 0, 1});      // +Z
    addQuad(m, {x, -y, -z}, {-x, -y, -z}, {-x, y, -z}, {x, y, -z}, {0, 0, -1}); // -Z
    addQuad(m, {x, -y, z}, {x, -y, -z}, {x, y, -z}, {x, y, z}, {1, 0, 0});      // +X
    addQuad(m, {-x, -y, -z}, {-x, -y, z}, {-x, y, z}, {-x, y, -z}, {-1, 0, 0}); // -X
    addQuad(m, {-x, y, z}, {x, y, z}, {x, y, -z}, {-x, y, -z}, {0, 1, 0});      // +Y
    addQuad(m, {-x, -y, -z}, {x, -y, -z}, {x, -y, z}, {-x, -y, z}, {0, -1, 0}); // -Y
    m.computeBounds();
    return m;
}

MeshData makeCylinder(float radius, float height, int segments) {
    MeshData m;
    const float h = height * 0.5f;
    // Side rings (per-vertex radial normals).
    for (int i = 0; i <= segments; ++i) {
        const float a = static_cast<float>(i) / segments * 2.0f * kPi;
        const Vec3 n{std::cos(a), 0, std::sin(a)};
        m.vertices.push_back({{n.x * radius, -h, n.z * radius}, n});
        m.vertices.push_back({{n.x * radius, h, n.z * radius}, n});
    }
    for (int i = 0; i < segments; ++i) {
        const uint32_t b = static_cast<uint32_t>(i * 2);
        // Outward-facing CCW (same pattern the capsule uses).
        m.indices.insert(m.indices.end(), {b + 1, b + 2, b, b + 1, b + 3, b + 2});
    }
    // Caps (fan around center).
    for (int cap = 0; cap < 2; ++cap) {
        const float yv = cap == 0 ? h : -h;
        const Vec3 n{0, cap == 0 ? 1.0f : -1.0f, 0};
        const uint32_t center = static_cast<uint32_t>(m.vertices.size());
        m.vertices.push_back({{0, yv, 0}, n});
        for (int i = 0; i <= segments; ++i) {
            const float a = static_cast<float>(i) / segments * 2.0f * kPi;
            m.vertices.push_back({{std::cos(a) * radius, yv, std::sin(a) * radius}, n});
        }
        for (int i = 0; i < segments; ++i) {
            const uint32_t v0 = center + 1 + i, v1 = center + 2 + i;
            if (cap == 0) {
                m.indices.insert(m.indices.end(), {center, v1, v0});
            } else {
                m.indices.insert(m.indices.end(), {center, v0, v1});
            }
        }
    }
    m.computeBounds();
    return m;
}

MeshData makeCapsule(float radius, float height, int segments, int rings) {
    MeshData m;
    const float cyl = std::fmax(height - 2.0f * radius, 0.0f);
    const float h = cyl * 0.5f;
    // Latitude bands from top pole to bottom pole; the equator band is
    // stretched by the cylinder half-height.
    const int totalRings = rings * 2 + 1;
    for (int r = 0; r <= totalRings; ++r) {
        // phi in [0, pi]: 0 = top pole.
        const float t = static_cast<float>(r) / totalRings;
        const float phi = t * kPi;
        const float sp = std::sin(phi), cp = std::cos(phi);
        const float yOffset = cp >= 0 ? h : -h;
        for (int s = 0; s <= segments; ++s) {
            const float a = static_cast<float>(s) / segments * 2.0f * kPi;
            const Vec3 n{sp * std::cos(a), cp, sp * std::sin(a)};
            m.vertices.push_back({{n.x * radius, n.y * radius + yOffset, n.z * radius}, n});
        }
    }
    const int stride = segments + 1;
    for (int r = 0; r < totalRings; ++r) {
        for (int s = 0; s < segments; ++s) {
            const uint32_t v0 = static_cast<uint32_t>(r * stride + s);
            const uint32_t v1 = v0 + 1;
            const uint32_t v2 = v0 + stride;
            const uint32_t v3 = v2 + 1;
            m.indices.insert(m.indices.end(), {v0, v3, v2, v0, v1, v3});
        }
    }
    m.computeBounds();
    return m;
}

void appendMesh(MeshData& dst, const MeshData& src, const Vec3& offset) {
    const uint32_t base = static_cast<uint32_t>(dst.vertices.size());
    dst.vertices.reserve(dst.vertices.size() + src.vertices.size());
    for (const Vertex& v : src.vertices) {
        dst.vertices.push_back({v.position + offset, v.normal});
    }
    dst.indices.reserve(dst.indices.size() + src.indices.size());
    for (uint32_t index : src.indices) dst.indices.push_back(base + index);
    dst.computeBounds();
}

MeshData makePlane(float width, float depth) {
    MeshData m;
    const float x = width * 0.5f, z = depth * 0.5f;
    addQuad(m, {-x, 0, z}, {x, 0, z}, {x, 0, -z}, {-x, 0, -z}, {0, 1, 0});
    m.computeBounds();
    return m;
}

}  // namespace mge
