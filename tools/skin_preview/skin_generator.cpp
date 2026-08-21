#include "skin_generator.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace mge::skin {
namespace {

// ----------------------------------------------------------------- noise ---
// Hash-based value noise. Deterministic by construction: same seed, same
// bytes, every build (MODELING.md §3.7 applies to content of every kind).

inline uint32_t hashU32(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

inline float hash3(int x, int y, int z, uint32_t seed) {
    uint32_t h = hashU32(uint32_t(x) * 73856093u ^ uint32_t(y) * 19349663u ^
                         uint32_t(z) * 83492791u ^ seed);
    return float(h & 0xFFFFFFu) * (1.0f / 16777215.0f);
}

inline float smoothstep5(float t) { return t * t * t * (t * (t * 6 - 15) + 10); }

// Value noise over 3D position, so it is continuous ACROSS UV SEAMS — the
// island boundary is invisible because the noise never knew about it. This is
// the whole reason the generator works in metres on the body.
float valueNoise(Vec3 p, uint32_t seed) {
    const float fx = std::floor(p.x), fy = std::floor(p.y), fz = std::floor(p.z);
    const int ix = int(fx), iy = int(fy), iz = int(fz);
    const float tx = smoothstep5(p.x - fx), ty = smoothstep5(p.y - fy), tz = smoothstep5(p.z - fz);
    float c[8];
    for (int i = 0; i < 8; ++i) {
        c[i] = hash3(ix + (i & 1), iy + ((i >> 1) & 1), iz + ((i >> 2) & 1), seed);
    }
    const float x00 = c[0] + (c[1] - c[0]) * tx, x10 = c[2] + (c[3] - c[2]) * tx;
    const float x01 = c[4] + (c[5] - c[4]) * tx, x11 = c[6] + (c[7] - c[6]) * tx;
    const float y0 = x00 + (x10 - x00) * ty, y1 = x01 + (x11 - x01) * ty;
    return y0 + (y1 - y0) * tz;
}

float fbm(Vec3 p, int octaves, uint32_t seed) {
    float sum = 0, amp = 0.5f, norm = 0;
    for (int o = 0; o < octaves; ++o) {
        sum += valueNoise(p, seed + uint32_t(o) * 977u) * amp;
        norm += amp;
        p = p * 2.03f;
        amp *= 0.5f;
    }
    return norm > 0 ? sum / norm : 0.0f;
}

// Cellular (Worley) distance — pores and the fine mottling that keeps a
// surface from reading as a flat wash.
float cellular(Vec3 p, uint32_t seed) {
    const int ix = int(std::floor(p.x)), iy = int(std::floor(p.y)), iz = int(std::floor(p.z));
    float best = 1e9f;
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                const int cx = ix + dx, cy = iy + dy, cz = iz + dz;
                const Vec3 f{float(cx) + hash3(cx, cy, cz, seed),
                             float(cy) + hash3(cx, cy, cz, seed + 7919u),
                             float(cz) + hash3(cx, cy, cz, seed + 15667u)};
                const Vec3 d = p - f;
                const float sq = d.x * d.x + d.y * d.y + d.z * d.z;
                best = std::min(best, sq);
            }
        }
    }
    return std::sqrt(best);
}

inline float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
inline float smoothBand(float v, float a, float b) {  // 1 inside a, 0 outside b
    if (a > b) std::swap(a, b);
    const float t = clamp01((v - a) / (b - a + 1e-6f));
    return 1.0f - smoothstep5(t);
}

// ---------------------------------------------------------------- colour ---
// CIELAB -> linear sRGB, D65. Skin is specified in Lab because that is the
// space its published science lives in: the "skin colour banana" (the locus
// human skin occupies) and the ITA classifier are both Lab constructs, so
// parameters expressed here are checkable against literature instead of being
// RGB triples somebody liked.

void labToLinearSrgb(double L, double a, double b, float& r, float& g, float& bl) {
    const double fy = (L + 16.0) / 116.0;
    const double fx = fy + a / 500.0;
    const double fz = fy - b / 200.0;
    auto finv = [](double t) {
        return t > 6.0 / 29.0 ? t * t * t : 3.0 * (6.0 / 29.0) * (6.0 / 29.0) * (t - 4.0 / 29.0);
    };
    // D65 white point
    const double X = 0.95047 * finv(fx), Y = 1.00000 * finv(fy), Z = 1.08883 * finv(fz);
    double rr = 3.2404542 * X - 1.5371385 * Y - 0.4985314 * Z;
    double gg = -0.9692660 * X + 1.8760108 * Y + 0.0415560 * Z;
    double bb = 0.0556434 * X - 0.2040259 * Y + 1.0572252 * Z;
    r = float(std::max(0.0, std::min(1.0, rr)));
    g = float(std::max(0.0, std::min(1.0, gg)));
    bl = float(std::max(0.0, std::min(1.0, bb)));
}

inline uint8_t encodeSrgb(float linear) {
    const float v = linear <= 0.0031308f ? linear * 12.92f
                                         : 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
    return uint8_t(clamp01(v) * 255.0f + 0.5f);
}

// Melanin and haemoglobin -> Lab.
//
// The melanin axis follows the measured shape of the human skin locus: L*
// falls monotonically with pigmentation while b* (yellowness) RISES to a
// maximum around intermediate tones and falls again at the darkest — the
// curvature that gives the locus its "banana" name. A straight line between a
// light and a dark RGB misses this, and the mid-tones it produces are the grey,
// lifeless ones that read as plastic.
void chromophoresToLab(float melanin, float haemoglobin, double& L, double& a, double& b) {
    const double m = std::max(0.0f, std::min(1.0f, melanin));
    const double h = std::max(0.0f, std::min(1.0f, haemoglobin));
    L = 75.0 - 44.0 * std::pow(m, 1.05);           // ITA +57 (type I) .. -37 (type VI)
    b = 13.0 + 22.0 * m - 17.0 * m * m;            // the locus curvature
    a = 6.0 + 13.0 * h + 5.0 * m * (1.0 - m);      // dermal blood, strongest at mid tones
}

}  // namespace

double itaDegrees(double L, double b) {
    return std::atan2(L - 50.0, b) * 180.0 / 3.14159265358979323846;
}

const char* fitzpatrickBand(double ita) {
    if (ita > 55.0) return "I very light";
    if (ita > 41.0) return "II light";
    if (ita > 28.0) return "III intermediate light";
    if (ita > 10.0) return "IV intermediate";
    if (ita > -30.0) return "V dark";
    return "VI very dark";
}

void baseLab(const SkinParams& p, double& L, double& a, double& b) {
    chromophoresToLab(p.melanin, p.haemoglobin, L, a, b);
}

// ------------------------------------------------------------- landmarks ---

HeadLandmarks measureHead(const SkinnedMeshData& mesh) {
    HeadLandmarks h;
    float chin = 1e9f, crown = -1e9f, faceZ = 1e9f, maxAbsX = 0;
    for (const MeshPart& part : mesh.parts) {
        const bool head = part.region == BodyRegion::Face || part.region == BodyRegion::Scalp;
        if (!head) continue;
        for (uint32_t i = 0; i < part.indexCount; ++i) {
            const SkinVertex& v = mesh.vertices[mesh.indices[part.firstIndex + i]];
            chin = std::min(chin, v.position.y);
            crown = std::max(crown, v.position.y);
            faceZ = std::min(faceZ, v.position.z);
            maxAbsX = std::max(maxAbsX, std::fabs(v.position.x));
        }
    }
    h.chinY = chin;
    h.crownY = crown;
    h.faceZ = faceZ;
    h.halfWidth = maxAbsX;

    // Canonical facial proportions, measured against THIS head's extent rather
    // than assumed in absolute metres. These fractions are the standard
    // artistic canon (crown to chin): brow ~0.42, eye line ~0.50, nose base
    // ~0.75, mouth ~0.85; five eye-widths across the face, so an eye is a
    // fifth of the head's width and its centre sits one eye-width off the
    // midline. Using the canon rather than my own judgment is the point.
    const float H = h.headHeight();
    h.browY = crown - H * 0.42f;
    h.eyeY = crown - H * 0.50f;
    h.noseBaseY = crown - H * 0.75f;
    h.mouthY = crown - H * 0.85f;
    h.eyeHalfWidth = h.halfWidth * 0.20f;
    h.eyeOffsetX = h.halfWidth * 0.40f;
    return h;
}

// ------------------------------------------------------- ambient occlusion --

namespace {

bool rayHits(const SkinnedMeshData& mesh, const Vec3& origin, const Vec3& dir, float maxT) {
    // Brute force over the body's triangles. The body is 2 200 triangles and
    // this runs once at bake time, so an acceleration structure would be
    // complexity nobody needs yet.
    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        const Vec3& a = mesh.vertices[mesh.indices[i]].position;
        const Vec3& b = mesh.vertices[mesh.indices[i + 1]].position;
        const Vec3& c = mesh.vertices[mesh.indices[i + 2]].position;
        const Vec3 e1 = b - a, e2 = c - a;
        const Vec3 pv{dir.y * e2.z - dir.z * e2.y, dir.z * e2.x - dir.x * e2.z,
                      dir.x * e2.y - dir.y * e2.x};
        const float det = e1.x * pv.x + e1.y * pv.y + e1.z * pv.z;
        if (det > -1e-8f && det < 1e-8f) continue;
        const float inv = 1.0f / det;
        const Vec3 tv = origin - a;
        const float u = (tv.x * pv.x + tv.y * pv.y + tv.z * pv.z) * inv;
        if (u < 0 || u > 1) continue;
        const Vec3 qv{tv.y * e1.z - tv.z * e1.y, tv.z * e1.x - tv.x * e1.z,
                      tv.x * e1.y - tv.y * e1.x};
        const float v = (dir.x * qv.x + dir.y * qv.y + dir.z * qv.z) * inv;
        if (v < 0 || u + v > 1) continue;
        const float t = (e2.x * qv.x + e2.y * qv.y + e2.z * qv.z) * inv;
        if (t > 1e-4f && t < maxT) return true;
    }
    return false;
}

}  // namespace

void computeAmbient(const SkinnedMeshData& mesh, std::vector<float>& out) {
    out.assign(mesh.vertices.size(), 1.0f);
    // Short rays: this is contact shading — the crease beside a nose, the
    // hollow under a chin, the gap between fingers — not a global sky bake.
    // 0.06 m is about the depth at which skin-to-skin occlusion stops reading.
    const float reach = 0.06f;
    const int kRays = 24;
    for (size_t i = 0; i < mesh.vertices.size(); ++i) {
        const Vec3 p = mesh.vertices[i].position;
        const Vec3 n = mesh.vertices[i].normal;
        // An orthonormal basis around the normal.
        Vec3 t = std::fabs(n.y) < 0.9f ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
        Vec3 bx{n.y * t.z - n.z * t.y, n.z * t.x - n.x * t.z, n.x * t.y - n.y * t.x};
        bx = bx.normalized();
        const Vec3 by{n.y * bx.z - n.z * bx.y, n.z * bx.x - n.x * bx.z, n.x * bx.y - n.y * bx.x};
        int open = 0;
        for (int r = 0; r < kRays; ++r) {
            // Cosine-weighted hemisphere, deterministic stratified sequence.
            const float u1 = (float(r) + 0.5f) / float(kRays);
            const float u2 = hash3(int(i), r, 0, 12345u);
            const float rad = std::sqrt(u1);
            const float phi = 6.2831853f * u2;
            const float z = std::sqrt(std::max(0.0f, 1.0f - u1));
            const Vec3 dir = (bx * (rad * std::cos(phi)) + by * (rad * std::sin(phi)) + n * z)
                                 .normalized();
            if (!rayHits(mesh, p + n * 1e-4f, dir, reach)) open++;
        }
        out[i] = float(open) / float(kRays);
    }
}

// ----------------------------------------------------------- surface map ---

void buildSurfaceMap(const SkinnedMeshData& mesh, uint32_t sheet,
                     std::vector<SurfaceTexel>& out) {
    out.assign(size_t(sheet) * sheet, SurfaceTexel{});
    std::vector<float> ao;
    computeAmbient(mesh, ao);

    for (const MeshPart& part : mesh.parts) {
        for (uint32_t i = 0; i + 2 < part.indexCount; i += 3) {
            const uint32_t ia = mesh.indices[part.firstIndex + i];
            const uint32_t ib = mesh.indices[part.firstIndex + i + 1];
            const uint32_t ic = mesh.indices[part.firstIndex + i + 2];
            const SkinVertex& va = mesh.vertices[ia];
            const SkinVertex& vb = mesh.vertices[ib];
            const SkinVertex& vc = mesh.vertices[ic];

            const float ax = va.uv[0] / 65535.0f * sheet, ay = va.uv[1] / 65535.0f * sheet;
            const float bx = vb.uv[0] / 65535.0f * sheet, by = vb.uv[1] / 65535.0f * sheet;
            const float cx = vc.uv[0] / 65535.0f * sheet, cy = vc.uv[1] / 65535.0f * sheet;

            // Conservative-ish rasterization: one texel of margin, so a texel
            // whose centre falls just outside a thin triangle still gets a
            // surface rather than a hole the dilation has to guess at.
            int x0 = int(std::floor(std::min({ax, bx, cx}))) - 1;
            int x1 = int(std::ceil(std::max({ax, bx, cx}))) + 1;
            int y0 = int(std::floor(std::min({ay, by, cy}))) - 1;
            int y1 = int(std::ceil(std::max({ay, by, cy}))) + 1;
            x0 = std::max(0, x0);
            y0 = std::max(0, y0);
            x1 = std::min(int(sheet) - 1, x1);
            y1 = std::min(int(sheet) - 1, y1);

            const float det = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy);
            if (std::fabs(det) < 1e-9f) continue;

            for (int y = y0; y <= y1; ++y) {
                for (int x = x0; x <= x1; ++x) {
                    const float px = float(x) + 0.5f, py = float(y) + 0.5f;
                    float l0 = ((by - cy) * (px - cx) + (cx - bx) * (py - cy)) / det;
                    float l1 = ((cy - ay) * (px - cx) + (ax - cx) * (py - cy)) / det;
                    float l2 = 1.0f - l0 - l1;
                    const float slack = -0.06f;  // the one-texel margin, in barycentric terms
                    if (l0 < slack || l1 < slack || l2 < slack) continue;
                    l0 = clamp01(l0);
                    l1 = clamp01(l1);
                    l2 = clamp01(l2);
                    const float norm = l0 + l1 + l2;
                    l0 /= norm;
                    l1 /= norm;
                    l2 /= norm;

                    SurfaceTexel& texel = out[size_t(y) * sheet + x];
                    if (texel.valid()) continue;  // first triangle wins; islands are disjoint
                    texel.position = va.position * l0 + vb.position * l1 + vc.position * l2;
                    texel.normal = (va.normal * l0 + vb.normal * l1 + vc.normal * l2).normalized();
                    texel.ambient = ao[ia] * l0 + ao[ib] * l1 + ao[ic] * l2;
                    texel.region = uint8_t(part.region);
                }
            }
        }
    }
}

// ---------------------------------------------------------------- the bake -

namespace {

struct Rgb {
    float r = 0, g = 0, b = 0;
};

// How much body one texel covers, measured from the mesh rather than assumed:
// the chart's own area ratio at this sheet size. Every feature edge is
// filtered to this, so the generator adapts to a re-packed chart or a
// different sheet without a constant changing.
float surfaceTexelMetres(const SkinnedMeshData& mesh, uint32_t sheet) {
    double world = 0, chart = 0;
    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        const SkinVertex& a = mesh.vertices[mesh.indices[i]];
        const SkinVertex& b = mesh.vertices[mesh.indices[i + 1]];
        const SkinVertex& c = mesh.vertices[mesh.indices[i + 2]];
        const Vec3 e1 = b.position - a.position, e2 = c.position - a.position;
        const Vec3 cr{e1.y * e2.z - e1.z * e2.y, e1.z * e2.x - e1.x * e2.z,
                      e1.x * e2.y - e1.y * e2.x};
        world += 0.5 * std::sqrt(double(cr.x) * cr.x + double(cr.y) * cr.y + double(cr.z) * cr.z);
        const double du1 = (b.uv[0] - a.uv[0]) / 65535.0 * sheet;
        const double dv1 = (b.uv[1] - a.uv[1]) / 65535.0 * sheet;
        const double du2 = (c.uv[0] - a.uv[0]) / 65535.0 * sheet;
        const double dv2 = (c.uv[1] - a.uv[1]) / 65535.0 * sheet;
        chart += 0.5 * std::fabs(du1 * dv2 - du2 * dv1);
    }
    if (world <= 0 || chart <= 0) return 1.0f / 512.0f;
    return float(1.0 / std::sqrt(chart / world));  // metres per texel
}

bool isHand(uint8_t region) {
    return region == uint8_t(BodyRegion::HandL) || region == uint8_t(BodyRegion::HandR);
}
bool isFoot(uint8_t region) {
    return region == uint8_t(BodyRegion::FootL) || region == uint8_t(BodyRegion::FootR);
}
bool isFace(uint8_t region) { return region == uint8_t(BodyRegion::Face); }

// How much sun a surface sees, from what it is and which way it faces. Real
// weathering is not uniform: a forearm tans, a belly does not.
float sunExposure(uint8_t region, const Vec3& normal) {
    float base = 0.15f;  // covered by clothing
    if (isFace(region) || region == uint8_t(BodyRegion::Neck) ||
        region == uint8_t(BodyRegion::Scalp)) {
        base = 1.0f;
    } else if (isHand(region)) {
        base = 0.85f;
    } else if (region == uint8_t(BodyRegion::ArmL) || region == uint8_t(BodyRegion::ArmR)) {
        base = 0.55f;
    }
    // Upward-facing surfaces catch more.
    return base * (0.65f + 0.35f * clamp01(normal.y * 0.5f + 0.5f));
}

// Where blood shows through. Placed by distance to landmarks measured on the
// mesh, in metres, so it follows the delivered head.
float perfusion(const Vec3& p, const Vec3& n, uint8_t region, const HeadLandmarks& h) {
    float v = 0.0f;
    if (isFace(region)) {
        // Cheeks: either side of the midline, between nose base and eye line.
        const float cheekY = (h.eyeY + h.noseBaseY) * 0.5f;
        const float dy = (p.y - cheekY) / (h.headHeight() * 0.16f);
        const float dx = (std::fabs(p.x) - h.halfWidth * 0.55f) / (h.halfWidth * 0.35f);
        v += 0.85f * std::exp(-(dx * dx + dy * dy));
        // Nose: the frontmost strip on the midline.
        const float nx = p.x / (h.halfWidth * 0.18f);
        const float ny = (p.y - (h.noseBaseY + h.headHeight() * 0.06f)) / (h.headHeight() * 0.10f);
        v += 0.55f * std::exp(-(nx * nx + ny * ny));
        // Chin and the brow ridge carry a little.
        const float cy2 = (p.y - h.chinY) / (h.headHeight() * 0.10f);
        v += 0.25f * std::exp(-cy2 * cy2);
    }
    // Knuckles and the backs of hands: thin skin over bone, facing outward.
    if (isHand(region)) v += 0.45f * clamp01(n.y * 0.5f + 0.5f);
    // Elbows and knees redden; both are simply "outward-facing limb", which
    // the normal already says.
    if (region == uint8_t(BodyRegion::ArmL) || region == uint8_t(BodyRegion::ArmR) ||
        region == uint8_t(BodyRegion::LegL) || region == uint8_t(BodyRegion::LegR)) {
        v += 0.12f;
    }
    return v;
}

// Palms and soles are genuinely different skin: far less melanin, more blood,
// no hair, coarser texture. Detected by which way the surface faces.
float palmar(const Vec3& n, uint8_t region) {
    if (isHand(region)) return clamp01(-n.y * 0.5f + 0.5f) > 0.72f ? 1.0f : 0.0f;
    if (isFoot(region)) return clamp01(-n.y) > 0.55f ? 1.0f : 0.0f;
    return 0.0f;
}

// The features the geometry deliberately does not carry. MODELING.md §1 omits
// eyes and mouth on purpose — "better to omit a feature than to suggest it
// badly" — and hands the job to the texture. Placement comes from the
// canonical proportions in `measureHead`, and the shapes are ellipses about
// those landmarks: this is the part of the generator most likely to need the
// owner's correction, and it is deliberately the simplest thing that can read.
struct FaceFeatures {
    float brow = 0;   // 1 inside a brow
    float lash = 0;   // upper lash line
    float sclera = 0; // white of the eye
    float iris = 0;
    float pupil = 0;
    float lipUpper = 0, lipLower = 0;
    float nostril = 0;
};

// A soft ellipse: 1 inside, 0 outside, with the transition spread over `aa`
// metres. Hard thresholds alias badly at the chart's density — an eye is only
// a few texels across — so every feature edge is filtered here rather than
// left to the mip chain to average after the damage is done.
float ellipse(float dx, float dy, float rx, float ry, float aa) {
    const float r = std::sqrt((dx / rx) * (dx / rx) + (dy / ry) * (dy / ry));
    const float edge = aa / std::min(rx, ry);
    return 1.0f - smoothstep5(clamp01((r - (1.0f - edge)) / (edge * 2.0f + 1e-6f)));
}

// Feature placement in MILLIMETRES from published adult facial anthropometry,
// scaled by this head's measured height so it follows the delivered body:
//
//   interpupillary distance      63 mm
//   palpebral fissure (opening)  30 mm wide, 10 mm high
//   iris diameter                11.7 mm
//   pupil diameter                4 mm
//   alar (nose) width            34 mm
//   mouth width                  50 mm
//   eyebrow length               45 mm, ~8 mm deep
//
// Using measured anatomy rather than fractions that look right is the whole
// discipline here: these are numbers a session may legitimately choose,
// because they are sourced rather than judged (AGENTS.md §6.2).
FaceFeatures faceFeatures(const Vec3& p, const Vec3& n, const HeadLandmarks& h, float texelMetres) {
    FaceFeatures f;
    // The canonical head this anthropometry describes is ~0.2375 m crown to
    // chin, which is what the delivered body measures; the ratio keeps the
    // features correct if the body is ever re-imported at another size.
    const float s = h.headHeight() / 0.2375f;
    const float mm = 0.001f * s;
    const float aa = std::max(texelMetres, 0.0004f);

    const float ax = std::fabs(p.x);
    const float eyeCentreX = 31.5f * mm;  // half the interpupillary distance

    // The eye opening, and the lids that cut it. Visible sclera is a lens, not
    // a full ellipse — a fully round white eye is the classic tell.
    const float dxEye = ax - eyeCentreX;
    const float dyEye = p.y - h.eyeY;
    const float opening = ellipse(dxEye, dyEye, 15.0f * mm, 5.0f * mm, aa);
    f.sclera = opening;
    f.iris = ellipse(dxEye, dyEye, 5.85f * mm, 5.85f * mm, aa) * opening;
    f.pupil = ellipse(dxEye, dyEye, 2.0f * mm, 2.0f * mm, aa) * opening;
    // Upper lash line: the top rim of the opening, a millimetre thick.
    const float outer = ellipse(dxEye, dyEye, 16.2f * mm, 6.2f * mm, aa);
    f.lash = clamp01(outer - opening) * (dyEye > -1.0f * mm ? 1.0f : 0.35f);

    // Brow, sitting above the opening.
    f.brow = ellipse(ax - eyeCentreX, p.y - (h.eyeY + 17.0f * mm), 22.5f * mm, 4.0f * mm, aa);

    // Mouth: upper vermilion 8 mm, lower 10 mm — the lower lip is fuller.
    const float dxM = p.x;
    const float dyM = p.y - h.mouthY;
    if (dyM >= 0) f.lipUpper = ellipse(dxM, dyM, 25.0f * mm, 8.0f * mm, aa);
    else f.lipLower = ellipse(dxM, dyM, 25.0f * mm, 10.0f * mm, aa);

    // Nostrils are on the UNDERSIDE of the nose, so they are drawn only where
    // the surface actually faces downward. Placing them on the front-facing
    // surface at the same height is what turns two nostrils into a moustache.
    const float underside = clamp01((-n.y - 0.15f) / 0.45f);
    f.nostril = ellipse(ax - 9.0f * mm, p.y - h.noseBaseY, 4.5f * mm, 3.0f * mm, aa) * underside;
    return f;
}

// Box-filter a mip level in LINEAR light. The standard requires it
// (`mip_generation_space linear`) because averaging sRGB values darkens every
// level — one of the two most common texture defects in shipping games.
void downsampleLinear(const std::vector<Rgb>& src, uint32_t sw, uint32_t sh,
                      std::vector<Rgb>& dst, uint32_t dw, uint32_t dh) {
    dst.assign(size_t(dw) * dh, Rgb{});
    for (uint32_t y = 0; y < dh; ++y) {
        for (uint32_t x = 0; x < dw; ++x) {
            Rgb sum;
            int n = 0;
            for (uint32_t dy = 0; dy < 2; ++dy) {
                for (uint32_t dx = 0; dx < 2; ++dx) {
                    const uint32_t sx = std::min(sw - 1, x * 2 + dx);
                    const uint32_t sy = std::min(sh - 1, y * 2 + dy);
                    const Rgb& s = src[size_t(sy) * sw + sx];
                    sum.r += s.r;
                    sum.g += s.g;
                    sum.b += s.b;
                    n++;
                }
            }
            dst[size_t(y) * dw + x] = Rgb{sum.r / n, sum.g / n, sum.b / n};
        }
    }
}

// Push colour outward into the unused sheet so bilinear filtering at every mip
// samples skin rather than background — the standard's island padding, done
// the only way that survives minification.
void dilate(std::vector<Rgb>& colour, std::vector<uint8_t>& filled, uint32_t sheet, int passes) {
    std::vector<Rgb> next = colour;
    std::vector<uint8_t> nextFilled = filled;
    for (int pass = 0; pass < passes; ++pass) {
        for (uint32_t y = 0; y < sheet; ++y) {
            for (uint32_t x = 0; x < sheet; ++x) {
                const size_t i = size_t(y) * sheet + x;
                if (filled[i]) continue;
                Rgb sum;
                int n = 0;
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int sx = int(x) + dx, sy = int(y) + dy;
                        if (sx < 0 || sy < 0 || sx >= int(sheet) || sy >= int(sheet)) continue;
                        const size_t j = size_t(sy) * sheet + sx;
                        if (!filled[j]) continue;
                        sum.r += colour[j].r;
                        sum.g += colour[j].g;
                        sum.b += colour[j].b;
                        n++;
                    }
                }
                if (n > 0) {
                    next[i] = Rgb{sum.r / n, sum.g / n, sum.b / n};
                    nextFilled[i] = 1;
                }
            }
        }
        colour = next;
        filled = nextFilled;
    }
}

void packMips(const std::vector<Rgb>& base, uint32_t sheet, ColorSpace space, TextureUsage usage,
              TextureData& out) {
    out.width = sheet;
    out.height = sheet;
    out.format = TextureFormat::Rgba8;
    out.colorSpace = space;
    out.usage = usage;
    out.mips.clear();
    out.pixels.clear();

    std::vector<Rgb> level = base;
    uint32_t w = sheet, h = sheet;
    const uint32_t levels = fullMipCount(sheet, sheet);
    for (uint32_t m = 0; m < levels; ++m) {
        TextureMip mip;
        mip.width = w;
        mip.height = h;
        mip.offset = out.pixels.size();
        mip.size = size_t(w) * h * 4;
        out.pixels.resize(out.pixels.size() + mip.size);
        uint8_t* dst = out.pixels.data() + mip.offset;
        for (size_t i = 0; i < size_t(w) * h; ++i) {
            const Rgb& c = level[i];
            if (space == ColorSpace::Srgb) {
                dst[i * 4 + 0] = encodeSrgb(c.r);
                dst[i * 4 + 1] = encodeSrgb(c.g);
                dst[i * 4 + 2] = encodeSrgb(c.b);
            } else {
                dst[i * 4 + 0] = uint8_t(clamp01(c.r) * 255.0f + 0.5f);
                dst[i * 4 + 1] = uint8_t(clamp01(c.g) * 255.0f + 0.5f);
                dst[i * 4 + 2] = uint8_t(clamp01(c.b) * 255.0f + 0.5f);
            }
            dst[i * 4 + 3] = 255;
        }
        out.mips.push_back(mip);
        if (w == 1 && h == 1) break;
        const uint32_t nw = std::max(1u, w / 2), nh = std::max(1u, h / 2);
        std::vector<Rgb> smaller;
        downsampleLinear(level, w, h, smaller, nw, nh);
        level.swap(smaller);
        w = nw;
        h = nh;
    }
}

}  // namespace

bool packImportedAlbedo(const std::vector<uint8_t>& rgb, const std::vector<uint8_t>& filled,
                        uint32_t sheet, TextureData& out) {
    const size_t count = size_t(sheet) * sheet;
    if (rgb.size() < count * 3 || filled.size() < count) return false;

    // The source bytes are sRGB-encoded. Decode to linear so the dilation and
    // the mip chain both happen in light rather than in gamma — the standard's
    // `mip_generation_space linear`, which matters just as much for an imported
    // map as for one we made.
    std::vector<Rgb> linear(count);
    for (size_t i = 0; i < count; ++i) {
        for (int ch = 0; ch < 3; ++ch) {
            const float s = rgb[i * 3 + ch] / 255.0f;
            const float l = s <= 0.04045f ? s / 12.92f : std::pow((s + 0.055f) / 1.055f, 2.4f);
            (&linear[i].r)[ch] = l;
        }
    }
    std::vector<uint8_t> mask = filled;
    dilate(linear, mask, sheet, 8);
    packMips(linear, sheet, ColorSpace::Srgb, TextureUsage::Albedo, out);
    return !out.mips.empty();
}

void generate(const SkinnedMeshData& mesh, const SurfaceTexel* surface, uint32_t sheet,
              const HeadLandmarks& head, const SkinParams& params, SkinMaps& out) {
    const size_t count = size_t(sheet) * sheet;
    // How much body one texel covers, used to filter every feature edge to the
    // resolution that actually ships rather than to an ideal one.
    const float texelMetres = surfaceTexelMetres(mesh, sheet);
    std::vector<Rgb> albedo(count), packed(count);
    std::vector<uint8_t> filled(count, 0);

    for (size_t i = 0; i < count; ++i) {
        const SurfaceTexel& t = surface[i];
        if (!t.valid()) continue;
        filled[i] = 1;

        const Vec3 p = t.position;
        const Vec3 n = t.normal;

        // --- the two chromophores, modulated by where we are on the body ---
        float melanin = params.melanin;
        float haemo = params.haemoglobin;

        // Constitutive variation: nobody is one flat colour. Two octave bands,
        // both in metres on the body so they cross UV seams invisibly.
        melanin += (fbm(p * 6.0f, 4, params.seed) - 0.5f) * 0.10f;
        melanin += (fbm(p * 22.0f, 3, params.seed + 101u) - 0.5f) * 0.045f;
        haemo += (fbm(p * 9.0f, 3, params.seed + 202u) - 0.5f) * 0.22f;

        // Sun.
        melanin += params.weathering * 0.30f * sunExposure(t.region, n);

        // Blood where vessels run close to the surface.
        haemo += perfusion(p, n, t.region, head) * 0.55f;

        // Palms and soles.
        const float palm = palmar(n, t.region);
        melanin *= (1.0f - 0.45f * palm);
        haemo += 0.20f * palm;

        // Freckles: sparse, seeded, and only where sun reaches — which is
        // where they actually appear.
        float freckle = 0.0f;
        if (params.freckles > 0.001f) {
            const float cell = cellular(p * 190.0f, params.seed + 5501u);
            const float spot = smoothBand(cell, 0.06f, 0.20f);
            const float density = hash3(int(p.x * 340), int(p.y * 340), int(p.z * 340),
                                        params.seed + 909u);
            if (density < params.freckles * 0.55f) {
                freckle = spot * sunExposure(t.region, n);
                melanin += freckle * 0.16f;
            }
        }

        // --- the features the geometry does not carry ---
        FaceFeatures f;
        if (t.region == uint8_t(BodyRegion::Face)) f = faceFeatures(p, n, head, texelMetres);

        double L, a, b;
        chromophoresToLab(clamp01(melanin), clamp01(haemo), L, a, b);

        // Lips: more blood, less melanin, and darker at the seam between them.
        const float lip = clamp01(std::max(f.lipUpper, f.lipLower));
        if (lip > 0.001f) {
            double lipL, lipA, lipB;
            chromophoresToLab(clamp01(melanin * 0.75f), clamp01(haemo + 0.45f), lipL, lipA, lipB);
            const double k = f.lipUpper > f.lipLower ? 0.88 : 1.0;  // upper lip reads darker
            L = L * (1.0 - lip) + lipL * k * lip;
            a = a * (1.0 - lip) + lipA * lip;
            b = b * (1.0 - lip) + lipB * lip;
        }

        float r, g, bl;
        labToLinearSrgb(L, a, b, r, g, bl);

        // Pores: a fine cellular break in luminance. Small on the cheek, larger
        // through the T-zone, which is how skin actually varies.
        const float poreScale = isFace(t.region) ? 620.0f : 420.0f;
        const float pore = cellular(p * poreScale, params.seed + 313u);
        const float poreAmount = isFace(t.region) ? 0.045f : 0.030f;
        const float poreTerm = 1.0f - poreAmount * clamp01(1.0f - pore * 1.6f);
        r *= poreTerm;
        g *= poreTerm;
        bl *= poreTerm;

        // Everything below blends by COVERAGE rather than switching on a
        // threshold, so a feature edge lands between texels instead of on one.
        auto mix = [](float& c, float target, float k) { c = c * (1.0f - k) + target * k; };

        // Brows and lashes sit on top as pigment, not as geometry.
        const float hairMask = clamp01(std::max(f.brow, f.lash));
        if (hairMask > 0.001f) {
            const float darkness = 0.16f + 0.10f * (1.0f - params.melanin);
            mix(r, r * darkness, hairMask);
            mix(g, g * darkness * 0.95f, hairMask);
            mix(bl, bl * darkness * 0.9f, hairMask);
        }
        if (f.nostril > 0.001f) {
            mix(r, r * 0.30f, f.nostril);
            mix(g, g * 0.28f, f.nostril);
            mix(bl, bl * 0.26f, f.nostril);
        }
        // The eye: sclera is never pure white — it is a pinkish grey in real
        // light, and painting it white is the classic tell.
        if (f.sclera > 0.001f) {
            mix(r, 0.62f, f.sclera);
            mix(g, 0.58f, f.sclera);
            mix(bl, 0.55f, f.sclera);
        }
        if (f.iris > 0.001f) {
            mix(r, 0.11f, f.iris);
            mix(g, 0.14f, f.iris);
            mix(bl, 0.12f, f.iris);
        }
        if (f.pupil > 0.001f) {
            mix(r, 0.015f, f.pupil);
            mix(g, 0.015f, f.pupil);
            mix(bl, 0.015f, f.pupil);
        }

        albedo[i] = Rgb{r, g, bl};

        // --- packed: R = AO, G = roughness, B = mask ---
        // Skin is not uniformly rough: lips and the T-zone are glossier than a
        // cheek, and that variation is most of what stops a face reading as
        // moulded plastic under a single light.
        float roughness = 0.58f;
        if (isFace(t.region)) roughness = 0.52f;
        roughness = roughness * (1.0f - lip) + 0.34f * lip;
        roughness = roughness * (1.0f - f.sclera) + 0.22f * f.sclera;
        roughness += (fbm(p * 30.0f, 3, params.seed + 77u) - 0.5f) * 0.10f;
        roughness *= 1.0f - 0.10f * palm;
        packed[i] = Rgb{clamp01(t.ambient), clamp01(roughness), 0.0f};
    }

    // Padding: the standard asks for 2 texels at the smallest streamed mip,
    // which means considerably more at mip 0.
    std::vector<uint8_t> filledAlbedo = filled;
    std::vector<uint8_t> filledPacked = filled;
    dilate(albedo, filledAlbedo, sheet, 8);
    dilate(packed, filledPacked, sheet, 8);

    packMips(albedo, sheet, ColorSpace::Srgb, TextureUsage::Albedo, out.albedo);
    packMips(packed, sheet, ColorSpace::Linear, TextureUsage::Packed, out.packed);
}

}  // namespace mge::skin
