#include "mge/graphics/mesh_io.h"

#include <cstdio>
#include <cstring>

#include "mge/core/log.h"

namespace mge {

namespace {

constexpr char kMagic[4] = {'M', 'G', 'E', 'M'};
constexpr uint32_t kVersion = 1;

struct FileHeader {
    char magic[4];
    uint32_t version;
    uint32_t lodCount;
    float bounds[6];  // min xyz, max xyz
};

struct LodHeader {
    uint32_t vertexCount;
    uint32_t indexCount;
    float switchDistance;  // 0 for the last LOD
    float bounds[6];
};

}  // namespace

bool writeMeshFile(const char* path, const LodMesh& mesh) {
    if (mesh.lods.empty()) return false;
    FILE* f = fopen(path, "wb");
    if (f == nullptr) return false;

    FileHeader header{};
    memcpy(header.magic, kMagic, 4);
    header.version = kVersion;
    header.lodCount = static_cast<uint32_t>(mesh.lods.size());
    header.bounds[0] = mesh.bounds.min.x;
    header.bounds[1] = mesh.bounds.min.y;
    header.bounds[2] = mesh.bounds.min.z;
    header.bounds[3] = mesh.bounds.max.x;
    header.bounds[4] = mesh.bounds.max.y;
    header.bounds[5] = mesh.bounds.max.z;
    fwrite(&header, sizeof(header), 1, f);

    bool ok = true;
    for (size_t i = 0; i < mesh.lods.size(); ++i) {
        const MeshData& lod = mesh.lods[i];
        LodHeader lodHeader{};
        lodHeader.vertexCount = static_cast<uint32_t>(lod.vertices.size());
        lodHeader.indexCount = static_cast<uint32_t>(lod.indices.size());
        lodHeader.switchDistance =
            i < mesh.switchDistances.size() ? mesh.switchDistances[i] : 0.0f;
        lodHeader.bounds[0] = lod.bounds.min.x;
        lodHeader.bounds[1] = lod.bounds.min.y;
        lodHeader.bounds[2] = lod.bounds.min.z;
        lodHeader.bounds[3] = lod.bounds.max.x;
        lodHeader.bounds[4] = lod.bounds.max.y;
        lodHeader.bounds[5] = lod.bounds.max.z;
        ok = ok && fwrite(&lodHeader, sizeof(lodHeader), 1, f) == 1;
        ok = ok && fwrite(lod.vertices.data(), sizeof(Vertex), lod.vertices.size(), f) ==
                       lod.vertices.size();
        ok = ok && fwrite(lod.indices.data(), sizeof(uint32_t), lod.indices.size(), f) ==
                       lod.indices.size();
    }
    fclose(f);
    return ok;
}

bool readMeshFile(const char* path, LodMesh& out) {
    FILE* f = fopen(path, "rb");
    if (f == nullptr) return false;

    FileHeader header{};
    if (fread(&header, sizeof(header), 1, f) != 1 || memcmp(header.magic, kMagic, 4) != 0 ||
        header.version != kVersion || header.lodCount == 0 || header.lodCount > 16) {
        MGE_LOGE("mesh_io", "bad mesh file: %s", path);
        fclose(f);
        return false;
    }

    out = LodMesh{};
    out.bounds.min = {header.bounds[0], header.bounds[1], header.bounds[2]};
    out.bounds.max = {header.bounds[3], header.bounds[4], header.bounds[5]};
    out.lods.resize(header.lodCount);

    bool ok = true;
    for (uint32_t i = 0; i < header.lodCount && ok; ++i) {
        LodHeader lodHeader{};
        ok = fread(&lodHeader, sizeof(lodHeader), 1, f) == 1;
        if (!ok) break;
        MeshData& lod = out.lods[i];
        lod.vertices.resize(lodHeader.vertexCount);
        lod.indices.resize(lodHeader.indexCount);
        ok = fread(lod.vertices.data(), sizeof(Vertex), lodHeader.vertexCount, f) ==
                 lodHeader.vertexCount &&
             fread(lod.indices.data(), sizeof(uint32_t), lodHeader.indexCount, f) ==
                 lodHeader.indexCount;
        lod.bounds.min = {lodHeader.bounds[0], lodHeader.bounds[1], lodHeader.bounds[2]};
        lod.bounds.max = {lodHeader.bounds[3], lodHeader.bounds[4], lodHeader.bounds[5]};
        if (i + 1 < header.lodCount) out.switchDistances.push_back(lodHeader.switchDistance);
    }
    fclose(f);
    if (!ok) out = LodMesh{};
    return ok;
}

}  // namespace mge
