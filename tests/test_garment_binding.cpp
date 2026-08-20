// Garment surface binding at runtime (Phase 13, tasks 13.3/13.4).
//
// The claims under test are the ones the fitting guarantee rests on:
//   * the contract hash notices when the body moves on, and refuses (13.3);
//   * a bound garment FOLLOWS a morph instead of ignoring it (13.4) — the gap
//     `buildPosedCharacter` documented before this landed;
//   * the re-fit is cached per (garment, morph-set) and the cache refuses at
//     capacity instead of growing (P1).

#include <cmath>
#include <cstdint>
#include <vector>

#include "mge/character/garment_binding.h"
#include "mge/core/jobs.h"
#include "test_framework.h"

using namespace mge;

namespace {

// A two-triangle quad in the XZ plane, facing +Y, with a morph that lifts it.
SkinnedMeshData quadSurface(float y = 0.0f) {
    SkinnedMeshData mesh;
    const float half = 0.5f;
    const Vec3 corner[4] = {{-half, y, -half}, {half, y, -half}, {half, y, half}, {-half, y, half}};
    for (const Vec3& p : corner) {
        SkinVertex v;
        v.position = p;
        v.normal = Vec3{0, 1, 0};
        v.joints[0] = 0;
        v.weights[0] = 255;
        mesh.vertices.push_back(v);
    }
    // Wound counter-clockwise seen from +Y, so the geometric normal agrees
    // with the declared vertex normal — a quad that disagrees is exactly the
    // defect the "consistently wound" gate exists to catch.
    mesh.indices = {0, 3, 2, 0, 2, 1};
    MeshPart part;
    part.region = BodyRegion::Torso;
    part.firstIndex = 0;
    part.indexCount = 6;
    mesh.parts.push_back(part);
    mesh.computeBounds();
    return mesh;
}

// Lifts every vertex of the quad by `metres` when the morph is at weight 1.
void addLiftMorph(SkinnedMeshData& mesh, float metres) {
    MorphTarget target;
    target.morph = Morph::BodyBelly;
    target.scale = 1.0f;
    for (uint16_t i = 0; i < static_cast<uint16_t>(mesh.vertices.size()); ++i) {
        MorphDelta d;
        d.vertex = i;
        d.position[1] = static_cast<int16_t>(std::lround(metres / target.scale * 32767.0f));
        target.deltas.push_back(d);
    }
    mesh.morphs.push_back(target);
}

// A garment vertex hovering `offset` above the middle of the quad, bound to
// the triangle under it. Built by hand so the runtime half can be tested
// without dragging the bake in.
GarmentBinding handBinding(const SkinnedMeshData& base, float offset, size_t vertexCount) {
    GarmentBinding binding;
    binding.baseHash = skinnedMeshContentHash(base);
    binding.rootBodyHash = binding.baseHash;
    binding.baseVertexCount = static_cast<uint32_t>(base.vertices.size());
    binding.baseTriangleCount = static_cast<uint32_t>(base.triangleCount());
    binding.offsetScale = 0.5f;
    binding.layer = 1;
    binding.binds.resize(vertexCount);
    for (SurfaceBind& bind : binding.binds) {
        bind.triangle = 0;
        bind.bary[0] = 65535 / 3;  // roughly the centroid
        bind.bary[1] = 65535 / 3;
        // Offset along the triangle's normal axis (index 2 of the frame).
        bind.offset[2] = static_cast<int16_t>(std::lround(offset / 0.5f * 32767.0f));
    }
    return binding;
}

std::vector<SkinVertex> garmentVertices(size_t count) {
    std::vector<SkinVertex> vertices(count);
    for (SkinVertex& v : vertices) {
        v.position = Vec3{0, 0, 0};
        v.normal = Vec3{0, 1, 0};
        v.weights[0] = 255;
    }
    return vertices;
}

}  // namespace

// ------------------------------------------------------------- the hash ----

MGE_TEST(content_hash_is_stable_and_sensitive) {
    const SkinnedMeshData a = quadSurface();
    const SkinnedMeshData b = quadSurface();
    MGE_CHECK(skinnedMeshContentHash(a) == skinnedMeshContentHash(b));

    // A moved vertex is a different body — that is the whole point (B-14).
    SkinnedMeshData moved = quadSurface();
    moved.vertices[0].position.x += 0.001f;
    MGE_CHECK(skinnedMeshContentHash(moved) != skinnedMeshContentHash(a));

    // So is a reordered index buffer, even with identical geometry.
    SkinnedMeshData reordered = quadSurface();
    std::swap(reordered.indices[0], reordered.indices[1]);
    MGE_CHECK(skinnedMeshContentHash(reordered) != skinnedMeshContentHash(a));
}

MGE_TEST(adding_a_morph_does_not_invalidate_bindings) {
    // Deliberate asymmetry: topology is the contract, shape parameters are not.
    // Adding a morph target must not force a re-bake of the whole catalogue.
    const SkinnedMeshData bare = quadSurface();
    SkinnedMeshData withMorph = quadSurface();
    addLiftMorph(withMorph, 0.1f);
    MGE_CHECK(skinnedMeshContentHash(bare) == skinnedMeshContentHash(withMorph));
}

// ------------------------------------------------------------ the refusal --

MGE_TEST(binding_refuses_when_the_body_moves_on) {
    const SkinnedMeshData base = quadSurface();
    const GarmentBinding binding = handBinding(base, 0.02f, 3);
    MGE_CHECK(checkBinding(binding, base, 3) == BindingCheck::Ok);

    // The body changed underneath a baked binding: refuse, loudly (13.3).
    SkinnedMeshData changed = quadSurface();
    changed.vertices[2].position.y += 0.01f;
    MGE_CHECK(checkBinding(binding, changed, 3) == BindingCheck::BaseHashMismatch);

    // A binding from another garment.
    MGE_CHECK(checkBinding(binding, base, 9) == BindingCheck::GarmentSizeMismatch);
}

// --------------------------------------------------------- the whole point --

MGE_TEST(a_bound_garment_follows_a_body_morph) {
    SkinnedMeshData base = quadSurface();
    addLiftMorph(base, 0.10f);  // belly morph lifts the surface 10 cm at weight 1

    const float thickness = 0.02f;
    const GarmentBinding binding = handBinding(base, thickness, 4);

    std::vector<Vec3> position, normal;
    std::vector<SkinVertex> garment = garmentVertices(4);

    // Unmorphed: the garment sits exactly `thickness` above the surface.
    float rest[kMorphCount] = {};
    morphedSurface(base, rest, position, normal);
    MGE_CHECK(applyBinding(binding, base, position, normal, garment));
    const float restY = garment[0].position.y;
    MGE_CHECK_NEAR(restY, thickness, 1e-4);

    // Morphed: the surface rises 10 cm, and the garment rises WITH it, still
    // holding its thickness. Before this task the garment stayed put and the
    // belly came through the tunic.
    float belly[kMorphCount] = {};
    belly[static_cast<size_t>(Morph::BodyBelly)] = 1.0f;
    morphedSurface(base, belly, position, normal);
    MGE_CHECK(applyBinding(binding, base, position, normal, garment));
    MGE_CHECK_NEAR(garment[0].position.y, 0.10f + thickness, 1e-4);

    // Thickness is preserved, not scaled away: that is what keeps a coat off
    // the skin on a heavy body.
    MGE_CHECK_NEAR(garment[0].position.y - 0.10f, thickness, 1e-4);
}

MGE_TEST(a_half_weight_morph_moves_the_garment_half_way) {
    SkinnedMeshData base = quadSurface();
    addLiftMorph(base, 0.10f);
    const GarmentBinding binding = handBinding(base, 0.0f, 2);

    std::vector<Vec3> position, normal;
    std::vector<SkinVertex> garment = garmentVertices(2);
    float half[kMorphCount] = {};
    half[static_cast<size_t>(Morph::BodyBelly)] = 0.5f;
    morphedSurface(base, half, position, normal);
    MGE_CHECK(applyBinding(binding, base, position, normal, garment));
    MGE_CHECK_NEAR(garment[0].position.y, 0.05f, 1e-3);
}

// ------------------------------------------------------------- the cache ---

MGE_TEST(fit_cache_serves_repeats_and_separates_morph_sets) {
    SkinnedMeshData base = quadSurface();
    addLiftMorph(base, 0.10f);

    SkinnedMeshData garment;
    garment.vertices = garmentVertices(4);
    const GarmentBinding binding = handBinding(base, 0.02f, 4);

    GarmentFitCache cache;
    cache.configure(nullptr);  // inline: tools and tests take this path

    float rest[kMorphCount] = {};
    float belly[kMorphCount] = {};
    belly[static_cast<size_t>(Morph::BodyBelly)] = 1.0f;

    const std::vector<SkinVertex>* fitted = nullptr;
    MGE_CHECK(cache.request(7, binding, base, garment, rest, &fitted) ==
              GarmentFitCache::Status::Ready);
    MGE_CHECK(fitted != nullptr && fitted->size() == 4);
    const float restY = (*fitted)[0].position.y;

    // Same character shape again: served from the cache, no new entry.
    const size_t afterFirst = cache.residentCount();
    MGE_CHECK(cache.request(7, binding, base, garment, rest, &fitted) ==
              GarmentFitCache::Status::Ready);
    MGE_CHECK(cache.residentCount() == afterFirst);

    // A different shape is a different fit, and gets its own entry.
    MGE_CHECK(cache.request(7, binding, base, garment, belly, &fitted) ==
              GarmentFitCache::Status::Ready);
    MGE_CHECK(fitted != nullptr);
    MGE_CHECK((*fitted)[0].position.y > restY + 0.09f);
    MGE_CHECK(cache.residentCount() == afterFirst + 1);
}

MGE_TEST(fit_cache_is_bounded_and_refuses_a_bad_binding) {
    SkinnedMeshData base = quadSurface();
    addLiftMorph(base, 0.10f);
    SkinnedMeshData garment;
    garment.vertices = garmentVertices(4);
    const GarmentBinding binding = handBinding(base, 0.02f, 4);

    GarmentFitCache cache;
    cache.configure(nullptr);

    // More distinct morph-sets than the cache holds: it evicts, it never grows.
    const std::vector<SkinVertex>* fitted = nullptr;
    for (int i = 0; i < static_cast<int>(GarmentFitCache::kCapacity) * 2; ++i) {
        float weights[kMorphCount] = {};
        weights[static_cast<size_t>(Morph::BodyBelly)] = static_cast<float>(i) * 0.01f;
        MGE_CHECK(cache.request(1, binding, base, garment, weights, &fitted) ==
                  GarmentFitCache::Status::Ready);
    }
    MGE_CHECK(cache.residentCount() <= GarmentFitCache::kCapacity);

    // A binding whose body moved on is refused with a reason an artist can
    // read, not silently mis-fitted (P12).
    SkinnedMeshData changed = quadSurface();
    addLiftMorph(changed, 0.10f);
    changed.vertices[1].position.z += 0.02f;
    float rest[kMorphCount] = {};
    MGE_CHECK(cache.request(1, binding, changed, garment, rest, &fitted) ==
              GarmentFitCache::Status::Refused);
    MGE_CHECK(fitted == nullptr);
    MGE_CHECK(cache.refusedCount() > 0);
}

MGE_TEST(fit_runs_on_a_job_lane_and_never_blocks_the_caller) {
    SkinnedMeshData base = quadSurface();
    addLiftMorph(base, 0.10f);
    SkinnedMeshData garment;
    garment.vertices = garmentVertices(4);
    const GarmentBinding binding = handBinding(base, 0.02f, 4);

    JobSystem jobs;
    GarmentFitCache cache;
    cache.configure(&jobs);

    float rest[kMorphCount] = {};
    const std::vector<SkinVertex>* fitted = nullptr;
    // A miss answers Pending immediately — the work is on the Decode lane,
    // the caller is not waiting on it.
    const GarmentFitCache::Status first =
        cache.request(3, binding, base, garment, rest, &fitted);
    MGE_CHECK(first == GarmentFitCache::Status::Pending);

    jobs.drain(Lane::Decode);

    MGE_CHECK(cache.request(3, binding, base, garment, rest, &fitted) ==
              GarmentFitCache::Status::Ready);
    MGE_CHECK(fitted != nullptr && fitted->size() == 4);
    MGE_CHECK_NEAR((*fitted)[0].position.y, 0.02f, 1e-4);
}

// ---------------------------------------------------------------- I/O ------

MGE_TEST(binding_round_trips_through_its_file_form) {
    const SkinnedMeshData base = quadSurface();
    const GarmentBinding binding = handBinding(base, 0.017f, 5);

    std::vector<uint8_t> bytes;
    serializeBinding(binding, bytes);
    GarmentBinding restored;
    MGE_CHECK(deserializeBinding(bytes.data(), bytes.size(), restored));
    MGE_CHECK(restored.baseHash == binding.baseHash);
    MGE_CHECK(restored.rootBodyHash == binding.rootBodyHash);
    MGE_CHECK(restored.layer == binding.layer);
    MGE_CHECK(restored.binds.size() == binding.binds.size());
    MGE_CHECK_NEAR(restored.offsetScale, binding.offsetScale, 1e-6);
    for (size_t i = 0; i < restored.binds.size(); ++i) {
        MGE_CHECK(restored.binds[i].triangle == binding.binds[i].triangle);
        MGE_CHECK(restored.binds[i].offset[2] == binding.binds[i].offset[2]);
    }

    // Truncated or foreign data is rejected, never half-read.
    MGE_CHECK(!deserializeBinding(bytes.data(), 8, restored));
    std::vector<uint8_t> garbage(64, 0xAB);
    MGE_CHECK(!deserializeBinding(garbage.data(), garbage.size(), restored));
}

// ------------------------------------------- the shipped catalogue ---------

MGE_TEST(garment_bindings_match_the_shipped_body) {
    // The contract gate, made visible (ADR 0008, task 13.3). The committed
    // `.mgefit` files were baked against the committed body; if the body
    // changes, every one of them is invalid and must be re-baked. This test
    // is the reminder — when it fails, run `mge_garment_fit`. It failing is
    // the system working: a contract-version event announcing itself instead
    // of characters quietly wearing mis-fitted clothes.
    const std::vector<SkinnedMeshData>& lods = sharedTemplateLods();
    if (lods.empty() || lods[0].vertices.empty()) return;  // assets not present

    const WearableKind kinds[] = {WearableKind::Tunic,     WearableKind::Pants,
                                  WearableKind::Boots,     WearableKind::HairShort,
                                  WearableKind::HairLong,  WearableKind::Armor};
    for (WearableKind kind : kinds) {
        const SkinnedMeshData& garment = sharedGarment(kind);
        if (garment.vertices.empty()) continue;
        const GarmentBinding& binding = sharedGarmentBinding(kind);
        MGE_CHECK(!binding.empty());
        if (binding.empty()) continue;
        const BindingCheck check = checkBinding(binding, lods[0], garment.vertices.size());
        if (check != BindingCheck::Ok) {
            printf("  garment %d: %s\n", static_cast<int>(kind), bindingCheckReason(check));
        }
        MGE_CHECK(check == BindingCheck::Ok);
    }
}

MGE_TEST(a_heavy_character_pushes_its_tunic_out) {
    // End to end, on the shipped assets and through the path a game actually
    // calls: the same tunic on a lean body and a heavy one. Before the binding
    // landed these were identical meshes and the belly came through the cloth.
    const std::vector<SkinnedMeshData>& lods = sharedTemplateLods();
    if (lods.empty() || lods[0].vertices.empty()) return;
    if (sharedGarment(WearableKind::Tunic).vertices.empty()) return;
    if (sharedGarmentBinding(WearableKind::Tunic).empty()) return;

    const WearableInstance outfit[] = {{WearableKind::Tunic, 1, false, {1, 1, 1, 1}}};
    Pose pose;
    for (size_t j = 0; j < kJointCount; ++j) pose.rotation[j] = Quat{0, 0, 0, 1};

    HumanoidVariant lean;
    lean.belly = -1.0f;
    HumanoidVariant heavy;
    heavy.belly = 1.0f;

    std::vector<CharacterPiece> leanPieces, heavyPieces;
    buildPosedCharacter(lean, outfit, 1, pose, BodyLod::Lod0, leanPieces);
    buildPosedCharacter(heavy, outfit, 1, pose, BodyLod::Lod0, heavyPieces);

    MGE_CHECK(leanPieces.size() == 2);   // body + tunic
    MGE_CHECK(heavyPieces.size() == 2);
    if (leanPieces.size() < 2 || heavyPieces.size() < 2) return;

    const MeshData& leanTunic = leanPieces[1].mesh;
    const MeshData& heavyTunic = heavyPieces[1].mesh;
    MGE_CHECK(leanTunic.vertices.size() == heavyTunic.vertices.size());

    // The tunic is a different shape on the two bodies — it followed.
    float moved = 0;
    for (size_t i = 0; i < leanTunic.vertices.size(); ++i) {
        moved = std::max(moved,
                         (heavyTunic.vertices[i].position - leanTunic.vertices[i].position)
                             .length());
    }
    MGE_CHECK(moved > 0.005f);  // at least 5 mm somewhere on the garment

    // And it moved OUTWARD at the waist: the heavy body's tunic is wider in Z
    // (depth) than the lean one's, rather than merely displaced.
    float leanDepth = 0, heavyDepth = 0;
    for (size_t i = 0; i < leanTunic.vertices.size(); ++i) {
        leanDepth = std::max(leanDepth, std::fabs(leanTunic.vertices[i].position.z));
        heavyDepth = std::max(heavyDepth, std::fabs(heavyTunic.vertices[i].position.z));
    }
    MGE_CHECK(heavyDepth > leanDepth);
}
