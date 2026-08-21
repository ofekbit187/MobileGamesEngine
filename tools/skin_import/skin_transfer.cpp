#include "skin_transfer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#include "stb_image.h"

#include "../skin_preview/skin_generator.h"

namespace mge::skin {
namespace {

inline float clamp01f(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }

// Is this colour skin?
//
// A sourced skin is authored for the SOURCE's geometry, and that geometry is
// not ours. MakeHuman models eyeballs, teeth and tongue as separate meshes, so
// its sheet carries saturated red patches for the eye and mouth INTERIORS —
// surfaces our body does not have, because our face is one closed shell with
// no eye opening. Sampling there paints a red eye and a red mouth, which is
// exactly what the first run produced.
//
// The rejection is not "that looks wrong to me". Human skin occupies a compact
// locus in CIELAB — the same locus the ITA classifier is built on — and the
// eye and mouth interiors sit far outside it. So the test is the published
// colour science used as a validator: outside the locus is not skin, and a
// texel that is not skin is left unfilled for the dilation to fill from the
// skin around it.
//
// Bounds are deliberately generous, covering Fitzpatrick I through VI with
// room to spare; they reject saturated reds and near-blacks, not dark skin.
bool isSkinColour(float r, float g, float b) {
    // sRGB -> linear -> XYZ (D65) -> Lab
    auto lin = [](float c) {
        return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
    };
    const float R = lin(r), G = lin(g), B = lin(b);
    const double X = 0.4124564 * R + 0.3575761 * G + 0.1804375 * B;
    const double Y = 0.2126729 * R + 0.7151522 * G + 0.0721750 * B;
    const double Z = 0.0193339 * R + 0.1191920 * G + 0.9503041 * B;
    auto f = [](double t) {
        return t > 0.008856 ? std::cbrt(t) : (7.787 * t + 16.0 / 116.0);
    };
    const double fx = f(X / 0.95047), fy = f(Y / 1.0), fz = f(Z / 1.08883);
    const double L = 116.0 * fy - 16.0;
    const double a = 500.0 * (fx - fy);
    const double bb = 200.0 * (fy - fz);
    if (L < 12.0 || L > 95.0) return false;   // near-black (pupil, nostril void) or blown white
    if (a < -2.0 || a > 30.0) return false;   // green, or the saturated red of a mouth interior
    if (bb < 2.0 || bb > 45.0) return false;  // blue-ish, or extreme yellow
    return true;
}

// One OBJ face vertex reference: `v`, `v/vt`, `v//vn` or `v/vt/vn`, 1-based
// and possibly negative (relative to the end).
bool parseRef(const std::string& tok, int posCount, int uvCount, uint32_t& pos, uint32_t& uv) {
    int p = 0, t = 0;
    const size_t slash = tok.find('/');
    if (slash == std::string::npos) {
        p = std::atoi(tok.c_str());
    } else {
        p = std::atoi(tok.substr(0, slash).c_str());
        const size_t slash2 = tok.find('/', slash + 1);
        const std::string mid = tok.substr(slash + 1, slash2 == std::string::npos
                                                          ? std::string::npos
                                                          : slash2 - slash - 1);
        if (!mid.empty()) t = std::atoi(mid.c_str());
    }
    if (p == 0) return false;
    pos = uint32_t(p > 0 ? p - 1 : posCount + p);
    uv = t == 0 ? 0u : uint32_t(t > 0 ? t - 1 : uvCount + t);
    return true;
}

}  // namespace

// -------------------------------------------------------------- loading ----

bool loadObj(const std::string& path, SourceMesh& out, std::string& error) {
    std::ifstream in(path);
    if (!in) {
        error = "cannot open " + path;
        return false;
    }
    out = SourceMesh{};
    std::string line;
    while (std::getline(in, line)) {
        if (line.size() < 2) continue;
        std::istringstream ls(line);
        std::string kind;
        ls >> kind;
        if (kind == "v") {
            Vec3 p;
            ls >> p.x >> p.y >> p.z;
            out.positions.push_back(p);
        } else if (kind == "vt") {
            float u = 0, v = 0;
            ls >> u >> v;
            out.uvs.push_back(u);
            out.uvs.push_back(v);
        } else if (kind == "f") {
            std::vector<uint32_t> fp, fu;
            std::string tok;
            while (ls >> tok) {
                uint32_t p = 0, u = 0;
                if (!parseRef(tok, int(out.positions.size()), int(out.uvs.size() / 2), p, u)) continue;
                fp.push_back(p);
                fu.push_back(u);
            }
            // Fan-triangulate. MakeHuman's meshes are all-quad, which fans
            // correctly; the fan is only wrong for concave n-gons, and there
            // are none here.
            for (size_t i = 2; i < fp.size(); ++i) {
                out.posIndex.push_back(fp[0]);
                out.posIndex.push_back(fp[i - 1]);
                out.posIndex.push_back(fp[i]);
                out.uvIndex.push_back(fu[0]);
                out.uvIndex.push_back(fu[i - 1]);
                out.uvIndex.push_back(fu[i]);
            }
        }
    }
    if (out.positions.empty() || out.posIndex.empty()) {
        error = "no geometry in " + path;
        return false;
    }
    if (out.uvs.empty()) {
        error = path + " has no texture coordinates — it cannot carry a skin";
        return false;
    }
    return true;
}

bool loadImage(const std::string& path, SourceImage& out, std::string& error) {
    int w = 0, h = 0, channels = 0;
    unsigned char* data = stbi_load(path.c_str(), &w, &h, &channels, 3);
    if (data == nullptr) {
        error = std::string("stb_image: ") + (stbi_failure_reason() ? stbi_failure_reason() : "?");
        return false;
    }
    out.width = w;
    out.height = h;
    out.rgb.assign(data, data + size_t(w) * h * 3);
    stbi_image_free(data);
    return true;
}

// ---------------------------------------------------------------- align ----

namespace {

struct Bounds {
    Vec3 lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
    void add(const Vec3& p) {
        lo.x = std::min(lo.x, p.x);
        lo.y = std::min(lo.y, p.y);
        lo.z = std::min(lo.z, p.z);
        hi.x = std::max(hi.x, p.x);
        hi.y = std::max(hi.y, p.y);
        hi.z = std::max(hi.z, p.z);
    }
    float height() const { return hi.y - lo.y; }
    Vec3 centre() const { return Vec3{(lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f, (lo.z + hi.z) * 0.5f}; }
};

// Squared distance from a point to a triangle, and the barycentric coordinates
// of the closest point. The standard region-based solve.
float closestPointOnTriangle(const Vec3& p, const Vec3& a, const Vec3& b, const Vec3& c, float& u,
                             float& v, float& w) {
    const Vec3 ab = b - a, ac = c - a, ap = p - a;
    const float d1 = ab.x * ap.x + ab.y * ap.y + ab.z * ap.z;
    const float d2 = ac.x * ap.x + ac.y * ap.y + ac.z * ap.z;
    if (d1 <= 0 && d2 <= 0) {
        u = 1; v = 0; w = 0;
        const Vec3 d = p - a;
        return d.x * d.x + d.y * d.y + d.z * d.z;
    }
    const Vec3 bp = p - b;
    const float d3 = ab.x * bp.x + ab.y * bp.y + ab.z * bp.z;
    const float d4 = ac.x * bp.x + ac.y * bp.y + ac.z * bp.z;
    if (d3 >= 0 && d4 <= d3) {
        u = 0; v = 1; w = 0;
        const Vec3 d = p - b;
        return d.x * d.x + d.y * d.y + d.z * d.z;
    }
    const float vc = d1 * d4 - d3 * d2;
    if (vc <= 0 && d1 >= 0 && d3 <= 0) {
        const float t = d1 / (d1 - d3);
        u = 1 - t; v = t; w = 0;
        const Vec3 q = a + ab * t, d = p - q;
        return d.x * d.x + d.y * d.y + d.z * d.z;
    }
    const Vec3 cp = p - c;
    const float d5 = ab.x * cp.x + ab.y * cp.y + ab.z * cp.z;
    const float d6 = ac.x * cp.x + ac.y * cp.y + ac.z * cp.z;
    if (d6 >= 0 && d5 <= d6) {
        u = 0; v = 0; w = 1;
        const Vec3 d = p - c;
        return d.x * d.x + d.y * d.y + d.z * d.z;
    }
    const float vb = d5 * d2 - d1 * d6;
    if (vb <= 0 && d2 >= 0 && d6 <= 0) {
        const float t = d2 / (d2 - d6);
        u = 1 - t; v = 0; w = t;
        const Vec3 q = a + ac * t, d = p - q;
        return d.x * d.x + d.y * d.y + d.z * d.z;
    }
    const float va = d3 * d6 - d5 * d4;
    if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0) {
        const float t = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        u = 0; v = 1 - t; w = t;
        const Vec3 q = b + (c - b) * t, d = p - q;
        return d.x * d.x + d.y * d.y + d.z * d.z;
    }
    const float denom = 1.0f / (va + vb + vc);
    v = vb * denom;
    w = vc * denom;
    u = 1.0f - v - w;
    const Vec3 q = a + ab * v + ac * w, d = p - q;
    return d.x * d.x + d.y * d.y + d.z * d.z;
}

// A uniform grid over the source triangles. The body is a thin shell inside a
// small box, which is the case a grid handles well and a tree does not repay.
struct TriGrid {
    Vec3 lo{0, 0, 0};
    float cell = 0.05f;
    int nx = 1, ny = 1, nz = 1;
    std::vector<std::vector<uint32_t>> buckets;
    std::vector<Vec3> a, b, c;  // world-space triangle corners
    std::vector<uint32_t> tri;  // triangle index

    int index(int x, int y, int z) const { return (z * ny + y) * nx + x; }

    void build(const SourceMesh& mesh, const Alignment& al, float cellSize) {
        Bounds bounds;
        a.clear(); b.clear(); c.clear(); tri.clear();
        const size_t n = mesh.triangleCount();
        a.reserve(n); b.reserve(n); c.reserve(n); tri.reserve(n);
        for (size_t t = 0; t < n; ++t) {
            const Vec3 p0 = al.apply(mesh.positions[mesh.posIndex[t * 3 + 0]]);
            const Vec3 p1 = al.apply(mesh.positions[mesh.posIndex[t * 3 + 1]]);
            const Vec3 p2 = al.apply(mesh.positions[mesh.posIndex[t * 3 + 2]]);
            a.push_back(p0); b.push_back(p1); c.push_back(p2); tri.push_back(uint32_t(t));
            bounds.add(p0); bounds.add(p1); bounds.add(p2);
        }
        cell = cellSize;
        lo = Vec3{bounds.lo.x - cell, bounds.lo.y - cell, bounds.lo.z - cell};
        nx = std::max(1, int((bounds.hi.x - lo.x) / cell) + 2);
        ny = std::max(1, int((bounds.hi.y - lo.y) / cell) + 2);
        nz = std::max(1, int((bounds.hi.z - lo.z) / cell) + 2);
        buckets.assign(size_t(nx) * ny * nz, {});
        for (size_t t = 0; t < a.size(); ++t) {
            Bounds tb;
            tb.add(a[t]); tb.add(b[t]); tb.add(c[t]);
            const int x0 = std::max(0, int((tb.lo.x - lo.x) / cell));
            const int x1 = std::min(nx - 1, int((tb.hi.x - lo.x) / cell));
            const int y0 = std::max(0, int((tb.lo.y - lo.y) / cell));
            const int y1 = std::min(ny - 1, int((tb.hi.y - lo.y) / cell));
            const int z0 = std::max(0, int((tb.lo.z - lo.z) / cell));
            const int z1 = std::min(nz - 1, int((tb.hi.z - lo.z) / cell));
            for (int z = z0; z <= z1; ++z)
                for (int y = y0; y <= y1; ++y)
                    for (int x = x0; x <= x1; ++x) buckets[index(x, y, z)].push_back(uint32_t(t));
        }
    }

    // Closest point on the source surface. Searches shells of cells outward
    // and stops once the shell cannot beat what is already found.
    bool closest(const Vec3& p, float maxDist, uint32_t& outTri, float& ou, float& ov, float& ow,
                 float& outDist) const {
        const int cx = int((p.x - lo.x) / cell), cy = int((p.y - lo.y) / cell),
                  cz = int((p.z - lo.z) / cell);
        float best = maxDist * maxDist;
        bool found = false;
        const int maxRing = int(maxDist / cell) + 1;
        for (int ring = 0; ring <= maxRing; ++ring) {
            if (found && float(ring - 1) * cell > std::sqrt(best)) break;
            for (int z = cz - ring; z <= cz + ring; ++z) {
                if (z < 0 || z >= nz) continue;
                for (int y = cy - ring; y <= cy + ring; ++y) {
                    if (y < 0 || y >= ny) continue;
                    for (int x = cx - ring; x <= cx + ring; ++x) {
                        if (x < 0 || x >= nx) continue;
                        // Only the shell, not the solid block.
                        const bool onRing = std::abs(x - cx) == ring || std::abs(y - cy) == ring ||
                                            std::abs(z - cz) == ring;
                        if (!onRing) continue;
                        for (uint32_t t : buckets[index(x, y, z)]) {
                            float u, v, w;
                            const float d2 = closestPointOnTriangle(p, a[t], b[t], c[t], u, v, w);
                            if (d2 < best) {
                                best = d2;
                                outTri = tri[t];
                                ou = u; ov = v; ow = w;
                                found = true;
                            }
                        }
                    }
                }
            }
        }
        outDist = found ? std::sqrt(best) : maxDist;
        return found;
    }
};

// Mean/median distance from our body's vertices to the source surface, for a
// candidate alignment. This is the number that decides the facing.
void scoreAlignment(const SourceMesh& source, const SkinnedMeshData& body, const Alignment& al,
                    float& mean, float& median) {
    TriGrid grid;
    grid.build(source, al, 0.05f);
    std::vector<float> d;
    d.reserve(body.vertices.size());
    for (const SkinVertex& v : body.vertices) {
        uint32_t t;
        float u, vv, w, dist;
        if (grid.closest(v.position, 0.5f, t, u, vv, w, dist)) d.push_back(dist);
    }
    if (d.empty()) {
        mean = median = 1e9f;
        return;
    }
    double sum = 0;
    for (float x : d) sum += x;
    mean = float(sum / d.size());
    std::nth_element(d.begin(), d.begin() + d.size() / 2, d.end());
    median = d[d.size() / 2];
}

}  // namespace

Alignment alignToBody(const SourceMesh& source, const SkinnedMeshData& body) {
    Bounds sb, bb;
    for (const Vec3& p : source.positions) sb.add(p);
    for (const SkinVertex& v : body.vertices) bb.add(v.position);

    Alignment al;
    // Scale by measured height: both are humans, and height is the one
    // dimension neither pose nor build changes much.
    al.scale = sb.height() > 0 ? bb.height() / sb.height() : 1.0f;

    // Seat the source on the ground and centre it on our body's midline and
    // depth. Doing this AFTER scaling keeps it in our units.
    auto seat = [&](bool flipped) {
        Alignment t = al;
        t.flipped = flipped;
        t.translation = Vec3{0, 0, 0};
        Bounds moved;
        for (const Vec3& p : source.positions) moved.add(t.apply(p));
        t.translation = Vec3{bb.centre().x - moved.centre().x, bb.lo.y - moved.lo.y,
                             bb.centre().z - moved.centre().z};
        return t;
    };

    // Which way does the source face? Measure, do not assume. A wrong guess
    // here puts the face on the back of the head and nothing downstream
    // notices.
    Alignment straight = seat(false), flipped = seat(true);
    float m0 = 0, md0 = 0, m1 = 0, md1 = 0;
    scoreAlignment(source, body, straight, m0, md0);
    scoreAlignment(source, body, flipped, m1, md1);

    Alignment chosen = (m0 <= m1) ? straight : flipped;
    chosen.meanDistance = std::min(m0, m1);
    chosen.medianDistance = (m0 <= m1) ? md0 : md1;
    chosen.meanDistanceFlipped = std::max(m0, m1);
    return chosen;
}

// ---------------------------------------------------------- region fit ----

RegionFit fitRegions(const SourceMesh& source, const SkinnedMeshData& body,
                     const Alignment& alignment, int iterations) {
    RegionFit fit;
    for (size_t r = 0; r < kBodyRegionCount; ++r) {
        fit.offset[r] = Vec3{0, 0, 0};
        fit.residual[r] = 0;
        fit.before[r] = 0;
        fit.samples[r] = 0;
    }

    TriGrid grid;
    grid.build(source, alignment, 0.05f);

    // Our vertices, grouped by the region that owns them. The body already
    // publishes this segmentation, so the source needs none of its own.
    std::vector<std::vector<Vec3>> byRegion(kBodyRegionCount);
    for (const MeshPart& part : body.parts) {
        const size_t r = size_t(part.region);
        if (r >= kBodyRegionCount) continue;
        for (uint32_t i = 0; i < part.indexCount; ++i) {
            byRegion[r].push_back(body.vertices[body.indices[part.firstIndex + i]].position);
        }
    }

    for (size_t r = 0; r < kBodyRegionCount; ++r) {
        const std::vector<Vec3>& pts = byRegion[r];
        if (pts.empty()) continue;
        fit.samples[r] = uint32_t(pts.size());

        Vec3 offset{0, 0, 0};
        float residual = 0;
        for (int it = 0; it <= iterations; ++it) {
            Vec3 sum{0, 0, 0};
            double distSum = 0;
            size_t hits = 0;
            for (const Vec3& p : pts) {
                const Vec3 q = p + offset;
                uint32_t t;
                float u, v, w, dist;
                // The search radius shrinks as the fit converges: a wide first
                // pass to find the region at all, then tight enough that a
                // finger cannot latch onto the thigh beside it.
                const float radius = it == 0 ? 0.25f : 0.10f;
                if (!grid.closest(q, radius, t, u, v, w, dist)) continue;
                const Vec3 hit = grid.a[t] * u + grid.b[t] * v + grid.c[t] * w;
                sum += hit - q;
                distSum += dist;
                hits++;
            }
            if (hits == 0) break;
            residual = float(distSum / double(hits));
            if (it == 0) fit.before[r] = residual;
            if (it == iterations) break;
            // Move the query point toward where this region's surface actually
            // is. Damped, because an undamped step oscillates on curved parts.
            const float inv = 0.85f / float(hits);
            offset += Vec3{sum.x * inv, sum.y * inv, sum.z * inv};
        }
        fit.offset[r] = offset;
        fit.residual[r] = residual;
    }

    // ---- make the field continuous -------------------------------------
    // Seed every vertex with its region's offset, then relax across the
    // surface. Without this the wrist — where a 91 mm arm meets a 223 mm hand
    // — samples 13 cm away from where it should and paints a black band.
    fit.vertexPosition.resize(body.vertices.size());
    fit.vertexOffset.assign(body.vertices.size(), Vec3{0, 0, 0});
    std::vector<uint8_t> seeded(body.vertices.size(), 0);
    for (size_t i = 0; i < body.vertices.size(); ++i) fit.vertexPosition[i] = body.vertices[i].position;
    for (const MeshPart& part : body.parts) {
        const size_t r = size_t(part.region);
        if (r >= kBodyRegionCount) continue;
        for (uint32_t i = 0; i < part.indexCount; ++i) {
            const uint32_t vi = body.indices[part.firstIndex + i];
            fit.vertexOffset[vi] = fit.offset[r];
            seeded[vi] = 1;
        }
    }
    // Relax along mesh edges: a few Jacobi passes blur the step at every
    // boundary without moving the interior of a region measurably.
    std::vector<std::vector<uint32_t>> adjacency(body.vertices.size());
    for (size_t i = 0; i + 2 < body.indices.size(); i += 3) {
        const uint32_t a = body.indices[i], b = body.indices[i + 1], c = body.indices[i + 2];
        adjacency[a].push_back(b); adjacency[a].push_back(c);
        adjacency[b].push_back(a); adjacency[b].push_back(c);
        adjacency[c].push_back(a); adjacency[c].push_back(b);
    }
    std::vector<Vec3> next = fit.vertexOffset;
    for (int pass = 0; pass < 12; ++pass) {
        for (size_t i = 0; i < fit.vertexOffset.size(); ++i) {
            if (adjacency[i].empty()) { next[i] = fit.vertexOffset[i]; continue; }
            Vec3 sum = fit.vertexOffset[i];
            int n = 1;
            for (uint32_t j : adjacency[i]) { sum += fit.vertexOffset[j]; n++; }
            const float inv = 1.0f / float(n);
            next[i] = Vec3{sum.x * inv, sum.y * inv, sum.z * inv};
        }
        fit.vertexOffset.swap(next);
    }
    return fit;
}

namespace {

// The smoothed offset at an arbitrary point on our surface: inverse-distance
// blend of the nearby vertices' offsets. Continuous by construction, so a
// region boundary leaves no seam.
Vec3 offsetAt(const RegionFit& fit, const Vec3& p) {
    if (fit.vertexOffset.empty()) return Vec3{0, 0, 0};
    // The body is small and this runs once per texel over a few thousand
    // vertices; a grid would be faster but the cost is already dominated by
    // the closest-point search against the source.
    float bestD[4] = {1e9f, 1e9f, 1e9f, 1e9f};
    size_t bestI[4] = {0, 0, 0, 0};
    for (size_t i = 0; i < fit.vertexPosition.size(); ++i) {
        const Vec3 d = fit.vertexPosition[i] - p;
        const float d2 = d.x * d.x + d.y * d.y + d.z * d.z;
        for (int k = 0; k < 4; ++k) {
            if (d2 < bestD[k]) {
                for (int m = 3; m > k; --m) { bestD[m] = bestD[m - 1]; bestI[m] = bestI[m - 1]; }
                bestD[k] = d2;
                bestI[k] = i;
                break;
            }
        }
    }
    Vec3 sum{0, 0, 0};
    float wsum = 0;
    for (int k = 0; k < 4; ++k) {
        if (bestD[k] > 1e8f) continue;
        const float w = 1.0f / (std::sqrt(bestD[k]) + 1e-4f);
        const Vec3& o = fit.vertexOffset[bestI[k]];
        sum += Vec3{o.x * w, o.y * w, o.z * w};
        wsum += w;
    }
    return wsum > 0 ? Vec3{sum.x / wsum, sum.y / wsum, sum.z / wsum} : Vec3{0, 0, 0};
}

}  // namespace

// --------------------------------------------------------------- transfer --

bool transfer(const SourceMesh& source, const SourceImage& image, const Alignment& alignment,
              const RegionFit& fit, const SurfaceTexel* surface, uint32_t sheet,
              std::vector<uint8_t>& outRgbLinear, std::vector<uint8_t>& outFilled,
              TransferStats& stats) {
    if (source.empty() || !image.valid() || surface == nullptr) return false;

    TriGrid grid;
    grid.build(source, alignment, 0.05f);

    const size_t count = size_t(sheet) * sheet;
    outRgbLinear.assign(count * 3, 0);
    outFilled.assign(count, 0);
    stats = TransferStats{};

    double sum = 0, faceSum = 0;
    size_t faceCount = 0;

    for (size_t i = 0; i < count; ++i) {
        const SurfaceTexel& t = surface[i];
        if (!t.valid()) continue;

        uint32_t triIndex = 0;
        float u = 0, v = 0, w = 0, dist = 0;
        // Query where THIS REGION corresponds, not where a whole-body average
        // says it should — the two meshes are in different poses.
        const Vec3 query = t.position + offsetAt(fit, t.position);
        // 0.10 m of search: generous enough to cross the two builds'
        // differences, tight enough that a hand never matches a thigh.
        if (!grid.closest(query, 0.10f, triIndex, u, v, w, dist)) {
            stats.texelsMissed++;
            continue;
        }

        const uint32_t i0 = source.uvIndex[triIndex * 3 + 0];
        const uint32_t i1 = source.uvIndex[triIndex * 3 + 1];
        const uint32_t i2 = source.uvIndex[triIndex * 3 + 2];
        const float su = source.uvs[i0 * 2 + 0] * u + source.uvs[i1 * 2 + 0] * v +
                         source.uvs[i2 * 2 + 0] * w;
        const float sv = source.uvs[i0 * 2 + 1] * u + source.uvs[i1 * 2 + 1] * v +
                         source.uvs[i2 * 2 + 1] * w;

        // OBJ v runs bottom-up; image rows run top-down.
        const float fx = clamp01f(su) * float(image.width - 1);
        const float fy = (1.0f - clamp01f(sv)) * float(image.height - 1);
        const int x0 = int(fx), y0 = int(fy);
        const int x1 = std::min(x0 + 1, image.width - 1), y1 = std::min(y0 + 1, image.height - 1);
        const float tx = fx - float(x0), ty = fy - float(y0);
        float rgb[3] = {0, 0, 0};
        for (int ch = 0; ch < 3; ++ch) {
            const float c00 = image.rgb[(size_t(y0) * image.width + x0) * 3 + ch] / 255.0f;
            const float c10 = image.rgb[(size_t(y0) * image.width + x1) * 3 + ch] / 255.0f;
            const float c01 = image.rgb[(size_t(y1) * image.width + x0) * 3 + ch] / 255.0f;
            const float c11 = image.rgb[(size_t(y1) * image.width + x1) * 3 + ch] / 255.0f;
            const float top = c00 + (c10 - c00) * tx, bot = c01 + (c11 - c01) * tx;
            rgb[ch] = top + (bot - top) * ty;
        }

        // The source's geometry is not ours: reject what is not skin and let
        // the dilation fill it from the skin around it.
        if (!isSkinColour(rgb[0], rgb[1], rgb[2])) {
            stats.texelsRejected++;
            if (t.region == uint8_t(BodyRegion::Face)) stats.faceRejected++;
            continue;
        }

        for (int ch = 0; ch < 3; ++ch) {
            outRgbLinear[i * 3 + ch] = uint8_t(clamp01f(rgb[ch]) * 255.0f + 0.5f);
        }
        outFilled[i] = 1;
        stats.texelsWritten++;
        sum += dist;
        stats.maxDistance = std::max(stats.maxDistance, double(dist));
        if (t.region == uint8_t(BodyRegion::Face)) {
            faceSum += dist;
            faceCount++;
        }
    }

    stats.meanDistance = stats.texelsWritten ? sum / double(stats.texelsWritten) : 0.0;
    stats.faceMeanDistance = faceCount ? faceSum / double(faceCount) : 0.0;
    return stats.texelsWritten > 0;
}

}  // namespace mge::skin
