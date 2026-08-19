#include "mge/character/body_mesh.h"

#include <cmath>
#include <cstring>
#include <unordered_map>

// The template body is modelled the way a character artist models one: a
// skeleton first, then cross-section rings along each limb whose radii follow
// real anatomy (deltoid, bicep, forearm flare, glute, calf, jaw), then quads
// stitched between neighbouring rings. What differs from a hand-authored mesh
// is only WHERE the rings come from — they are computed from the canonical
// rig, so the same profiles generate every LOD and every garment, and so no
// asset can ever drift out of sync with the skeleton.
//
// Deformation rules the profiles obey (standard character-topology practice):
//   * three loops across every bending joint — above, at, and below the
//     elbow/knee — so a bend keeps its volume instead of collapsing
//   * a loop exactly at the joint centre, weighted 50/50 across the two bones
//   * the shoulder is a domed cap that stays inside the torso, so raising the
//     arm cannot tear the armpit
//   * no vertex carries more than two influences: linear blend skinning is
//     what mobile GPUs run, and two influences keep its twisting artifacts
//     ("candy wrapper") out of the shipped body

namespace mge {

namespace {

constexpr size_t idx(Joint j) { return static_cast<size_t>(j); }

float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

// ------------------------------------------------------------ ring model ---

// One cross-section of the body. Rings are authored in bind space (character
// local, +Y up, facing -Z, +X is the character's left).
struct RingDef {
    Vec3 center;
    Vec3 axisU{1, 0, 0};  // cross-section plane axes
    Vec3 axisV{0, 0, 1};
    float ru = 0.1f;      // radius along u
    float rv = 0.1f;      // radius along v
    float square = 1.0f;  // 1 = ellipse, < 1 = squarer (chest, sole)
    Joint ja = Joint::Hips;
    Joint jb = Joint::Hips;
    float wb = 0.0f;   // weight of jb (ja gets 1 - wb)
    float v01 = 0.0f;  // UV coordinate along the shell
    uint8_t detail = 0;  // 0 keep always, 1 drop at Lod1, 2 drop at Lod2
};

using Rings = std::vector<RingDef>;

// Per-region UV islands of the template layout: every skin texture ever made
// for this engine follows this chart (CHARACTERS.md §4).
struct Island {
    float u0, v0, u1, v1;
};

Island islandOf(BodyRegion region) {
    switch (region) {
        case BodyRegion::Scalp: return {0.50f, 0.72f, 0.78f, 1.00f};
        case BodyRegion::Face: return {0.78f, 0.72f, 1.00f, 1.00f};
        case BodyRegion::Neck: return {0.50f, 0.62f, 0.66f, 0.72f};
        case BodyRegion::Torso: return {0.00f, 0.55f, 0.48f, 1.00f};
        case BodyRegion::ArmL: return {0.00f, 0.28f, 0.22f, 0.53f};
        case BodyRegion::ArmR: return {0.24f, 0.28f, 0.46f, 0.53f};
        case BodyRegion::HandL: return {0.50f, 0.40f, 0.68f, 0.60f};
        case BodyRegion::HandR: return {0.70f, 0.40f, 0.88f, 0.60f};
        case BodyRegion::LegL: return {0.00f, 0.00f, 0.22f, 0.26f};
        case BodyRegion::LegR: return {0.24f, 0.00f, 0.46f, 0.26f};
        case BodyRegion::FootL: return {0.50f, 0.14f, 0.68f, 0.34f};
        case BodyRegion::FootR: return {0.70f, 0.14f, 0.88f, 0.34f};
        default: return {0.0f, 0.0f, 1.0f, 1.0f};
    }
}

uint16_t quantizeUv(float t) {
    return static_cast<uint16_t>(clampf(t, 0.0f, 1.0f) * 65535.0f + 0.5f);
}

void setSkin(SkinVertex& v, Joint ja, Joint jb, float wb) {
    const float w = clampf(wb, 0.0f, 1.0f);
    const uint8_t b = static_cast<uint8_t>(w * 255.0f + 0.5f);
    v.joints[0] = static_cast<uint8_t>(idx(ja));
    v.joints[1] = static_cast<uint8_t>(idx(jb));
    v.joints[2] = 0;
    v.joints[3] = 0;
    v.weights[0] = static_cast<uint8_t>(255 - b);
    v.weights[1] = b;
    v.weights[2] = 0;
    v.weights[3] = 0;
}

// Superellipse sample: exponent < 1 flattens the section toward a rounded
// box (chest, sole), exponent 1 is a plain ellipse (limbs).
void sectionSample(float angle, float square, float& cu, float& cv) {
    const float c = std::cos(angle);
    const float s = std::sin(angle);
    if (square >= 0.999f) {
        cu = c;
        cv = s;
        return;
    }
    cu = (c < 0 ? -1.0f : 1.0f) * std::pow(std::fabs(c), square);
    cv = (s < 0 ? -1.0f : 1.0f) * std::pow(std::fabs(s), square);
}

// ---------------------------------------------------------- shell emitter --

// Emits one closed shell: a ring-to-ring quad tube plus end caps. The seam
// column is duplicated so the UV chart has no wrap-around distortion.
void emitShell(const Rings& rings, int segments, BodyRegion region, bool capStart, bool capEnd,
               SkinnedMeshData& out) {
    if (rings.size() < 2 || segments < 3) return;
    const Island island = islandOf(region);
    const uint32_t firstIndex = static_cast<uint32_t>(out.indices.size());
    const uint32_t base = static_cast<uint32_t>(out.vertices.size());
    const int cols = segments + 1;  // duplicated seam column

    for (const RingDef& ring : rings) {
        for (int i = 0; i < cols; ++i) {
            const float t = static_cast<float>(i % segments) / static_cast<float>(segments);
            float cu, cv;
            sectionSample(t * 2.0f * kPi, ring.square, cu, cv);
            SkinVertex v;
            v.position = ring.center + ring.axisU * (ring.ru * cu) + ring.axisV * (ring.rv * cv);
            v.normal = Vec3{0, 0, 0};
            const float u01 = static_cast<float>(i) / static_cast<float>(segments);
            v.uv[0] = quantizeUv(island.u0 + (island.u1 - island.u0) * u01);
            v.uv[1] = quantizeUv(island.v0 + (island.v1 - island.v0) * ring.v01);
            setSkin(v, ring.ja, ring.jb, ring.wb);
            out.vertices.push_back(v);
        }
    }

    for (size_t r = 0; r + 1 < rings.size(); ++r) {
        const uint32_t a = base + static_cast<uint32_t>(r * cols);
        const uint32_t b = a + static_cast<uint32_t>(cols);
        for (int i = 0; i < segments; ++i) {
            const uint32_t v0 = a + static_cast<uint32_t>(i);
            const uint32_t v1 = a + static_cast<uint32_t>(i + 1);
            const uint32_t v2 = b + static_cast<uint32_t>(i + 1);
            const uint32_t v3 = b + static_cast<uint32_t>(i);
            out.indices.push_back(v0);
            out.indices.push_back(v1);
            out.indices.push_back(v2);
            out.indices.push_back(v0);
            out.indices.push_back(v2);
            out.indices.push_back(v3);
        }
    }

    const auto cap = [&](size_t ringIndex, bool start) {
        const RingDef& ring = rings[ringIndex];
        SkinVertex center;
        center.position = ring.center;
        center.normal = Vec3{0, 0, 0};
        const float uMid = (island.u0 + island.u1) * 0.5f;
        center.uv[0] = quantizeUv(uMid);
        center.uv[1] = quantizeUv(island.v0 + (island.v1 - island.v0) * ring.v01);
        setSkin(center, ring.ja, ring.jb, ring.wb);
        const uint32_t c = static_cast<uint32_t>(out.vertices.size());
        out.vertices.push_back(center);
        const uint32_t ringBase = base + static_cast<uint32_t>(ringIndex * cols);
        for (int i = 0; i < segments; ++i) {
            const uint32_t v0 = ringBase + static_cast<uint32_t>(i);
            const uint32_t v1 = ringBase + static_cast<uint32_t>(i + 1);
            out.indices.push_back(c);
            if (start) {
                out.indices.push_back(v1);
                out.indices.push_back(v0);
            } else {
                out.indices.push_back(v0);
                out.indices.push_back(v1);
            }
        }
    };
    if (capStart) cap(0, true);
    if (capEnd) cap(rings.size() - 1, false);

    // Orient outward: a closed shell's faces must face away from its centre.
    // Deciding it here, from the geometry, keeps every profile builder free of
    // winding bookkeeping.
    Vec3 centroid{0, 0, 0};
    for (size_t i = base; i < out.vertices.size(); ++i) centroid += out.vertices[i].position;
    centroid *= 1.0f / static_cast<float>(out.vertices.size() - base);
    float outward = 0.0f;
    for (size_t i = firstIndex; i < out.indices.size(); i += 3) {
        const Vec3& p0 = out.vertices[out.indices[i]].position;
        const Vec3& p1 = out.vertices[out.indices[i + 1]].position;
        const Vec3& p2 = out.vertices[out.indices[i + 2]].position;
        const Vec3 n = (p1 - p0).cross(p2 - p0);
        outward += n.dot(((p0 + p1 + p2) * (1.0f / 3.0f)) - centroid);
    }
    if (outward < 0.0f) {
        for (size_t i = firstIndex; i < out.indices.size(); i += 3) {
            const uint32_t tmp = out.indices[i + 1];
            out.indices[i + 1] = out.indices[i + 2];
            out.indices[i + 2] = tmp;
        }
    }

    MeshPart part;
    part.region = region;
    part.firstIndex = firstIndex;
    part.indexCount = static_cast<uint32_t>(out.indices.size()) - firstIndex;
    out.parts.push_back(part);
}

// Smooth normals, welded across the duplicated seam column so shading has no
// visible seam down the body.
void computeNormals(SkinnedMeshData& mesh) {
    for (SkinVertex& v : mesh.vertices) v.normal = Vec3{0, 0, 0};
    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        SkinVertex& a = mesh.vertices[mesh.indices[i]];
        SkinVertex& b = mesh.vertices[mesh.indices[i + 1]];
        SkinVertex& c = mesh.vertices[mesh.indices[i + 2]];
        const Vec3 n = (b.position - a.position).cross(c.position - a.position);
        a.normal += n;  // area-weighted: the cross product's length is 2*area
        b.normal += n;
        c.normal += n;
    }
    // Weld: vertices sharing a position share a normal. The key packs the
    // quantized coordinates exactly (21 bits each) rather than hashing them —
    // a hash collision here silently averages two unrelated normals, which
    // shows up as a black facet nowhere near the vertices involved.
    std::unordered_map<uint64_t, Vec3> welded;
    const auto key = [](const Vec3& p) {
        const auto q = [](float v) {
            const int64_t t = std::lround(v * 10000.0f) + (1 << 20);
            return static_cast<uint64_t>(t < 0 ? 0 : (t > 0x1FFFFF ? 0x1FFFFF : t));
        };
        return (q(p.x) << 42) | (q(p.y) << 21) | q(p.z);
    };
    welded.reserve(mesh.vertices.size() * 2);
    for (const SkinVertex& v : mesh.vertices) welded[key(v.position)] += v.normal;
    for (SkinVertex& v : mesh.vertices) {
        const Vec3 n = welded[key(v.position)];
        v.normal = n.lengthSq() > 0.0f ? n.normalized() : Vec3{0, 1, 0};
    }
}

void dropDetail(Rings& rings, BodyLod lod) {
    const uint8_t keep = lod == BodyLod::Lod0 ? 2 : (lod == BodyLod::Lod1 ? 1 : 0);
    Rings kept;
    kept.reserve(rings.size());
    for (const RingDef& r : rings) {
        if (r.detail <= keep) kept.push_back(r);
    }
    rings.swap(kept);
}

int segmentsFor(BodyLod lod, int lod0) {
    switch (lod) {
        case BodyLod::Lod1: return lod0 <= 8 ? 6 : (lod0 / 2 < 6 ? 6 : lod0 / 2);
        case BodyLod::Lod2: return lod0 <= 8 ? 4 : 6;
        default: return lod0;
    }
}

// ---------------------------------------------------- template proportions --

// Landmarks of the template body, all derived from the canonical rig so mesh
// and skeleton can never disagree.
struct Landmarks {
    Skeleton skeleton;
    Vec3 bind[kJointCount];
    float hipY, waistY, chestY, shoulderY, neckY, chinY, crownY;
    float elbowY, wristY, kneeY, ankleY;
    float shoulderX, armX, hipX, legX;
    float headHalfW, headHalfD, headHalfH, headCenterY;
};

const Landmarks& landmarks() {
    static const Landmarks lm = [] {
        Landmarks l;
        l.skeleton = buildSkeleton(HumanoidVariant{});
        for (size_t j = 0; j < kJointCount; ++j) {
            const int8_t parent = l.skeleton.parent[j];
            l.bind[j] = parent < 0 ? l.skeleton.bindOffset[j]
                                   : l.bind[static_cast<size_t>(parent)] +
                                         l.skeleton.bindOffset[j];
        }
        l.hipY = l.bind[idx(Joint::Hips)].y;
        l.waistY = l.bind[idx(Joint::Spine)].y;
        l.chestY = l.bind[idx(Joint::Chest)].y;
        l.neckY = l.bind[idx(Joint::Neck)].y;
        l.shoulderY = l.bind[idx(Joint::UpperArmL)].y;
        l.elbowY = l.bind[idx(Joint::ForearmL)].y;
        l.wristY = l.bind[idx(Joint::HandL)].y;
        l.kneeY = l.bind[idx(Joint::ShinL)].y;
        l.ankleY = l.bind[idx(Joint::FootL)].y;
        l.shoulderX = l.bind[idx(Joint::UpperArmL)].x;
        l.hipX = l.bind[idx(Joint::ThighL)].x;
        // The mesh sits inboard of the leg joints and just inboard of the
        // shoulder joints: joint separation is skeletal width, not surface
        // width, and a 1.75 m body measures ~0.38 m across the hips.
        l.armX = l.shoulderX * 0.86f;
        l.legX = l.hipX * 0.62f;
        const float head = l.skeleton.headSize;
        l.headHalfW = head * 0.33f;   // 0.156 m across — a real skull
        l.headHalfD = head * 0.41f;   // 0.194 m front to back: heads are deep
        l.headHalfH = head * 0.472f;
        // Sits high enough on the rig that the neck is visible between the
        // jaw and the trapezius, which is what makes a body read as a body.
        l.headCenterY = l.bind[idx(Joint::Head)].y + head * 0.40f;
        l.chinY = l.headCenterY - l.headHalfH;
        l.crownY = l.headCenterY + l.headHalfH;
        return l;
    }();
    return lm;
}

// ------------------------------------------------------------- profiles ----

// A trunk ring: horizontal section, u = X (width), v = Z (depth).
RingDef trunkRing(float y, float halfWidth, float halfDepth, float zShift, float square, Joint ja,
                  Joint jb, float wb, float v01, uint8_t detail = 0) {
    RingDef r;
    r.center = Vec3{0, y, zShift};
    r.axisU = Vec3{1, 0, 0};
    r.axisV = Vec3{0, 0, 1};
    r.ru = halfWidth;
    r.rv = halfDepth;
    r.square = square;
    r.ja = ja;
    r.jb = jb;
    r.wb = wb;
    r.v01 = v01;
    r.detail = detail;
    return r;
}

RingDef limbRing(float x, float y, float z, float radiusX, float radiusZ, Joint ja, Joint jb,
                 float wb, float v01, uint8_t detail = 0) {
    RingDef r;
    r.center = Vec3{x, y, z};
    r.axisU = Vec3{1, 0, 0};
    r.axisV = Vec3{0, 0, 1};
    r.ru = radiusX;
    r.rv = radiusZ;
    r.ja = ja;
    r.jb = jb;
    r.wb = wb;
    r.v01 = v01;
    r.detail = detail;
    return r;
}

// Trunk: pelvis -> waist -> ribcage -> chest -> shoulder shelf -> neck base.
// The waist pinch and the chest flare are what stop a body reading as a box.
Rings trunkRings(float grow) {
    const Landmarks& l = landmarks();
    const float g = grow;
    Rings r;
    // The crotch ring descends BETWEEN the thighs: without it the pelvis ends
    // in a flat shelf and the hips read as a box.
    r.push_back(trunkRing(l.hipY - 0.165f, 0.108f + g, 0.096f + g, 0.008f, 0.95f, Joint::Hips,
                          Joint::Hips, 0.0f, 0.00f));
    r.push_back(trunkRing(l.hipY - 0.075f, 0.150f + g, 0.114f + g, 0.010f, 0.90f, Joint::Hips,
                          Joint::Hips, 0.0f, 0.08f));
    r.push_back(trunkRing(l.hipY - 0.010f, 0.162f + g, 0.118f + g, 0.004f, 0.88f, Joint::Hips,
                          Joint::Hips, 0.0f, 0.16f, 1));
    r.push_back(trunkRing(l.hipY + 0.070f, 0.150f + g, 0.108f + g, 0.000f, 0.88f, Joint::Hips,
                          Joint::Spine, 0.40f, 0.26f));
    r.push_back(trunkRing(l.waistY, 0.139f + g, 0.099f + g, 0.000f, 0.86f, Joint::Spine,
                          Joint::Spine, 0.0f, 0.38f));
    r.push_back(trunkRing((l.waistY + l.chestY) * 0.5f, 0.166f + g, 0.115f + g, -0.005f, 0.82f,
                          Joint::Spine, Joint::Chest, 0.45f, 0.50f, 1));
    r.push_back(trunkRing(l.chestY, 0.188f + g, 0.124f + g, -0.008f, 0.80f, Joint::Chest,
                          Joint::Chest, 0.0f, 0.62f));
    r.push_back(trunkRing(l.chestY + 0.080f, 0.190f + g, 0.116f + g, -0.004f, 0.80f, Joint::Chest,
                          Joint::Chest, 0.0f, 0.74f, 1));
    r.push_back(trunkRing(l.shoulderY - 0.055f, 0.172f + g, 0.104f + g, 0.000f, 0.84f,
                          Joint::Chest, Joint::Chest, 0.0f, 0.86f));
    // Trapezius: the slope from the shoulder line into the neck. It has to
    // end BELOW the jaw or the head looks bolted to the chest.
    r.push_back(trunkRing(l.shoulderY - 0.012f, 0.132f + g, 0.098f + g, 0.004f, 0.90f,
                          Joint::Chest, Joint::Neck, 0.15f, 0.94f, 1));
    r.push_back(trunkRing(l.shoulderY + 0.014f, 0.082f + g, 0.074f + g, 0.006f, 0.94f,
                          Joint::Chest, Joint::Neck, 0.35f, 1.00f));
    return r;
}

Rings neckRings(float grow) {
    const Landmarks& l = landmarks();
    const float rw = 0.057f + grow;
    Rings r;
    r.push_back(limbRing(0, l.shoulderY - 0.020f, 0.006f, rw * 1.22f, rw * 1.20f, Joint::Chest,
                         Joint::Neck, 0.5f, 0.0f));
    r.push_back(limbRing(0, l.neckY, 0.004f, rw, rw * 1.06f, Joint::Neck, Joint::Neck, 0.0f, 0.5f,
                         1));
    r.push_back(limbRing(0, l.chinY + 0.012f, 0.000f, rw * 0.96f, rw * 1.02f, Joint::Neck,
                         Joint::Head, 0.6f, 1.0f));
    return r;
}

// Head: an ovoid with a cranium, a brow, a tapering jaw and a nose. No eye or
// mouth geometry — those belong to the skin texture at this budget.
Rings headRings(float grow, bool cranialOnly) {
    const Landmarks& l = landmarks();
    const float w = l.headHalfW + grow;
    const float d = l.headHalfD + grow;
    const float h = l.headHalfH;
    const float cy = l.headCenterY;
    struct Slice {
        float t;      // -1 chin .. +1 crown
        float sw;     // width scale
        float sd;     // depth scale
        float zoff;   // section shift (jaw sits back, brow forward)
        uint8_t det;
    };
    static const Slice slices[] = {
        {-1.00f, 0.30f, 0.42f, 0.012f, 0},  // chin
        {-0.78f, 0.62f, 0.74f, 0.006f, 1},  // jaw
        {-0.50f, 0.84f, 0.93f, -0.002f, 0}, // cheekbone / ear line
        {-0.18f, 0.97f, 1.00f, -0.006f, 1}, // brow
        {0.16f, 1.00f, 0.98f, -0.002f, 0},  // temple
        {0.50f, 0.94f, 0.92f, 0.004f, 1},   // upper cranium
        {0.80f, 0.76f, 0.74f, 0.008f, 0},   // crown
        {0.95f, 0.46f, 0.44f, 0.010f, 1},
    };
    Rings r;
    for (const Slice& s : slices) {
        if (cranialOnly && s.t < -0.16f) continue;
        // `grow` inflates the ovoid in every direction, vertically included —
        // a hairstyle has to sit ABOVE the skull, not just around it.
        RingDef ring = limbRing(0, cy + (h + grow) * s.t, s.zoff, w * s.sw, d * s.sd, Joint::Head,
                                Joint::Head, 0.0f, (s.t + 1.0f) * 0.5f, s.det);
        ring.square = 0.92f;
        r.push_back(ring);
    }
    return r;
}

// Arm: deltoid dome, bicep taper, three loops across the elbow, forearm flare,
// wrist. Sections are slightly flattened (rz < rx) like a real limb.
Rings armRings(int side, float grow, float stopBelowShoulder) {
    const Landmarks& l = landmarks();
    const float x = l.armX * (side == 0 ? 1.0f : -1.0f);
    const float upper = l.skeleton.upperArmLength;
    const float fore = l.skeleton.forearmLength;
    const float lowest = l.shoulderY - stopBelowShoulder;
    struct Slice {
        float y;
        float r;
        Joint ja, jb;
        float wb;
        float v01;
        uint8_t det;
    };
    const float sy = l.shoulderY;
    const Slice slices[] = {
        {sy + 0.048f, 0.015f, Joint::Chest, Joint::UpperArmL, 0.40f, 0.00f, 0},
        {sy + 0.036f, 0.032f, Joint::Chest, Joint::UpperArmL, 0.45f, 0.02f, 1},
        {sy + 0.020f, 0.045f, Joint::Chest, Joint::UpperArmL, 0.55f, 0.04f, 0},
        {sy + 0.002f, 0.050f, Joint::Chest, Joint::UpperArmL, 0.75f, 0.06f, 1},
        {sy - 0.045f, 0.052f, Joint::UpperArmL, Joint::UpperArmL, 0.0f, 0.12f, 0},
        {sy - upper * 0.45f, 0.048f, Joint::UpperArmL, Joint::UpperArmL, 0.0f, 0.30f, 1},
        {l.elbowY + 0.048f, 0.041f, Joint::UpperArmL, Joint::UpperArmL, 0.0f, 0.46f, 0},
        {l.elbowY, 0.042f, Joint::UpperArmL, Joint::ForearmL, 0.5f, 0.53f, 0},
        {l.elbowY - 0.048f, 0.044f, Joint::ForearmL, Joint::ForearmL, 0.0f, 0.60f, 0},
        {l.elbowY - fore * 0.45f, 0.038f, Joint::ForearmL, Joint::ForearmL, 0.0f, 0.76f, 1},
        {l.wristY + 0.028f, 0.030f, Joint::ForearmL, Joint::ForearmL, 0.0f, 0.92f, 1},
        {l.wristY + 0.004f, 0.027f, Joint::ForearmL, Joint::HandL, 0.35f, 1.00f, 0},
    };
    Rings r;
    for (const Slice& s : slices) {
        if (s.y < lowest) continue;
        const Joint ja = side == 0 ? s.ja : mirrored(s.ja);
        const Joint jb = side == 0 ? s.jb : mirrored(s.jb);
        r.push_back(limbRing(x, s.y, 0.0f, s.r + grow, (s.r + grow) * 0.92f, ja, jb, s.wb, s.v01,
                             s.det));
    }
    return r;
}

// Hand: a tapered palm with a thumb — enough shape to read as a hand and to
// hold a grip point, no finger geometry at this budget.
Rings handRings(int side, float grow) {
    const Landmarks& l = landmarks();
    const float x = l.armX * (side == 0 ? 1.0f : -1.0f);
    const Joint hand = side == 0 ? Joint::HandL : Joint::HandR;
    const float w = l.wristY;
    Rings r;
    // A relaxed arm hangs with the palm toward the thigh: the hand is thin
    // across (X) and deep front-to-back (Z). The reverse — the shape a naive
    // box gives you — reads instantly as wrong.
    r.push_back(limbRing(x, w + 0.006f, 0.000f, 0.021f + grow, 0.027f + grow, hand, hand, 0,
                         0.00f));
    r.push_back(limbRing(x, w - 0.030f, -0.004f, 0.024f + grow, 0.044f + grow, hand, hand, 0,
                         0.25f, 1));
    r.push_back(limbRing(x, w - 0.080f, -0.006f, 0.023f + grow, 0.046f + grow, hand, hand, 0,
                         0.55f));
    r.push_back(limbRing(x, w - 0.130f, -0.008f, 0.020f + grow, 0.042f + grow, hand, hand, 0,
                         0.80f, 1));
    r.push_back(limbRing(x, w - 0.162f, -0.010f, 0.016f + grow, 0.034f + grow, hand, hand, 0,
                         0.94f));
    r.push_back(limbRing(x, w - 0.178f, -0.011f, 0.008f + grow, 0.019f + grow, hand, hand, 0,
                         1.00f));
    return r;
}

Rings thumbRings(int side) {
    const Landmarks& l = landmarks();
    const float s = side == 0 ? 1.0f : -1.0f;
    const float x = l.armX * s;
    const Joint hand = side == 0 ? Joint::HandL : Joint::HandR;
    const float w = l.wristY;
    Rings r;
    // Forward and slightly inward, along the front edge of the palm.
    r.push_back(limbRing(x - 0.006f * s, w - 0.034f, -0.036f, 0.015f, 0.016f, hand, hand, 0,
                         0.0f));
    r.push_back(limbRing(x - 0.012f * s, w - 0.058f, -0.050f, 0.013f, 0.014f, hand, hand, 0,
                         0.5f));
    r.push_back(limbRing(x - 0.016f * s, w - 0.082f, -0.058f, 0.009f, 0.010f, hand, hand, 0,
                         1.0f));
    return r;
}

// Leg: glute, thigh taper, three loops across the knee, calf bulge (set back,
// as it is on a real leg), ankle.
Rings legRings(int side, float grow, float stopAboveAnkle) {
    const Landmarks& l = landmarks();
    const float x = l.legX * (side == 0 ? 1.0f : -1.0f);
    const float thigh = l.skeleton.thighLength;
    struct Slice {
        float y;
        float rx;
        float rz;
        float z;
        Joint ja, jb;
        float wb;
        float v01;
        uint8_t det;
    };
    const Slice slices[] = {
        {l.hipY + 0.085f, 0.046f, 0.050f, 0.004f, Joint::Hips, Joint::ThighL, 0.40f, 0.00f, 0},
        {l.hipY + 0.035f, 0.082f, 0.088f, 0.006f, Joint::Hips, Joint::ThighL, 0.55f, 0.04f, 1},
        {l.hipY - 0.045f, 0.095f, 0.102f, 0.008f, Joint::ThighL, Joint::ThighL, 0.0f, 0.12f, 1},
        {l.hipY - thigh * 0.42f, 0.084f, 0.090f, 0.004f, Joint::ThighL, Joint::ThighL, 0.0f,
         0.28f, 0},
        {l.kneeY + 0.060f, 0.068f, 0.072f, 0.000f, Joint::ThighL, Joint::ThighL, 0.0f, 0.44f, 1},
        {l.kneeY, 0.064f, 0.068f, -0.004f, Joint::ThighL, Joint::ShinL, 0.5f, 0.52f, 0},
        {l.kneeY - 0.055f, 0.064f, 0.070f, 0.004f, Joint::ShinL, Joint::ShinL, 0.0f, 0.60f, 0},
        {l.kneeY - 0.130f, 0.060f, 0.068f, 0.012f, Joint::ShinL, Joint::ShinL, 0.0f, 0.72f, 1},
        {l.ankleY + 0.140f, 0.043f, 0.046f, 0.004f, Joint::ShinL, Joint::ShinL, 0.0f, 0.88f, 1},
        {l.ankleY + 0.040f, 0.034f, 0.038f, 0.000f, Joint::ShinL, Joint::FootL, 0.30f, 1.00f, 0},
    };
    Rings r;
    for (const Slice& s : slices) {
        if (s.y < l.ankleY + stopAboveAnkle) continue;
        const Joint ja = side == 0 ? s.ja : mirrored(s.ja);
        const Joint jb = side == 0 ? s.jb : mirrored(s.jb);
        RingDef ring = limbRing(x, s.y, s.z, s.rx + grow, s.rz + grow, ja, jb, s.wb, s.v01, s.det);
        ring.square = 0.95f;
        r.push_back(ring);
    }
    return r;
}

// Foot: sections stacked along -Z (toes forward), sole flat on the ground.
Rings footRings(int side, float grow) {
    const Landmarks& l = landmarks();
    const float x = l.legX * (side == 0 ? 1.0f : -1.0f);
    const Joint foot = side == 0 ? Joint::FootL : Joint::FootR;
    struct Slice {
        float z;
        float rx;
        float ry;
        float v01;
        uint8_t det;
    };
    static const Slice slices[] = {
        {0.076f, 0.014f, 0.018f, 0.00f, 1},   // heel round-off
        {0.062f, 0.026f, 0.030f, 0.06f, 0},   // heel
        {0.030f, 0.033f, 0.042f, 0.15f, 1},   // under the ankle
        {-0.030f, 0.036f, 0.034f, 0.35f, 0},  // arch
        {-0.105f, 0.040f, 0.026f, 0.65f, 0},  // ball
        {-0.155f, 0.036f, 0.020f, 0.85f, 1},
        {-0.185f, 0.024f, 0.014f, 1.00f, 0},  // toe
    };
    Rings r;
    for (const Slice& s : slices) {
        RingDef ring;
        ring.center = Vec3{x, s.ry + grow, s.z};  // sole stays on the ground
        ring.axisU = Vec3{1, 0, 0};
        ring.axisV = Vec3{0, 1, 0};
        ring.ru = s.rx + grow;
        ring.rv = s.ry + grow;
        ring.square = 0.62f;  // flat sole, boxy sides
        ring.ja = foot;
        ring.jb = foot;
        ring.wb = 0.0f;
        ring.v01 = s.v01;
        ring.detail = s.det;
        r.push_back(ring);
    }
    return r;
}

// Shapes the nose and brow into the finished head shell: two small pushes
// forward (-Z) on the vertices nearest the face's mid-line.
void sculptFace(SkinnedMeshData& mesh, uint32_t firstVertex) {
    const Landmarks& l = landmarks();
    const float noseY = l.headCenterY - l.headHalfH * 0.28f;
    const float browY = l.headCenterY - l.headHalfH * 0.10f;
    for (size_t i = firstVertex; i < mesh.vertices.size(); ++i) {
        SkinVertex& v = mesh.vertices[i];
        if (v.position.z >= -0.01f) continue;  // front of the head only
        const float lateral = std::fabs(v.position.x) / l.headHalfW;
        const float nose = std::exp(-std::pow((v.position.y - noseY) / 0.030f, 2.0f)) *
                           std::exp(-std::pow(lateral / 0.32f, 2.0f));
        const float brow = std::exp(-std::pow((v.position.y - browY) / 0.022f, 2.0f)) *
                           clampf(1.0f - lateral * 0.9f, 0.0f, 1.0f);
        v.position.z -= nose * 0.020f + brow * 0.005f;
    }
}

}  // namespace

// Mirrors a left-side joint to its right-side twin (the rig's enum pairs them
// in order, so the arms/legs blocks are a fixed offset apart).
Joint mirrored(Joint j) {
    switch (j) {
        case Joint::UpperArmL: return Joint::UpperArmR;
        case Joint::ForearmL: return Joint::ForearmR;
        case Joint::HandL: return Joint::HandR;
        case Joint::ThighL: return Joint::ThighR;
        case Joint::ShinL: return Joint::ShinR;
        case Joint::FootL: return Joint::FootR;
        default: return j;
    }
}

void SkinnedMeshData::computeBounds() {
    if (vertices.empty()) {
        bounds = Aabb{};
        return;
    }
    bounds.min = bounds.max = vertices[0].position;
    for (const SkinVertex& v : vertices) bounds.extend(v.position);
}

const HumanoidVariant& templateVariant() {
    static const HumanoidVariant reference{};
    return reference;
}

void templateBindPositions(Vec3 out[kJointCount]) {
    const Landmarks& l = landmarks();
    for (size_t j = 0; j < kJointCount; ++j) out[j] = l.bind[j];
}

void buildTemplateBody(const BodyBuildDesc& desc, SkinnedMeshData& out) {
    out.clear();
    const BodyLod lod = desc.lod;
    const auto wanted = [&](BodyRegion r) { return (desc.regions & regionBit(r)) != 0; };
    const auto emit = [&](Rings rings, int seg0, BodyRegion region, bool capStart, bool capEnd) {
        dropDetail(rings, lod);
        emitShell(rings, segmentsFor(lod, seg0), region, capStart, capEnd, out);
    };

    if (wanted(BodyRegion::Torso)) emit(trunkRings(0.0f), 16, BodyRegion::Torso, true, true);
    if (wanted(BodyRegion::Neck)) emit(neckRings(0.0f), 10, BodyRegion::Neck, true, true);
    if (wanted(BodyRegion::Scalp)) {
        const uint32_t first = static_cast<uint32_t>(out.vertices.size());
        emit(headRings(0.0f, false), 14, BodyRegion::Scalp, true, true);
        if (lod != BodyLod::Lod2) sculptFace(out, first);
    }
    for (int side = 0; side < 2; ++side) {
        const BodyRegion arm = side == 0 ? BodyRegion::ArmL : BodyRegion::ArmR;
        const BodyRegion hand = side == 0 ? BodyRegion::HandL : BodyRegion::HandR;
        const BodyRegion leg = side == 0 ? BodyRegion::LegL : BodyRegion::LegR;
        const BodyRegion foot = side == 0 ? BodyRegion::FootL : BodyRegion::FootR;
        if (wanted(arm)) emit(armRings(side, 0.0f, 1e9f), 10, arm, true, true);
        if (wanted(hand)) {
            emit(handRings(side, 0.0f), 8, hand, true, true);
            if (lod == BodyLod::Lod0) emit(thumbRings(side), 6, hand, true, true);
        }
        if (wanted(leg)) emit(legRings(side, 0.0f, -1e9f), 10, leg, true, true);
        if (wanted(foot)) emit(footRings(side, 0.0f), 8, foot, true, true);
    }

    computeNormals(out);
    out.computeBounds();
}

void buildTemplateBodyLods(uint32_t regions, std::vector<SkinnedMeshData>& out) {
    out.clear();
    out.resize(kBodyLodCount);
    for (size_t i = 0; i < kBodyLodCount; ++i) {
        BodyBuildDesc desc;
        desc.lod = static_cast<BodyLod>(i);
        desc.regions = regions;
        buildTemplateBody(desc, out[i]);
    }
}

// ------------------------------------------------------- variant palette ---

void buildSkinPalette(const HumanoidVariant& variant, const Pose& pose,
                      Mat4 outPalette[kJointCount]) {
    const Landmarks& l = landmarks();
    const Skeleton& tmpl = l.skeleton;
    const Skeleton skeleton = buildSkeleton(variant);

    Mat4 world[kJointCount];
    evaluatePose(skeleton, pose, world);

    const float heightScale = variant.height / templateVariant().height;
    const float thickness = variant.bulk * heightScale;
    const float shoulderScale = variant.shoulderWidth / templateVariant().shoulderWidth;
    const float hipScale = variant.hipWidth / templateVariant().hipWidth;
    const auto ratio = [](float a, float b) { return b > 1e-6f ? a / b : 1.0f; };

    for (size_t j = 0; j < kJointCount; ++j) {
        // Along-bone stretch (the bone this joint drives) and cross-section
        // scale (how thick the body is around it). Applied in joint-local
        // space, so it never leaks into child bones.
        Vec3 s{thickness, heightScale, thickness};
        switch (static_cast<Joint>(j)) {
            case Joint::Hips:
                s = {hipScale, heightScale, thickness};
                break;
            case Joint::Spine:
                s = {(hipScale + shoulderScale) * 0.5f, ratio(skeleton.torsoLength,
                                                              tmpl.torsoLength),
                     thickness};
                break;
            case Joint::Chest:
                s = {shoulderScale, ratio(skeleton.torsoLength, tmpl.torsoLength), thickness};
                break;
            case Joint::Neck:
                s = {thickness, ratio(skeleton.torsoLength, tmpl.torsoLength), thickness};
                break;
            case Joint::Head: {
                const float h = ratio(skeleton.headSize, tmpl.headSize);
                s = {h, h, h};
                break;
            }
            case Joint::UpperArmL:
            case Joint::UpperArmR:
                s = {thickness, ratio(skeleton.upperArmLength, tmpl.upperArmLength), thickness};
                break;
            case Joint::ForearmL:
            case Joint::ForearmR:
                s = {thickness, ratio(skeleton.forearmLength, tmpl.forearmLength), thickness};
                break;
            case Joint::HandL:
            case Joint::HandR:
                s = {thickness, heightScale, thickness};
                break;
            case Joint::ThighL:
            case Joint::ThighR:
                s = {thickness, ratio(skeleton.thighLength, tmpl.thighLength), thickness};
                break;
            case Joint::ShinL:
            case Joint::ShinR:
                s = {thickness, ratio(skeleton.shinLength, tmpl.shinLength), thickness};
                break;
            case Joint::FootL:
            case Joint::FootR:
                s = {thickness, heightScale, heightScale};
                break;
            default:
                break;
        }
        outPalette[j] = world[j] * Mat4::scale(s) * Mat4::translation(-l.bind[j]);
    }
}

namespace {

// Skins every vertex but emits only the parts whose region survives masking:
// one shared body mesh serves the dressed and the undressed character, so
// clothing costs no extra geometry (CHARACTERS.md §5.4).
void skinMeshMasked(const SkinnedMeshData& mesh, const Mat4 palette[kJointCount],
                    uint32_t regions, MeshData& out);

}  // namespace

void skinMesh(const SkinnedMeshData& mesh, const Mat4 palette[kJointCount], MeshData& out) {
    out.vertices.resize(mesh.vertices.size());
    out.indices = mesh.indices;
    for (size_t i = 0; i < mesh.vertices.size(); ++i) {
        const SkinVertex& sv = mesh.vertices[i];
        Vec3 position{0, 0, 0};
        Vec3 normal{0, 0, 0};
        for (int k = 0; k < 4; ++k) {
            const float w = static_cast<float>(sv.weights[k]) * (1.0f / 255.0f);
            if (w <= 0.0f) continue;
            const Mat4& m = palette[sv.joints[k]];
            position += m.transformPoint(sv.position) * w;
            // Rotation+scale part only; scales here are mild and near-uniform
            // per axis, so the linear part is an adequate normal transform.
            const Vec3 n{m.m[0] * sv.normal.x + m.m[4] * sv.normal.y + m.m[8] * sv.normal.z,
                         m.m[1] * sv.normal.x + m.m[5] * sv.normal.y + m.m[9] * sv.normal.z,
                         m.m[2] * sv.normal.x + m.m[6] * sv.normal.y + m.m[10] * sv.normal.z};
            normal += n * w;
        }
        out.vertices[i].position = position;
        out.vertices[i].normal = normal.lengthSq() > 0.0f ? normal.normalized() : Vec3{0, 1, 0};
    }
    out.computeBounds();
}

namespace {

void skinMeshMasked(const SkinnedMeshData& mesh, const Mat4 palette[kJointCount], uint32_t regions,
                    MeshData& out) {
    skinMesh(mesh, palette, out);
    if (regions == kAllRegions) return;
    out.indices.clear();
    for (const MeshPart& part : mesh.parts) {
        if ((regions & regionBit(part.region)) == 0) continue;
        out.indices.insert(out.indices.end(), mesh.indices.begin() + part.firstIndex,
                           mesh.indices.begin() + part.firstIndex + part.indexCount);
    }
}

}  // namespace

// ------------------------------------------------------------- garments ----

uint32_t garmentCoverage(WearableKind kind) {
    switch (kind) {
        case WearableKind::Tunic:
        case WearableKind::Armor: return regionBit(BodyRegion::Torso);
        case WearableKind::Pants: return kRegionsLegs;
        case WearableKind::Boots: return kRegionsFeet;
        case WearableKind::HairShort:
        case WearableKind::HairLong:
        case WearableKind::Sword: return 0;
    }
    return 0;
}

void buildGarmentMesh(const GarmentBuildDesc& desc, SkinnedMeshData& out) {
    out.clear();
    const Landmarks& l = landmarks();
    const BodyLod lod = desc.lod;
    // Each layer sits outside the one beneath it: the offset is the sum of
    // the thicknesses under it plus this garment's own (CHARACTERS.md §5.4).
    const float thickness = 0.008f + 0.011f * static_cast<float>(desc.layer);
    const auto emit = [&](Rings rings, int seg0, BodyRegion region, bool capStart, bool capEnd) {
        dropDetail(rings, lod);
        emitShell(rings, segmentsFor(lod, seg0), region, capStart, capEnd, out);
    };
    const auto above = [](Rings rings, float y) {
        Rings kept;
        for (const RingDef& r : rings) {
            if (r.center.y >= y) kept.push_back(r);
        }
        return kept;
    };

    switch (desc.kind) {
        case WearableKind::Tunic:
        case WearableKind::Armor: {
            const bool plate = desc.kind == WearableKind::Armor;
            Rings trunk = above(trunkRings(thickness), l.hipY - 0.12f);
            if (plate) {
                for (RingDef& r : trunk) r.square = std::fmin(r.square, 0.72f);
            }
            emit(trunk, 16, BodyRegion::Torso, true, true);
            for (int side = 0; side < 2; ++side) {
                const BodyRegion arm = side == 0 ? BodyRegion::ArmL : BodyRegion::ArmR;
                // Sleeve: short on cloth, a pauldron-length cap on plate.
                const float drop = plate ? 0.14f : 0.20f;
                Rings sleeve = armRings(side, thickness + (plate ? 0.010f : 0.0f), drop);
                emit(sleeve, 10, arm, true, true);
            }
            break;
        }
        case WearableKind::Pants: {
            for (int side = 0; side < 2; ++side) {
                const BodyRegion leg = side == 0 ? BodyRegion::LegL : BodyRegion::LegR;
                // Down to the ankle: the trouser must reach the boot, or the
                // masked-away shin leaves a gap between hem and cuff.
                emit(legRings(side, thickness, 0.02f), 10, leg, true, true);
            }
            break;
        }
        case WearableKind::Boots: {
            for (int side = 0; side < 2; ++side) {
                const BodyRegion foot = side == 0 ? BodyRegion::FootL : BodyRegion::FootR;
                const BodyRegion leg = side == 0 ? BodyRegion::LegL : BodyRegion::LegR;
                emit(footRings(side, thickness), 8, foot, true, true);
                // Cuff up the lower shin.
                Rings shaft = legRings(side, thickness + 0.006f, 0.0f);
                Rings cuff;
                for (const RingDef& r : shaft) {
                    if (r.center.y <= l.ankleY + 0.16f) cuff.push_back(r);
                }
                emit(cuff, 10, leg, true, true);
            }
            break;
        }
        case WearableKind::HairShort: {
            emit(headRings(thickness + 0.004f, true), 14, BodyRegion::Scalp, true, true);
            break;
        }
        case WearableKind::HairLong: {
            emit(headRings(thickness + 0.005f, true), 14, BodyRegion::Scalp, true, true);
            // A fall of hair down the back of the neck, bound to the head so
            // it swings with it.
            Rings fall;
            const float back = l.headHalfD * 0.55f;
            for (int i = 0; i < 5; ++i) {
                const float t = static_cast<float>(i) / 4.0f;
                RingDef r = limbRing(0, l.chinY + 0.06f - t * 0.22f, back + t * 0.012f,
                                     l.headHalfW * (0.95f - 0.15f * t),
                                     l.headHalfD * (0.42f - 0.18f * t), Joint::Head, Joint::Head,
                                     0.0f, t, i % 2 == 1 ? 1 : 0);
                r.square = 0.8f;
                fall.push_back(r);
            }
            emit(fall, 12, BodyRegion::Scalp, true, true);
            break;
        }
        case WearableKind::Sword:
            // Held items attach rigidly at grip points (CHARACTERS.md §6.1);
            // they are not fitted garments and are built elsewhere.
            break;
    }

    computeNormals(out);
    out.computeBounds();
}

// --------------------------------------------------- one-call integration --

const std::vector<SkinnedMeshData>& sharedTemplateLods() {
    static const std::vector<SkinnedMeshData> lods = [] {
        std::vector<SkinnedMeshData> built;
        buildTemplateBodyLods(kAllRegions, built);
        return built;
    }();
    return lods;
}

namespace {

// Garments are as shareable as the body: one mesh per (kind, layer, lod) for
// the whole process, posed per character by the palette.
const SkinnedMeshData& sharedGarment(WearableKind kind, uint8_t layer, BodyLod lod) {
    static constexpr size_t kKinds = 8, kLayers = 3;
    static std::vector<SkinnedMeshData> cache;
    static const bool built = [] {
        cache.resize(kKinds * kLayers * kBodyLodCount);
        for (size_t k = 0; k < kKinds; ++k) {
            for (size_t l = 0; l < kLayers; ++l) {
                for (size_t d = 0; d < kBodyLodCount; ++d) {
                    GarmentBuildDesc desc;
                    desc.kind = static_cast<WearableKind>(k);
                    desc.layer = static_cast<uint8_t>(l);
                    desc.lod = static_cast<BodyLod>(d);
                    buildGarmentMesh(desc, cache[(k * kLayers + l) * kBodyLodCount + d]);
                }
            }
        }
        return true;
    }();
    (void)built;
    const size_t k = static_cast<size_t>(kind) % kKinds;
    const size_t l = layer % kLayers;
    const size_t d = static_cast<size_t>(lod) % kBodyLodCount;
    return cache[(k * kLayers + l) * kBodyLodCount + d];
}

}  // namespace

void buildPosedCharacter(const HumanoidVariant& variant, const WearableInstance* wearables,
                         size_t wearableCount, const Pose& pose, BodyLod lod,
                         std::vector<CharacterPiece>& out) {
    out.clear();
    Mat4 palette[kJointCount];
    buildSkinPalette(variant, pose, palette);

    uint32_t regions = kAllRegions;
    for (size_t i = 0; i < wearableCount; ++i) regions &= ~garmentCoverage(wearables[i].kind);

    const std::vector<SkinnedMeshData>& lods = sharedTemplateLods();
    const SkinnedMeshData& body = lods[static_cast<size_t>(lod) % lods.size()];
    out.emplace_back();
    skinMeshMasked(body, palette, regions, out.back().mesh);
    for (int c = 0; c < 4; ++c) out.back().color[c] = variant.skin[c];

    for (size_t i = 0; i < wearableCount; ++i) {
        const SkinnedMeshData& garment =
            sharedGarment(wearables[i].kind, wearables[i].layer, lod);
        if (garment.vertices.empty()) continue;  // held items are not garments
        out.emplace_back();
        skinMesh(garment, palette, out.back().mesh);
        for (int c = 0; c < 4; ++c) out.back().color[c] = wearables[i].color[c];
    }
}

}  // namespace mge
