#include "mge/import/garment_fit.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <tuple>
#include <vector>

namespace mge {

// Closest point on a triangle to `p`, with its barycentric coordinates.
// The standard region-based solution: test the vertex regions, then the edge
// regions, then the interior — no iteration, no failure cases.
Vec3 closestPointOnTriangle(const Vec3& p, const Vec3& a, const Vec3& b, const Vec3& c,
                            float& outU, float& outV) {
    const Vec3 ab = b - a;
    const Vec3 ac = c - a;
    const Vec3 ap = p - a;
    const float d1 = ab.dot(ap);
    const float d2 = ac.dot(ap);
    if (d1 <= 0.0f && d2 <= 0.0f) {
        outU = 0.0f;
        outV = 0.0f;
        return a;
    }
    const Vec3 bp = p - b;
    const float d3 = ab.dot(bp);
    const float d4 = ac.dot(bp);
    if (d3 >= 0.0f && d4 <= d3) {
        outU = 1.0f;
        outV = 0.0f;
        return b;
    }
    const float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) {
        const float denom = d1 - d3;
        const float t = denom != 0.0f ? d1 / denom : 0.0f;
        outU = t;
        outV = 0.0f;
        return a + ab * t;
    }
    const Vec3 cp = p - c;
    const float d5 = ab.dot(cp);
    const float d6 = ac.dot(cp);
    if (d6 >= 0.0f && d5 <= d6) {
        outU = 0.0f;
        outV = 1.0f;
        return c;
    }
    const float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) {
        const float denom = d2 - d6;
        const float t = denom != 0.0f ? d2 / denom : 0.0f;
        outU = 0.0f;
        outV = t;
        return a + ac * t;
    }
    const float va = d3 * d6 - d5 * d4;
    if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f) {
        const float denom = (d4 - d3) + (d5 - d6);
        const float t = denom != 0.0f ? (d4 - d3) / denom : 0.0f;
        outU = 1.0f - t;
        outV = t;
        return b + (c - b) * t;
    }
    const float denom = va + vb + vc;
    const float u = denom != 0.0f ? vb / denom : 0.0f;
    const float v = denom != 0.0f ? vc / denom : 0.0f;
    outU = u;
    outV = v;
    return a + ab * u + ac * v;
}

namespace {

// ------------------------------------------------------------- geometry ----

struct TriangleFrame {
    Vec3 axis[3];  // 0 = tangent, 1 = bitangent, 2 = normal
};

// Must match `triangleFrame` in garment_binding.cpp exactly: the bake stores
// an offset in this basis and the runtime reads it back in the same basis.
TriangleFrame triangleFrame(const Vec3& a, const Vec3& b, const Vec3& c) {
    TriangleFrame f;
    const Vec3 e1 = b - a;
    const Vec3 e2 = c - a;
    Vec3 n = e1.cross(e2);
    if (n.lengthSq() <= 1e-20f) {
        f.axis[0] = Vec3{1, 0, 0};
        f.axis[1] = Vec3{0, 0, 1};
        f.axis[2] = Vec3{0, 1, 0};
        return f;
    }
    n = n.normalized();
    Vec3 t = e1;
    if (t.lengthSq() <= 1e-20f) t = Vec3{1, 0, 0};
    t = (t - n * n.dot(t));
    t = t.lengthSq() > 1e-20f ? t.normalized() : Vec3{1, 0, 0};
    f.axis[0] = t;
    f.axis[1] = n.cross(t);
    f.axis[2] = n;
    return f;
}

// -------------------------------------------------------------- weights ----

// Dense per-joint weights for one vertex during transfer and diffusion. The
// packed 4-influence form is the shipping format; this is the working form.
using DenseWeights = std::vector<float>;  // kJointCount per vertex, flattened

void addPacked(const SkinVertex& v, float scale, float* dense) {
    for (int i = 0; i < 4; ++i) {
        if (v.weights[i] == 0) continue;
        const size_t joint = v.joints[i];
        if (joint >= kJointCount) continue;
        dense[joint] += scale * static_cast<float>(v.weights[i]) * (1.0f / 255.0f);
    }
}

// Packs dense weights back into the shipping vertex: the four strongest
// influences, renormalized so the bytes sum to exactly 255. Four is the
// hardware-skinning maximum every mobile GPU agrees on (ADR 0007).
bool packWeights(const float* dense, SkinVertex& out) {
    int order[kJointCount];
    for (size_t i = 0; i < kJointCount; ++i) order[i] = static_cast<int>(i);
    std::partial_sort(order, order + 4, order + kJointCount,
                      [dense](int a, int b) { return dense[a] > dense[b]; });

    float total = 0;
    for (int i = 0; i < 4; ++i) total += std::max(0.0f, dense[order[i]]);
    if (total <= 1e-8f) return false;

    int assigned[4] = {0, 0, 0, 0};
    int sum = 0;
    for (int i = 0; i < 4; ++i) {
        const float w = std::max(0.0f, dense[order[i]]) / total;
        assigned[i] = static_cast<int>(std::lround(w * 255.0f));
        sum += assigned[i];
    }
    // Rounding never quite lands on 255; push the remainder onto the dominant
    // influence, which is where it is least visible.
    assigned[0] += 255 - sum;
    if (assigned[0] < 0) assigned[0] = 0;

    for (int i = 0; i < 4; ++i) {
        out.joints[i] = static_cast<uint8_t>(order[i]);
        out.weights[i] = static_cast<uint8_t>(std::clamp(assigned[i], 0, 255));
    }
    // Re-check the sum after clamping; the dominant influence absorbs the
    // difference so the invariant "weights sum to 255" always holds.
    int packed = 0;
    for (int i = 0; i < 4; ++i) packed += out.weights[i];
    if (packed != 255) {
        const int fix = std::clamp(static_cast<int>(out.weights[0]) + (255 - packed), 0, 255);
        out.weights[0] = static_cast<uint8_t>(fix);
    }
    return true;
}

bool hasAuthoredWeights(const SkinnedMeshData& mesh) {
    // An unweighted import lands everything on joint 0 with full weight; a
    // real rig spreads influence across at least two joints somewhere.
    for (const SkinVertex& v : mesh.vertices) {
        if (v.weights[1] != 0 || v.joints[0] != 0) return true;
    }
    return false;
}

// --------------------------------------------------------------- regions ---

// Per-vertex region, taken from the triangle ranges the mesh already carries.
// A vertex shared by two regions takes the first one that claims it, which is
// stable because the part order is stable.
void vertexRegions(const SkinnedMeshData& mesh, std::vector<uint8_t>& out) {
    out.assign(mesh.vertices.size(), static_cast<uint8_t>(kBodyRegionCount));
    for (const MeshPart& part : mesh.parts) {
        for (uint32_t i = 0; i < part.indexCount; ++i) {
            const uint32_t index = part.firstIndex + i;
            if (index >= mesh.indices.size()) continue;
            const uint32_t vertex = mesh.indices[index];
            if (vertex >= out.size()) continue;
            if (out[vertex] == static_cast<uint8_t>(kBodyRegionCount)) {
                out[vertex] = static_cast<uint8_t>(part.region);
            }
        }
    }
}

struct BaseTriangle {
    uint32_t index[3] = {0, 0, 0};
    uint32_t id = 0;
    uint8_t region = 0;
    Vec3 min{0, 0, 0};
    Vec3 max{0, 0, 0};
};

void buildTriangles(const SkinnedMeshData& mesh, std::vector<BaseTriangle>& out) {
    out.clear();
    out.reserve(mesh.triangleCount());
    // Region per triangle, from the part table.
    std::vector<uint8_t> triangleRegion(mesh.triangleCount(),
                                        static_cast<uint8_t>(BodyRegion::Torso));
    for (const MeshPart& part : mesh.parts) {
        for (uint32_t i = 0; i + 2 < part.indexCount; i += 3) {
            const uint32_t triangle = (part.firstIndex + i) / 3;
            if (triangle < triangleRegion.size()) {
                triangleRegion[triangle] = static_cast<uint8_t>(part.region);
            }
        }
    }
    for (size_t t = 0; t < mesh.triangleCount(); ++t) {
        BaseTriangle tri;
        tri.id = static_cast<uint32_t>(t);
        tri.region = triangleRegion[t];
        for (int k = 0; k < 3; ++k) tri.index[k] = mesh.indices[t * 3 + k];
        const Vec3& a = mesh.vertices[tri.index[0]].position;
        const Vec3& b = mesh.vertices[tri.index[1]].position;
        const Vec3& c = mesh.vertices[tri.index[2]].position;
        tri.min = Vec3{std::min({a.x, b.x, c.x}), std::min({a.y, b.y, c.y}),
                       std::min({a.z, b.z, c.z})};
        tri.max = Vec3{std::max({a.x, b.x, c.x}), std::max({a.y, b.y, c.y}),
                       std::max({a.z, b.z, c.z})};
        out.push_back(tri);
    }
}

float aabbDistanceSq(const Vec3& p, const Vec3& min, const Vec3& max) {
    float d = 0;
    const float px[3] = {p.x, p.y, p.z};
    const float lo[3] = {min.x, min.y, min.z};
    const float hi[3] = {max.x, max.y, max.z};
    for (int i = 0; i < 3; ++i) {
        if (px[i] < lo[i]) {
            const float t = lo[i] - px[i];
            d += t * t;
        } else if (px[i] > hi[i]) {
            const float t = px[i] - hi[i];
            d += t * t;
        }
    }
    return d;
}

// Vertices welded by EXACT position. A garment splits vertices wherever its
// UV chart or its region shells seam, so the raw index graph of a real garment
// falls into twenty-odd disconnected islands — diffusing weights across it
// strands every island that holds no trusted vertex.
//
// Welding is not just a way to connect them: two vertices at the same point
// MUST carry the same skin weights, or the garment tears open along its seams
// the first time the joint under it bends. Exact position, never a hashed one
// (MODELING.md §3.6): a hash collision would fuse two unrelated points.
void weldByPosition(const SkinnedMeshData& mesh, std::vector<uint32_t>& representative) {
    representative.resize(mesh.vertices.size());
    std::map<std::tuple<float, float, float>, uint32_t> seen;
    for (size_t i = 0; i < mesh.vertices.size(); ++i) {
        const Vec3& p = mesh.vertices[i].position;
        const auto key = std::make_tuple(p.x, p.y, p.z);
        const auto it = seen.find(key);
        if (it == seen.end()) {
            seen.emplace(key, static_cast<uint32_t>(i));
            representative[i] = static_cast<uint32_t>(i);
        } else {
            representative[i] = it->second;
        }
    }
}

// Undirected adjacency over the WELDED surface — the graph weight inpainting
// diffuses across.
void buildAdjacency(const SkinnedMeshData& mesh, const std::vector<uint32_t>& representative,
                    std::vector<uint32_t>& offsets, std::vector<uint32_t>& neighbours) {
    const size_t count = mesh.vertices.size();
    std::vector<std::vector<uint32_t>> lists(count);
    const auto link = [&](uint32_t a, uint32_t b) {
        if (a >= count || b >= count) return;
        a = representative[a];
        b = representative[b];
        if (a == b) return;
        if (std::find(lists[a].begin(), lists[a].end(), b) == lists[a].end()) {
            lists[a].push_back(b);
        }
    };
    for (size_t t = 0; t + 2 < mesh.indices.size(); t += 3) {
        const uint32_t i0 = mesh.indices[t + 0];
        const uint32_t i1 = mesh.indices[t + 1];
        const uint32_t i2 = mesh.indices[t + 2];
        link(i0, i1);
        link(i1, i0);
        link(i1, i2);
        link(i2, i1);
        link(i2, i0);
        link(i0, i2);
    }
    offsets.assign(count + 1, 0);
    for (size_t i = 0; i < count; ++i) {
        offsets[i + 1] = offsets[i] + static_cast<uint32_t>(lists[i].size());
    }
    neighbours.clear();
    neighbours.reserve(offsets.back());
    for (const std::vector<uint32_t>& list : lists) {
        neighbours.insert(neighbours.end(), list.begin(), list.end());
    }
}

}  // namespace

// ---------------------------------------------------------------- groups ---

uint32_t permittedBindRegions(BodyRegion region) {
    // A garment vertex binds within its own region plus the regions it
    // genuinely meets at a seam — never across the body. The armpit is the
    // case this exists for: torso and upper arm are millimetres apart there,
    // and an unconstrained search puts sleeve vertices on the ribcage.
    switch (region) {
        case BodyRegion::Scalp: return regionBit(BodyRegion::Scalp) | regionBit(BodyRegion::Face);
        case BodyRegion::Face: return regionBit(BodyRegion::Face) | regionBit(BodyRegion::Scalp);
        case BodyRegion::Neck:
            return regionBit(BodyRegion::Neck) | regionBit(BodyRegion::Torso) |
                   regionBit(BodyRegion::Face);
        case BodyRegion::Torso:
            return regionBit(BodyRegion::Torso) | regionBit(BodyRegion::Neck);
        case BodyRegion::ArmL:
            return regionBit(BodyRegion::ArmL) | regionBit(BodyRegion::HandL);
        case BodyRegion::ArmR:
            return regionBit(BodyRegion::ArmR) | regionBit(BodyRegion::HandR);
        case BodyRegion::HandL:
            return regionBit(BodyRegion::HandL) | regionBit(BodyRegion::ArmL);
        case BodyRegion::HandR:
            return regionBit(BodyRegion::HandR) | regionBit(BodyRegion::ArmR);
        case BodyRegion::LegL:
            return regionBit(BodyRegion::LegL) | regionBit(BodyRegion::FootL);
        case BodyRegion::LegR:
            return regionBit(BodyRegion::LegR) | regionBit(BodyRegion::FootR);
        case BodyRegion::FootL:
            return regionBit(BodyRegion::FootL) | regionBit(BodyRegion::LegL);
        case BodyRegion::FootR:
            return regionBit(BodyRegion::FootR) | regionBit(BodyRegion::LegR);
        case BodyRegion::Count: break;
    }
    return kAllRegions;
}

// ----------------------------------------------------------------- bake ----

void offsetSurface(const SkinnedMeshData& src, float thickness, SkinnedMeshData& out) {
    out = src;
    for (size_t i = 0; i < out.vertices.size(); ++i) {
        const Vec3 n = src.vertices[i].normal;
        if (n.lengthSq() <= 1e-12f) continue;
        out.vertices[i].position = src.vertices[i].position + n.normalized() * thickness;
    }
    out.computeBounds();
}

bool fitGarment(const SkinnedMeshData& base, uint64_t rootBodyHash, SkinnedMeshData& garment,
                uint8_t layer, const FitParams& params, GarmentBinding& outBinding,
                FitReport& outReport) {
    outBinding.clear();
    outReport = FitReport{};
    outReport.garmentVertices = garment.vertices.size();

    if (base.vertices.empty() || base.indices.size() < 3) {
        std::snprintf(outReport.message, sizeof outReport.message,
                      "base surface is empty - nothing to fit against");
        return false;
    }
    if (garment.vertices.empty()) {
        std::snprintf(outReport.message, sizeof outReport.message,
                      "garment mesh has no vertices");
        return false;
    }

    std::vector<BaseTriangle> triangles;
    buildTriangles(base, triangles);

    std::vector<uint8_t> garmentRegion;
    vertexRegions(garment, garmentRegion);

    const size_t count = garment.vertices.size();
    std::vector<float> dense(count * kJointCount, 0.0f);
    std::vector<uint8_t> trusted(count, 0);
    size_t unbound = 0;  // vertices with no permitted surface to sit on

    outBinding.binds.resize(count);
    outBinding.baseVertexCount = static_cast<uint32_t>(base.vertices.size());
    outBinding.baseTriangleCount = static_cast<uint32_t>(base.triangleCount());
    outBinding.baseHash = skinnedMeshContentHash(base);
    outBinding.rootBodyHash = rootBodyHash != 0 ? rootBodyHash : outBinding.baseHash;
    outBinding.layer = layer;

    const bool keepAuthored = params.acceptAuthoredWeights && hasAuthoredWeights(garment);
    if (keepAuthored) outReport.authored = count;

    float offsetSum = 0;
    float offsetMax = 0;

    for (size_t i = 0; i < count; ++i) {
        const SkinVertex& gv = garment.vertices[i];
        const uint32_t permitted =
            params.constrainToRegions && garmentRegion[i] < kBodyRegionCount
                ? permittedBindRegions(static_cast<BodyRegion>(garmentRegion[i]))
                : kAllRegions;

        // Nearest permitted point on the base surface. Brute force with an
        // AABB reject: a 2200-triangle body against a 1600-vertex garment is
        // a bake-time cost measured in milliseconds, and a spatial index would
        // be one more thing that can silently differ between builds.
        float bestDistSq = 3.4e38f;
        const BaseTriangle* bestTri = nullptr;
        float bestU = 0, bestV = 0;
        Vec3 bestPoint{0, 0, 0};

        for (const BaseTriangle& tri : triangles) {
            if ((permitted & regionBit(static_cast<BodyRegion>(tri.region))) == 0) continue;
            if (aabbDistanceSq(gv.position, tri.min, tri.max) >= bestDistSq) continue;
            const Vec3& a = base.vertices[tri.index[0]].position;
            const Vec3& b = base.vertices[tri.index[1]].position;
            const Vec3& c = base.vertices[tri.index[2]].position;
            float u = 0, v = 0;
            const Vec3 point = closestPointOnTriangle(gv.position, a, b, c, u, v);
            const float distSq = (point - gv.position).lengthSq();
            if (distSq < bestDistSq) {
                bestDistSq = distSq;
                bestTri = &tri;
                bestU = u;
                bestV = v;
                bestPoint = point;
            }
        }

        if (bestTri == nullptr) {
            // No permitted triangle at all — the garment declares a region the
            // body does not have. Binds to nothing; counted once below.
            ++unbound;
            continue;
        }

        const float w = 1.0f - bestU - bestV;
        const Vec3& a = base.vertices[bestTri->index[0]].position;
        const Vec3& b = base.vertices[bestTri->index[1]].position;
        const Vec3& c = base.vertices[bestTri->index[2]].position;
        const TriangleFrame frame = triangleFrame(a, b, c);

        // The binding is recorded for EVERY vertex, trusted or not: the
        // confidence gate governs whether the body's WEIGHTS are believed, not
        // whether the vertex has a place on the surface. A sleeve's underside
        // still needs to follow the arm; it just must not be weighted to the
        // ribcage while doing so.
        SurfaceBind& bind = outBinding.binds[i];
        bind.triangle = bestTri->id;
        bind.bary[0] = static_cast<uint16_t>(std::lround(std::clamp(bestU, 0.0f, 1.0f) * 65535.0f));
        bind.bary[1] = static_cast<uint16_t>(std::lround(std::clamp(bestV, 0.0f, 1.0f) * 65535.0f));

        const Vec3 delta = gv.position - bestPoint;
        const float local[3] = {delta.dot(frame.axis[0]), delta.dot(frame.axis[1]),
                                delta.dot(frame.axis[2])};
        const float distance = std::sqrt(bestDistSq);
        offsetSum += distance;
        offsetMax = std::max(offsetMax, distance);
        for (int k = 0; k < 3; ++k) {
            const float clamped = std::clamp(local[k], -0.5f, 0.5f);
            bind.offset[k] = static_cast<int16_t>(std::lround(clamped * 65534.0f));
        }

        if (keepAuthored) continue;

        // Confidence gate (13.1): near enough AND facing the same way.
        const Vec3 surfaceNormal = (base.vertices[bestTri->index[0]].normal * w +
                                    base.vertices[bestTri->index[1]].normal * bestU +
                                    base.vertices[bestTri->index[2]].normal * bestV);
        const bool nearEnough = distance <= params.maxDistance;
        const bool facesSameWay =
            surfaceNormal.lengthSq() > 1e-12f && gv.normal.lengthSq() > 1e-12f &&
            surfaceNormal.normalized().dot(gv.normal.normalized()) >= params.minNormalDot;

        if (nearEnough && facesSameWay) {
            float* row = &dense[i * kJointCount];
            addPacked(base.vertices[bestTri->index[0]], w, row);
            addPacked(base.vertices[bestTri->index[1]], bestU, row);
            addPacked(base.vertices[bestTri->index[2]], bestV, row);
            trusted[i] = 1;
            ++outReport.matched;
        }
    }

    outBinding.offsetScale = 0.5f;  // the clamp above, in metres
    outReport.maxOffset = offsetMax;
    outReport.meanOffset = count > 0 ? offsetSum / static_cast<float>(count) : 0.0f;

    if (keepAuthored) {
        outReport.stranded = unbound;
        outReport.ok = outReport.stranded == 0;
        std::snprintf(outReport.message, sizeof outReport.message,
                      "%s: kept authored weights for %zu vertices; bound to surface "
                      "(mean offset %.1f mm, max %.1f mm)%s",
                      outReport.ok ? "OK" : "FAILED", count, outReport.meanOffset * 1000.0f,
                      outReport.maxOffset * 1000.0f,
                      outReport.stranded > 0 ? "; some vertices found no permitted surface" : "");
        return outReport.ok;
    }

    // Weight inpainting (13.1): diffuse the trusted weights across the
    // garment's own surface into the vertices the gate rejected. This is the
    // step that makes a loose sleeve or a hanging hem behave — the published
    // fix for exactly the case naive nearest-point transfer gets wrong.
    //
    // It runs on the WELDED graph, because a real garment is split at every
    // seam and the raw index graph is not connected (see `weldByPosition`).
    if (!keepAuthored && outReport.matched > 0) {
        std::vector<uint32_t> representative;
        weldByPosition(garment, representative);

        // Collapse onto representatives: a welded point is trusted if any of
        // its split copies was, and takes the mean of the trusted copies.
        std::vector<float> welded(count * kJointCount, 0.0f);
        std::vector<uint32_t> contributors(count, 0);
        for (size_t i = 0; i < count; ++i) {
            if (!trusted[i]) continue;
            const uint32_t r = representative[i];
            const float* src = &dense[i * kJointCount];
            float* dst = &welded[r * kJointCount];
            for (size_t k = 0; k < kJointCount; ++k) dst[k] += src[k];
            ++contributors[r];
        }
        std::vector<uint8_t> known(count, 0);
        for (size_t r = 0; r < count; ++r) {
            if (contributors[r] == 0) continue;
            const float inv = 1.0f / static_cast<float>(contributors[r]);
            float* row = &welded[r * kJointCount];
            for (size_t k = 0; k < kJointCount; ++k) row[k] *= inv;
            known[r] = 1;
        }

        // Which points were trusted BEFORE diffusion. A split copy of one of
        // these inherited its weights by welding, which is not the same event
        // as being filled by diffusion — and an artist reads these numbers to
        // judge whether the transfer struggled, so they are counted apart.
        const std::vector<uint8_t> directlyKnown = known;

        std::vector<uint32_t> offsets, neighbours;
        buildAdjacency(garment, representative, offsets, neighbours);
        std::vector<float> next(welded.size(), 0.0f);
        std::vector<uint8_t> nextKnown(count, 0);

        for (int iteration = 0; iteration < params.inpaintIterations; ++iteration) {
            bool changed = false;
            next = welded;
            nextKnown = known;
            for (size_t r = 0; r < count; ++r) {
                if (known[r] || representative[r] != r) continue;
                float accum[kJointCount] = {};
                int sources = 0;
                for (uint32_t n = offsets[r]; n < offsets[r + 1]; ++n) {
                    const uint32_t j = neighbours[n];
                    if (!known[j]) continue;
                    const float* row = &welded[j * kJointCount];
                    for (size_t k = 0; k < kJointCount; ++k) accum[k] += row[k];
                    ++sources;
                }
                if (sources == 0) continue;
                float* row = &next[r * kJointCount];
                const float inv = 1.0f / static_cast<float>(sources);
                for (size_t k = 0; k < kJointCount; ++k) row[k] = accum[k] * inv;
                nextKnown[r] = 1;
                changed = true;
            }
            welded.swap(next);
            known.swap(nextKnown);
            if (!changed) break;
        }

        // Scatter back: every split copy of a point takes its point's weights,
        // so a seam cannot come apart under animation.
        for (size_t i = 0; i < count; ++i) {
            const uint32_t r = representative[i];
            if (!known[r]) continue;
            if (!trusted[i]) {
                if (directlyKnown[r]) {
                    ++outReport.welded;
                } else {
                    ++outReport.inpainted;
                }
            }
            float* dst = &dense[i * kJointCount];
            const float* src = &welded[r * kJointCount];
            for (size_t k = 0; k < kJointCount; ++k) dst[k] = src[k];
            trusted[i] = 1;
        }
    }

    // Pack, and count what could not be resolved at all.
    for (size_t i = 0; i < count; ++i) {
        if (!trusted[i] || !packWeights(&dense[i * kJointCount], garment.vertices[i])) {
            ++outReport.stranded;
        }
    }

    outReport.ok = outReport.stranded == 0;
    if (outReport.ok) {
        std::snprintf(outReport.message, sizeof outReport.message,
                      "OK: %zu vertices bound (%zu matched, %zu welded, %zu inpainted); "
                      "mean offset %.1f mm, max %.1f mm",
                      count, outReport.matched, outReport.welded, outReport.inpainted,
                      outReport.meanOffset * 1000.0f, outReport.maxOffset * 1000.0f);
    } else if (outReport.matched == 0) {
        std::snprintf(outReport.message, sizeof outReport.message,
                      "FAILED: no vertex passed the confidence gate (max distance %.0f mm, "
                      "normal agreement %.2f). The garment is probably not modelled against "
                      "this body, or its normals are inverted",
                      params.maxDistance * 1000.0f, params.minNormalDot);
    } else {
        std::snprintf(outReport.message, sizeof outReport.message,
                      "FAILED: %zu of %zu vertices could not be weighted - they are isolated "
                      "from every trusted vertex. Check for loose geometry detached from the "
                      "garment's main surface",
                      outReport.stranded, count);
    }
    return outReport.ok;
}

bool fitGarment(const SkinnedMeshData& body, SkinnedMeshData& garment, uint8_t layer,
                const FitParams& params, GarmentBinding& outBinding, FitReport& outReport) {
    return fitGarment(body, skinnedMeshContentHash(body), garment, layer, params, outBinding,
                      outReport);
}

bool fitGarmentStack(const SkinnedMeshData& body, std::vector<SkinnedMeshData*>& garments,
                     const std::vector<uint8_t>& layers, const FitParams& params,
                     std::vector<GarmentBinding>& outBindings,
                     std::vector<FitReport>& outReports) {
    outBindings.assign(garments.size(), GarmentBinding{});
    outReports.assign(garments.size(), FitReport{});
    if (garments.size() != layers.size()) return false;

    const uint64_t rootHash = skinnedMeshContentHash(body);

    // The surface the next layer binds against. Starts as the body; after each
    // garment it becomes that garment's own surface pushed out by its
    // thickness — which is what makes layer k+1 enclose layer k on every body
    // the chain is later evaluated for (13.5).
    SkinnedMeshData surface = body;
    bool allOk = true;

    for (size_t i = 0; i < garments.size(); ++i) {
        if (garments[i] == nullptr) {
            allOk = false;
            continue;
        }
        const uint8_t layer = layers[i];
        if (!fitGarment(surface, rootHash, *garments[i], layer, params, outBindings[i],
                        outReports[i])) {
            allOk = false;
        }
        const float thickness = params.layerThickness[layer < 3 ? layer : 2];
        SkinnedMeshData next;
        offsetSurface(*garments[i], thickness, next);
        surface = next;
    }
    return allOk;
}

}  // namespace mge
