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
