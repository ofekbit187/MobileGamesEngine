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

void append(std::vector<uint8_t>& out, const void* data, size_t size) {
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    out.insert(out.end(), bytes, bytes + size);
}

bool read(const uint8_t*& cursor, size_t& remaining, void* dest, size_t size) {
    if (remaining < size) return false;
    memcpy(dest, cursor, size);
    cursor += size;
    remaining -= size;
    return true;
}

}  // namespace

void serializeMesh(const LodMesh& mesh, std::vector<uint8_t>& out) {
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
    append(out, &header, sizeof(header));

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
        append(out, &lodHeader, sizeof(lodHeader));
        append(out, lod.vertices.data(), lod.vertices.size() * sizeof(Vertex));
        append(out, lod.indices.data(), lod.indices.size() * sizeof(uint32_t));
    }
}

bool deserializeMesh(const uint8_t* data, size_t size, LodMesh& out) {
    const uint8_t* cursor = data;
    size_t remaining = size;

    FileHeader header{};
    if (!read(cursor, remaining, &header, sizeof(header)) ||
        memcmp(header.magic, kMagic, 4) != 0 || header.version != kVersion ||
        header.lodCount == 0 || header.lodCount > 16) {
        return false;
    }

    out = LodMesh{};
    out.bounds.min = {header.bounds[0], header.bounds[1], header.bounds[2]};
    out.bounds.max = {header.bounds[3], header.bounds[4], header.bounds[5]};
    out.lods.resize(header.lodCount);

    for (uint32_t i = 0; i < header.lodCount; ++i) {
        LodHeader lodHeader{};
        if (!read(cursor, remaining, &lodHeader, sizeof(lodHeader))) return false;
        MeshData& lod = out.lods[i];
        lod.vertices.resize(lodHeader.vertexCount);
        lod.indices.resize(lodHeader.indexCount);
        if (!read(cursor, remaining, lod.vertices.data(),
                  lodHeader.vertexCount * sizeof(Vertex)) ||
            !read(cursor, remaining, lod.indices.data(),
                  lodHeader.indexCount * sizeof(uint32_t))) {
            out = LodMesh{};
            return false;
        }
        lod.bounds.min = {lodHeader.bounds[0], lodHeader.bounds[1], lodHeader.bounds[2]};
        lod.bounds.max = {lodHeader.bounds[3], lodHeader.bounds[4], lodHeader.bounds[5]};
        if (i + 1 < header.lodCount) out.switchDistances.push_back(lodHeader.switchDistance);
    }
    return true;
}

bool writeMeshFile(const char* path, const LodMesh& mesh) {
    if (mesh.lods.empty()) return false;
    std::vector<uint8_t> blob;
    serializeMesh(mesh, blob);
    FILE* f = fopen(path, "wb");
    if (f == nullptr) return false;
    const bool ok = fwrite(blob.data(), 1, blob.size(), f) == blob.size();
    fclose(f);
    return ok;
}

bool readMeshFile(const char* path, LodMesh& out) {
    FILE* f = fopen(path, "rb");
    if (f == nullptr) return false;
    fseek(f, 0, SEEK_END);
    const long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0) {
        fclose(f);
        return false;
    }
    std::vector<uint8_t> blob(static_cast<size_t>(size));
    const bool readOk = fread(blob.data(), 1, blob.size(), f) == blob.size();
    fclose(f);
    if (!readOk || !deserializeMesh(blob.data(), blob.size(), out)) {
        MGE_LOGE("mesh_io", "bad mesh file: %s", path);
        return false;
    }
    return true;
}

}  // namespace mge
