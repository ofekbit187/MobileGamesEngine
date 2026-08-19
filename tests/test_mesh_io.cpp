#include <cstdlib>
#include <cstring>
#include <string>

#include "mge/graphics/mesh_io.h"
#include "mge/graphics/primitives.h"
#include "test_framework.h"

using namespace mge;

namespace {
std::string tmpPath(const char* name) {
    std::string dir = "/tmp";
    if (const char* t = getenv("TMPDIR")) dir = t;
    return dir + "/" + name;
}
}  // namespace

MGE_TEST(mesh_file_roundtrip) {
    LodMesh original;
    original.lods.push_back(makeBox({2, 1, 3}));
    original.lods.push_back(makeBox({2, 1, 3}));  // stand-in coarse LOD
    original.switchDistances = {25.0f};
    original.computeBounds();

    const std::string path = tmpPath("mge_roundtrip.mgemesh");
    MGE_CHECK(writeMeshFile(path.c_str(), original));

    LodMesh loaded;
    MGE_CHECK(readMeshFile(path.c_str(), loaded));
    MGE_CHECK(loaded.lods.size() == 2);
    MGE_CHECK(loaded.switchDistances.size() == 1);
    MGE_CHECK_NEAR(loaded.switchDistances[0], 25.0f, 1e-6);
    MGE_CHECK(loaded.lods[0].vertices.size() == original.lods[0].vertices.size());
    MGE_CHECK(loaded.lods[0].indices.size() == original.lods[0].indices.size());
    MGE_CHECK_NEAR(loaded.bounds.max.z, 1.5f, 1e-6);
    // Bit-exact payload.
    MGE_CHECK(memcmp(loaded.lods[0].vertices.data(), original.lods[0].vertices.data(),
                     original.lods[0].vertices.size() * sizeof(Vertex)) == 0);
    MGE_CHECK(memcmp(loaded.lods[0].indices.data(), original.lods[0].indices.data(),
                     original.lods[0].indices.size() * sizeof(uint32_t)) == 0);
}

MGE_TEST(mesh_file_rejects_garbage) {
    const std::string path = tmpPath("mge_garbage.mgemesh");
    FILE* f = fopen(path.c_str(), "wb");
    fputs("not a mesh file at all", f);
    fclose(f);
    LodMesh loaded;
    MGE_CHECK(!readMeshFile(path.c_str(), loaded));
    MGE_CHECK(!readMeshFile("/nonexistent/x.mgemesh", loaded));
}
