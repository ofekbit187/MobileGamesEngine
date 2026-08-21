#pragma once

// The wearables system's acceptance gates for the humanoid body
// (task 13.10, BODY_CONTRACT.md §9) — host-side tooling.
//
// The body is judged done by MODELING.md's own definition; these are the
// extra gates the WEARABLES system needs, because a body that renders
// beautifully can still be impossible to dress. They exist so that a body
// change is answered by a machine, in millimetres, instead of by someone
// noticing a coat clipping three weeks later.
//
// P12 governs what "done" means here: every gate returns a written reason an
// artist or modeller can act on without an engineer to interpret it. That is
// why each result carries a `detail` string and not just a bool —
// `tools/wearable_gates` prints them, and `tests/test_wearable_gates.cpp`
// asserts them, from the same code.
//
// A gate can also be BLOCKED: the body deliverable it judges has not shipped
// yet. Blocked is not passing and is not failing — it is "cannot be judged",
// and it says which task would unblock it.

#include <cstddef>
#include <cstdint>
#include <vector>

#include "mge/character/body_mesh.h"

namespace mge {

enum class GateStatus : uint8_t {
    Pass = 0,
    Fail,
    Blocked,  // waiting on a body deliverable; `detail` names which
};

const char* gateStatusName(GateStatus status);

struct GateResult {
    const char* id = "";    // the contract clause, e.g. "B-24"
    const char* name = "";  // the gate's name in BODY_CONTRACT.md §9
    GateStatus status = GateStatus::Fail;
    char detail[512] = {};
};

struct GateReport {
    std::vector<GateResult> gates;

    bool allPassed() const {
        for (const GateResult& g : gates) {
            if (g.status != GateStatus::Pass) return false;
        }
        return true;
    }
    size_t countOf(GateStatus status) const {
        size_t n = 0;
        for (const GateResult& g : gates) {
            if (g.status == status) ++n;
        }
        return n;
    }
};

// ------------------------------------------------------- anatomical pits ---

// The tightest place on the body where two surfaces face each other across a
// gap — the armpit, the crotch, the space under the jaw. A garment layer on
// each side of such a pit collides if the gap is smaller than their combined
// thickness, which is why the body has to publish this number.
//
// Measuring it correctly is subtler than it looks, and two wrong ways both
// give confident answers:
//   * comparing triangle CENTROIDS invents gaps that are not there;
//   * measuring between REGIONS gives 0 everywhere, because this body is one
//     shell and neighbouring regions share their boundary edges.
// The measurement that means something excludes points that are close ALONG
// THE SURFACE and keeps the ones that are far along the surface but near in
// space — that is what a pit is. Surface distance is measured in millimetres,
// not mesh hops: a hop crosses a few millimetres on the dense face and a
// couple of centimetres on a thigh.
struct PitMeasurement {
    BodyRegion a = BodyRegion::Torso;
    BodyRegion b = BodyRegion::Torso;
    float millimetres = 0;
    float height = 0;  // y, metres — where to look on the model
};

// How far apart two points must be along the surface before the gap between
// them counts as a pit rather than local curvature.
constexpr float kPitGeodesicRadiusM = 0.12f;

// The clearance a pair needs to carry an INDEPENDENT garment layer on each
// side: two base layers, the thinnest the engine ships.
constexpr float kIndependentLayerClearanceMm = 16.0f;

// Below this two surfaces are touching or passing through each other, which
// IS a defect at any shape: no garment can be fitted into a body that
// intersects itself.
constexpr float kPitIntersectionMm = 0.5f;

// How much tighter than its recorded baseline a pit may drift before the gate
// calls it a regression.
constexpr float kPitToleranceMm = 2.0f;

// The pits measured on the shipped body, minimum across the template and both
// shape extremes. Recorded rather than "fixed": every one of them is human
// anatomy, and a modeller cannot open them without making the body stop
// looking like a person. What they mean for wearables is that two INDEPENDENT
// layers cannot both sit across them — a garment covering both sides has to
// span the pit as one surface, the way real trousers span a crotch.
//
// The tightest, by a distance, is the inner thigh at maximum shape: 1.0 mm.
// Heavy thighs touch, and the shape range is clamped just before they do.
const PitMeasurement* recordedPits(size_t& outCount);

// Measures the tightest pits on a body at one shape. Sorted tightest first.
void measurePits(const SkinnedMeshData& body, const float morphWeights[kMorphCount],
                 std::vector<PitMeasurement>& out);

// --------------------------------------------------------------- gates -----

// 1. Mask integrity — the rim a mask opens is covered by the outfit that
//    opened it. On this body a mask cannot leave a CLOSED surface (it is one
//    shell, so cutting a region opens a boundary); what it must not leave is
//    an UNCOVERED one, and that is judged per outfit rather than per garment.
GateResult gateMaskIntegrity(const std::vector<SkinnedMeshData>& lods);

// 2. Pit clearance (B-24) — the tightest gap between two surfaces that are
//    far apart along the body but near in space, on the template and at both
//    shape extremes, checked against the published baseline above.
GateResult gatePitClearance(const SkinnedMeshData& body);

// 3. Hem-loop table (B-11) — every named loop exists, closed, at its declared
//    height. Blocked until the body ships the loop table (task 13.8).
GateResult gateHemLoops(const SkinnedMeshData& body);

// 4. Scalp-cap fallback (B-9) — with no hair worn the scalp is real, closed
//    geometry rather than a hole. The mechanical half of a gate whose other
//    half is a render someone looks at (MODELING.md §6).
GateResult gateScalpCap(const SkinnedMeshData& body);

// 5. Contract hash (B-14) — hashing is deterministic and survives a
//    serialize/deserialize round trip, so the identity a binding is baked
//    against means the same thing on every machine.
GateResult gateContractHash(const SkinnedMeshData& body);

// 6. Groups & anchors (B-25/B-19) — every vertex belongs to exactly one
//    region, no vertex to none and none to two. The attachment-point half is
//    blocked until the body ships anchors (task 13.9).
GateResult gateGroupsAndAnchors(const SkinnedMeshData& body);

// 7. The model standard the wearables system depends on — skin weights valid
//    and within four influences, LOD triangle budgets, LODs strictly
//    decreasing. The render review stays a looked-at gate.
GateResult gateModelStandard(const std::vector<SkinnedMeshData>& lods);

// All seven, in contract order.
void runWearableGates(const std::vector<SkinnedMeshData>& lods, GateReport& out);

}  // namespace mge
