#pragma once

// Mesh-to-mesh skin transfer — the import path (task 15.2, ADR 0014 amendment).
//
// A sourced skin is authored against SOMEBODY ELSE'S mesh and UV layout. Ours
// is bespoke, so the transform between them is not an affine we can write
// down: it is a correspondence between two surfaces. Task 15.0 measured that
// this is the only route to a face — the reversal's two routes turned out to be
// an empty input set and a tiling material that cannot place an eye.
//
// The method, and why each step is a measurement rather than an assumption:
//
//   1. ALIGN. Both meshes are ~1.75 m humans in bind pose, but in different
//      units, origins and facing directions. Scale by measured height, seat
//      both on the ground, centre them, then CHOOSE THE FACING BY MEASURING —
//      try both yaws and keep whichever puts the two surfaces closer. A guess
//      about which way a mesh faces is exactly the kind of thing that is wrong
//      half the time and silently produces a face on the back of a head.
//   2. CORRESPOND. For every texel of OUR chart we already know the 3D point it
//      covers (the surface map). Find the closest point on the SOURCE surface,
//      take its barycentric coordinates, and read the source's UV there.
//   3. SAMPLE. Read the source texture at that UV.
//
// Step 2 is the whole cost, so the source mesh goes into a uniform grid: the
// body is a thin shell in a small box, which is the case a grid handles well.
//
// The result is written into our frozen chart, so it inherits every property
// the chart already has — even density, disjoint islands, no seam — and the
// existing bake path (dilate, mip in linear space, validate) finishes it.

#include <cstdint>
#include <string>
#include <vector>

#include "mge/character/body_mesh.h"
#include "mge/graphics/texture_data.h"

namespace mge::skin {

// ------------------------------------------------------------- the source --

// A source mesh with the UV layout its texture was authored against. Loaded
// from OBJ because that is what the CC0 packs ship.
struct SourceMesh {
    std::vector<Vec3> positions;
    std::vector<float> uvs;          // 2 per texcoord
    std::vector<uint32_t> posIndex;  // 3 per triangle
    std::vector<uint32_t> uvIndex;   // 3 per triangle
    size_t triangleCount() const { return posIndex.size() / 3; }
    bool empty() const { return posIndex.empty(); }
};

// Quads are triangulated on load; MakeHuman's meshes are all-quad.
bool loadObj(const std::string& path, SourceMesh& out, std::string& error);

// A decoded source texture (any format stb_image reads).
struct SourceImage {
    int width = 0, height = 0;
    std::vector<uint8_t> rgb;  // 3 bytes per pixel
    bool valid() const { return width > 0 && height > 0 && !rgb.empty(); }
};

bool loadImage(const std::string& path, SourceImage& out, std::string& error);

// ---------------------------------------------------------------- align ----

// The similarity transform that puts the source mesh into our body's space.
// Every field is measured, and `flipped` is decided by trying both.
struct Alignment {
    float scale = 1.0f;
    Vec3 translation{0, 0, 0};
    bool flipped = false;      // 180 degrees about Y
    float meanDistance = 0;    // metres, source surface to our vertices
    float medianDistance = 0;
    float meanDistanceFlipped = 0;  // what the rejected orientation scored
    Vec3 apply(const Vec3& p) const {
        const Vec3 s{p.x * scale, p.y * scale, p.z * scale};
        const Vec3 r = flipped ? Vec3{-s.x, s.y, -s.z} : s;
        return r + translation;
    }
};

Alignment alignToBody(const SourceMesh& source, const SkinnedMeshData& body);

// A per-region corrective offset, in metres.
//
// One rigid transform cannot fit two humans in DIFFERENT POSES: the source's
// arms are held wider than ours, so a fit that satisfies the torso pulls the
// hands tens of millimetres off, and the face — where a 30 mm eye lives —
// inherits whatever compromise the limbs forced. Measured on the first run:
// 71 mm mean, 64 mm across the face, which is an eye and a half.
//
// So each body region gets its own offset, found by iterated closest-point:
// query the source where the region actually corresponds rather than where a
// whole-body average says it should. Regions are the segmentation the body
// already publishes, so this needs no segmentation of the source at all.
// A per-region offset alone is DISCONTINUOUS at region boundaries, and the
// discontinuity is visible: the arms move 91 mm and the hands 223 mm, so at
// the wrist the two disagree by 13 cm and the texels there sample somewhere
// else entirely — which showed up as a black band around both wrists on the
// first run that used them.
//
// So the offsets are carried per VERTEX and smoothed across the surface, then
// blended per texel. The region fit decides where each part of the body
// corresponds; the smoothing makes the field continuous so no seam appears
// where two regions meet.
struct RegionFit {
    Vec3 offset[kBodyRegionCount];
    float residual[kBodyRegionCount];  // mean distance after fitting, metres
    float before[kBodyRegionCount];    // and before, so the gain is visible
    uint32_t samples[kBodyRegionCount];

    // The smoothed field: one offset per body vertex, plus the positions to
    // interpolate between.
    std::vector<Vec3> vertexOffset;
    std::vector<Vec3> vertexPosition;
    float blendRadius = 0.05f;  // metres
};

RegionFit fitRegions(const SourceMesh& source, const SkinnedMeshData& body,
                     const Alignment& alignment, int iterations);

// --------------------------------------------------------------- transfer --

struct TransferStats {
    size_t texelsWritten = 0;
    size_t texelsMissed = 0;      // no source surface within the search radius
    double meanDistance = 0;      // metres from our surface to the source's
    double maxDistance = 0;
    double faceMeanDistance = 0;  // the face alone: the number that matters most
    size_t texelsRejected = 0;    // sampled, but the colour was not skin
    size_t faceRejected = 0;      // of those, on the face
};

// Resample the source texture into our chart. `surface` is the rasterized
// surface map (one entry per texel of a `sheet` x `sheet` chart).
struct SurfaceTexel;  // from skin_generator.h
bool transfer(const SourceMesh& source, const SourceImage& image, const Alignment& alignment,
              const RegionFit& fit, const SurfaceTexel* surface, uint32_t sheet,
              std::vector<uint8_t>& outRgbLinear, std::vector<uint8_t>& outFilled,
              TransferStats& stats);

}  // namespace mge::skin
