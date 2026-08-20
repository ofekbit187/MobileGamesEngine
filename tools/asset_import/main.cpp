// asset_import: bakes interchange assets (glTF 2.0) into the engine's runtime
// formats (tasks 2.5, 8.12).
//   mge_asset_import in.gltf out.mgemesh            static mesh
//   mge_asset_import --skinned in.glb out.mgeskin   rigged character mesh

#include <cstdio>
#include <cstring>
#include <string>

#include "mge/graphics/mesh_io.h"
#include "mge/import/gltf_import.h"
#include "mge/import/gltf_skin_import.h"

namespace {

int importSkinned(const char* in, const char* out) {
    mge::SkinnedMeshData mesh;
    std::string error;
    if (!mge::importSkinnedGltf(in, mesh, &error)) {
        fprintf(stderr, "skinned import failed: %s\n", error.c_str());
        return 1;
    }
    if (!mge::writeSkinnedMeshFile(out, mesh)) {
        fprintf(stderr, "write failed: %s\n", out);
        return 1;
    }
    size_t morphDeltas = 0;
    for (const mge::MorphTarget& t : mesh.morphs) morphDeltas += t.deltas.size();
    printf("%s -> %s: %zu vertices, %zu triangles, %zu regions, %zu morph targets "
           "(%zu deltas, %zu B)\n",
           in, out, mesh.vertices.size(), mesh.triangleCount(), mesh.parts.size(),
           mesh.morphs.size(), morphDeltas, morphDeltas * sizeof(mge::MorphDelta));
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc == 4 && std::strcmp(argv[1], "--skinned") == 0) {
        return importSkinned(argv[2], argv[3]);
    }
    if (argc != 3) {
        fprintf(stderr, "usage: %s [--skinned] <in.gltf|in.glb> <out.mgemesh|out.mgeskin>\n",
                argv[0]);
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
