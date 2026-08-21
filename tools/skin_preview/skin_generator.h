#pragma once

// Procedural skin — the generator (ADR 0014, task 15.1).
//
// Skin is produced by code, not by a brush. Not a staffing workaround: a
// painted skin is ONE skin, and Dictation 5 dictates that every person is
// unique with hereditary features, which no finite set of painted skins can
// deliver. The generator takes the phenotype's numbers and produces a surface.
//
// Two rules shape this file, both ruled before it was written:
//
//   * **Memory is O(1) in crowd size** (ADR 0014 Ruling 1). What is generated
//     here is a SHARED map set, baked once and sampled by everyone. Per-person
//     variation is a handful of scalars evaluated in the shader. Nothing here
//     may ever be called once per character.
//   * **Nothing is chosen by eye** (AGENTS.md §6.2). Every constant below is
//     either measured from the mesh at runtime or cited to a published source
//     in the comment beside it. Where a number is a judgment, it says so.
//
// The generator evaluates skin as a function of POSITION ON THE BODY, not of
// texture coordinates: each texel is rasterized back to the 3D surface it
// covers, so anatomy (where blood shows, where skin is thin, where a palm is)
// is addressed in metres on the body and the chart layout is irrelevant to it.
// Repack the chart and this code does not change.

#include <cstdint>
#include <vector>

#include "mge/character/body_mesh.h"
#include "mge/graphics/texture_data.h"

namespace mge::skin {

// ------------------------------------------------------------- phenotype ---

// What one person's skin is. These are the values ADR 0014 Ruling 1 says ride
// as material parameters — task 15.3 binds them to the Genome's phenotype.
// For 15.1 one set is chosen and rendered.
//
// The two axes are the two dominant chromophores of human skin: melanin in the
// epidermis and haemoglobin in the dermis. Everything else is modulation of
// those two.
struct SkinParams {
    // 0 = the lightest constitutive pigmentation, 1 = the darkest.
    // Calibrated so the resulting colour's ITA° lands in the published
    // Fitzpatrick bands (see `itaDegrees`), which is a measurable property of
    // the output rather than an opinion about it.
    float melanin = 0.35f;

    // Dermal blood. Drives a* (redness) and the perfusion zones — cheeks,
    // nose, ears, knuckles — where vessels sit close to the surface.
    float haemoglobin = 0.45f;

    // Weathering: sun exposure raises melanin on what the sun reaches (face,
    // neck, forearms, hands) and leaves what clothes cover alone. A farmhand
    // and a scribe differ mostly in this number.
    float weathering = 0.35f;

    // Freckle density, 0 = none. Placement is seeded, so two people with the
    // same density have different freckles.
    float freckles = 0.25f;
    uint32_t seed = 1u;
};

// ITA° = arctan((L* - 50) / b*) * 180/pi — the standard objective classifier
// of constitutive skin colour. Published bands: >55 very light (I), 41..55
// light (II), 28..41 intermediate light (III), 10..28 intermediate (IV),
// -30..10 dark (V), <=-30 very dark (VI).
double itaDegrees(double L, double b);
const char* fitzpatrickBand(double ita);

// The CIELAB colour the parameters resolve to before any spatial modulation —
// what `itaDegrees` is measured on, and what a test can assert.
void baseLab(const SkinParams& p, double& L, double& a, double& b);

// ----------------------------------------------------------- surface map ---

// One texel's footprint on the body: where it lands in metres, which way that
// surface faces, which region owns it, and how occluded it is. This is what
// turns "a texture" into "this body's texture" — and it is measured from the
// mesh, never assumed about the chart.
struct SurfaceTexel {
    Vec3 position{0, 0, 0};
    Vec3 normal{0, 1, 0};
    float ambient = 1.0f;  // 0 = fully occluded (nostril, eye socket, under chin)
    uint8_t region = 0xFF;  // BodyRegion, 0xFF = no surface here
    bool valid() const { return region != 0xFF; }
};

// Landmarks measured from the mesh itself, so feature placement follows the
// delivered body rather than a remembered one. If the body is re-imported
// slightly taller, the eyes move with it.
struct HeadLandmarks {
    float chinY = 0, crownY = 0;      // head vertical extent
    float faceZ = 0;                  // frontmost face surface (the face looks -Z)
    float halfWidth = 0;              // head half-width in x
    float eyeY = 0, browY = 0, noseBaseY = 0, mouthY = 0;
    float eyeOffsetX = 0, eyeHalfWidth = 0;
    float headHeight() const { return crownY - chinY; }
};

// Rasterize the body into chart space. `sheet` is the sheet size in texels.
void buildSurfaceMap(const SkinnedMeshData& mesh, uint32_t sheet,
                     std::vector<SurfaceTexel>& out);

// Per-vertex ambient occlusion by ray casting against the body itself, then
// interpolated into the surface map. Local only (rays are short): this is the
// contact shading that makes a nose read as a nose, not a global bake.
void computeAmbient(const SkinnedMeshData& mesh, std::vector<float>& outPerVertex);

HeadLandmarks measureHead(const SkinnedMeshData& mesh);

// ------------------------------------------------------------- the bake ----

// The shared map set. `albedo` is sRGB-typed base colour; `packed` is linear
// R=AO G=roughness B=mask in the standard's fixed order. Both carry a full
// mip chain generated in LINEAR space, as the standard requires.
struct SkinMaps {
    TextureData albedo;
    TextureData packed;
};

// Wrap an already-sRGB-encoded RGB sheet (an IMPORTED map) as a TextureData:
// dilate into the padding, build the mip chain in LINEAR light, tag it sRGB.
// Shared with the generator because those steps are properties of our chart
// and our standard, not of where the pixels came from.
bool packImportedAlbedo(const std::vector<uint8_t>& rgb, const std::vector<uint8_t>& filled,
                        uint32_t sheet, TextureData& out);

// Generate the shared maps. One call produces what an entire crowd samples.
void generate(const SkinnedMeshData& mesh, const SurfaceTexel* surface, uint32_t sheet,
              const HeadLandmarks& head, const SkinParams& params, SkinMaps& out);

}  // namespace mge::skin
