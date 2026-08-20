// The humanoid template body (task 8.12): the model's own acceptance tests.
// A body is "good" here in four measurable senses — it is a closed, correctly
// wound surface; it has human proportions; it deforms without collapsing; and
// it stays inside the mobile triangle budget at every LOD. Every claim the
// modelling doc makes is checked below.

#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <set>
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

MeshData restPose(const HumanoidVariant& variant, const SkinnedMeshData& mesh) {
    Pose pose;
    Mat4 palette[kJointCount];
    buildSkinPalette(variant, pose, palette);
    MeshData out;
    skinMesh(mesh, palette, out);
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
    const float shoulders =
        std::fmax(maxAbsX(mesh, mesh.vertices, BodyRegion::Torso, 1.40f, 1.50f),
                  maxAbsX(mesh, mesh.vertices, BodyRegion::ArmL, 1.40f, 1.50f)) * 2.0f;
    const float waist = maxAbsX(mesh, mesh.vertices, BodyRegion::Torso, 1.05f, 1.09f) * 2.0f;
    const float hips = maxAbsX(mesh, mesh.vertices, BodyRegion::Torso, 0.82f, 0.88f) * 2.0f;
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
    // Face is a texture island in v1, not a separate shell; every other
    // region is present as real geometry.
    for (size_t r = 0; r < kBodyRegionCount; ++r) {
        if (static_cast<BodyRegion>(r) == BodyRegion::Face) continue;
        MGE_CHECK(seen.count(static_cast<uint8_t>(r)) == 1);
    }
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

// ------------------------------------------------------------------ cost ---

MGE_TEST(body_mesh_stays_inside_the_mobile_budget) {
    std::vector<SkinnedMeshData> lods;
    buildTemplateBodyLods(kAllRegions, lods);
    MGE_CHECK(lods.size() == kBodyLodCount);

    const size_t budget[kBodyLodCount] = {2200, 1300, 650};
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
    const float skinW = maxAbsX(skin, skin.vertices, BodyRegion::Torso, 1.26f, 1.32f);
    const float tunicW = maxAbsX(tunic, tunic.vertices, BodyRegion::Torso, 1.26f, 1.32f);
    const float armorW = maxAbsX(armor, armor.vertices, BodyRegion::Torso, 1.26f, 1.32f);
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
