// UV chart report — the skin-texture pipeline's first gate.
//
// A skin texture is only as even as the chart it is painted on, so the chart
// is checked before a texel is ever authored. This tool loads the standard
// (`assets/standards/skin_texture.mgestd` — data, not prose), measures the
// delivered template body, and returns pass/fail per rule WITH A REASON. That
// is P12 applied to textures: the artist self-serves, no engineer in the loop.
//
// It runs in two phases, in this order, because the second is meaningless
// without the first:
//
//   1. INTEGRITY — are the UVs usable at all? Inside the 0..1 tile the vertex
//      format can represent (B-3), no zero-area triangles, every maskable
//      region present, islands disjoint.
//   2. EVENNESS — texel density per region and stretch inside each island,
//      against the standard's tolerances (MODELING.md §5, B-27).
//
// Exit code is 0 by default so a knowingly-broken chart does not redden the
// shared build; `--gate` makes it exit non-zero on any refusal, which is how
// the body session runs it as an acceptance gate (BODY_CONTRACT.md §9).
//
// Findings and the handoff they produced: docs/research/uv-audit.md.

#include <cmath>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "mge/character/body_mesh.h"

using namespace mge;

namespace {

// ------------------------------------------------------------ standard -----

// The rules this run was measured against, read from the .mgestd file. Only
// the fields the chart phase needs are parsed; the image-bake rules belong to
// the baker and are reported as "not checkable yet" rather than assumed good.
struct Standard {
    bool loaded = false;
    int sheetW = 1024, sheetH = 1024;
    double densityTolerance = 0.15;
    double stretchMax = 1.5;
    double minUtilisation = 0.55;
    double maxDegenerateFrac = 0.0;
    double minIslandGapTexels = 4.0;
    int standardDensity = 512;
    std::vector<std::string> requiredRegions;
};

bool loadStandard(const char* path, Standard& s) {
    std::ifstream in(path);
    if (!in) return false;
    std::string line;
    while (std::getline(in, line)) {
        const size_t hash = line.find('#');
        if (hash != std::string::npos) line.resize(hash);
        std::istringstream ls(line);
        std::string key;
        if (!(ls >> key)) continue;

        if (key == "sheet_tier") {
            std::string tier;
            int w = 0, h = 0;
            ls >> tier >> w >> h;
            if (tier == "hero" && w > 0 && h > 0) {
                s.sheetW = w;
                s.sheetH = h;
            }
        } else if (key == "density_tolerance") {
            ls >> s.densityTolerance;
        } else if (key == "stretch_max") {
            ls >> s.stretchMax;
        } else if (key == "chart_min_utilisation") {
            ls >> s.minUtilisation;
        } else if (key == "chart_max_degenerate_frac") {
            ls >> s.maxDegenerateFrac;
        } else if (key == "chart_min_island_gap_texels") {
            ls >> s.minIslandGapTexels;
        } else if (key == "density_class") {
            std::string name;
            int px = 0;
            ls >> name >> px;
            if (name == "standard" && px > 0) s.standardDensity = px;
        } else if (key == "chart_regions_required") {
            std::string r;
            while (ls >> r) s.requiredRegions.push_back(r);
        }
    }
    s.loaded = true;
    return true;
}

// -------------------------------------------------------------- report -----

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
    bool valid() const { return u1 > u0 && v1 > v0; }
    void extend(double u, double v) {
        u0 = u < u0 ? u : u0;
        v0 = v < v0 ? v : v0;
        u1 = u > u1 ? u : u1;
        v1 = v > v1 ? v : v1;
    }
    double area() const { return valid() ? (u1 - u0) * (v1 - v0) : 0.0; }
    // Overlap in UV units; negative means a gap, and how wide it is.
    double overlapArea(const Rect& o) const {
        const double w = std::min(u1, o.u1) - std::max(u0, o.u0);
        const double h = std::min(v1, o.v1) - std::max(v0, o.v0);
        return (w > 0 && h > 0) ? w * h : 0.0;
    }
};

struct RegionStat {
    BodyRegion region = BodyRegion::Torso;
    int shells = 0;
    uint32_t triangles = 0;
    uint32_t degenerateUv = 0;  // zero UV area: nowhere to put a texel
    uint32_t clampedTris = 0;   // every vertex pinned to the tile edge
    double worldArea = 0;       // m^2, over triangles with usable UVs
    double worldAreaAll = 0;    // m^2, over every triangle in the region
    double chartArea = 0;       // texels^2
    double stretchSum = 0;
    Rect island;

    bool measurable() const { return worldArea > 0 && chartArea > 0; }
    double density() const { return measurable() ? std::sqrt(chartArea / worldArea) : 0.0; }
    double stretch() const { return worldArea > 0 ? stretchSum / worldArea : 0.0; }
};

// One rule's verdict, printed with the reason it failed — a bare FAIL teaches
// an artist nothing.
struct Verdict {
    const char* rule;
    bool pass;
    std::string reason;
};

void say(std::vector<Verdict>& out, const char* rule, bool pass, const std::string& reason) {
    out.push_back({rule, pass, reason});
}

std::string fmt(const char* f, ...) {
    char buf[512];
    va_list args;
    va_start(args, f);
    vsnprintf(buf, sizeof(buf), f, args);
    va_end(args);
    return std::string(buf);
}

}  // namespace

int main(int argc, char** argv) {
    bool gate = false;
    const char* stdPath = "assets/standards/skin_texture.mgestd";
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--gate") == 0) gate = true;
        else if (std::strcmp(argv[i], "--standard") == 0 && i + 1 < argc) stdPath = argv[++i];
    }

    Standard std_;
    if (!loadStandard(stdPath, std_)) {
        // Tests may run from the build directory; try the source tree above it.
        loadStandard((std::string("../") + stdPath).c_str(), std_);
    }
    if (!std_.loaded) {
        // Never measure against invented defaults and call the result a
        // verdict — silently substituting a fallback for the real rules is the
        // same failure this tool exists to catch (docs/research/uv-audit.md).
        printf("FAIL: standard not found at '%s'\n"
               "      Pass --standard <path> to assets/standards/skin_texture.mgestd.\n"
               "      Refusing to report verdicts against defaults nobody agreed to.\n",
               stdPath);
        return 1;
    }

    SkinnedMeshData mesh;
    BodyBuildDesc desc;
    desc.lod = BodyLod::Lod0;
    desc.regions = kAllRegions;
    buildTemplateBody(desc, mesh);

    if (mesh.vertices.empty() || mesh.parts.empty()) {
        printf("FAIL: template body built empty — nothing to measure\n");
        return 1;
    }

    const double sheet = std_.sheetW;
    const uint16_t kUvMax = 65535;

    std::vector<RegionStat> stats(kBodyRegionCount);
    for (size_t i = 0; i < stats.size(); ++i) stats[i].region = static_cast<BodyRegion>(i);

    uint32_t totalTris = 0, totalDegenerate = 0, totalClamped = 0, vertsAtEdge = 0;
    for (const SkinVertex& v : mesh.vertices) {
        if (v.uv[0] == kUvMax || v.uv[1] == kUvMax || v.uv[0] == 0 || v.uv[1] == 0) vertsAtEdge++;
    }

    for (const MeshPart& part : mesh.parts) {
        RegionStat& s = stats[static_cast<size_t>(part.region)];
        s.shells++;

        for (uint32_t i = 0; i + 2 < part.indexCount; i += 3) {
            const SkinVertex& a = mesh.vertices[mesh.indices[part.firstIndex + i]];
            const SkinVertex& b = mesh.vertices[mesh.indices[part.firstIndex + i + 1]];
            const SkinVertex& c = mesh.vertices[mesh.indices[part.firstIndex + i + 2]];
            s.triangles++;
            totalTris++;

            const Vec3 e1{b.position.x - a.position.x, b.position.y - a.position.y,
                          b.position.z - a.position.z};
            const Vec3 e2{c.position.x - a.position.x, c.position.y - a.position.y,
                          c.position.z - a.position.z};
            const Vec3 cross{e1.y * e2.z - e1.z * e2.y, e1.z * e2.x - e1.x * e2.z,
                             e1.x * e2.y - e1.y * e2.x};
            const double worldArea =
                0.5 * std::sqrt(double(cross.x) * cross.x + double(cross.y) * cross.y +
                                double(cross.z) * cross.z);
            s.worldAreaAll += worldArea;

            // A triangle whose vertices are all pinned to the tile edge is the
            // signature of an island that lived outside 0..1 and was clamped
            // flat on import — the difference between "uneven" and "destroyed".
            const int atEdgeU = (a.uv[0] == kUvMax) + (b.uv[0] == kUvMax) + (c.uv[0] == kUvMax);
            const int atEdgeV = (a.uv[1] == kUvMax) + (b.uv[1] == kUvMax) + (c.uv[1] == kUvMax);
            if (atEdgeU == 3 || atEdgeV == 3) {
                s.clampedTris++;
                totalClamped++;
            }

            const double au = a.uv[0] / 65535.0, av = a.uv[1] / 65535.0;
            const double bu = b.uv[0] / 65535.0, bv = b.uv[1] / 65535.0;
            const double cu = c.uv[0] / 65535.0, cv = c.uv[1] / 65535.0;

            const double du1 = (bu - au) * sheet, dv1 = (bv - av) * sheet;
            const double du2 = (cu - au) * sheet, dv2 = (cv - av) * sheet;
            const double det = du1 * dv2 - du2 * dv1;
            if (worldArea <= 1e-12 || std::fabs(det) <= 1e-9) {
                s.degenerateUv++;
                totalDegenerate++;
                continue;
            }

            s.island.extend(au, av);
            s.island.extend(bu, bv);
            s.island.extend(cu, cv);
            s.worldArea += worldArea;
            s.chartArea += 0.5 * std::fabs(det);

            // World tangents per texel; the ratio of the metric's singular
            // values is what a painted circle becomes on the body.
            const Vec3 tu{float((e1.x * dv2 - e2.x * dv1) / det),
                          float((e1.y * dv2 - e2.y * dv1) / det),
                          float((e1.z * dv2 - e2.z * dv1) / det)};
            const Vec3 tv{float((e2.x * du1 - e1.x * du2) / det),
                          float((e2.y * du1 - e1.y * du2) / det),
                          float((e2.z * du1 - e1.z * du2) / det)};
            const double E = double(tu.x) * tu.x + double(tu.y) * tu.y + double(tu.z) * tu.z;
            const double G = double(tv.x) * tv.x + double(tv.y) * tv.y + double(tv.z) * tv.z;
            const double F = double(tu.x) * tv.x + double(tu.y) * tv.y + double(tu.z) * tv.z;
            const double half = (E + G) * 0.5;
            const double disc = std::sqrt(std::max(0.0, half * half - (E * G - F * F)));
            const double sMax = std::sqrt(std::max(0.0, half + disc));
            const double sMin = std::sqrt(std::max(0.0, half - disc));
            if (sMin > 1e-9) s.stretchSum += (sMax / sMin) * worldArea;
        }
    }

    // ------------------------------------------------------------ output ---

    printf("UV chart report — template body, LOD0\n");
    printf("standard: %s%s\n", stdPath, std_.loaded ? "" : "  (NOT FOUND — built-in defaults)");
    printf("%zu vertices, %zu triangles, %zu parts; sheet %.0f x %.0f\n\n", mesh.vertices.size(),
           mesh.triangleCount(), mesh.parts.size(), sheet, sheet);

    printf("PHASE 1 — chart integrity\n");
    printf("%-7s %6s %6s %10s %10s %9s\n", "region", "shells", "tris", "no-UV-area", "edge-locked",
           "usable%");
    printf("------- ------ ------ ---------- ----------- ---------\n");
    for (const RegionStat& s : stats) {
        if (s.shells == 0) {
            printf("%-7s %6s %6s %10s %11s %9s   <- no geometry\n", regionName(s.region), "-", "-",
                   "-", "-", "-");
            continue;
        }
        const uint32_t usable = s.triangles - s.degenerateUv;
        printf("%-7s %6d %6u %10u %11u %8.1f%%\n", regionName(s.region), s.shells, s.triangles,
               s.degenerateUv, s.clampedTris, 100.0 * usable / (s.triangles ? s.triangles : 1));
    }

    const double degenFrac = totalTris ? double(totalDegenerate) / totalTris : 0.0;
    printf("\nwhole body: %u triangles, %u with no UV area (%.1f%%), %u edge-locked\n", totalTris,
           totalDegenerate, 100.0 * degenFrac, totalClamped);
    printf("            %u of %zu vertices sit exactly on a tile edge (%.1f%%)\n", vertsAtEdge,
           mesh.vertices.size(), 100.0 * vertsAtEdge / mesh.vertices.size());

    printf("\nPHASE 2 — evenness (only over triangles that have UV area)\n");
    printf("%-7s %9s %9s %8s %8s\n", "region", "skin m2", "measured%", "px/m", "stretch");
    printf("------- --------- --------- -------- --------\n");

    double totalWorld = 0, totalChart = 0, usedSheet = 0;
    int measured = 0;
    for (const RegionStat& s : stats) {
        if (s.shells == 0) continue;
        if (!s.measurable()) {
            printf("%-7s %9.4f %9s %8s %8s   <- unmeasurable\n", regionName(s.region),
                   s.worldAreaAll, "0%", "-", "-");
            continue;
        }
        totalWorld += s.worldArea;
        totalChart += s.chartArea;
        usedSheet += s.island.area();
        measured++;
        printf("%-7s %9.4f %8.1f%% %8.0f %8.2f\n", regionName(s.region), s.worldAreaAll,
               100.0 * s.worldArea / (s.worldAreaAll > 0 ? s.worldAreaAll : 1), s.density(),
               s.stretch());
    }

    const double mean = totalWorld > 0 ? std::sqrt(totalChart / totalWorld) : 0.0;
    if (measured > 0) {
        printf("\nmeasurable skin    %.3f m2 over %d of %zu regions\n", totalWorld, measured,
               kBodyRegionCount);
        printf("mean density       %.0f px/m at %.0f^2 (standard: %d px/m)\n", mean, sheet,
               std_.standardDensity);
    }

    // ----------------------------------------------------------- verdicts --

    std::vector<Verdict> verdicts;

    say(verdicts, "chart_in_unit_tile", totalClamped == 0,
        totalClamped == 0
            ? "no triangle is locked to the tile edge"
            : fmt("%u triangles (%.1f%%) have every vertex pinned to a tile edge — the "
                  "signature of islands that lay outside 0..1 and were clamped flat on import; "
                  "the uint16 UV format (B-3) cannot represent them",
                  totalClamped, 100.0 * totalClamped / (totalTris ? totalTris : 1)));

    say(verdicts, "chart_max_degenerate_frac", degenFrac <= std_.maxDegenerateFrac,
        fmt("%u of %u triangles have zero UV area (%.1f%%, allowed %.1f%%) — those surfaces "
            "have nowhere to put a texel",
            totalDegenerate, totalTris, 100.0 * degenFrac, 100.0 * std_.maxDegenerateFrac));

    {
        // The data file names the regions that must exist, so extending the
        // body's region set is a line in the standard, not a code change.
        std::string missing;
        for (const std::string& want : std_.requiredRegions) {
            bool found = false;
            for (const RegionStat& s : stats) {
                if (s.shells > 0 && want == regionName(s.region)) found = true;
            }
            if (!found) {
                if (!missing.empty()) missing += ", ";
                missing += want;
            }
        }
        say(verdicts, "chart_regions_required", missing.empty(),
            missing.empty() ? fmt("all %zu required regions own geometry", std_.requiredRegions.size())
                            : "regions with no geometry: " + missing +
                                  " — nothing can be painted there, and texture-space masking "
                                  "cannot address them");
    }

    bool islandsOverlap = false;
    {
        std::string overlaps;
        for (size_t i = 0; i < stats.size(); ++i) {
            if (!stats[i].island.valid()) continue;
            for (size_t j = i + 1; j < stats.size(); ++j) {
                if (!stats[j].island.valid()) continue;
                if (stats[i].island.overlapArea(stats[j].island) > 1e-6) {
                    islandsOverlap = true;
                    if (!overlaps.empty()) overlaps += ", ";
                    overlaps += std::string(regionName(stats[i].region)) + "/" +
                                regionName(stats[j].region);
                }
            }
        }
        say(verdicts, "chart_islands_disjoint", overlaps.empty(),
            overlaps.empty() ? "no two regions share texture space"
                             : "regions sharing texels: " + overlaps +
                                   " — neither can be painted differently from the other");
    }

    // Summing island boxes only means "coverage" when they are disjoint; with
    // overlaps the total exceeds the sheet and would report a false pass.
    say(verdicts, "chart_min_utilisation", !islandsOverlap && usedSheet >= std_.minUtilisation,
        islandsOverlap
            ? fmt("not checkable: islands overlap, so their areas sum to %.1f%% of the sheet — "
                  "coverage is undefined until they are disjoint",
                  100.0 * usedSheet)
            : fmt("islands cover %.1f%% of the sheet (minimum %.0f%%) — unused sheet is memory "
                  "paid for and not received",
                  100.0 * usedSheet, 100.0 * std_.minUtilisation));

    // Density and stretch are only a verdict on the body if most of the body
    // was measurable. Passing on the 4% that survived would be the worst kind
    // of green: true about the sample, false about the asset.
    double bodyArea = 0;
    for (const RegionStat& s : stats) bodyArea += s.worldAreaAll;
    const double coverage = bodyArea > 0 ? totalWorld / bodyArea : 0.0;
    const bool conclusive = coverage >= 0.9;

    if (measured > 0 && mean > 0 && conclusive) {
        int outOfTol = 0;
        std::string worst;
        double worstDev = 0;
        for (const RegionStat& s : stats) {
            if (!s.measurable()) continue;
            const double dev = std::fabs(s.density() - mean) / mean;
            if (dev > std_.densityTolerance) outOfTol++;
            if (dev > worstDev) {
                worstDev = dev;
                worst = regionName(s.region);
            }
        }
        say(verdicts, "density_outside_tolerance", outOfTol == 0,
            fmt("%d of %d measurable regions differ from the chart mean by more than %.0f%% "
                "(worst: %s at %.0f%%)",
                outOfTol, measured, 100.0 * std_.densityTolerance, worst.c_str(),
                100.0 * worstDev));

        int stretched = 0;
        for (const RegionStat& s : stats) {
            if (s.measurable() && s.stretch() > std_.stretchMax) stretched++;
        }
        say(verdicts, "stretch_above_max", stretched == 0,
            fmt("%d of %d measurable regions stretch more than %.2fx — a painted circle reads "
                "as an ellipse there",
                stretched, measured, std_.stretchMax));
    } else {
        const std::string why =
            fmt("not conclusive: only %.1f%% of the body's surface has usable UVs (need 90%%) — "
                "any verdict here would describe the surviving sliver, not the body",
                100.0 * coverage);
        say(verdicts, "density_outside_tolerance", false, why);
        say(verdicts, "stretch_above_max", false, why);
    }

    printf("\nVERDICTS (against %s)\n", stdPath);
    int failed = 0;
    for (const Verdict& v : verdicts) {
        printf("  %s  %-26s %s\n", v.pass ? "pass" : "FAIL", v.rule, v.reason.c_str());
        if (!v.pass) failed++;
    }

    printf("\nchart status: %s\n", failed == 0 ? "CONFORMS" : "REFUSED");
    if (failed > 0) {
        printf("%d rule(s) refused. No skin texture should be authored against this chart —\n"
               "see docs/research/uv-audit.md for the defects and the handoff.\n",
               failed);
    }
    printf("\nnot checkable here (no textures exist yet — the baker's gates):\n"
           "  colour space, mip chain, island padding, compression PSNR, determinism\n");

    return (gate && failed > 0) ? 1 : 0;
}
