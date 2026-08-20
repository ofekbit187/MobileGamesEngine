// Garment fitting bake (Phase 13, tasks 13.1/13.2/13.5) — host-side.
//
// What is proved here is what an artist is promised (P12): import a garment
// modelled against the template and it comes back weighted, bound, and
// layered, with a pass/fail message they can act on and no engineer involved.
//
//   13.1 weights   — trusted matches copy from the body; the confidence gate
//                    rejects the ribcage-under-a-sleeve case; inpainting fills
//                    what the gate rejected.
//   13.2 binding   — at rest the binding reproduces the authored garment
//                    exactly (the identity everything else rests on), and a
//                    sleeve cannot bind to the torso.
//   13.5 layering  — the outer layer encloses the inner one, arithmetically.

#include <cmath>
#include <cstdint>
#include <vector>

#include "mge/character/body_mesh.h"
#include "mge/character/garment_binding.h"
#include "mge/import/garment_fit.h"
#include "test_framework.h"

using namespace mge;

namespace {

// A flat quad facing +Y, in a named region, skinned to a named joint.
SkinnedMeshData quad(Vec3 center, float half, BodyRegion region, Joint joint,
                     float y = 0.0f) {
    SkinnedMeshData mesh;
    const Vec3 corner[4] = {{center.x - half, y, center.z - half},
                            {center.x + half, y, center.z - half},
                            {center.x + half, y, center.z + half},
                            {center.x - half, y, center.z + half}};
    for (const Vec3& p : corner) {
        SkinVertex v;
        v.position = p;
        v.normal = Vec3{0, 1, 0};
        v.joints[0] = static_cast<uint8_t>(joint);
        v.weights[0] = 255;
        mesh.vertices.push_back(v);
    }
    // Wound counter-clockwise seen from +Y, so the geometric normal agrees
    // with the declared vertex normal — a quad that disagrees is exactly the
    // defect the "consistently wound" gate exists to catch.
    mesh.indices = {0, 3, 2, 0, 2, 1};
    MeshPart part;
    part.region = region;
    part.firstIndex = 0;
    part.indexCount = 6;
    mesh.parts.push_back(part);
    mesh.computeBounds();
    return mesh;
}

// Appends `src` into `dst`, keeping its region as its own part.
void append(SkinnedMeshData& dst, const SkinnedMeshData& src) {
    const uint32_t base = static_cast<uint32_t>(dst.vertices.size());
    const uint32_t first = static_cast<uint32_t>(dst.indices.size());
    dst.vertices.insert(dst.vertices.end(), src.vertices.begin(), src.vertices.end());
    for (uint32_t index : src.indices) dst.indices.push_back(index + base);
    for (const MeshPart& part : src.parts) {
        MeshPart moved = part;
        moved.firstIndex = first + part.firstIndex;
        dst.parts.push_back(moved);
    }
    dst.computeBounds();
}

// Strips a mesh's weights back to "nobody rigged this", so the transfer path
// is the one under test rather than the authored-weights shortcut.
void clearWeights(SkinnedMeshData& mesh) {
    for (SkinVertex& v : mesh.vertices) {
        v.joints[0] = v.joints[1] = v.joints[2] = v.joints[3] = 0;
        v.weights[0] = 255;
        v.weights[1] = v.weights[2] = v.weights[3] = 0;
    }
}

bool weightsAreValid(const SkinnedMeshData& mesh) {
    for (const SkinVertex& v : mesh.vertices) {
        int sum = 0;
        int influences = 0;
        for (int i = 0; i < 4; ++i) {
            sum += v.weights[i];
            if (v.weights[i] > 0) ++influences;
            if (v.joints[i] >= kJointCount) return false;
        }
        if (sum != 255) return false;
        if (influences > 4 || influences == 0) return false;
    }
    return true;
}

// The rest-pose surface of a mesh (no morphs) — what a binding is evaluated
// against when the character has default shape.
void restSurface(const SkinnedMeshData& mesh, std::vector<Vec3>& position,
                 std::vector<Vec3>& normal) {
    float rest[kMorphCount] = {};
    morphedSurface(mesh, rest, position, normal);
}

}  // namespace

// ------------------------------------------------------- the identity ------

MGE_TEST(binding_at_rest_reproduces_the_authored_garment) {
    // The property everything else depends on: baking a garment and evaluating
    // its binding on the unchanged body must give back the garment the artist
    // modelled, to within quantization. If this drifts, every garment silently
    // shifts on every character.
    const SkinnedMeshData body = quad(Vec3{0, 0, 0}, 0.5f, BodyRegion::Torso, Joint::Chest);
    SkinnedMeshData garment = quad(Vec3{0, 0, 0}, 0.4f, BodyRegion::Torso, Joint::Chest, 0.02f);
    const std::vector<SkinVertex> authored = garment.vertices;

    GarmentBinding binding;
    FitReport report;
    MGE_CHECK(fitGarment(body, garment, 1, FitParams{}, binding, report));
    MGE_CHECK(binding.binds.size() == garment.vertices.size());

    std::vector<Vec3> position, normal;
    restSurface(body, position, normal);
    std::vector<SkinVertex> evaluated = garment.vertices;
    MGE_CHECK(applyBinding(binding, body, position, normal, evaluated));

    for (size_t i = 0; i < authored.size(); ++i) {
        MGE_CHECK_NEAR(evaluated[i].position.x, authored[i].position.x, 5e-5);
        MGE_CHECK_NEAR(evaluated[i].position.y, authored[i].position.y, 5e-5);
        MGE_CHECK_NEAR(evaluated[i].position.z, authored[i].position.z, 5e-5);
    }
}

MGE_TEST(the_bake_records_the_body_hash_it_was_baked_against) {
    const SkinnedMeshData body = quad(Vec3{0, 0, 0}, 0.5f, BodyRegion::Torso, Joint::Chest);
    SkinnedMeshData garment = quad(Vec3{0, 0, 0}, 0.4f, BodyRegion::Torso, Joint::Chest, 0.02f);

    GarmentBinding binding;
    FitReport report;
    MGE_CHECK(fitGarment(body, garment, 1, FitParams{}, binding, report));
    MGE_CHECK(binding.baseHash == skinnedMeshContentHash(body));
    MGE_CHECK(binding.rootBodyHash == binding.baseHash);
    MGE_CHECK(checkBinding(binding, body, garment.vertices.size()) == BindingCheck::Ok);
}

// -------------------------------------------------------- 13.1 weights -----

MGE_TEST(weights_transfer_from_the_body_when_none_are_authored) {
    SkinnedMeshData body = quad(Vec3{0, 0, 0}, 0.5f, BodyRegion::Torso, Joint::Chest);
    // Give the body two influences so the transfer has something to blend.
    for (SkinVertex& v : body.vertices) {
        v.joints[0] = static_cast<uint8_t>(Joint::Chest);
        v.weights[0] = 128;
        v.joints[1] = static_cast<uint8_t>(Joint::Spine);
        v.weights[1] = 127;
    }

    SkinnedMeshData garment = quad(Vec3{0, 0, 0}, 0.4f, BodyRegion::Torso, Joint::Chest, 0.01f);
    clearWeights(garment);

    FitParams params;
    params.acceptAuthoredWeights = false;
    GarmentBinding binding;
    FitReport report;
    MGE_CHECK(fitGarment(body, garment, 1, params, binding, report));
    MGE_CHECK(report.matched == garment.vertices.size());
    MGE_CHECK(report.stranded == 0);
    MGE_CHECK(weightsAreValid(garment));

    // The garment now carries the body's two influences, not the placeholder.
    bool sawChest = false, sawSpine = false;
    for (const SkinVertex& v : garment.vertices) {
        for (int i = 0; i < 4; ++i) {
            if (v.weights[i] == 0) continue;
            if (v.joints[i] == static_cast<uint8_t>(Joint::Chest)) sawChest = true;
            if (v.joints[i] == static_cast<uint8_t>(Joint::Spine)) sawSpine = true;
        }
    }
    MGE_CHECK(sawChest && sawSpine);
}

MGE_TEST(authored_weights_are_kept_when_the_artist_supplied_them) {
    SkinnedMeshData body = quad(Vec3{0, 0, 0}, 0.5f, BodyRegion::Torso, Joint::Chest);
    SkinnedMeshData garment = quad(Vec3{0, 0, 0}, 0.4f, BodyRegion::Torso, Joint::Hips, 0.01f);
    for (SkinVertex& v : garment.vertices) {
        v.joints[0] = static_cast<uint8_t>(Joint::Hips);
        v.weights[0] = 200;
        v.joints[1] = static_cast<uint8_t>(Joint::Spine);
        v.weights[1] = 55;
    }

    GarmentBinding binding;
    FitReport report;
    MGE_CHECK(fitGarment(body, garment, 1, FitParams{}, binding, report));
    MGE_CHECK(report.authored == garment.vertices.size());
    // Untouched: a deliberate rig is never overwritten by the transfer.
    MGE_CHECK(garment.vertices[0].joints[0] == static_cast<uint8_t>(Joint::Hips));
    MGE_CHECK(garment.vertices[0].weights[0] == 200);
}

MGE_TEST(the_confidence_gate_rejects_a_far_match_and_inpainting_fills_it) {
    // The documented failure: part of a garment hangs far enough from the body
    // that the nearest surface point is the wrong body part. The gate must not
    // trust it, and diffusion across the garment must weight it anyway.
    SkinnedMeshData body = quad(Vec3{0, 0, 0}, 0.5f, BodyRegion::Torso, Joint::Chest);
    for (SkinVertex& v : body.vertices) {
        v.joints[0] = static_cast<uint8_t>(Joint::Chest);
        v.weights[0] = 255;
    }

    // A garment whose first quad hugs the body and whose second quad hangs
    // 40 cm away — beyond the trust distance.
    SkinnedMeshData garment = quad(Vec3{0, 0, 0}, 0.4f, BodyRegion::Torso, Joint::Chest, 0.01f);
    SkinnedMeshData hanging =
        quad(Vec3{0, 0, 0}, 0.4f, BodyRegion::Torso, Joint::Chest, 0.40f);
    // Stitch the hanging quad onto the first so diffusion has a path to it.
    const uint32_t base = static_cast<uint32_t>(garment.vertices.size());
    garment.vertices.insert(garment.vertices.end(), hanging.vertices.begin(),
                            hanging.vertices.end());
    for (uint32_t i = 0; i < 4; ++i) {
        garment.indices.push_back(i);
        garment.indices.push_back(base + i);
        garment.indices.push_back(base + ((i + 1) % 4));
    }
    garment.parts[0].indexCount = static_cast<uint32_t>(garment.indices.size());
    clearWeights(garment);

    FitParams params;
    params.acceptAuthoredWeights = false;
    params.maxDistance = 0.10f;  // the hanging quad is well beyond this
    GarmentBinding binding;
    FitReport report;
    const bool ok = fitGarment(body, garment, 1, params, binding, report);

    MGE_CHECK(report.matched > 0);              // the hugging part is trusted
    MGE_CHECK(report.matched < garment.vertices.size());  // the hanging part is not
    MGE_CHECK(report.inpainted > 0);            // and diffusion reached it
    MGE_CHECK(ok);
    MGE_CHECK(report.stranded == 0);
    MGE_CHECK(weightsAreValid(garment));
}

MGE_TEST(a_report_explains_a_failure_in_words_an_artist_can_act_on) {
    // P12: the gate is self-serve. A garment modelled somewhere else entirely
    // must come back with a reason, not a silent mis-fit.
    const SkinnedMeshData body = quad(Vec3{0, 0, 0}, 0.5f, BodyRegion::Torso, Joint::Chest);
    SkinnedMeshData garment =
        quad(Vec3{40.0f, 0, 40.0f}, 0.4f, BodyRegion::Torso, Joint::Chest, 30.0f);
    clearWeights(garment);

    FitParams params;
    params.acceptAuthoredWeights = false;
    GarmentBinding binding;
    FitReport report;
    MGE_CHECK(!fitGarment(body, garment, 1, params, binding, report));
    MGE_CHECK(report.matched == 0);
    MGE_CHECK(report.message[0] != '\0');
    MGE_CHECK(std::string(report.message).find("FAILED") != std::string::npos);
}

// -------------------------------------------------------- 13.2 binding -----

MGE_TEST(a_sleeve_cannot_bind_to_the_torso) {
    // The armpit case. Torso and arm surfaces are millimetres apart, and the
    // arm's is slightly FURTHER — an unconstrained search binds the sleeve to
    // the ribcage and the sleeve tears off when the arm swings.
    SkinnedMeshData body;
    append(body, quad(Vec3{0, 0, 0}, 0.20f, BodyRegion::Torso, Joint::Chest, 0.00f));
    append(body, quad(Vec3{0, 0, 0}, 0.20f, BodyRegion::ArmL, Joint::UpperArmL, -0.05f));

    // A sleeve vertex sitting just above the torso quad, but declared ArmL.
    SkinnedMeshData sleeve = quad(Vec3{0, 0, 0}, 0.10f, BodyRegion::ArmL, Joint::UpperArmL,
                                  0.01f);
    clearWeights(sleeve);

    FitParams params;
    params.acceptAuthoredWeights = false;
    params.maxDistance = 0.50f;
    GarmentBinding binding;
    FitReport report;
    MGE_CHECK(fitGarment(body, sleeve, 1, params, binding, report));

    // Every bind must land on an ArmL triangle — triangles 2 and 3 of the
    // combined body — never on the torso, however close the torso is.
    for (const SurfaceBind& bind : binding.binds) {
        MGE_CHECK(bind.triangle >= 2);
    }
    // And the transferred weights come from the arm joint, not the chest.
    for (const SkinVertex& v : sleeve.vertices) {
        MGE_CHECK(v.joints[0] == static_cast<uint8_t>(Joint::UpperArmL));
    }
}

// ------------------------------------------------------- 13.5 layering -----

MGE_TEST(an_outer_layer_encloses_the_inner_one) {
    const SkinnedMeshData body = quad(Vec3{0, 0, 0}, 0.5f, BodyRegion::Torso, Joint::Chest);
    SkinnedMeshData tunic = quad(Vec3{0, 0, 0}, 0.45f, BodyRegion::Torso, Joint::Chest, 0.005f);
    SkinnedMeshData coat = quad(Vec3{0, 0, 0}, 0.45f, BodyRegion::Torso, Joint::Chest, 0.010f);

    std::vector<SkinnedMeshData*> stack = {&tunic, &coat};
    const std::vector<uint8_t> layers = {1, 2};
    FitParams params;
    std::vector<GarmentBinding> bindings;
    std::vector<FitReport> reports;
    MGE_CHECK(fitGarmentStack(body, stack, layers, params, bindings, reports));
    MGE_CHECK(bindings.size() == 2);

    // Layer 0 binds to the body; layer 1 binds to the tunic's offset surface,
    // NOT to the body — that is what makes enclosure arithmetic.
    MGE_CHECK(bindings[0].baseHash == skinnedMeshContentHash(body));
    MGE_CHECK(bindings[1].baseHash != skinnedMeshContentHash(body));
    // Both still name the same body at the root of the chain.
    MGE_CHECK(bindings[0].rootBodyHash == skinnedMeshContentHash(body));
    MGE_CHECK(bindings[1].rootBodyHash == skinnedMeshContentHash(body));

    // The coat sits outside the tunic, which sits outside the skin.
    MGE_CHECK(coat.vertices[0].position.y > tunic.vertices[0].position.y);
    MGE_CHECK(tunic.vertices[0].position.y > body.vertices[0].position.y);
}

MGE_TEST(offset_surface_pushes_out_without_changing_topology) {
    const SkinnedMeshData src = quad(Vec3{0, 0, 0}, 0.5f, BodyRegion::Torso, Joint::Chest);
    SkinnedMeshData out;
    offsetSurface(src, 0.03f, out);
    MGE_CHECK(out.vertices.size() == src.vertices.size());
    MGE_CHECK(out.indices == src.indices);
    for (size_t i = 0; i < out.vertices.size(); ++i) {
        MGE_CHECK_NEAR(out.vertices[i].position.y, src.vertices[i].position.y + 0.03f, 1e-5);
    }
}

// ------------------------------------------------ against the real body ----

MGE_TEST(the_shipped_garments_fit_the_shipped_body) {
    // The end-to-end shape of the thing, on the actual delivered assets: bake
    // a real garment against the real template and evaluate it back.
    const std::vector<SkinnedMeshData>& lods = sharedTemplateLods();
    if (lods.empty() || lods[0].vertices.empty()) return;  // assets not present
    const SkinnedMeshData& body = lods[0];

    const WearableKind kinds[] = {WearableKind::Tunic, WearableKind::Pants,
                                 WearableKind::Boots, WearableKind::HairShort};
    for (WearableKind kind : kinds) {
        SkinnedMeshData garment = sharedGarment(kind);
        if (garment.vertices.empty()) continue;
        const std::vector<SkinVertex> authored = garment.vertices;

        FitParams params;
        params.acceptAuthoredWeights = false;  // exercise the transfer path
        GarmentBinding binding;
        FitReport report;
        const bool ok = fitGarment(body, garment, 1, params, binding, report);
        if (!ok) printf("  fit report: %s\n", report.message);
        MGE_CHECK(ok);
        MGE_CHECK(weightsAreValid(garment));
        MGE_CHECK(binding.binds.size() == garment.vertices.size());

        // At rest the binding reproduces the authored surface.
        std::vector<Vec3> position, normal;
        restSurface(body, position, normal);
        std::vector<SkinVertex> evaluated = garment.vertices;
        MGE_CHECK(applyBinding(binding, body, position, normal, evaluated));
        float worst = 0;
        for (size_t i = 0; i < authored.size(); ++i) {
            worst = std::max(worst, (evaluated[i].position - authored[i].position).length());
        }
        MGE_CHECK(worst < 1e-3f);  // sub-millimetre: quantization only
    }
}

MGE_TEST(the_normal_gate_is_what_keeps_a_sleeve_off_the_ribcage) {
    // Locks the reason ~half of a closed garment inpaints, so nobody "fixes"
    // the number by turning the gate off. With the gate disabled every vertex
    // matches — including the ones that would take the wrong body part's
    // weights. The high inpaint count is the gate working, not failing.
    const std::vector<SkinnedMeshData>& lods = sharedTemplateLods();
    if (lods.empty() || lods[0].vertices.empty()) return;
    const SkinnedMeshData& body = lods[0];
    if (sharedGarment(WearableKind::Tunic).vertices.empty()) return;

    FitParams gated;
    gated.acceptAuthoredWeights = false;
    FitParams ungated = gated;
    ungated.minNormalDot = -1.0f;  // accept any orientation

    SkinnedMeshData a = sharedGarment(WearableKind::Tunic);
    SkinnedMeshData b = sharedGarment(WearableKind::Tunic);
    GarmentBinding bindA, bindB;
    FitReport reportA, reportB;
    MGE_CHECK(fitGarment(body, a, 1, gated, bindA, reportA));
    MGE_CHECK(fitGarment(body, b, 1, ungated, bindB, reportB));

    // Ungated: everything "matches" — which is exactly the danger.
    MGE_CHECK(reportB.matched == reportB.garmentVertices);
    // Gated: the inner wall is refused and inpainted instead.
    MGE_CHECK(reportA.matched < reportA.garmentVertices);
    MGE_CHECK(reportA.inpainted > 0);
    // Either way the garment ends up fully weighted and valid.
    MGE_CHECK(reportA.stranded == 0);
    MGE_CHECK(weightsAreValid(a));
}
