// The skinned-import gates: what the importer refuses, and why.
//
// This file exists because of a specific, expensive failure. The importer used
// to CLAMP UV coordinates that fell outside the 0-1 tile, silently. The
// template body's source unwrap ran past u = 1, so 89.7 % of its triangles
// arrived with zero UV area — and shipped that way through three LODs and six
// committed garments before a measuring tool found it
// (docs/research/uv-audit.md). Nothing failed. Nothing warned.
//
// The architect's ruling on that seam request is the contract these tests hold
// down: the import REFUSES what the vertex format cannot represent, and says
// what it measured. The two fixtures differ in exactly one thing — their UVs —
// so a pass here isolates the gate rather than the mesh.

#include <string>

#include "mge/import/gltf_skin_import.h"
#include "test_framework.h"

using namespace mge;

namespace {

std::string fixture(const char* name) {
    return std::string(MGE_TEST_DATA_DIR) + "/" + name;
}

}  // namespace

MGE_TEST(skin_import_accepts_a_chart_inside_the_tile) {
    SkinnedMeshData mesh;
    std::string error;
    MGE_CHECK(importSkinnedGltf(fixture("skinned_quad.gltf").c_str(), mesh, &error));
    MGE_CHECK(error.empty());
    MGE_CHECK(mesh.vertices.size() == 8);
    MGE_CHECK(mesh.indices.size() == 12);

    // u = 1.0 is INSIDE the tile and must survive as the top of the uint16
    // range. A gate that rejected its own boundary would push every artist to
    // pad their layout away from the edge and waste the sheet.
    bool sawTileEdge = false;
    for (const SkinVertex& v : mesh.vertices) {
        if (v.uv[0] == 65535) sawTileEdge = true;
    }
    MGE_CHECK(sawTileEdge);
}

MGE_TEST(skin_import_refuses_uvs_outside_the_tile_instead_of_clamping) {
    SkinnedMeshData mesh;
    std::string error;
    MGE_CHECK(!importSkinnedGltf(fixture("skinned_quad_uv_outside.gltf").c_str(), mesh, &error));

    // Refusing is only half the contract. The message has to name the measured
    // extent, because that is what tells the artist which export setting is
    // wrong — the clamped data never could.
    MGE_CHECK(error.find("1.9825") != std::string::npos);
    MGE_CHECK(error.find("4 of 8") != std::string::npos);
    MGE_CHECK(error.find("0-1 tile") != std::string::npos);

    // And a refused import leaves nothing behind. Half a body is worse than no
    // body: it is the shape a later stage would happily bake.
    MGE_CHECK(mesh.vertices.empty());
    MGE_CHECK(mesh.indices.empty());
    MGE_CHECK(mesh.parts.empty());
}

MGE_TEST(skin_import_refuses_a_model_that_is_not_on_the_canonical_rig) {
    SkinnedMeshData mesh;
    std::string error;
    MGE_CHECK(!importSkinnedGltf(fixture("cube.gltf").c_str(), mesh, &error));
    MGE_CHECK(!error.empty());
}
