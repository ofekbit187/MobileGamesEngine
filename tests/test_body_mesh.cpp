// The humanoid template body (task 8.12): the model's own acceptance tests.
// A body is "good" here in four measurable senses — it is a closed, correctly
// wound surface; it has human proportions; it deforms without collapsing; and
// it stays inside the mobile triangle budget at every LOD. Every claim the
// modelling doc makes is checked below.

#include <cmath>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <vector>

#include "mge/character/body_mesh.h"
#include "test_framework.h"

using namespace mge;

namespace {

constexpr size_t J(Joint j) { return static_cast<size_t>(j); }

SkinnedMeshData body(BodyLod lod = BodyLod::Lod0, uint32_t regions = kAllRegions) {
    BodyBuildDesc desc;
    desc.lod = lod;
    desc.regions = regions;
    SkinnedMeshData mesh;
    buildTemplateBody(desc, mesh);
    return mesh;
}

// Positions are welded before topology checks: the UV seam column duplicates
// vertices by design, and a seam is not a hole.
int64_t weldKey(const Vec3& p) {
    // Exact packed key, not a hash: a collision would hide a real hole.
    const auto q = [](float v) {
        const int64_t t = std::lround(v * 10000.0f) + (1 << 20);
        return t < 0 ? 0 : (t > 0x1FFFFF ? 0x1FFFFF : t);
    };
    return (q(p.x) << 42) | (q(p.y) << 21) | q(p.z);
}

// The bind pose of one variant: BOTH halves of the scope applied — the
// palette for proportions, the morph weights for shape.
MeshData restPose(const HumanoidVariant& variant, const SkinnedMeshData& mesh) {
    Pose pose;
    Mat4 palette[kJointCount];
    buildSkinPalette(variant, pose, palette);
    float shape[kMorphCount];
    morphWeights(variant, shape);
    MeshData out;
    skinMesh(mesh, palette, shape, out);
    return out;
}

// Measurements are taken per region: an arm hanging beside the chest would
// otherwise be measured as part of the chest.
std::vector<uint32_t> regionVertices(const SkinnedMeshData& mesh, BodyRegion region) {
    std::set<uint32_t> unique;
    for (const MeshPart& part : mesh.parts) {
        if (part.region != region) continue;
        for (uint32_t i = 0; i < part.indexCount; ++i) {
            unique.insert(mesh.indices[part.firstIndex + i]);
        }
    }
    return {unique.begin(), unique.end()};
}

template <typename VertexArray>
float maxAbsX(const SkinnedMeshData& mesh, const VertexArray& vertices, BodyRegion region,
              float y0, float y1) {
    float best = 0;
    for (uint32_t i : regionVertices(mesh, region)) {
        const Vec3& p = vertices[i].position;
        if (p.y < y0 || p.y > y1) continue;
        best = std::fmax(best, std::fabs(p.x));
    }
    return best;
}

// Thickness across the region (max X minus min X) — independent of where the
// limb sits, so it measures build rather than stance.
template <typename VertexArray>
float widthOf(const SkinnedMeshData& mesh, const VertexArray& vertices, BodyRegion region,
              float y0, float y1) {
    float lo = 1e9f, hi = -1e9f;
    for (uint32_t i : regionVertices(mesh, region)) {
        const Vec3& p = vertices[i].position;
        if (p.y < y0 || p.y > y1) continue;
        lo = std::fmin(lo, p.x);
        hi = std::fmax(hi, p.x);
    }
    return hi > lo ? hi - lo : 0.0f;
}

// Thickness across a limb that does not hang straight down: the region's
// extent in Z, which for the arms and legs is perpendicular to the bone.
template <typename VertexArray>
float depthOf(const SkinnedMeshData& mesh, const VertexArray& vertices, BodyRegion region) {
    float lo = 1e9f, hi = -1e9f;
    for (uint32_t i : regionVertices(mesh, region)) {
        lo = std::fmin(lo, vertices[i].position.z);
        hi = std::fmax(hi, vertices[i].position.z);
    }
    return hi > lo ? hi - lo : 0.0f;
}

// How far forward a surface sits at a given height, on the mid-line. Layer
// thickness has to be measured where the layers are actually stacked: the
// widest point of the torso band is the armpit, where a garment is squeezed
// into a crease and its thickness says nothing about its layer.
template <typename VertexArray>
float frontOf(const SkinnedMeshData& mesh, const VertexArray& vertices, BodyRegion region,
              float y0, float y1) {
    float front = 0;
    for (uint32_t i : regionVertices(mesh, region)) {
        const Vec3& p = vertices[i].position;
        if (p.y < y0 || p.y > y1 || std::fabs(p.x) > 0.08f) continue;
        front = std::fmin(front, p.z);  // the character faces -Z
    }
    return -front;
}

const char* regionNameForTest(BodyRegion r) {
    switch (r) {
        case BodyRegion::Scalp: return "Scalp";
        case BodyRegion::Face: return "Face";
        case BodyRegion::Neck: return "Neck";
        case BodyRegion::Torso: return "Torso";
        case BodyRegion::ArmL: return "ArmL";
        case BodyRegion::ArmR: return "ArmR";
        case BodyRegion::HandL: return "HandL";
        case BodyRegion::HandR: return "HandR";
        case BodyRegion::LegL: return "LegL";
        case BodyRegion::LegR: return "LegR";
        case BodyRegion::FootL: return "FootL";
        default: return "FootR";
    }
}

Aabb boundsOfRegion(const SkinnedMeshData& mesh, BodyRegion region) {
    Aabb box;
    bool first = true;
    for (uint32_t i : regionVertices(mesh, region)) {
        const Vec3& p = mesh.vertices[i].position;
        if (first) {
            box.min = box.max = p;
            first = false;
        } else {
            box.extend(p);
        }
    }
    return box;
}

// Surface area of the triangles touching a joint — the collapse detector.
float areaNear(const MeshData& mesh, const Vec3& center, float radius) {
    float area = 0;
    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        const Vec3& a = mesh.vertices[mesh.indices[i]].position;
        const Vec3& b = mesh.vertices[mesh.indices[i + 1]].position;
        const Vec3& c = mesh.vertices[mesh.indices[i + 2]].position;
        const Vec3 mid = (a + b + c) * (1.0f / 3.0f);
        if ((mid - center).lengthSq() > radius * radius) continue;
        area += (b - a).cross(c - a).length() * 0.5f;
    }
    return area;
}

}  // namespace

// --------------------------------------------------------------- surface ---

MGE_TEST(body_mesh_is_closed_and_consistently_wound) {
    const SkinnedMeshData mesh = body();
    // The template body is ONE watertight shell. Regions are a partition of
    // its triangles (what a garment masks), not separate shells, so the test
    // is on the whole surface: every directed edge occurs exactly once and
    // its opposite exists. That is closed AND consistently wound in one check.
    std::map<std::pair<int64_t, int64_t>, int> directed;
    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        int64_t k[3];
        for (int e = 0; e < 3; ++e) {
            k[e] = weldKey(mesh.vertices[mesh.indices[i + e]].position);
        }
        for (int e = 0; e < 3; ++e) directed[{k[e], k[(e + 1) % 3]}] += 1;
    }
    size_t doubled = 0, boundary = 0;
    for (const auto& entry : directed) {
        if (entry.second != 1) ++doubled;
        if (directed.find({entry.first.second, entry.first.first}) == directed.end()) ++boundary;
    }
    printf("  directed edges: %zu, doubled %zu, boundary %zu\n", directed.size(), doubled,
           boundary);
    MGE_CHECK(doubled == 0);
    MGE_CHECK(boundary == 0);

    // Closed and outward: the divergence-theorem volume of a human-sized body.
    double volume = 0;
    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        const Vec3& a = mesh.vertices[mesh.indices[i]].position;
        const Vec3& b = mesh.vertices[mesh.indices[i + 1]].position;
        const Vec3& c = mesh.vertices[mesh.indices[i + 2]].position;
        volume += a.dot(b.cross(c));
    }
    volume /= 6.0;
    MGE_CHECK(volume > 0.055 && volume < 0.100);  // ~70 kg of person
}

MGE_TEST(body_mesh_has_no_degenerate_triangles) {
    const SkinnedMeshData mesh = body();
    size_t degenerate = 0;
    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        const Vec3& a = mesh.vertices[mesh.indices[i]].position;
        const Vec3& b = mesh.vertices[mesh.indices[i + 1]].position;
        const Vec3& c = mesh.vertices[mesh.indices[i + 2]].position;
        if ((b - a).cross(c - a).length() * 0.5f < 1e-8f) ++degenerate;
    }
    MGE_CHECK(degenerate == 0);
}

MGE_TEST(body_mesh_normals_point_outward) {
    const SkinnedMeshData mesh = body();
    // The authoritative test, on every triangle: the shading normal must agree
    // with the winding. A flipped face or an inverted normal fails here.
    size_t triangles = 0, agreeing = 0;
    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        const SkinVertex& a = mesh.vertices[mesh.indices[i]];
        const SkinVertex& b = mesh.vertices[mesh.indices[i + 1]];
        const SkinVertex& c = mesh.vertices[mesh.indices[i + 2]];
        const Vec3 face = (b.position - a.position).cross(c.position - a.position);
        const Vec3 shading = a.normal + b.normal + c.normal;
        if (face.lengthSq() < 1e-12f || shading.lengthSq() < 1e-12f) continue;
        ++triangles;
        if (face.normalized().dot(shading.normalized()) > 0.0f) ++agreeing;
    }
    printf("  normals agree with winding on %zu/%zu triangles\n", agreeing, triangles);
    MGE_CHECK(triangles > 1000);
    // A creased fold (armpit, crotch) can smooth its vertex normals past the
    // face plane; a modelling error shows up as whole patches, not a handful.
    MGE_CHECK(agreeing * 200 >= triangles * 199);

    // And on the chest, where "outward" is unambiguous, the normal points away
    // from the body's axis.
    size_t checked = 0, outward = 0;
    for (uint32_t i : regionVertices(mesh, BodyRegion::Torso)) {
        const SkinVertex& v = mesh.vertices[i];
        if (v.position.y < 1.15f || v.position.y > 1.35f) continue;
        const Vec3 radial{v.position.x, 0, v.position.z};
        if (radial.length() < 0.05f) continue;
        ++checked;
        if (radial.normalized().dot(v.normal) > 0.0f) ++outward;
    }
    MGE_CHECK(checked > 50);
    MGE_CHECK(outward == checked);
}

MGE_TEST(body_mesh_is_deterministic) {
    const SkinnedMeshData a = body();
    const SkinnedMeshData b = body();
    MGE_CHECK(a.vertices.size() == b.vertices.size());
    MGE_CHECK(a.indices == b.indices);
    bool identical = a.vertices.size() == b.vertices.size();
    for (size_t i = 0; identical && i < a.vertices.size(); ++i) {
        identical = std::memcmp(&a.vertices[i], &b.vertices[i], sizeof(SkinVertex)) == 0;
    }
    MGE_CHECK(identical);
}

// ----------------------------------------------------------- proportions ---

MGE_TEST(body_mesh_has_human_proportions) {
    const SkinnedMeshData mesh = body();
    const Aabb& b = mesh.bounds;
    // Sole on the ground, crown at the template height.
    MGE_CHECK_NEAR(b.min.y, 0.0f, 0.02f);
    MGE_CHECK_NEAR(b.max.y, templateVariant().height, 0.02f);

    // Head height (chin to crown) is between 1/8 and 1/7 of the body — the
    // classic realistic range; a stylized "chibi" body would fail this.
    const Aabb head = boundsOfRegion(mesh, BodyRegion::Scalp);
    const float heads = templateVariant().height / (head.max.y - head.min.y);
    MGE_CHECK(heads > 6.8f && heads < 8.2f);
    MGE_CHECK(head.max.z - head.min.z > head.max.x - head.min.x);  // deeper than wide

    // Shoulders wider than hips, waist narrower than both — measured on the
    // trunk itself, not on the arms hanging beside it.
    //
    // The bands come from the TORSO'S OWN extent, not from fixed world heights,
    // and that is the whole point (ADR 0012 Ruling 3). Sampling a fixed 60 mm
    // band at 0.82-0.88 m made this gate pass by a single vertex: the hips do
    // not change width with the triangle budget, but below about 2 200
    // triangles the Torso region stops REACHING into that band, so the gate
    // measured a narrower part of the body and called it a proportion failure.
    // Measured, hip width read 0.347 m at 2 200 and 0.146 m at 2 100 —
    // discretely, for a reason that has nothing to do with proportions. A gate
    // that fails for the wrong reason is worse than no gate: it teaches people
    // to route around it. Taken as fractions of the torso's own height, the
    // bands follow the body instead of the decimator.
    const Aabb trunk = boundsOfRegion(mesh, BodyRegion::Torso);
    const float trunkH = trunk.max.y - trunk.min.y;
    MGE_CHECK(trunkH > 0.2f);   // a torso that thin means the region is broken
    const float shoulderLo = trunk.max.y - 0.15f * trunkH;
    const float hipHi = trunk.min.y + 0.15f * trunkH;
    const float waistMid = trunk.min.y + 0.55f * trunkH;

    const float shoulders =
        std::fmax(maxAbsX(mesh, mesh.vertices, BodyRegion::Torso, shoulderLo, trunk.max.y),
                  maxAbsX(mesh, mesh.vertices, BodyRegion::ArmL, shoulderLo, trunk.max.y)) * 2.0f;
    const float waist = maxAbsX(mesh, mesh.vertices, BodyRegion::Torso,
                                waistMid - 0.08f * trunkH, waistMid + 0.08f * trunkH) * 2.0f;
    const float hips =
        maxAbsX(mesh, mesh.vertices, BodyRegion::Torso, trunk.min.y, hipHi) * 2.0f;
    printf("  proportions: shoulders %.3f m, waist %.3f m, hips %.3f m "
           "(torso %.3f..%.3f m)\n",
           shoulders, waist, hips, trunk.min.y, trunk.max.y);
    MGE_CHECK(shoulders > hips);
    MGE_CHECK(hips > waist);
    MGE_CHECK(shoulders > 0.40f && shoulders < 0.56f);
    MGE_CHECK(hips > 0.30f && hips < 0.44f);

    // Left/right symmetry.
    MGE_CHECK_NEAR(b.min.x, -b.max.x, 0.001f);
}

MGE_TEST(body_mesh_covers_every_region) {
    const SkinnedMeshData mesh = body();
    std::set<uint8_t> seen;
    uint32_t indexed = 0;
    for (const MeshPart& part : mesh.parts) {
        seen.insert(static_cast<uint8_t>(part.region));
        indexed += part.indexCount;
    }
    MGE_CHECK(indexed == mesh.indices.size());  // every triangle belongs to a part
    // ALL TWELVE, Face included. It was exempted here from v1 until task 13.7,
    // because the rig has one Head joint and the importer read regions off the
    // rig, so every head triangle came back Scalp and `BodyRegion::Face` was
    // empty — the B-8 debt ADR 0008 logged against v3, and what blocked the
    // first mask, visor or face-covering helm. The exemption is gone; if this
    // fails, the body regressed to a headless-mask body and no skip is coming
    // back.
    for (size_t r = 0; r < kBodyRegionCount; ++r) {
        MGE_CHECK(seen.count(static_cast<uint8_t>(r)) == 1);
    }
}

MGE_TEST(the_face_is_a_real_region_in_front_of_the_scalp) {
    // Face exists as geometry (task 13.7), and it is the FRONT of the head.
    // Checking where it sits, not just that it is non-empty: a Face region
    // accidentally tagged onto the back of the skull would satisfy a count and
    // put every visor on the back of the head.
    const SkinnedMeshData mesh = body();
    const Aabb face = boundsOfRegion(mesh, BodyRegion::Face);
    const Aabb scalp = boundsOfRegion(mesh, BodyRegion::Scalp);
    MGE_CHECK(!regionVertices(mesh, BodyRegion::Face).empty());

    // The engine's forward is -Z, so the face is the more NEGATIVE z.
    MGE_CHECK(face.min.z < scalp.min.z);
    MGE_CHECK((face.min.z + face.max.z) < (scalp.min.z + scalp.max.z));
    // Both are head: they share the same height band, and the scalp reaches
    // higher because it is the cranium cap (B-9).
    MGE_CHECK(scalp.max.y >= face.max.y);
    MGE_CHECK(face.min.y > 1.30f);
    // The most forward point of the HEAD is the nose, and it belongs to Face.
    // Deliberately not "of the body": the toes reach further forward than the
    // nose (-0.187 m against -0.169 m), which is what a body standing in an
    // A-pose with its feet in front of it looks like.
    float headFront = 1e9f;
    for (uint32_t i : regionVertices(mesh, BodyRegion::Face)) {
        headFront = std::fmin(headFront, mesh.vertices[i].position.z);
    }
    for (uint32_t i : regionVertices(mesh, BodyRegion::Scalp)) {
        headFront = std::fmin(headFront, mesh.vertices[i].position.z);
    }
    MGE_CHECK_NEAR(face.min.z, headFront, 0.001f);
}

MGE_TEST(the_hairline_is_one_closed_loop) {
    // B-9: "the hairline (cap/face boundary) is an authored loop, not an
    // accident". Task 13.7 shipped the Face region with the boundary merely
    // CLASSIFIED per face — a staircase one face wide — because cutting it cost
    // ~200 triangles on a LOD0 already at its cap. ADR 0012 funded it (LOD0
    // 2 200 -> 2 400, LOD1/LOD2 untouched) and 13.7a cuts it.
    //
    // A render cannot settle this: the body is smooth-shaded and the seam does
    // not show. What settles it is topology — every vertex on the boundary has
    // exactly two boundary edges, and they close into ONE ring.
    //
    // Matched by POSITION, not by index: the regions are separate glTF
    // primitives, so Face and Scalp own distinct vertex copies along the seam
    // and no edge is literally shared between them.
    const SkinnedMeshData mesh = body();

    auto key = [](const Vec3& p) {
        auto q = [](float v) { return static_cast<long long>(std::lround(v * 100000.0f)); };
        return std::make_tuple(q(p.x), q(p.y), q(p.z));
    };
    using Key = decltype(key(Vec3{}));

    // Edges of the Face region, counted; a boundary edge is used exactly once.
    std::map<std::pair<Key, Key>, int> edges;
    for (const MeshPart& part : mesh.parts) {
        if (part.region != BodyRegion::Face) continue;
        for (uint32_t i = 0; i + 2 < part.indexCount; i += 3) {
            const uint32_t* t = &mesh.indices[part.firstIndex + i];
            for (int e = 0; e < 3; ++e) {
                Key a = key(mesh.vertices[t[e]].position);
                Key b = key(mesh.vertices[t[(e + 1) % 3]].position);
                if (b < a) std::swap(a, b);
                edges[{a, b}]++;
            }
        }
    }

    std::map<Key, std::vector<Key>> ring;
    size_t boundaryEdges = 0;
    for (const auto& kv : edges) {
        if (kv.second != 1) continue;
        ring[kv.first.first].push_back(kv.first.second);
        ring[kv.first.second].push_back(kv.first.first);
        boundaryEdges++;
    }
    MGE_CHECK(boundaryEdges > 16);   // a real hairline, not a stray sliver

    // Every boundary vertex has exactly two boundary neighbours — that is what
    // makes it a loop rather than a branching seam.
    for (const auto& kv : ring) MGE_CHECK(kv.second.size() == 2);

    // Walk every cycle and report their sizes.
    std::set<Key> seen;
    std::vector<size_t> loops;
    std::vector<Vec3> loopAt;
    for (const auto& kv : ring) {
        if (seen.count(kv.first)) continue;
        Key start = kv.first;
        Key prev = start;
        Key at = ring[start][0];
        seen.insert(start);
        size_t n = 1;
        Vec3 centre{0, 0, 0};
        auto add = [&centre](const Key& k) {
            centre.x += static_cast<float>(std::get<0>(k)) / 100000.0f;
            centre.y += static_cast<float>(std::get<1>(k)) / 100000.0f;
            centre.z += static_cast<float>(std::get<2>(k)) / 100000.0f;
        };
        add(start);
        while (!(at == start) && n <= ring.size()) {
            seen.insert(at);
            add(at);
            const std::vector<Key>& next = ring[at];
            Key step = (next[0] == prev) ? next[1] : next[0];
            prev = at;
            at = step;
            n++;
        }
        centre.x /= static_cast<float>(n);
        centre.y /= static_cast<float>(n);
        centre.z /= static_cast<float>(n);
        loops.push_back(n);
        loopAt.push_back(centre);
    }
    printf("  hairline: %zu boundary edges in %zu closed loop(s):\n", boundaryEdges,
           loops.size());
    for (size_t k = 0; k < loops.size(); ++k) {
        printf("    %zu vertices, centred (%.3f, %.3f, %.3f)\n", loops[k], loopAt[k].x,
               loopAt[k].y, loopAt[k].z);
    }
    std::sort(loops.begin(), loops.end(), std::greater<size_t>());

    // Measured, this comes out as two rings and both are real:
    //
    //   77 vertices centred (-0.001, 1.616, -0.061)  <- the hairline itself
    //   10 vertices centred (-0.003, 1.558, -0.080)  <- the mouth
    //
    // The second sits on the mid-line at the measured mouth height (1.553 m),
    // not out at the ears (|x| 0.093 m) — it is the rim of the source mesh's
    // mouth opening, which falls inside the Face region and so bounds it. That
    // is geometry the CC0 body came with, not a stray patch.
    //
    // What the assertions hold is the part B-9 is about: the cap/face boundary
    // is ONE dominant ring, not a shattered seam.
    MGE_CHECK(loops.size() <= 3);
    MGE_CHECK(loops[0] > ring.size() / 2);   // the hairline dominates
}

MGE_TEST(a_visor_can_hide_the_face_without_hiding_the_scalp) {
    // The point of task 13.7. Before it, Face and Scalp were one shell, so a
    // visor could only take the whole head off with it.
    const SkinnedMeshData full = body();
    const SkinnedMeshData noFace = body(BodyLod::Lod0, kAllRegions & ~regionBit(BodyRegion::Face));
    MGE_CHECK(noFace.triangleCount() < full.triangleCount());

    bool faceGone = true, scalpKept = false;
    for (const MeshPart& part : noFace.parts) {
        if (part.region == BodyRegion::Face) faceGone = false;
        if (part.region == BodyRegion::Scalp) scalpKept = true;
    }
    MGE_CHECK(faceGone);
    MGE_CHECK(scalpKept);   // the bald cap survives on its own (B-9)

    // And the reverse: a helmet taking the cranium leaves the face behind.
    const SkinnedMeshData noScalp =
        body(BodyLod::Lod0, kAllRegions & ~regionBit(BodyRegion::Scalp));
    bool faceKept = false;
    for (const MeshPart& part : noScalp.parts) {
        if (part.region == BodyRegion::Face) faceKept = true;
        MGE_CHECK(part.region != BodyRegion::Scalp);
    }
    MGE_CHECK(faceKept);
}

// -------------------------------------------------------------- skinning ---

MGE_TEST(body_mesh_skin_weights_are_valid) {
    const SkinnedMeshData mesh = body();
    Vec3 bind[kJointCount];
    templateBindPositions(bind);
    // The bone a joint drives runs from that joint to its first child; a leaf
    // bone continues the direction it came in on. Measuring against the BONE
    // rather than the joint is what makes the rule pose-independent: a long
    // femur legitimately moves vertices far from the hip, but nothing it
    // moves is far from the femur itself.
    Vec3 boneEnd[kJointCount];
    {
        const Skeleton s = buildSkeleton(templateVariant());
        int8_t child[kJointCount];
        for (size_t j = 0; j < kJointCount; ++j) child[j] = -1;
        for (size_t j = 0; j < kJointCount; ++j) {
            const int8_t parent = s.parent[j];
            if (parent >= 0 && child[static_cast<size_t>(parent)] < 0) {
                child[static_cast<size_t>(parent)] = static_cast<int8_t>(j);
            }
        }
        for (size_t j = 0; j < kJointCount; ++j) {
            boneEnd[j] = child[j] >= 0 ? bind[static_cast<size_t>(child[j])] : bind[j];
        }
    }
    const auto distanceToBone = [&](const Vec3& p, size_t j) {
        const Vec3 ab = boneEnd[j] - bind[j];
        const float denom = ab.lengthSq();
        float t = denom < 1e-12f ? 0.0f : (p - bind[j]).dot(ab) / denom;
        t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
        return (p - (bind[j] + ab * t)).length();
    };

    bool ok = true;
    float worstReach = 0;
    for (const SkinVertex& v : mesh.vertices) {
        int sum = 0;
        for (int k = 0; k < 4; ++k) {
            sum += v.weights[k];
            if (v.joints[k] >= kJointCount) {
                ok = false;
                continue;
            }
            if (v.weights[k] == 0) continue;
            // A stray influence is the classic rigging bug — bone-heat weights
            // leak across a joint and the limb swims when the joint bends.
            // Every influence must be on a bone essentially as close to the
            // vertex as the closest bone influencing it.
            float nearest = 1e9f;
            for (int q = 0; q < 4; ++q) {
                if (v.weights[q] == 0 || v.joints[q] >= kJointCount) continue;
                nearest = std::fmin(nearest, distanceToBone(v.position, v.joints[q]));
            }
            const float reach = distanceToBone(v.position, v.joints[k]) - nearest;
            worstReach = std::fmax(worstReach, reach);
            if (reach > 0.12f) ok = false;
        }
        if (sum != 255) ok = false;
    }
    printf("  worst influence reach past the nearest bone: %.4f m\n", worstReach);
    MGE_CHECK(ok);
}

MGE_TEST(body_mesh_rest_pose_matches_the_template) {
    // The template variant through the palette must reproduce the authored
    // mesh exactly — the palette is an identity for the body it was built on.
    const SkinnedMeshData mesh = body();
    const MeshData skinned = restPose(templateVariant(), mesh);
    float worst = 0;
    for (size_t i = 0; i < mesh.vertices.size(); ++i) {
        worst = std::fmax(worst,
                          (skinned.vertices[i].position - mesh.vertices[i].position).length());
    }
    MGE_CHECK(worst < 1e-4f);
}

MGE_TEST(body_variants_come_from_the_palette_not_new_meshes) {
    const SkinnedMeshData mesh = body();  // ONE mesh for every character
    const float heights[] = {1.50f, 1.62f, 1.75f, 1.95f, 2.10f};
    for (float h : heights) {
        HumanoidVariant v;
        v.height = h;
        const MeshData skinned = restPose(v, mesh);
        MGE_CHECK_NEAR(skinned.bounds.max.y, h, h * 0.03f);
        MGE_CHECK_NEAR(skinned.bounds.min.y, 0.0f, 0.03f);
    }

    // Shoulder breadth follows the variant's shoulder parameter; bulk
    // thickens the limbs. Same mesh, different palette.
    HumanoidVariant narrow, broad;
    narrow.shoulderWidth = 0.36f;
    broad.shoulderWidth = 0.56f;
    const MeshData narrowPosed = restPose(narrow, mesh);
    const MeshData broadPosed = restPose(broad, mesh);
    const float narrowW = widthOf(mesh, narrowPosed.vertices, BodyRegion::Torso, -1.0f, 3.0f);
    const float broadW = widthOf(mesh, broadPosed.vertices, BodyRegion::Torso, -1.0f, 3.0f);
    MGE_CHECK(broadW > narrowW * 1.3f);

    HumanoidVariant thin, heavy;
    thin.bulk = 0.75f;
    heavy.bulk = 1.5f;
    const MeshData thinPosed = restPose(thin, mesh);
    const MeshData heavyPosed = restPose(heavy, mesh);
    // Measured ACROSS the arm (its depth in Z), not along X: the template's
    // bind pose is the base mesh's relaxed stance, so the arm hangs
    // diagonally and its X span is mostly length. Bulk must thicken the limb
    // without lengthening it — that is what the bone-local scale frame buys.
    const float thinArm = depthOf(mesh, thinPosed.vertices, BodyRegion::ArmL);
    const float heavyArm = depthOf(mesh, heavyPosed.vertices, BodyRegion::ArmL);
    printf("  arm thickness: bulk 0.75 -> %.4f m, bulk 1.50 -> %.4f m\n", thinArm, heavyArm);
    MGE_CHECK(heavyArm > thinArm * 1.5f);  // bulk 1.5 vs 0.75: half again as thick

    // ...and the arm keeps its length: a heavy character is not a long-armed
    // one. Length is the span of the arm's vertices ALONG the bone.
    Vec3 bindPos[kJointCount];
    templateBindPositions(bindPos);
    const Vec3 armAxis =
        (bindPos[J(Joint::HandL)] - bindPos[J(Joint::UpperArmL)]).normalized();
    const std::vector<uint32_t> armVerts = regionVertices(mesh, BodyRegion::ArmL);
    const auto armSpan = [&](const MeshData& posed) {
        float lo = 1e9f, hi = -1e9f;
        for (uint32_t i : armVerts) {
            const float t = posed.vertices[i].position.dot(armAxis);
            lo = std::fmin(lo, t);
            hi = std::fmax(hi, t);
        }
        return hi - lo;
    };
    const float thinSpan = armSpan(thinPosed), heavySpan = armSpan(heavyPosed);
    printf("  arm span: thin %.4f m, heavy %.4f m\n", thinSpan, heavySpan);
    MGE_CHECK(std::fabs(heavySpan - thinSpan) < 0.06f);
}

MGE_TEST(body_mesh_survives_joint_bending) {
    // Linear blend skinning loses volume at a bend; what it must never do is
    // collapse the joint. Elbow and knee are bent hard and the surface area
    // around the joint is compared against rest.
    const SkinnedMeshData mesh = body();
    Vec3 bind[kJointCount];
    templateBindPositions(bind);

    Pose rest;
    Mat4 palette[kJointCount];
    buildSkinPalette(templateVariant(), rest, palette);
    MeshData restMesh;
    skinMesh(mesh, palette, restMesh);

    Pose bent;
    bent.rotation[J(Joint::ForearmL)] = Quat::fromAxisAngle({1, 0, 0}, -1.9f);   // elbow ~109°
    bent.rotation[J(Joint::ShinL)] = Quat::fromAxisAngle({1, 0, 0}, -1.9f);      // knee
    bent.rotation[J(Joint::UpperArmL)] = Quat::fromAxisAngle({0, 0, 1}, 1.4f);   // arm raised
    buildSkinPalette(templateVariant(), bent, palette);
    MeshData bentMesh;
    skinMesh(mesh, palette, bentMesh);

    // The collapse metric: the loop of vertices sitting exactly at the joint
    // (the 50/50-weighted ring) must keep its girth when the joint folds.
    // That ring is what a "candy wrapper" pinch destroys first.
    const auto loopGirth = [&](Joint child, const MeshData& posed) {
        Vec3 sum{0, 0, 0};
        float radius = 0;
        size_t count = 0;
        std::vector<uint32_t> loop;
        for (size_t i = 0; i < mesh.vertices.size(); ++i) {
            const SkinVertex& v = mesh.vertices[i];
            bool onLoop = false;
            for (int k = 0; k < 4; ++k) {
                if (v.joints[k] == J(child) && v.weights[k] > 100 && v.weights[k] < 155) {
                    onLoop = true;
                }
            }
            if (!onLoop) continue;
            loop.push_back(static_cast<uint32_t>(i));
            sum += posed.vertices[i].position;
            ++count;
        }
        if (count == 0) return 0.0f;
        const Vec3 center = sum * (1.0f / static_cast<float>(count));
        for (uint32_t i : loop) radius += (posed.vertices[i].position - center).length();
        return radius / static_cast<float>(count);
    };

    const float elbowRest = loopGirth(Joint::ForearmL, restMesh);
    const float kneeRest = loopGirth(Joint::ShinL, restMesh);
    MGE_CHECK(elbowRest > 0.01f && kneeRest > 0.01f);
    MGE_CHECK(loopGirth(Joint::ForearmL, bentMesh) > elbowRest * 0.75f);
    MGE_CHECK(loopGirth(Joint::ShinL, bentMesh) > kneeRest * 0.75f);

    // The bend actually happened (a test that passes on an unmoved mesh is
    // no test): the wrist swings well away from where it hangs at rest.
    Mat4 world[kJointCount];
    evaluatePose(buildSkeleton(templateVariant()), bent, world);
    const Vec3 wristRest = bind[J(Joint::HandL)];
    const Vec3 wristBent = world[J(Joint::HandL)].transformPoint({0, 0, 0});
    MGE_CHECK((wristBent - wristRest).length() > 0.3f);
    MGE_CHECK(areaNear(bentMesh, wristBent, 0.12f) > 0.0f);

    // No vertex may be flung away by a bad weight.
    for (const Vertex& v : bentMesh.vertices) {
        MGE_CHECK(v.position.length() < 3.0f);
    }
}

// ------------------------------------------------------- variation scope ---

namespace {

// Every shape parameter, and how to set it — so the tests below can sweep the
// whole scope instead of naming five of the fifteen and hoping.
struct ShapeAxis {
    const char* name;
    void (*set)(HumanoidVariant&, float);
};

const ShapeAxis kShapeAxes[] = {
    {"chest", [](HumanoidVariant& v, float x) { v.chest = x; }},
    {"belly", [](HumanoidVariant& v, float x) { v.belly = x; }},
    {"seat", [](HumanoidVariant& v, float x) { v.seat = x; }},
    {"muscle", [](HumanoidVariant& v, float x) { v.muscle = x; }},
    {"neck", [](HumanoidVariant& v, float x) { v.neck = x; }},
    {"face.skull", [](HumanoidVariant& v, float x) { v.face.skull = x; }},
    {"face.brow", [](HumanoidVariant& v, float x) { v.face.brow = x; }},
    {"face.cheeks", [](HumanoidVariant& v, float x) { v.face.cheeks = x; }},
    {"face.jawWidth", [](HumanoidVariant& v, float x) { v.face.jawWidth = x; }},
    {"face.chin", [](HumanoidVariant& v, float x) { v.face.chin = x; }},
    {"face.noseLength", [](HumanoidVariant& v, float x) { v.face.noseLength = x; }},
    {"face.noseWidth", [](HumanoidVariant& v, float x) { v.face.noseWidth = x; }},
    {"face.mouth", [](HumanoidVariant& v, float x) { v.face.mouth = x; }},
    {"face.eyes", [](HumanoidVariant& v, float x) { v.face.eyes = x; }},
    {"face.ears", [](HumanoidVariant& v, float x) { v.face.ears = x; }},
};
constexpr size_t kShapeAxisCount = sizeof kShapeAxes / sizeof kShapeAxes[0];

}  // namespace

MGE_TEST(every_shape_parameter_is_authored_and_moves_the_body) {
    // The scope is only real if every parameter in the schema has geometry
    // behind it. A parameter that silently does nothing is worse than one
    // that does not exist: a variant file would set it and see no change.
    const SkinnedMeshData mesh = body();
    MGE_CHECK(mesh.morphs.size() == kMorphCount);
    MGE_CHECK(kShapeAxisCount == kMorphCount);
    for (size_t i = 0; i < kMorphCount; ++i) {
        const MorphTarget* target = mesh.morph(static_cast<Morph>(i));
        MGE_CHECK(target != nullptr);
        if (target == nullptr) continue;
        MGE_CHECK(!target->deltas.empty());
        MGE_CHECK(target->scale > 0.0f);
    }

    const MeshData rest = restPose(HumanoidVariant{}, mesh);
    for (const ShapeAxis& axis : kShapeAxes) {
        HumanoidVariant plus, minus;
        axis.set(plus, 1.0f);
        axis.set(minus, -1.0f);
        const MeshData high = restPose(plus, mesh);
        const MeshData low = restPose(minus, mesh);
        float worstHigh = 0, worstLow = 0, opposite = 0;
        for (size_t i = 0; i < rest.vertices.size(); ++i) {
            const Vec3 up = high.vertices[i].position - rest.vertices[i].position;
            const Vec3 down = low.vertices[i].position - rest.vertices[i].position;
            worstHigh = std::fmax(worstHigh, up.length());
            worstLow = std::fmax(worstLow, down.length());
            opposite = std::fmax(opposite, (up + down).length());
        }
        printf("  %-16s +1 moves %5.1f mm, -1 moves %5.1f mm\n", axis.name, worstHigh * 1000.0f,
               worstLow * 1000.0f);
        // Visible at arm's length, but still a human being: a parameter that
        // moves 1 mm is decoration, one that moves 10 cm is a different body.
        MGE_CHECK(worstHigh > 0.004f && worstHigh < 0.090f);
        // One authored delta, both directions: -1 must be exactly the mirror
        // of +1, or the file is storing twice what it needs to (P1).
        MGE_CHECK_NEAR(worstLow, worstHigh, 1e-4f);
        MGE_CHECK(opposite < 1e-4f);
    }
}

MGE_TEST(shape_parameters_do_not_break_the_body) {
    // A variant file is content, and content will eventually contain every
    // combination. Each extreme, and all of them at once, must still be a
    // closed, human-sized body with its feet on the ground.
    const SkinnedMeshData mesh = body();
    const auto inspect = [&](const char* what, const HumanoidVariant& v) {
        const MeshData posed = restPose(v, mesh);
        double volume = 0;
        for (size_t i = 0; i + 2 < posed.indices.size(); i += 3) {
            const Vec3& a = posed.vertices[posed.indices[i]].position;
            const Vec3& b = posed.vertices[posed.indices[i + 1]].position;
            const Vec3& c = posed.vertices[posed.indices[i + 2]].position;
            volume += a.dot(b.cross(c));
        }
        volume /= 6.0;
        MGE_CHECK_NEAR(posed.bounds.min.y, 0.0f, 0.03f);
        MGE_CHECK_NEAR(posed.bounds.max.y, v.height, v.height * 0.03f);
        MGE_CHECK(volume > 0.045 && volume < 0.180);
        (void)what;
    };
    for (const ShapeAxis& axis : kShapeAxes) {
        for (float value : {-1.0f, 1.0f}) {
            HumanoidVariant v;
            axis.set(v, value);
            inspect(axis.name, v);
        }
    }
    HumanoidVariant everything;
    for (const ShapeAxis& axis : kShapeAxes) axis.set(everything, 1.0f);
    inspect("all +1", everything);
    HumanoidVariant nothing;
    for (const ShapeAxis& axis : kShapeAxes) axis.set(nothing, -1.0f);
    inspect("all -1", nothing);
}

MGE_TEST(shape_and_proportions_compose) {
    // The two halves of the scope are independent by construction — shape is
    // a bind-space delta, proportions are the palette — so a heavy belly on a
    // tall body is a tall body with a heavy belly, not a surprise.
    const SkinnedMeshData mesh = body();
    for (float height : {kHeightRange.min, 1.75f, kHeightRange.max}) {
        HumanoidVariant lean, heavy;
        lean.height = heavy.height = height;
        heavy.belly = 1.0f;
        const MeshData leanPosed = restPose(lean, mesh);
        const MeshData heavyPosed = restPose(heavy, mesh);
        // Height still means sole to crown, whatever the shape is doing.
        MGE_CHECK_NEAR(leanPosed.bounds.max.y, height, height * 0.02f);
        MGE_CHECK_NEAR(heavyPosed.bounds.max.y, height, height * 0.02f);
        // ...and the belly is deeper on the taller body, in proportion.
        const float leanDepth = depthOf(mesh, leanPosed.vertices, BodyRegion::Torso);
        const float heavyDepth = depthOf(mesh, heavyPosed.vertices, BodyRegion::Torso);
        printf("  height %.2f m: torso depth lean %.3f m, heavy %.3f m\n", height, leanDepth,
               heavyDepth);
        MGE_CHECK(heavyDepth > leanDepth * 1.05f);
    }
}

MGE_TEST(the_face_is_its_own_sub_schema) {
    // A facial parameter touches the head and nothing else. If it leaked into
    // the body a hat would fit and a shirt would not, and nobody would know
    // which parameter did it.
    const SkinnedMeshData mesh = body();
    const MeshData rest = restPose(HumanoidVariant{}, mesh);
    for (size_t i = static_cast<size_t>(Morph::FaceSkull); i < kMorphCount; ++i) {
        HumanoidVariant v;
        kShapeAxes[i].set(v, 1.0f);
        const MeshData posed = restPose(v, mesh);
        float lowest = 9.0f;
        for (size_t k = 0; k < rest.vertices.size(); ++k) {
            if ((posed.vertices[k].position - rest.vertices[k].position).length() < 1e-5f) continue;
            lowest = std::fmin(lowest, rest.vertices[k].position.y);
        }
        printf("  %-16s reaches down to y = %.3f m\n", kShapeAxes[i].name, lowest);
        MGE_CHECK(lowest > 1.44f);  // the jaw line; nothing facial goes below it
    }
}

MGE_TEST(the_variation_scope_costs_almost_nothing_per_character) {
    // The P1 claim for the whole scope: the shared cost is paid once and the
    // per-character cost is a handful of floats.
    const SkinnedMeshData mesh = body();
    size_t deltas = 0;
    for (const MorphTarget& t : mesh.morphs) deltas += t.deltas.size();
    const size_t sharedBytes = deltas * sizeof(MorphDelta);
    const size_t perCharacter = kMorphCount * sizeof(float) + kJointCount * sizeof(Mat4);
    printf("  variation: %zu deltas = %zu B shared, %zu B per character\n", deltas, sharedBytes,
           perCharacter);
    MGE_CHECK(sharedBytes < 96 * 1024);
    MGE_CHECK(perCharacter < 1200);
    // Sparse, or it is not worth the machinery: a facial parameter must not
    // carry a delta for most of the body.
    const MorphTarget* jaw = mesh.morph(Morph::FaceJawWidth);
    MGE_CHECK(jaw != nullptr && jaw->deltas.size() * 4 < mesh.vertices.size());
}

// ------------------------------------------------------------- hem loops ---

MGE_TEST(the_hem_loop_table_names_every_canonical_loop) {
    // B-11 names eleven canonical loops; the eight limb ones exist per side,
    // so the table is nineteen entries.
    const char* trunk[] = {"neck_base", "waist", "hip"};
    const char* sided[] = {"shoulder", "mid_upper_arm", "elbow",     "wrist",
                           "mid_thigh", "knee",         "boot_cuff", "ankle"};
    for (const char* n : trunk) MGE_CHECK(findHemLoop(n) != nullptr);
    for (const char* n : sided) {
        MGE_CHECK(findHemLoop((std::string(n) + "_l").c_str()) != nullptr);
        MGE_CHECK(findHemLoop((std::string(n) + "_r").c_str()) != nullptr);
    }
    MGE_CHECK(templateHemLoopCount() == 3 + 2 * 8);

    for (size_t i = 0; i < templateHemLoopCount(); ++i) {
        const HemLoop& l = templateHemLoop(i);
        const float len = std::sqrt(l.normal.x * l.normal.x + l.normal.y * l.normal.y +
                                    l.normal.z * l.normal.z);
        MGE_CHECK_NEAR(len, 1.0f, 1e-4f);   // a plane needs a unit normal
        MGE_CHECK(l.point.y > 0.0f && l.point.y < templateVariant().height);
    }
}

MGE_TEST(every_hem_loop_closes_on_the_body) {
    // The gate B-11 exists for: a loop that names a height nothing encircles is
    // worse than no table, because a garment authored against it terminates on
    // nothing. The body is a closed shell, so every plane through it must make
    // closed rings and no open chains.
    const SkinnedMeshData mesh = body();
    for (size_t i = 0; i < templateHemLoopCount(); ++i) {
        const HemLoop& l = templateHemLoop(i);
        const HemLoopFit fit = fitHemLoop(mesh, l);
        if (fit.openChains != 0 || fit.rings < 1 || fit.circumference <= 0.0f) {
            printf("  %s: rings %d, open %d, circumference %.3f\n", l.name, fit.rings,
                   fit.openChains, fit.circumference);
        }
        MGE_CHECK(fit.openChains == 0);
        MGE_CHECK(fit.rings >= 1);
        MGE_CHECK(fit.circumference > 0.0f);
    }
}

MGE_TEST(hem_loops_measure_the_limb_they_name) {
    // Circumferences on the template, measured. A garment artist terminating an
    // opening on one of these is entitled to know it is a ring around the part
    // it is named after and roughly how big — so the numbers are asserted, not
    // just printed. Bands are generous; they exist to catch a loop that has
    // moved onto the wrong part of the body, which is exactly what happened
    // three times while this was being built.
    struct Expect {
        const char* name;
        float low, high;   // metres of circumference
        float minShare;    // how much of the ring must be the loop's own region
    };
    const Expect expected[] = {
        {"neck_base", 0.30f, 0.55f, 0.30f},
        {"waist", 0.70f, 1.00f, 0.90f},
        {"hip", 0.85f, 1.15f, 0.10f},   // sits on the Torso/Leg seam
        {"elbow_l", 0.20f, 0.36f, 0.90f},   {"elbow_r", 0.20f, 0.36f, 0.90f},
        {"wrist_l", 0.18f, 0.34f, 0.40f},   {"wrist_r", 0.18f, 0.34f, 0.40f},
        {"mid_thigh_l", 0.42f, 0.62f, 0.90f}, {"mid_thigh_r", 0.42f, 0.62f, 0.90f},
        {"knee_l", 0.28f, 0.42f, 0.90f},    {"knee_r", 0.28f, 0.42f, 0.90f},
        {"boot_cuff_l", 0.25f, 0.40f, 0.90f}, {"boot_cuff_r", 0.25f, 0.40f, 0.90f},
        {"ankle_l", 0.18f, 0.30f, 0.40f},   {"ankle_r", 0.18f, 0.30f, 0.40f},
    };
    const SkinnedMeshData mesh = body();
    for (const Expect& e : expected) {
        const HemLoop* l = findHemLoop(e.name);
        MGE_CHECK(l != nullptr);
        if (l == nullptr) continue;
        const HemLoopFit fit = fitHemLoop(mesh, *l);
        printf("  %-16s %.3f m around, %.0f%% of it %s\n", e.name, fit.circumference,
               100.0 * fit.regionShare, regionNameForTest(l->region));
        MGE_CHECK(fit.circumference > e.low && fit.circumference < e.high);
        MGE_CHECK(fit.regionShare >= e.minShare);
    }
}

MGE_TEST(the_upper_arm_loops_do_not_encircle_the_arm_yet) {
    // Recorded, not hidden. A plane perpendicular to the upper-arm bone does
    // NOT separate the arm from the trunk near the shoulder on this body: the
    // arm hangs about 21 degrees out, so the plane keeps clipping the chest.
    // Measured by scanning down the bone, the first plane that encircles the
    // arm alone is 0.18 m along it — 56% of its 0.324 m length. Above that the
    // ring is the chest wearing a shoulder's name: 1.19 m around, 11% of it
    // actually ArmL.
    //
    // Whether `shoulder` and `mid_upper_arm` should move down to where an
    // arm-only ring exists is a B-11 question and therefore the architect's,
    // not this session's. This test pins the measured state so the answer
    // cannot change silently while it is open.
    const SkinnedMeshData mesh = body();
    for (const char* name : {"shoulder_l", "shoulder_r"}) {
        const HemLoop* l = findHemLoop(name);
        MGE_CHECK(l != nullptr);
        if (l == nullptr) continue;
        const HemLoopFit fit = fitHemLoop(mesh, *l);
        MGE_CHECK(fit.openChains == 0);       // it still closes
        MGE_CHECK(fit.circumference > 0.9f);  // ...around the chest, not the arm
        MGE_CHECK(fit.regionShare < 0.5f);
    }
}

// ------------------------------------------------- region vertex groups ---

MGE_TEST(region_vertex_groups_cover_every_vertex_exactly_once) {
    // B-25, and BODY_CONTRACT.md 9.6: the groups cover 100% of vertices with no
    // overlap. Since 13.7 the body is exported one primitive per region, so no
    // vertex is shared and this is a total function — `bodyVertexRegions`
    // returning false means the body is malformed, not merely untagged.
    const SkinnedMeshData mesh = body();
    std::vector<BodyRegion> groups;
    const bool total = bodyVertexRegions(mesh, groups);
    MGE_CHECK(total);
    MGE_CHECK(groups.size() == mesh.vertices.size());

    size_t perRegion[kBodyRegionCount] = {0};
    for (BodyRegion r : groups) {
        MGE_CHECK(static_cast<size_t>(r) < kBodyRegionCount);
        perRegion[static_cast<size_t>(r)]++;
    }
    size_t counted = 0;
    for (size_t r = 0; r < kBodyRegionCount; ++r) {
        MGE_CHECK(perRegion[r] > 0);   // every region owns vertices, Face included
        counted += perRegion[r];
    }
    MGE_CHECK(counted == mesh.vertices.size());
    printf("  region groups: %zu vertices across %zu regions, none shared, none orphaned\n",
           counted, kBodyRegionCount);
}

// ------------------------------------------------------------------ cost ---

MGE_TEST(body_mesh_stays_inside_the_mobile_budget) {
    std::vector<SkinnedMeshData> lods;
    buildTemplateBodyLods(kAllRegions, lods);
    MGE_CHECK(lods.size() == kBodyLodCount);

    // B-5's caps. LOD0 was 2 200 until ADR 0012 raised it to 2 400 to fund B-9's
    // authored hairline loop; LOD1 and LOD2 did NOT move, and that is the half
    // that matters — a crowd draws LOD1/LOD2, so the working set P1 defends is
    // unchanged. If a future change wants headroom, it does not come from here.
    const size_t budget[kBodyLodCount] = {2400, 1300, 650};
    for (size_t i = 0; i < lods.size(); ++i) {
        printf("  LOD%zu: %zu triangles, %zu vertices\n", i, lods[i].triangleCount(),
               lods[i].vertices.size());
        MGE_CHECK(lods[i].triangleCount() <= budget[i]);
        MGE_CHECK(lods[i].triangleCount() > 0);
        if (i > 0) MGE_CHECK(lods[i].triangleCount() < lods[i - 1].triangleCount());
    }
    // A crowd shares this one mesh; per character the runtime pays only the
    // joint palette. Both numbers are the P1 claim, so both are asserted.
    const size_t meshBytes = lods[0].vertices.size() * sizeof(SkinVertex) +
                             lods[0].indices.size() * sizeof(uint32_t);
    MGE_CHECK(meshBytes < 160 * 1024);
    MGE_CHECK(sizeof(Mat4) * kJointCount <= 1088);  // per-character cost
}

MGE_TEST(body_lods_keep_the_same_silhouette) {
    std::vector<SkinnedMeshData> lods;
    buildTemplateBodyLods(kAllRegions, lods);
    for (size_t i = 1; i < lods.size(); ++i) {
        MGE_CHECK_NEAR(lods[i].bounds.max.y, lods[0].bounds.max.y, 0.03f);
        MGE_CHECK_NEAR(lods[i].bounds.min.y, lods[0].bounds.min.y, 0.03f);
        MGE_CHECK_NEAR(lods[i].bounds.max.x, lods[0].bounds.max.x, 0.03f);
    }
}

// ------------------------------------------------------------- wearables ---

MGE_TEST(body_masking_removes_covered_regions_whole) {
    const SkinnedMeshData full = body();
    const uint32_t dressed = kAllRegions & ~garmentCoverage(WearableKind::Tunic) &
                             ~garmentCoverage(WearableKind::Pants) &
                             ~garmentCoverage(WearableKind::Boots);
    const SkinnedMeshData masked = body(BodyLod::Lod0, dressed);
    MGE_CHECK(masked.triangleCount() < full.triangleCount());
    for (const MeshPart& part : masked.parts) {
        MGE_CHECK(part.region != BodyRegion::Torso);
        MGE_CHECK(part.region != BodyRegion::LegL && part.region != BodyRegion::LegR);
        MGE_CHECK(part.region != BodyRegion::FootL && part.region != BodyRegion::FootR);
    }
    // Hands, arms and head survive — a tunic does not hide them.
    bool hasHead = false;
    for (const MeshPart& part : masked.parts) {
        if (part.region == BodyRegion::Scalp) hasHead = true;
    }
    MGE_CHECK(hasHead);
}

MGE_TEST(garments_enclose_the_body_layer_by_layer) {
    const SkinnedMeshData skin = body();
    GarmentBuildDesc tunicDesc;
    tunicDesc.kind = WearableKind::Tunic;
    tunicDesc.layer = 1;
    GarmentBuildDesc armorDesc;
    armorDesc.kind = WearableKind::Armor;
    armorDesc.layer = 2;
    SkinnedMeshData tunic, armor;
    buildGarmentMesh(tunicDesc, tunic);
    buildGarmentMesh(armorDesc, armor);

    // At chest height: skin < tunic < armor, on the same body — the layering
    // rule of CHARACTERS.md §5.4 measured on real geometry.
    const float skinW = frontOf(skin, skin.vertices, BodyRegion::Torso, 1.26f, 1.32f);
    const float tunicW = frontOf(tunic, tunic.vertices, BodyRegion::Torso, 1.26f, 1.32f);
    const float armorW = frontOf(armor, armor.vertices, BodyRegion::Torso, 1.26f, 1.32f);
    printf("  chest: skin %.4f m, tunic %.4f m, armour %.4f m\n", skinW, tunicW, armorW);
    MGE_CHECK(tunicW > skinW);
    MGE_CHECK(armorW > tunicW);
    MGE_CHECK(armorW - skinW < 0.05f);  // bulk stays plausible, not a barrel

    // Garments are skinned to the same rig, so they animate with the body.
    for (const SkinVertex& v : armor.vertices) {
        int sum = 0;
        for (int k = 0; k < 4; ++k) sum += v.weights[k];
        MGE_CHECK(sum == 255);
    }
}

MGE_TEST(garments_fit_every_variant_by_construction) {
    // One authored garment, many bodies: the palette that stretches the body
    // stretches the garment identically, so the gap between them is constant.
    const SkinnedMeshData skin = body();
    GarmentBuildDesc desc;
    desc.kind = WearableKind::Tunic;
    desc.layer = 1;
    SkinnedMeshData tunic;
    buildGarmentMesh(desc, tunic);

    const float bulks[] = {0.75f, 1.0f, 1.45f};
    const float heightsToTry[] = {1.55f, 1.75f, 2.05f};
    for (float bulk : bulks) {
        for (float height : heightsToTry) {
            HumanoidVariant v;
            v.bulk = bulk;
            v.height = height;
            const MeshData bodyPosed = restPose(v, skin);
            const MeshData tunicPosed = restPose(v, tunic);
            const float y0 = height * 0.70f, y1 = height * 0.76f;
            const float bodyW = maxAbsX(skin, bodyPosed.vertices, BodyRegion::Torso, y0, y1);
            const float tunicW = maxAbsX(tunic, tunicPosed.vertices, BodyRegion::Torso, y0, y1);
            MGE_CHECK(tunicW > bodyW);              // never inside the skin
            MGE_CHECK(tunicW - bodyW < 0.05f);      // never floating off it
        }
    }
}

MGE_TEST(hair_sits_on_the_scalp_of_every_head) {
    const SkinnedMeshData skin = body();
    GarmentBuildDesc desc;
    desc.kind = WearableKind::HairShort;
    desc.layer = 2;
    SkinnedMeshData hair;
    buildGarmentMesh(desc, hair);
    MGE_CHECK(hair.triangleCount() > 0);
    // Above the skull, below a hat's worth of height.
    MGE_CHECK(hair.bounds.max.y > skin.bounds.max.y);
    MGE_CHECK(hair.bounds.max.y - skin.bounds.max.y < 0.04f);

    HumanoidVariant big;
    big.headScale = 1.2f;
    const MeshData head = restPose(big, skin);
    const MeshData hairPosed = restPose(big, hair);
    MGE_CHECK(hairPosed.bounds.max.y > head.bounds.max.y);
    MGE_CHECK(hairPosed.bounds.max.y - head.bounds.max.y < 0.05f);
}

MGE_TEST(posed_character_shares_one_template_and_masks_what_it_wears) {
    // The integration path a game uses: variant + outfit + pose -> drawables.
    const WearableInstance outfit[] = {
        {WearableKind::Tunic, 1, false, {0.5f, 0.4f, 0.25f, 1}},
        {WearableKind::Pants, 1, false, {0.3f, 0.24f, 0.18f, 1}},
        {WearableKind::Boots, 1, false, {0.24f, 0.17f, 0.11f, 1}},
        {WearableKind::HairShort, 2, false, {0.2f, 0.13f, 0.08f, 1}},
    };
    HumanoidVariant variant;
    variant.height = 1.82f;
    variant.bulk = 1.15f;
    Pose pose;

    std::vector<CharacterPiece> naked;
    buildPosedCharacter(variant, nullptr, 0, pose, BodyLod::Lod0, naked);
    MGE_CHECK(naked.size() == 1);

    std::vector<CharacterPiece> dressed;
    buildPosedCharacter(variant, outfit, 4, pose, BodyLod::Lod0, dressed);
    MGE_CHECK(dressed.size() == 5);  // body + four garments
    for (const CharacterPiece& piece : dressed) MGE_CHECK(!piece.mesh.indices.empty());

    // Covered skin is not drawn — and the saving is real geometry, not a
    // flag: the body draws fewer triangles when dressed.
    MGE_CHECK(dressed[0].mesh.indices.size() < naked[0].mesh.indices.size());
    // Same vertex buffer either way: masking is an index-range decision.
    MGE_CHECK(dressed[0].mesh.vertices.size() == naked[0].mesh.vertices.size());

    // The shared template really is shared — same object for every character.
    const void* first = &sharedTemplateLods()[0];
    std::vector<CharacterPiece> other;
    buildPosedCharacter(HumanoidVariant{}, nullptr, 0, pose, BodyLod::Lod0, other);
    MGE_CHECK(first == &sharedTemplateLods()[0]);
    MGE_CHECK(sharedTemplateLods().size() == kBodyLodCount);

    // Everything the character wears follows the same variant.
    for (const CharacterPiece& piece : dressed) {
        MGE_CHECK(piece.mesh.bounds.max.y < variant.height + 0.06f);
        MGE_CHECK(piece.mesh.bounds.min.y > -0.03f);
    }
}

MGE_TEST(trousers_and_boots_leave_no_bare_gap) {
    // Pants mask the whole leg, so what they do not cover, nothing covers.
    // Hem and cuff must overlap — this is the defect that reads as a floating
    // boot on a legless shin.
    GarmentBuildDesc pantsDesc;
    pantsDesc.kind = WearableKind::Pants;
    GarmentBuildDesc bootDesc;
    bootDesc.kind = WearableKind::Boots;
    SkinnedMeshData pants, boots;
    buildGarmentMesh(pantsDesc, pants);
    buildGarmentMesh(bootDesc, boots);
    MGE_CHECK(pants.bounds.min.y < boots.bounds.max.y);

    // And the same must hold on a tall body and a short one, since both are
    // the same meshes through a different palette.
    for (float height : {1.52f, 1.75f, 2.10f}) {
        HumanoidVariant v;
        v.height = height;
        Pose pose;
        Mat4 palette[kJointCount];
        buildSkinPalette(v, pose, palette);
        MeshData pantsPosed, bootsPosed;
        skinMesh(pants, palette, pantsPosed);
        skinMesh(boots, palette, bootsPosed);
        MGE_CHECK(pantsPosed.bounds.min.y < bootsPosed.bounds.max.y);
    }
}
