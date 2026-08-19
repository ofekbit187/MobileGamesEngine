// asset_import: bakes interchange assets (glTF 2.0) into the engine's
// runtime mesh format (task 2.5). Usage: mge_asset_import in.gltf out.mgemesh

#include <cstdio>
#include <string>

#include "mge/graphics/mesh_io.h"
#include "mge/import/gltf_import.h"

int main(int argc, char** argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: %s <in.gltf|in.glb> <out.mgemesh>\n", argv[0]);
        return 2;
    }
    mge::LodMesh mesh;
    std::string error;
    if (!mge::importGltf(argv[1], mesh, &error)) {
        fprintf(stderr, "import failed: %s\n", error.c_str());
        return 1;
    }
    if (!mge::writeMeshFile(argv[2], mesh)) {
        fprintf(stderr, "write failed: %s\n", argv[2]);
        return 1;
    }
    printf("%s -> %s: %zu vertices, %zu indices, %zu LOD(s)\n", argv[1], argv[2],
           mesh.lods[0].vertices.size(), mesh.lods[0].indices.size(), mesh.lods.size());
    return 0;
}
