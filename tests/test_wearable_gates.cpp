// The wearables acceptance gates (task 13.10, BODY_CONTRACT.md §9).
//
// `tools/wearable_gates` and these tests run the same code: the tool is how a
// modeller self-serves an answer (P12), and this file is how CI refuses a body
// that would break the wearables system. A gate that only passes in one of the
// two would be worth nothing.
//
// Note what these tests do NOT do: re-assert the body's own modelling standard,
// which `test_body_mesh.cpp` already owns. These are the extra properties a
// body needs in order to be DRESSABLE.

#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include "mge/character/body_mesh.h"
#include "mge/character/garment_binding.h"
#include "mge/import/wearable_gates.h"
#include "test_framework.h"

using namespace mge;

namespace {

const std::vector<SkinnedMeshData>& bodyLods() { return sharedTemplateLods(); }

bool assetsPresent() {
    const std::vector<SkinnedMeshData>& lods = bodyLods();
    return !lods.empty() && !lods[0].vertices.empty();
}

// Every gate must say something an artist can act on, whatever its verdict.
void checkSpeaks(const GateResult& gate) {
    MGE_CHECK(gate.detail[0] != '\0');
    MGE_CHECK(std::strlen(gate.detail) > 30);
    MGE_CHECK(gate.id[0] != '\0');
    MGE_CHECK(gate.name[0] != '\0');
}

}  // namespace

MGE_TEST(the_shipped_body_passes_every_unblocked_wearable_gate) {
    if (!assetsPresent()) return;
    GateReport report;
    runWearableGates(bodyLods(), report);

    MGE_CHECK(report.gates.size() == 7);
    for (const GateResult& gate : report.gates) {
        checkSpeaks(gate);
        if (gate.status == GateStatus::Fail) {
            printf("  %s %s FAILED: %s\n", gate.id, gate.name, gate.detail);
        }
        MGE_CHECK(gate.status != GateStatus::Fail);
    }

    // Exactly one gate is blocked today, and it is the one waiting on the
    // body's hem-loop table (B-11, task 13.8). When that lands this count
    // drops to zero — and if it drops early, someone marked a gate passing
    // that has nothing to check.
    MGE_CHECK(report.countOf(GateStatus::Blocked) == 1);
    MGE_CHECK(report.countOf(GateStatus::Pass) == 6);
}

MGE_TEST(a_blocked_gate_names_the_task_that_would_unblock_it) {
    // P12: "waiting on something" is only useful if it says what. A blocked
    // gate that does not name its dependency sends someone to an engineer.
    if (!assetsPresent()) return;
    const GateResult gate = gateHemLoops(bodyLods()[0]);
    MGE_CHECK(gate.status == GateStatus::Blocked);
    const std::string detail = gate.detail;
    MGE_CHECK(detail.find("13.8") != std::string::npos);
    MGE_CHECK(detail.find("B-11") != std::string::npos);
}

// --------------------------------------------------------------- pits ------

MGE_TEST(pit_measurement_ignores_local_curvature_and_finds_real_gaps) {
    if (!assetsPresent()) return;
    float rest[kMorphCount] = {};
    std::vector<PitMeasurement> pits;
    measurePits(bodyLods()[0], rest, pits);

    MGE_CHECK(!pits.empty());
    // Sorted tightest first — the report and the gate both rely on it.
    for (size_t i = 1; i < pits.size(); ++i) {
        MGE_CHECK(pits[i - 1].millimetres <= pits[i].millimetres);
    }
    // Nothing is reported at zero: a zero would mean the measurement is
    // picking up surfaces that share an edge, which is the mistake that makes
    // this whole gate meaningless (regions of a one-shell body touch at every
    // partition line).
    MGE_CHECK(pits[0].millimetres > 0.1f);

    // The tightest spot on the template is under the jaw — the chin overhangs
    // the throat. If this stops being true the body's head changed shape.
    const bool jaw = (pits[0].a == BodyRegion::Face && pits[0].b == BodyRegion::Neck) ||
                     (pits[0].a == BodyRegion::Neck && pits[0].b == BodyRegion::Face);
    MGE_CHECK(jaw);
    MGE_CHECK(pits[0].height > 1.4f);  // it is a head, not a hip
}

MGE_TEST(the_inner_thigh_is_the_tightest_pit_at_maximum_shape) {
    // The constraint the trouser archetype exists to respect: at full seat and
    // muscle the thighs nearly touch, so trousers must span the crotch as one
    // surface. Two independent per-leg garments would collide there.
    if (!assetsPresent()) return;
    HumanoidVariant heavy;
    heavy.belly = 1;
    heavy.chest = 1;
    heavy.seat = 1;
    heavy.muscle = 1;
    heavy.neck = 1;
    float weights[kMorphCount];
    morphWeights(heavy, weights);

    std::vector<PitMeasurement> pits;
    measurePits(bodyLods()[0], weights, pits);
    MGE_CHECK(!pits.empty());
    MGE_CHECK(pits[0].a == BodyRegion::LegL && pits[0].b == BodyRegion::LegR);
    // Tight, but the shape range stops short of self-intersection.
    MGE_CHECK(pits[0].millimetres > kPitIntersectionMm);
    MGE_CHECK(pits[0].millimetres < kIndependentLayerClearanceMm);
}

MGE_TEST(every_recorded_pit_still_measures_what_was_recorded) {
    // The regression half of B-24. `recordedPits()` is a measurement of the
    // shipped body, so it has to keep matching the shipped body.
    if (!assetsPresent()) return;
    const GateResult gate = gatePitClearance(bodyLods()[0]);
    if (gate.status != GateStatus::Pass) printf("  %s\n", gate.detail);
    MGE_CHECK(gate.status == GateStatus::Pass);

    size_t count = 0;
    const PitMeasurement* recorded = recordedPits(count);
    MGE_CHECK(count > 0);
    for (size_t i = 0; i < count; ++i) {
        MGE_CHECK(recorded[i].millimetres > 0.0f);
        MGE_CHECK(recorded[i].height > 0.0f);
    }
}

// -------------------------------------------------------------- masks ------

MGE_TEST(a_dressed_character_never_shows_the_rim_a_mask_opened) {
    if (!assetsPresent()) return;
    const GateResult gate = gateMaskIntegrity(bodyLods());
    if (gate.status != GateStatus::Pass) printf("  %s\n", gate.detail);
    MGE_CHECK(gate.status == GateStatus::Pass);
    // The gate must say out loud that this body is one shell, because the
    // contract's original wording assumed per-region closed shells and the
    // difference decides what "no hole" can even mean here.
    MGE_CHECK(std::string(gate.detail).find("ONE shell") != std::string::npos);
}

MGE_TEST(a_bald_head_is_geometry_rather_than_a_hole) {
    if (!assetsPresent()) return;
    const GateResult gate = gateScalpCap(bodyLods()[0]);
    if (gate.status != GateStatus::Pass) printf("  %s\n", gate.detail);
    MGE_CHECK(gate.status == GateStatus::Pass);
}

// ------------------------------------------------------- hash & groups -----

MGE_TEST(the_contract_hash_survives_the_shipping_format) {
    // If the hash changed across serialization, a binding baked from an
    // imported body would be refused after that body shipped — the fitting
    // pipeline would refuse every garment for a reason nobody could find.
    if (!assetsPresent()) return;
    const GateResult gate = gateContractHash(bodyLods()[0]);
    if (gate.status != GateStatus::Pass) printf("  %s\n", gate.detail);
    MGE_CHECK(gate.status == GateStatus::Pass);
}

MGE_TEST(region_groups_are_a_clean_partition) {
    // Binding constrains a garment vertex by its region (task 13.2), so a
    // vertex owned by two regions or none can bind to the wrong body part.
    if (!assetsPresent()) return;
    const GateResult gate = gateGroupsAndAnchors(bodyLods()[0]);
    if (gate.status != GateStatus::Pass) printf("  %s\n", gate.detail);
    MGE_CHECK(gate.status == GateStatus::Pass);
}

MGE_TEST(the_body_stays_inside_the_budgets_wearables_assume) {
    if (!assetsPresent()) return;
    const GateResult gate = gateModelStandard(bodyLods());
    if (gate.status != GateStatus::Pass) printf("  %s\n", gate.detail);
    MGE_CHECK(gate.status == GateStatus::Pass);
}

// ------------------------------------------------------ the gates bite -----

MGE_TEST(the_gates_actually_fail_on_a_body_that_deserves_it) {
    // A suite of gates that has only ever seen a good body proves nothing.
    // Break one thing at a time and check the matching gate notices.
    if (!assetsPresent()) return;

    // A vertex influenced by a joint off the canonical rig.
    std::vector<SkinnedMeshData> broken = bodyLods();
    broken[0].vertices[0].joints[0] = static_cast<uint8_t>(kJointCount + 3);
    GateResult gate = gateModelStandard(broken);
    MGE_CHECK(gate.status == GateStatus::Fail);
    MGE_CHECK(std::string(gate.detail).find("canonical rig") != std::string::npos);

    // Weights that do not sum to 255 — a garment inherits these.
    broken = bodyLods();
    broken[0].vertices[5].weights[0] = 3;
    broken[0].vertices[5].weights[1] = 4;
    broken[0].vertices[5].weights[2] = 0;
    broken[0].vertices[5].weights[3] = 0;
    gate = gateModelStandard(broken);
    MGE_CHECK(gate.status == GateStatus::Fail);

    // A region left with no geometry at all: the scalp gate must catch a
    // body whose bald head would be a hole.
    SkinnedMeshData scalpless = bodyLods()[0];
    for (MeshPart& part : scalpless.parts) {
        if (part.region == BodyRegion::Scalp) part.indexCount = 0;
    }
    gate = gateScalpCap(scalpless);
    MGE_CHECK(gate.status == GateStatus::Fail);

    // An empty body is BLOCKED, not failed: nothing to judge is not the same
    // answer as judged and found wanting.
    const SkinnedMeshData empty;
    gate = gateContractHash(empty);
    MGE_CHECK(gate.status == GateStatus::Blocked);
}
