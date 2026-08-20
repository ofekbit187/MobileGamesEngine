#pragma once

// Garment fitting bake (Phase 13, tasks 13.1/13.2/13.5) — host-side tooling.
//
// This is where P12 is paid. An artist models a garment in a DCC against the
// published template, imports it, and this decides everything that follows:
// which bones move each vertex, where each vertex sits on the body, and how a
// coat sits over the shirt beneath it. Nothing here runs on a device and
// nothing here runs per frame; the runtime consumes the baked binding
// (`mge/character/garment_binding.h`) and skins it with the usual palette.
//
// The two jobs, and why each is done the way it is:
//
//   WEIGHTS (13.1) — a garment must deform exactly as the skin under it, so
//   its weights come from the body rather than from hand painting. Nearest
//   surface point works for anything tight and fails badly for anything loose:
//   the closest body point to a hanging sleeve's underside is the ribcage, and
//   copying ribcage weights makes the sleeve tear off the arm when it swings.
//   So every match is CONFIDENCE-GATED on distance *and* normal agreement, and
//   the vertices that fail the gate are filled by diffusing the trusted
//   weights across the garment's own surface (weight inpainting). That is the
//   published fix for exactly this failure, and it is what makes an
//   unsupervised import safe enough to hand an artist.
//
//   Expect roughly half of a closed garment's vertices to fail that gate and
//   be inpainted, and do not read it as a defect: a garment shell has an inner
//   wall as well as an outer one, and the inner wall faces the body, so its
//   normals disagree with the surface under it by construction. Those vertices
//   are filled by diffusion from the outer wall they join at the hem, which is
//   the same answer transfer would have given. Loosening `minNormalDot` to
//   "fix" the number is the one change that reintroduces the ribcage bug —
//   with the gate off, every vertex matches and a sleeve happily takes torso
//   weights.
//
//   BINDING (13.2) — each garment vertex is recorded as a point on the body:
//   triangle, barycentric coordinates, and an offset in the triangle's tangent
//   frame. Constrained to the region's permitted group, so a sleeve vertex
//   physically cannot bind to the torso even where the two surfaces touch.
//
// Layer chaining (13.5) is the same bake run against a different surface:
// layer k binds to layer k-1's outer surface pushed out by its thickness, so
// enclosure is arithmetic rather than hope. No cages, no RBF, nothing at
// runtime.

#include <cstddef>
#include <cstdint>
#include <vector>

#include "mge/character/body_mesh.h"
#include "mge/character/garment_binding.h"

namespace mge {

// --------------------------------------------------------- the knobs -------

struct FitParams {
    // Trust gate. A match beyond this distance, or whose surface faces more
    // than ~78 degrees away, is not believed and goes to inpainting instead.
    float maxDistance = 0.15f;    // metres
    float minNormalDot = 0.20f;   // cos of the permitted normal disagreement

    // Diffusion passes over the untrusted vertices. Converges long before this
    // on real garments; the cap only bounds a pathological mesh.
    int inpaintIterations = 128;

    // An artist who weighted the garment deliberately keeps their weights;
    // transfer is for the (usual) case where nobody did.
    bool acceptAuthoredWeights = true;

    // A sleeve may not bind to the torso: matches are restricted to the body
    // regions permitted for the garment vertex's own region.
    bool constrainToRegions = true;

    // Per-layer cloth thickness used when chaining layers (13.5).
    float layerThickness[3] = {0.008f, 0.011f, 0.014f};
};

// What the artist gets back — pass/fail with a reason, no engineer in the
// loop (P12). `message` is written for a human reading an import log.
struct FitReport {
    size_t garmentVertices = 0;
    size_t matched = 0;     // passed the confidence gate outright
    size_t welded = 0;      // took weights from a coincident trusted copy
    size_t inpainted = 0;   // no trusted copy anywhere: filled by diffusion
    size_t stranded = 0;    // neither matched nor reachable by diffusion
    size_t authored = 0;    // kept the artist's own weights
    float maxOffset = 0;    // metres, furthest a vertex sits off the surface
    float meanOffset = 0;
    bool ok = false;
    char message[256] = {};
};

// Which body regions a garment vertex of `region` is allowed to bind to: its
// own region plus the ones it legitimately meets at a seam. This is the
// "vertex group" constraint of ADR 0008 expressed against the region table the
// body already ships; when the body delivers explicit groups (B-25, task 13.8)
// this is where they replace it.
uint32_t permittedBindRegions(BodyRegion region);

// ---------------------------------------------------------- the bake -------

// Pushes a surface out along its vertex normals — layer k-1's outer surface,
// which layer k binds against (13.5). Topology is untouched, so the result
// still indexes 1:1 with the surface it came from.
void offsetSurface(const SkinnedMeshData& src, float thickness, SkinnedMeshData& out);

// Bakes one garment against one surface. Rewrites the garment's joint
// influences (unless authored weights are kept) and produces the binding.
//
// `rootBodyHash` is the hash of the body at the root of the layer chain — for
// a layer-0 garment that is `base` itself; for an outer layer it is the body,
// not the garment beneath. Recorded so a mismatch can name the body.
bool fitGarment(const SkinnedMeshData& base, uint64_t rootBodyHash, SkinnedMeshData& garment,
                uint8_t layer, const FitParams& params, GarmentBinding& outBinding,
                FitReport& outReport);

// One garment stacked on a body with nothing between it and the skin.
bool fitGarment(const SkinnedMeshData& body, SkinnedMeshData& garment, uint8_t layer,
                const FitParams& params, GarmentBinding& outBinding, FitReport& outReport);

// A whole stack, innermost first: each layer binds against the previous
// layer's offset surface, so every layer encloses the one beneath it on every
// body the chain is later evaluated for (13.5, CHARACTERS.md §5.4).
//
// `garments` are modified in place (weights); `outBindings` comes back with
// one binding per garment, and `outReports` with one report each.
bool fitGarmentStack(const SkinnedMeshData& body, std::vector<SkinnedMeshData*>& garments,
                     const std::vector<uint8_t>& layers, const FitParams& params,
                     std::vector<GarmentBinding>& outBindings,
                     std::vector<FitReport>& outReports);

}  // namespace mge
