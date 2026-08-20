#include "mge/graphics/primitives.h"

#include <cmath>

namespace mge {

namespace {

// UVs are in METRES of surface (mesh_data.h): the quad is projected onto the
// two axes it actually spans, so a 4 m x 3 m wall comes out u = 0..4, v = 0..3
// and one tiling material covers it at a uniform texel density. Projecting on
// the dominant-normal axis pair is exact for an axis-aligned quad, which every
// primitive face here is.
void planarUv(const Vec3& normal, const Vec3& p, float& u, float& v) {
    const float ax = std::fabs(normal.x), ay = std::fabs(normal.y), az = std::fabs(normal.z);
    if (ax >= ay && ax >= az) {
        u = p.z;
        v = p.y;
    } else if (ay >= az) {
        u = p.x;
        v = p.z;
    } else {
        u = p.x;
        v = p.y;
    }
}

void addQuad(MeshData& mesh, const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& d,
             const Vec3& normal) {
    const uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
    const Vec3 corners[4] = {a, b, c, d};
    for (const Vec3& p : corners) {
        Vertex vertex{p, normal, {0, 0}};
        planarUv(normal, p, vertex.uv[0], vertex.uv[1]);
        mesh.vertices.push_back(vertex);
    }
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
        // u wraps the circumference in metres, v runs the height in metres.
        const float u = a * radius;
        m.vertices.push_back({{n.x * radius, -h, n.z * radius}, n, {u, 0}});
        m.vertices.push_back({{n.x * radius, h, n.z * radius}, n, {u, height}});
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
        m.vertices.push_back({{0, yv, 0}, n, {0, 0}});
        for (int i = 0; i <= segments; ++i) {
            const float a = static_cast<float>(i) / segments * 2.0f * kPi;
            m.vertices.push_back({{std::cos(a) * radius, yv, std::sin(a) * radius}, n,
                                  {std::cos(a) * radius, std::sin(a) * radius}});
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
            m.vertices.push_back({{n.x * radius, n.y * radius + yOffset, n.z * radius}, n,
                                  {std::atan2(n.z, n.x) * radius,
                                   n.y * radius + yOffset}});
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
        dst.vertices.push_back({v.position + offset, v.normal, {v.uv[0], v.uv[1]}});
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
