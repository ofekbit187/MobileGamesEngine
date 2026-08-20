// UV chart report (docs/TEXTURING.md §6) — the texture artist's first
// instrument.
//
// A skin texture is only as even as the chart it is painted on. This tool
// measures the template body's actual UV layout instead of trusting it:
// for every body region it sums triangle area in the world and in the chart,
// and reports
//
//   * texel density (texels per metre) at a reference sheet size,
//   * anisotropy — how much a circle painted on the sheet is stretched into
//     an ellipse on the body,
//   * how much of the sheet the chart actually uses, and whether two shells
//     land on the same texels.
//
// MODELING.md §5 requires an even chart ("a texture must not be sharper on
// the hands than on the torso"); this is the measurement that says whether
// it is. It reports rather than gates, because the chart is knowingly
// uneven today — the gate lands with the fix, next to the topology tests
// that already guard the body.

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "mge/character/body_mesh.h"

using namespace mge;

namespace {

// The sheet the report is expressed in. Densities scale linearly with it:
// halving to 512 halves every px/m figure.
constexpr double kSheet = 1024.0;

// The evenness MODELING.md §5 asks for, as a number: every region within
// this fraction of the chart mean.
constexpr double kDensityTolerance = 0.15;

const char* regionName(BodyRegion r) {
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
        case BodyRegion::FootR: return "FootR";
        default: return "?";
    }
}

struct Rect {
    double u0 = 2, v0 = 2, u1 = -1, v1 = -1;
    void extend(double u, double v) {
        u0 = u < u0 ? u : u0;
        v0 = v < v0 ? v : v0;
        u1 = u > u1 ? u : u1;
        v1 = v > v1 ? v : v1;
    }
    double area() const { return (u1 > u0 && v1 > v0) ? (u1 - u0) * (v1 - v0) : 0.0; }
    bool overlaps(const Rect& o) const {
        return u0 < o.u1 - 1e-6 && o.u0 < u1 - 1e-6 && v0 < o.v1 - 1e-6 && o.v0 < v1 - 1e-6;
    }
};

struct RegionStat {
    BodyRegion region = BodyRegion::Torso;
    int shells = 0;          // separate mesh parts landing on this region
    bool shellsOverlap = false;
    double worldArea = 0;    // m^2 of skin
    double chartArea = 0;    // texels^2 covered by the triangles
    double stretchSum = 0;   // area-weighted anisotropy
    double densityMin = 1e30;
    double densityMax = 0;
    Rect island;
    std::vector<Rect> shellRects;

    double density() const { return worldArea > 0 ? std::sqrt(chartArea / worldArea) : 0.0; }
    double stretch() const { return worldArea > 0 ? stretchSum / worldArea : 0.0; }
};

}  // namespace

int main() {
    SkinnedMeshData mesh;
    BodyBuildDesc desc;
    desc.lod = BodyLod::Lod0;
    desc.regions = kAllRegions;
    buildTemplateBody(desc, mesh);

    if (mesh.vertices.empty() || mesh.parts.empty()) {
        printf("FAIL: template body built empty\n");
        return 1;
    }

    std::vector<RegionStat> stats(kBodyRegionCount);
    for (size_t i = 0; i < stats.size(); ++i) stats[i].region = static_cast<BodyRegion>(i);

    for (const MeshPart& part : mesh.parts) {
        RegionStat& s = stats[static_cast<size_t>(part.region)];
        Rect shell;
        s.shells++;

        for (uint32_t i = 0; i + 2 < part.indexCount; i += 3) {
            const SkinVertex& a = mesh.vertices[mesh.indices[part.firstIndex + i]];
            const SkinVertex& b = mesh.vertices[mesh.indices[part.firstIndex + i + 1]];
            const SkinVertex& c = mesh.vertices[mesh.indices[part.firstIndex + i + 2]];

            const Vec3 e1{b.position.x - a.position.x, b.position.y - a.position.y,
                          b.position.z - a.position.z};
            const Vec3 e2{c.position.x - a.position.x, c.position.y - a.position.y,
                          c.position.z - a.position.z};
            const Vec3 cross{e1.y * e2.z - e1.z * e2.y, e1.z * e2.x - e1.x * e2.z,
                             e1.x * e2.y - e1.y * e2.x};
            const double worldArea =
                0.5 * std::sqrt(double(cross.x) * cross.x + double(cross.y) * cross.y +
                                double(cross.z) * cross.z);

            const double au = a.uv[0] / 65535.0, av = a.uv[1] / 65535.0;
            const double bu = b.uv[0] / 65535.0, bv = b.uv[1] / 65535.0;
            const double cu = c.uv[0] / 65535.0, cv = c.uv[1] / 65535.0;
            shell.extend(au, av);
            shell.extend(bu, bv);
            shell.extend(cu, cv);
            s.island.extend(au, av);
            s.island.extend(bu, bv);
            s.island.extend(cu, cv);

            const double du1 = (bu - au) * kSheet, dv1 = (bv - av) * kSheet;
            const double du2 = (cu - au) * kSheet, dv2 = (cv - av) * kSheet;
            const double det = du1 * dv2 - du2 * dv1;
            const double chartArea = 0.5 * std::fabs(det);
            if (worldArea <= 1e-12 || chartArea <= 1e-12) continue;  // degenerate either side

            s.worldArea += worldArea;
            s.chartArea += chartArea;

            // World tangents per texel: invert the (u,v) -> (e1,e2) map. The
            // singular values of that 2x2 metric are metres-per-texel along
            // the least and most stretched directions; their ratio is what a
            // painted circle turns into.
            const Vec3 tu{float((e1.x * dv2 - e2.x * dv1) / det), float((e1.y * dv2 - e2.y * dv1) / det),
                          float((e1.z * dv2 - e2.z * dv1) / det)};
            const Vec3 tv{float((e2.x * du1 - e1.x * du2) / det), float((e2.y * du1 - e1.y * du2) / det),
                          float((e2.z * du1 - e1.z * du2) / det)};
            const double E = double(tu.x) * tu.x + double(tu.y) * tu.y + double(tu.z) * tu.z;
            const double G = double(tv.x) * tv.x + double(tv.y) * tv.y + double(tv.z) * tv.z;
            const double F = double(tu.x) * tv.x + double(tu.y) * tv.y + double(tu.z) * tv.z;
            const double half = (E + G) * 0.5;
            const double disc = std::sqrt(std::max(0.0, half * half - (E * G - F * F)));
            const double sMax = std::sqrt(std::max(0.0, half + disc));  // metres per texel
            const double sMin = std::sqrt(std::max(0.0, half - disc));
            if (sMin > 1e-9) {
                s.stretchSum += (sMax / sMin) * worldArea;
                s.densityMin = std::min(s.densityMin, 1.0 / sMax);
                s.densityMax = std::max(s.densityMax, 1.0 / sMin);
            }
        }

        for (const Rect& other : s.shellRects) {
            if (shell.overlaps(other)) s.shellsOverlap = true;
        }
        s.shellRects.push_back(shell);
    }

    printf("UV chart report — template body, LOD0\n");
    printf("%zu vertices, %zu triangles, %zu parts; reference sheet %.0f x %.0f\n\n",
           mesh.vertices.size(), mesh.triangleCount(), mesh.parts.size(), kSheet, kSheet);

    printf("%-7s %6s %9s %9s %8s %8s %13s\n", "region", "shells", "skin m2", "island%", "px/m",
           "stretch", "px/m min-max");
    printf("------- ------ --------- --------- -------- -------- -------------\n");

    double totalWorld = 0, totalChart = 0, usedSheet = 0;
    int measured = 0;
    for (const RegionStat& s : stats) {
        if (s.shells == 0) {
            printf("%-7s %6s %9s %9s %8s %8s %13s\n", regionName(s.region), "-", "-", "-", "-", "-",
                   "no geometry");
            continue;
        }
        totalWorld += s.worldArea;
        totalChart += s.chartArea;
        usedSheet += s.island.area();
        measured++;
        printf("%-7s %6d %9.4f %9.2f %8.0f %8.2f %6.0f-%-6.0f\n", regionName(s.region), s.shells,
               s.worldArea, 100.0 * s.island.area(), s.density(), s.stretch(), s.densityMin,
               s.densityMax);
    }

    const double mean = totalWorld > 0 ? std::sqrt(totalChart / totalWorld) : 0.0;
    double lo = 1e30, hi = 0, worstDev = 0;
    const char* worstName = "-";
    int outOfTolerance = 0, overlapping = 0;
    for (const RegionStat& s : stats) {
        if (s.shells == 0) continue;
        const double d = s.density();
        lo = std::min(lo, d);
        hi = std::max(hi, d);
        const double dev = mean > 0 ? std::fabs(d - mean) / mean : 0.0;
        if (dev > kDensityTolerance) outOfTolerance++;
        if (dev > worstDev) {
            worstDev = dev;
            worstName = regionName(s.region);
        }
        if (s.shellsOverlap) overlapping++;
    }

    printf("\nskin area          %.3f m2 over %d regions\n", totalWorld, measured);
    printf("sheet used         %.1f%% (islands), %.1f%% (triangles, overlap counted twice)\n",
           100.0 * usedSheet, 100.0 * totalChart / (kSheet * kSheet));
    printf("mean density       %.0f px/m at %.0f^2  (=> %.0f px/m at 512^2)\n", mean, kSheet,
           mean * 0.5);
    printf("region spread      %.0f .. %.0f px/m  (%.2fx)\n", lo, hi, lo > 0 ? hi / lo : 0.0);
    printf("worst deviation    %s, %.0f%% from the mean (tolerance %.0f%%)\n", worstName,
           worstDev * 100.0, kDensityTolerance * 100.0);

    printf("\nchart status: %s\n",
           (outOfTolerance == 0 && overlapping == 0) ? "EVEN" : "UNEVEN — see docs/TEXTURING.md §6");
    if (outOfTolerance > 0)
        printf("  * %d of %d regions outside the +-%.0f%% density tolerance\n", outOfTolerance,
               measured, kDensityTolerance * 100.0);
    if (overlapping > 0)
        printf("  * %d region(s) with two shells on the same texels — one cannot be painted\n"
               "    differently from the other\n",
               overlapping);
    for (const RegionStat& s : stats) {
        if (s.shells == 0)
            printf("  * %s: island reserved in the chart, no geometry emits into it\n",
                   regionName(s.region));
    }
    for (const RegionStat& s : stats) {
        if (s.shells > 0 && s.stretch() > 2.0)
            printf("  * %s: %.1fx average stretch — a circle painted here reads as an ellipse\n",
                   regionName(s.region), s.stretch());
    }

    // Reports, never gates: the chart is uneven today by measurement, and a
    // failing build helps nobody until the fix lands. What must not happen
    // silently is the mesh losing its UVs altogether.
    if (totalChart <= 0.0) {
        printf("\nFAIL: no measurable UV area — the chart is gone\n");
        return 1;
    }
    return 0;
}
