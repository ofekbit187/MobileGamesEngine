// Host-only: glTF import (the runtime never parses glTF — task 2.5).

#include <string>

#include "mge/import/gltf_import.h"
#include "test_framework.h"

using namespace mge;

#ifndef MGE_TEST_DATA_DIR
#define MGE_TEST_DATA_DIR "tests/data"
#endif

MGE_TEST(gltf_import_cube) {
    LodMesh mesh;
    std::string error;
    const std::string path = std::string(MGE_TEST_DATA_DIR) + "/cube.gltf";
    MGE_CHECK(importGltf(path.c_str(), mesh, &error));
    if (!error.empty()) printf("  import error: %s\n", error.c_str());

    MGE_CHECK(mesh.lods.size() == 1);
    MGE_CHECK(mesh.lods[0].vertices.size() == 24);
    MGE_CHECK(mesh.lods[0].indices.size() == 36);
    // Unit cube bounds.
    MGE_CHECK_NEAR(mesh.bounds.min.x, -0.5f, 1e-6);
    MGE_CHECK_NEAR(mesh.bounds.max.y, 0.5f, 1e-6);
    // Normals imported (unit length).
    for (const Vertex& v : mesh.lods[0].vertices) {
        MGE_CHECK_NEAR(v.normal.length(), 1.0f, 1e-4);
    }
}

MGE_TEST(gltf_import_missing_file) {
    LodMesh mesh;
    std::string error;
    MGE_CHECK(!importGltf("/nonexistent/model.gltf", mesh, &error));
    MGE_CHECK(!error.empty());
}

MGE_TEST(gltf_import_carries_tiling_uvs) {
    // Static world geometry tiles: a chart that runs 0..4 is a four-times
    // repeat, not a defect, and the static vertex stream must carry it
    // unchanged (mesh_data.h). Clamping this to the unit tile is exactly the
    // damage docs/research/uv-audit.md found on the body.
    LodMesh mesh;
    std::string error;
    const std::string path = std::string(MGE_TEST_DATA_DIR) + "/quad_uv_tiling.gltf";
    MGE_CHECK(importGltf(path.c_str(), mesh, &error));
    MGE_CHECK(mesh.lods.size() == 1);
    MGE_CHECK(mesh.lods[0].vertices.size() == 4);

    float maxU = 0, maxV = 0;
    for (const Vertex& v : mesh.lods[0].vertices) {
        maxU = v.uv[0] > maxU ? v.uv[0] : maxU;
        maxV = v.uv[1] > maxV ? v.uv[1] : maxV;
    }
    MGE_CHECK_NEAR(maxU, 4.0f, 1e-5);
    MGE_CHECK_NEAR(maxV, 4.0f, 1e-5);
}

MGE_TEST(gltf_import_refuses_unrepresentable_uvs) {
    // Refuse, never clamp — and put the measurement in the error, so the
    // person who exported it can see WHICH vertex and WHAT value.
    LodMesh mesh;
    std::string error;
    const std::string path = std::string(MGE_TEST_DATA_DIR) + "/quad_uv_broken.gltf";
    MGE_CHECK(!importGltf(path.c_str(), mesh, &error));
    MGE_CHECK(!error.empty());
    // The reason names the vertex and the offending coordinate rather than
    // saying "unsupported primitive data".
    MGE_CHECK(error.find("vertex") != std::string::npos);
    MGE_CHECK(error.find("u") != std::string::npos);
    printf("  refusal: %s\n", error.c_str());
}
