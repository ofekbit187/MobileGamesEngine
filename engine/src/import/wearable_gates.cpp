#include "mge/import/wearable_gates.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <map>
#include <queue>
#include <set>
#include <string>
#include <tuple>
#include <vector>

#include "mge/character/garment_binding.h"
#include "mge/graphics/mesh_io.h"
#include "mge/import/garment_fit.h"

namespace mge {

namespace {

const char* kRegionName[kBodyRegionCount] = {"Scalp", "Face", "Neck",  "Torso",
                                             "ArmL",  "ArmR", "HandL", "HandR",
                                             "LegL",  "LegR", "FootL", "FootR"};

const char* wearableKindName(WearableKind kind) {
    switch (kind) {
        case WearableKind::Tunic: return "tunic";
        case WearableKind::Armor: return "armour";
        case WearableKind::Pants: return "trousers";
        case WearableKind::Boots: return "boots";
        case WearableKind::HairShort: return "short hair";
        case WearableKind::HairLong: return "long hair";
        case WearableKind::Sword: return "sword";
    }
    return "?";
}

const char* regionName(BodyRegion r) {
    const size_t i = static_cast<size_t>(r);
    return i < kBodyRegionCount ? kRegionName[i] : "?";
}

void say(GateResult& result, const char* format, ...) __attribute__((format(printf, 2, 3)));

void say(GateResult& result, const char* format, ...) {
    va_list args;
    va_start(args, format);
    std::vsnprintf(result.detail, sizeof result.detail, format, args);
    va_end(args);
}

// Per-vertex and per-triangle region, from the part table the body ships.
void regionTables(const SkinnedMeshData& mesh, std::vector<uint8_t>& vertexRegion,
                  std::vector<uint8_t>& triangleRegion) {
    vertexRegion.assign(mesh.vertices.size(), static_cast<uint8_t>(kBodyRegionCount));
    triangleRegion.assign(mesh.triangleCount(), static_cast<uint8_t>(kBodyRegionCount));
    for (const MeshPart& part : mesh.parts) {
        for (uint32_t i = 0; i + 2 < part.indexCount; i += 3) {
            const uint32_t triangle = (part.firstIndex + i) / 3;
            if (triangle < triangleRegion.size()) {
                triangleRegion[triangle] = static_cast<uint8_t>(part.region);
            }
        }
        for (uint32_t i = 0; i < part.indexCount; ++i) {
            const uint32_t index = part.firstIndex + i;
            if (index >= mesh.indices.size()) continue;
            const uint32_t vertex = mesh.indices[index];
            if (vertex < vertexRegion.size() &&
                vertexRegion[vertex] == static_cast<uint8_t>(kBodyRegionCount)) {
                vertexRegion[vertex] = static_cast<uint8_t>(part.region);
            }
        }
    }
}

// Vertices welded by exact position. The body splits vertices at its UV
// seams, so index-identity edges do not match across a seam even though the
// surface is continuous there (MODELING.md §3.6; the same reason the fitting
// bake welds before diffusing weights).
int64_t weldKey(const Vec3& p) {
    const int64_t x = static_cast<int64_t>(std::lround(p.x * 100000.0f));
    const int64_t y = static_cast<int64_t>(std::lround(p.y * 100000.0f));
    const int64_t z = static_cast<int64_t>(std::lround(p.z * 100000.0f));
    return (x * 73856093LL) ^ (y * 19349663LL) ^ (z * 83492791LL);
}

// Is the surface made of the given regions closed and consistently wound?
// Every directed edge must occur exactly once and its opposite must exist.
bool maskedSurfaceIsClosed(const SkinnedMeshData& mesh, uint32_t regions,
                           std::string* outWhy = nullptr) {
    std::map<std::pair<int64_t, int64_t>, int> directed;
    for (const MeshPart& part : mesh.parts) {
        if ((regions & regionBit(part.region)) == 0) continue;
        for (uint32_t i = 0; i + 2 < part.indexCount; i += 3) {
            int64_t k[3];
            for (int e = 0; e < 3; ++e) {
                k[e] = weldKey(mesh.vertices[mesh.indices[part.firstIndex + i + e]].position);
            }
            for (int e = 0; e < 3; ++e) ++directed[{k[e], k[(e + 1) % 3]}];
        }
    }
    for (const auto& entry : directed) {
        if (entry.second != 1) {
            if (outWhy != nullptr) *outWhy = "an edge is used more than once";
            return false;
        }
        if (directed.find({entry.first.second, entry.first.first}) == directed.end()) {
            if (outWhy != nullptr) *outWhy = "an edge has no opposite (open boundary)";
            return false;
        }
    }
    return true;
}

// The welded vertices left on the rim when `regions` are masked away: edges
// whose opposite is gone. On a one-shell body this is exactly the cut line a
// mask opens, and what the garment causing the mask has to cover.
void maskBoundary(const SkinnedMeshData& mesh, uint32_t keptRegions,
                  std::vector<Vec3>& outRim) {
    outRim.clear();
    std::map<std::pair<int64_t, int64_t>, int> directed;
    std::map<int64_t, Vec3> byKey;
    for (const MeshPart& part : mesh.parts) {
        if ((keptRegions & regionBit(part.region)) == 0) continue;
        for (uint32_t i = 0; i + 2 < part.indexCount; i += 3) {
            int64_t k[3];
            for (int e = 0; e < 3; ++e) {
                const Vec3& p = mesh.vertices[mesh.indices[part.firstIndex + i + e]].position;
                k[e] = weldKey(p);
                byKey[k[e]] = p;
            }
            for (int e = 0; e < 3; ++e) ++directed[{k[e], k[(e + 1) % 3]}];
        }
    }
    std::set<int64_t> rim;
    for (const auto& entry : directed) {
        if (directed.find({entry.first.second, entry.first.first}) == directed.end()) {
            rim.insert(entry.first.first);
            rim.insert(entry.first.second);
        }
    }
    for (int64_t key : rim) outRim.push_back(byKey[key]);
}

// Closest approach between two regions' surfaces, point-to-triangle.
float closestApproach(const SkinnedMeshData& mesh, const std::vector<Vec3>& position,
                      const std::vector<uint8_t>& vertexRegion,
                      const std::vector<uint8_t>& triangleRegion, BodyRegion a, BodyRegion b,
                      float& outY) {
    float best = 1e30f;
    outY = 0;
    for (size_t v = 0; v < mesh.vertices.size(); ++v) {
        if (vertexRegion[v] != static_cast<uint8_t>(a)) continue;
        const Vec3& p = position[v];
        for (size_t t = 0; t < mesh.triangleCount(); ++t) {
            if (triangleRegion[t] != static_cast<uint8_t>(b)) continue;
            const Vec3& t0 = position[mesh.indices[t * 3 + 0]];
            const Vec3& t1 = position[mesh.indices[t * 3 + 1]];
            const Vec3& t2 = position[mesh.indices[t * 3 + 2]];
            float u = 0, w = 0;
            const Vec3 point = closestPointOnTriangle(p, t0, t1, t2, u, w);
            const float d = (point - p).length();
            if (d < best) {
                best = d;
                outY = p.y;
            }
        }
    }
    return best;
}

struct ShapeCase {
    const char* name;
    HumanoidVariant variant;
};

std::vector<ShapeCase> shapeExtremes() {
    std::vector<ShapeCase> cases;
    cases.push_back({"template", HumanoidVariant{}});
    HumanoidVariant heavy;
    heavy.belly = 1;
    heavy.chest = 1;
    heavy.seat = 1;
    heavy.muscle = 1;
    heavy.neck = 1;
    cases.push_back({"shape max", heavy});
    HumanoidVariant lean;
    lean.belly = -1;
    lean.chest = -1;
    lean.seat = -1;
    lean.muscle = -1;
    lean.neck = -1;
    cases.push_back({"shape min", lean});
    return cases;
}

}  // namespace

const char* gateStatusName(GateStatus status) {
    switch (status) {
        case GateStatus::Pass: return "PASS";
        case GateStatus::Fail: return "FAIL";
        case GateStatus::Blocked: return "BLOCKED";
    }
    return "?";
}

// -------------------------------------------------------- pit measuring ----

const PitMeasurement* recordedPits(size_t& outCount) {
    // Minimum across template / shape max / shape min, body c4f91ac3fa2fcff5.
    static const PitMeasurement pits[] = {
        {BodyRegion::LegL, BodyRegion::LegR, 1.0f, 0.72f},    // inner thigh, shape max
        {BodyRegion::ArmR, BodyRegion::ArmR, 12.0f, 0.99f},   // arm to itself, shape min
        {BodyRegion::Face, BodyRegion::Neck, 14.7f, 1.54f},   // under the jaw
        {BodyRegion::HandL, BodyRegion::HandL, 16.5f, 0.75f}, // thumb to palm
        {BodyRegion::HandR, BodyRegion::HandR, 17.7f, 0.75f},
        {BodyRegion::Face, BodyRegion::Face, 18.8f, 1.55f},
        {BodyRegion::ArmL, BodyRegion::ArmL, 22.1f, 1.01f},
        {BodyRegion::Neck, BodyRegion::Neck, 26.6f, 1.51f},
        {BodyRegion::LegR, BodyRegion::LegR, 30.9f, 0.20f},
    };
    outCount = sizeof pits / sizeof pits[0];
    return pits;
}


void measurePits(const SkinnedMeshData& body, const float weights[kMorphCount],
                 std::vector<PitMeasurement>& out) {
    out.clear();
    const size_t vertexCount = body.vertices.size();
    if (vertexCount == 0 || body.indices.size() < 3) return;

    std::vector<Vec3> position, normal;
    morphedSurface(body, weights, position, normal);

    // Weld first: the surface graph has to be continuous across UV seams or
    // "far along the surface" is measured on a mesh that is full of cuts.
    std::map<std::tuple<float, float, float>, uint32_t> seen;
    std::vector<uint32_t> rep(vertexCount);
    for (size_t i = 0; i < vertexCount; ++i) {
        const Vec3& p = body.vertices[i].position;
        const auto key = std::make_tuple(p.x, p.y, p.z);
        const auto it = seen.find(key);
        if (it == seen.end()) {
            seen.emplace(key, static_cast<uint32_t>(i));
            rep[i] = static_cast<uint32_t>(i);
        } else {
            rep[i] = it->second;
        }
    }
    std::vector<std::vector<uint32_t>> adjacency(vertexCount);
    for (size_t t = 0; t + 2 < body.indices.size(); t += 3) {
        const uint32_t v[3] = {rep[body.indices[t]], rep[body.indices[t + 1]],
                               rep[body.indices[t + 2]]};
        for (int e = 0; e < 3; ++e) {
            adjacency[v[e]].push_back(v[(e + 1) % 3]);
            adjacency[v[(e + 1) % 3]].push_back(v[e]);
        }
    }

    std::vector<uint8_t> vertexRegion, triangleRegion;
    regionTables(body, vertexRegion, triangleRegion);

    // Tightest gap per region pair, so the answer is a short readable list
    // instead of two thousand vertex records.
    std::map<std::pair<uint8_t, uint8_t>, PitMeasurement> tightest;
    std::vector<float> geodesic(vertexCount);

    for (size_t s = 0; s < vertexCount; ++s) {
        if (rep[s] != s) continue;
        std::fill(geodesic.begin(), geodesic.end(), 1e30f);
        std::priority_queue<std::pair<float, uint32_t>, std::vector<std::pair<float, uint32_t>>,
                            std::greater<>>
            frontier;
        geodesic[s] = 0;
        frontier.push({0.0f, static_cast<uint32_t>(s)});
        while (!frontier.empty()) {
            const std::pair<float, uint32_t> top = frontier.top();
            frontier.pop();
            if (top.first > geodesic[top.second] || top.first > kPitGeodesicRadiusM) continue;
            for (uint32_t n : adjacency[top.second]) {
                const float step =
                    top.first + (position[n] - position[top.second]).length();
                if (step < geodesic[n] && step <= kPitGeodesicRadiusM) {
                    geodesic[n] = step;
                    frontier.push({step, n});
                }
            }
        }

        const Vec3& p = position[s];
        float best = 1e30f;
        uint8_t bestRegion = static_cast<uint8_t>(kBodyRegionCount);
        for (size_t t = 0; t < body.triangleCount(); ++t) {
            const uint32_t a = rep[body.indices[t * 3 + 0]];
            const uint32_t b = rep[body.indices[t * 3 + 1]];
            const uint32_t c = rep[body.indices[t * 3 + 2]];
            // Local patch: near along the surface, so any small gap here is
            // curvature, not a pit.
            if (geodesic[a] <= kPitGeodesicRadiusM || geodesic[b] <= kPitGeodesicRadiusM ||
                geodesic[c] <= kPitGeodesicRadiusM) {
                continue;
            }
            float u = 0, w = 0;
            const Vec3 point = closestPointOnTriangle(p, position[body.indices[t * 3 + 0]],
                                                      position[body.indices[t * 3 + 1]],
                                                      position[body.indices[t * 3 + 2]], u, w);
            const float d = (point - p).length();
            if (d < best) {
                best = d;
                bestRegion = triangleRegion[t];
            }
        }
        if (bestRegion >= kBodyRegionCount) continue;

        const uint8_t ra = vertexRegion[s] < kBodyRegionCount ? vertexRegion[s] : bestRegion;
        const std::pair<uint8_t, uint8_t> key{std::min(ra, bestRegion), std::max(ra, bestRegion)};
        const float mm = best * 1000.0f;
        const auto existing = tightest.find(key);
        if (existing == tightest.end() || mm < existing->second.millimetres) {
            PitMeasurement m;
            m.a = static_cast<BodyRegion>(key.first);
            m.b = static_cast<BodyRegion>(key.second);
            m.millimetres = mm;
            m.height = p.y;
            tightest[key] = m;
        }
    }

    for (const auto& entry : tightest) out.push_back(entry.second);
    std::sort(out.begin(), out.end(), [](const PitMeasurement& a, const PitMeasurement& b) {
        return a.millimetres < b.millimetres;
    });
}

// -------------------------------------------------------------- gate 1 -----

GateResult gateMaskIntegrity(const std::vector<SkinnedMeshData>& lods) {
    GateResult result;
    result.id = "§9.1";
    result.name = "Mask integrity";
    if (lods.empty() || lods[0].vertices.empty()) {
        result.status = GateStatus::Blocked;
        say(result, "no body asset loaded - nothing to judge");
        return result;
    }

    // The imported body is ONE watertight shell whose regions partition its
    // triangles — not the per-region closed shells the v2 generated body had.
    // So masking a region necessarily OPENS a boundary, and "no hole" cannot
    // mean "still closed". What it must mean instead is that the rim a mask
    // opens is covered by the garment that opened it: the cut line lives
    // under the cloth, exactly as a hidden geoset boundary does.
    const SkinnedMeshData& body = lods[0];

    // Judged per OUTFIT, not per garment. "Does this character show a hole?"
    // is the only question that matters, and it is not answerable one garment
    // at a time: armour is an outer layer meant to sit over a tunic, and a
    // trouser hem is met by a boot cuff. A lone garment leaving a rim short is
    // only a defect if some outfit actually leaves it that way.
    struct Outfit {
        const char* name;
        WearableKind worn[4];
        size_t count;
    };
    const Outfit outfits[] = {
        {"tunic + trousers + boots (D-4 catalogue)",
         {WearableKind::Tunic, WearableKind::Pants, WearableKind::Boots}, 3},
        {"armour + tunic + trousers + boots",
         {WearableKind::Armor, WearableKind::Tunic, WearableKind::Pants, WearableKind::Boots},
         4},
        {"armour + trousers + boots (no tunic under)",
         {WearableKind::Armor, WearableKind::Pants, WearableKind::Boots}, 3},
        {"tunic alone", {WearableKind::Tunic}, 1},
        {"trousers alone", {WearableKind::Pants}, 1},
    };

    // A rim vertex is covered when some worn garment's surface reaches it. The
    // allowance is an outer layer's thickness plus the margin a hem needs to
    // sit past the cut rather than exactly on it.
    constexpr float kRimAllowanceMm = 45.0f;

    size_t checked = 0;
    float worstMm = 0;
    char worstLine[256] = {};

    for (const Outfit& outfit : outfits) {
        uint32_t covers = 0;
        bool haveAll = true;
        for (size_t i = 0; i < outfit.count; ++i) {
            if (sharedGarment(outfit.worn[i]).vertices.empty()) haveAll = false;
            covers |= garmentCoverage(outfit.worn[i]);
        }
        if (!haveAll || covers == 0) continue;

        std::vector<Vec3> rim;
        maskBoundary(body, kAllRegions & ~covers, rim);
        if (rim.empty()) continue;

        for (const Vec3& p : rim) {
            float best = 1e30f;
            for (size_t i = 0; i < outfit.count; ++i) {
                const SkinnedMeshData& garment = sharedGarment(outfit.worn[i]);
                for (size_t t = 0; t + 2 < garment.indices.size(); t += 3) {
                    float u = 0, w = 0;
                    const Vec3 point = closestPointOnTriangle(
                        p, garment.vertices[garment.indices[t]].position,
                        garment.vertices[garment.indices[t + 1]].position,
                        garment.vertices[garment.indices[t + 2]].position, u, w);
                    best = std::min(best, (point - p).length());
                }
            }
            const float mm = best * 1000.0f;
            if (mm > worstMm) {
                worstMm = mm;
                std::snprintf(worstLine, sizeof worstLine, "%s, at y=%.2f m", outfit.name, p.y);
            }
        }
        ++checked;
    }

    if (checked == 0) {
        result.status = GateStatus::Blocked;
        say(result, "no masking garments are loaded - nothing to judge");
        return result;
    }
    if (worstMm > kRimAllowanceMm) {
        result.status = GateStatus::Fail;
        say(result,
            "an outfit leaves %.0f mm of masked-away body rim uncovered (%s; allowance "
            "%.0f mm). Masking a region on a one-shell body opens a cut line, and nothing "
            "this outfit wears reaches it - the character shows a hole there",
            worstMm, worstLine, kRimAllowanceMm);
        return result;
    }

    result.status = GateStatus::Pass;
    say(result,
        "%zu outfits checked; every rim a mask opens is reached by something the outfit wears "
        "(worst %.0f mm, allowance %.0f mm). NOTE: this body is ONE shell, so a mask opens a "
        "boundary by construction - the property that holds is covered, not closed",
        checked, worstMm, kRimAllowanceMm);
    return result;
}

// -------------------------------------------------------------- gate 2 -----

GateResult gatePitClearance(const SkinnedMeshData& body) {
    GateResult result;
    result.id = "B-24";
    result.name = "Pit clearance";
    if (body.vertices.empty()) {
        result.status = GateStatus::Blocked;
        say(result, "no body asset loaded - nothing to measure");
        return result;
    }

    size_t recordedCount = 0;
    const PitMeasurement* recorded = recordedPits(recordedCount);

    // Tightest measurement per pair across every shape we ship.
    std::map<std::pair<uint8_t, uint8_t>, PitMeasurement> measured;
    std::map<std::pair<uint8_t, uint8_t>, const char*> shapeOf;
    for (const ShapeCase& shape : shapeExtremes()) {
        float weights[kMorphCount];
        morphWeights(shape.variant, weights);
        std::vector<PitMeasurement> pits;
        measurePits(body, weights, pits);
        for (const PitMeasurement& pit : pits) {
            const std::pair<uint8_t, uint8_t> key{static_cast<uint8_t>(pit.a),
                                                  static_cast<uint8_t>(pit.b)};
            const auto it = measured.find(key);
            if (it == measured.end() || pit.millimetres < it->second.millimetres) {
                measured[key] = pit;
                shapeOf[key] = shape.name;
            }
        }
    }
    if (measured.empty()) {
        result.status = GateStatus::Blocked;
        say(result, "no pit could be measured - the body surface graph is not connected");
        return result;
    }

    const PitMeasurement* tightest = nullptr;
    const char* tightestShape = "";
    for (const auto& entry : measured) {
        if (tightest == nullptr || entry.second.millimetres < tightest->millimetres) {
            tightest = &entry.second;
            tightestShape = shapeOf[entry.first];
        }
    }

    // A body that passes through itself cannot be dressed at all.
    if (tightest->millimetres < kPitIntersectionMm) {
        result.status = GateStatus::Fail;
        say(result,
            "the body touches or passes through itself: %s-%s is %.1f mm apart at y=%.2f m "
            "(%s). No garment can be fitted across a self-intersection",
            regionName(tightest->a), regionName(tightest->b), tightest->millimetres,
            tightest->height, tightestShape);
        return result;
    }

    // Regression against what was recorded, and anything newly tight.
    size_t belowClearance = 0;
    for (const auto& entry : measured) {
        const PitMeasurement& pit = entry.second;
        const PitMeasurement* baseline = nullptr;
        for (size_t i = 0; i < recordedCount; ++i) {
            if (static_cast<uint8_t>(recorded[i].a) == entry.first.first &&
                static_cast<uint8_t>(recorded[i].b) == entry.first.second) {
                baseline = &recorded[i];
                break;
            }
        }
        if (pit.millimetres < kIndependentLayerClearanceMm) ++belowClearance;

        if (baseline == nullptr) {
            if (pit.millimetres < kIndependentLayerClearanceMm) {
                result.status = GateStatus::Fail;
                say(result,
                    "a pit that is not in the recorded table came within the %.0f mm "
                    "two-layer clearance: %s-%s at %.1f mm, y=%.2f m (%s). The body changed "
                    "shape somewhere garments depend on; re-measure with "
                    "`mge_wearable_gates` and update recordedPits()",
                    kIndependentLayerClearanceMm, regionName(pit.a), regionName(pit.b),
                    pit.millimetres, pit.height, shapeOf[entry.first]);
                return result;
            }
            continue;
        }
        if (pit.millimetres < baseline->millimetres - kPitToleranceMm) {
            result.status = GateStatus::Fail;
            say(result,
                "%s-%s tightened to %.1f mm at y=%.2f m (%s), %.1f mm below its recorded "
                "%.1f mm. Garments authored against the old clearance may now clip",
                regionName(pit.a), regionName(pit.b), pit.millimetres, pit.height,
                shapeOf[entry.first], baseline->millimetres - pit.millimetres,
                baseline->millimetres);
            return result;
        }
    }

    result.status = GateStatus::Pass;
    say(result,
        "tightest pit %.1f mm (%s-%s at y=%.2f m, %s), clear of self-intersection and within "
        "%.1f mm of every recorded baseline. %zu pair(s) sit under the %.0f mm two-layer "
        "clearance - anatomy, not defects: a garment covering both sides of one must span it "
        "as a single surface rather than layer independently",
        tightest->millimetres, regionName(tightest->a), regionName(tightest->b),
        tightest->height, tightestShape, kPitToleranceMm, belowClearance,
        kIndependentLayerClearanceMm);
    return result;
}

// -------------------------------------------------------------- gate 3 -----

GateResult gateHemLoops(const SkinnedMeshData& body) {
    GateResult result;
    result.id = "B-11";
    result.name = "Hem-loop table";
    (void)body;
    result.status = GateStatus::Blocked;
    say(result,
        "the body ships no hem-loop table yet (B-11, task 13.8, pipeline session). A garment "
        "artist needs named loops - collar, cuff, hem, waist, boot line - to terminate "
        "openings on; until they exist this gate has nothing to check");
    return result;
}

// -------------------------------------------------------------- gate 4 -----

GateResult gateScalpCap(const SkinnedMeshData& body) {
    GateResult result;
    result.id = "B-9";
    result.name = "Scalp-cap fallback";
    if (body.vertices.empty()) {
        result.status = GateStatus::Blocked;
        say(result, "no body asset loaded - nothing to judge");
        return result;
    }

    size_t scalpTriangles = 0;
    for (const MeshPart& part : body.parts) {
        if (part.region == BodyRegion::Scalp) scalpTriangles += part.indexCount / 3;
    }
    if (scalpTriangles == 0) {
        result.status = GateStatus::Fail;
        say(result,
            "the Scalp region has no geometry, so a character with no hair equipped is a hole "
            "in the top of the head");
        return result;
    }

    // With every hair item removed, the head must still be a closed surface.
    std::string why;
    if (!maskedSurfaceIsClosed(body, kAllRegions, &why)) {
        result.status = GateStatus::Fail;
        say(result, "the bare body is not a closed surface (%s)", why.c_str());
        return result;
    }

    result.status = GateStatus::Pass;
    say(result,
        "bald head is real geometry: %zu scalp triangles, closed. The other half of this gate "
        "is a render someone looks at (MODELING.md §6) - `tools/body_preview` produces it",
        scalpTriangles);
    return result;
}

// -------------------------------------------------------------- gate 5 -----

GateResult gateContractHash(const SkinnedMeshData& body) {
    GateResult result;
    result.id = "B-14";
    result.name = "Contract hash";
    if (body.vertices.empty()) {
        result.status = GateStatus::Blocked;
        say(result, "no body asset loaded - nothing to hash");
        return result;
    }

    const uint64_t first = skinnedMeshContentHash(body);
    if (skinnedMeshContentHash(body) != first) {
        result.status = GateStatus::Fail;
        say(result, "hashing the same mesh twice gave two different answers");
        return result;
    }

    // The hash must survive the trip through the shipping format, or a binding
    // baked from a loaded asset would not match one baked from a fresh import.
    std::vector<uint8_t> bytes;
    serializeSkinnedMesh(body, bytes);
    SkinnedMeshData restored;
    if (!deserializeSkinnedMesh(bytes.data(), bytes.size(), restored)) {
        result.status = GateStatus::Fail;
        say(result, "the body did not survive a serialize/deserialize round trip");
        return result;
    }
    const uint64_t after = skinnedMeshContentHash(restored);
    if (after != first) {
        result.status = GateStatus::Fail;
        say(result,
            "hash changed across a serialize/deserialize round trip: %016llx -> %016llx. A "
            "binding baked before shipping would be refused after",
            static_cast<unsigned long long>(first), static_cast<unsigned long long>(after));
        return result;
    }

    result.status = GateStatus::Pass;
    say(result, "body content hash %016llx, stable and unchanged through the shipping format",
        static_cast<unsigned long long>(first));
    return result;
}

// -------------------------------------------------------------- gate 6 -----

GateResult gateGroupsAndAnchors(const SkinnedMeshData& body) {
    GateResult result;
    result.id = "B-25/B-19";
    result.name = "Groups & anchors";
    if (body.vertices.empty()) {
        result.status = GateStatus::Blocked;
        say(result, "no body asset loaded - nothing to judge");
        return result;
    }

    // B-25's substance: every vertex belongs to exactly one region.
    std::vector<std::set<uint8_t>> owners(body.vertices.size());
    for (const MeshPart& part : body.parts) {
        for (uint32_t i = 0; i < part.indexCount; ++i) {
            const uint32_t index = part.firstIndex + i;
            if (index >= body.indices.size()) continue;
            const uint32_t vertex = body.indices[index];
            if (vertex < owners.size()) owners[vertex].insert(static_cast<uint8_t>(part.region));
        }
    }
    size_t unowned = 0, shared = 0;
    for (const std::set<uint8_t>& set : owners) {
        if (set.empty()) {
            ++unowned;
        } else if (set.size() > 1) {
            ++shared;
        }
    }
    if (unowned > 0 || shared > 0) {
        result.status = GateStatus::Fail;
        say(result,
            "region coverage is not a clean partition: %zu vertices belong to no region and "
            "%zu belong to more than one (of %zu). Binding constrains a garment vertex by its "
            "region, so an ambiguous vertex can bind to the wrong body part",
            unowned, shared, owners.size());
        return result;
    }

    result.status = GateStatus::Pass;
    say(result,
        "region groups are a clean partition: all %zu vertices belong to exactly one of %zu "
        "regions, none shared, none orphaned - B-25's substance holds on the shipped part "
        "table. The explicit exported groups (B-25) and attachment points (B-19) arrive with "
        "tasks 13.8/13.9 and will be checked here too",
        owners.size(), body.parts.size());
    return result;
}

// -------------------------------------------------------------- gate 7 -----

GateResult gateModelStandard(const std::vector<SkinnedMeshData>& lods) {
    GateResult result;
    result.id = "§9.7";
    result.name = "Model standard";
    if (lods.empty() || lods[0].vertices.empty()) {
        result.status = GateStatus::Blocked;
        say(result, "no body asset loaded - nothing to judge");
        return result;
    }

    // The budgets the wearables system depends on (MODELING.md §2, raised for
    // LOD0 by ADR 0012 ruling 2; garment budgets did not move).
    const size_t budget[kBodyLodCount] = {2400, 1300, 650};

    for (size_t level = 0; level < lods.size() && level < kBodyLodCount; ++level) {
        const SkinnedMeshData& body = lods[level];
        if (body.vertices.empty()) continue;

        if (body.triangleCount() > budget[level]) {
            result.status = GateStatus::Fail;
            say(result, "LOD%zu is %zu triangles, over its budget of %zu", level,
                body.triangleCount(), budget[level]);
            return result;
        }
        if (level > 0 && body.triangleCount() >= lods[level - 1].triangleCount()) {
            result.status = GateStatus::Fail;
            say(result, "LOD%zu (%zu triangles) is not smaller than LOD%zu (%zu)", level,
                body.triangleCount(), level - 1, lods[level - 1].triangleCount());
            return result;
        }

        for (size_t v = 0; v < body.vertices.size(); ++v) {
            const SkinVertex& vertex = body.vertices[v];
            int sum = 0, influences = 0;
            for (int i = 0; i < 4; ++i) {
                sum += vertex.weights[i];
                if (vertex.weights[i] > 0) ++influences;
                if (vertex.joints[i] >= kJointCount) {
                    result.status = GateStatus::Fail;
                    say(result, "LOD%zu vertex %zu is influenced by joint %u, which is not on "
                                "the canonical rig",
                        level, v, static_cast<unsigned>(vertex.joints[i]));
                    return result;
                }
            }
            if (sum != 255 || influences == 0) {
                result.status = GateStatus::Fail;
                say(result,
                    "LOD%zu vertex %zu has weights summing to %d across %d influences "
                    "(must be 255, at least one). A garment inherits these weights, so a bad "
                    "one spreads to everything worn over it",
                    level, v, sum, influences);
                return result;
            }
        }
    }

    result.status = GateStatus::Pass;
    say(result,
        "%zu LODs inside budget and strictly decreasing (%zu/%zu/%zu triangles), all skin "
        "weights valid within four influences. The render review stays a looked-at gate",
        lods.size(), lods.size() > 0 ? lods[0].triangleCount() : 0,
        lods.size() > 1 ? lods[1].triangleCount() : 0,
        lods.size() > 2 ? lods[2].triangleCount() : 0);
    return result;
}

// ----------------------------------------------------------------- all -----

void runWearableGates(const std::vector<SkinnedMeshData>& lods, GateReport& out) {
    out.gates.clear();
    const SkinnedMeshData empty;
    const SkinnedMeshData& body = lods.empty() ? empty : lods[0];
    out.gates.push_back(gateMaskIntegrity(lods));
    out.gates.push_back(gatePitClearance(body));
    out.gates.push_back(gateHemLoops(body));
    out.gates.push_back(gateScalpCap(body));
    out.gates.push_back(gateContractHash(body));
    out.gates.push_back(gateGroupsAndAnchors(body));
    out.gates.push_back(gateModelStandard(lods));
}

}  // namespace mge
