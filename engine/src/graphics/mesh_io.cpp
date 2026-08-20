#include "mge/graphics/mesh_io.h"

#include <cstdio>
#include <cstring>

#include "mge/core/log.h"

namespace mge {

namespace {

constexpr char kMagic[4] = {'M', 'G', 'E', 'M'};
// v2 widened the vertex from 24 to 32 bytes to carry a texture coordinate.
// v1 files still load — their vertices simply have no UV, which is the honest
// reading of a file written before the engine could texture anything.
constexpr uint32_t kVersion = 2;
constexpr uint32_t kVersionNoUv = 1;

// The v1 vertex, kept so old files can be read rather than rejected.
struct VertexV1 {
    Vec3 position;
    Vec3 normal;
};
static_assert(sizeof(VertexV1) == 24, "v1 vertex layout is frozen by shipped files");

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
        memcmp(header.magic, kMagic, 4) != 0 ||
        (header.version != kVersion && header.version != kVersionNoUv) ||
        header.lodCount == 0 || header.lodCount > 16) {
        return false;
    }
    const bool hasUv = header.version == kVersion;

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
        bool vertexOk = true;
        if (hasUv) {
            vertexOk = read(cursor, remaining, lod.vertices.data(),
                            lodHeader.vertexCount * sizeof(Vertex));
        } else {
            for (uint32_t v = 0; v < lodHeader.vertexCount && vertexOk; ++v) {
                VertexV1 old{};
                vertexOk = read(cursor, remaining, &old, sizeof(old));
                lod.vertices[v] = Vertex{old.position, old.normal, {0, 0}};
            }
        }
        if (!vertexOk || !read(cursor, remaining, lod.indices.data(),
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

// ------------------------------------------------- skinned meshes (8.12) ---

namespace {

constexpr uint32_t kSkinMagic = 0x4B534D47;  // 'GMSK'
constexpr uint32_t kSkinVersion = 2;  // v2 adds morph targets

template <typename T>
void put(std::vector<uint8_t>& out, const T& value) {
    const uint8_t* raw = reinterpret_cast<const uint8_t*>(&value);
    out.insert(out.end(), raw, raw + sizeof(T));
}

template <typename T>
bool take(const uint8_t*& cursor, const uint8_t* end, T& value) {
    if (static_cast<size_t>(end - cursor) < sizeof(T)) return false;
    std::memcpy(&value, cursor, sizeof(T));
    cursor += sizeof(T);
    return true;
}

}  // namespace

void serializeSkinnedMesh(const SkinnedMeshData& mesh, std::vector<uint8_t>& out) {
    out.clear();
    put(out, kSkinMagic);
    put(out, kSkinVersion);
    put(out, static_cast<uint32_t>(mesh.vertices.size()));
    put(out, static_cast<uint32_t>(mesh.indices.size()));
    put(out, static_cast<uint32_t>(mesh.parts.size()));
    put(out, mesh.bounds);
    for (const SkinVertex& v : mesh.vertices) put(out, v);
    for (uint32_t i : mesh.indices) put(out, i);
    for (const MeshPart& p : mesh.parts) {
        put(out, static_cast<uint8_t>(p.region));
        put(out, p.firstIndex);
        put(out, p.indexCount);
    }
    put(out, static_cast<uint32_t>(mesh.morphs.size()));
    for (const MorphTarget& t : mesh.morphs) {
        put(out, static_cast<uint8_t>(t.morph));
        put(out, t.scale);
        put(out, static_cast<uint32_t>(t.deltas.size()));
        for (const MorphDelta& d : t.deltas) put(out, d);
    }
}

bool deserializeSkinnedMesh(const uint8_t* data, size_t size, SkinnedMeshData& out) {
    out.clear();
    const uint8_t* cursor = data;
    const uint8_t* end = data + size;
    uint32_t magic = 0, version = 0, vertexCount = 0, indexCount = 0, partCount = 0;
    if (!take(cursor, end, magic) || magic != kSkinMagic) return false;
    // v1 assets have no morph block; everything before it is byte-identical.
    if (!take(cursor, end, version) || version < 1 || version > kSkinVersion) return false;
    if (!take(cursor, end, vertexCount) || !take(cursor, end, indexCount) ||
        !take(cursor, end, partCount)) {
        return false;
    }
    if (!take(cursor, end, out.bounds)) return false;
    // Refuse rather than trust the header: a truncated asset must not make
    // the loader allocate gigabytes (P1).
    const size_t needed = static_cast<size_t>(vertexCount) * sizeof(SkinVertex) +
                          static_cast<size_t>(indexCount) * sizeof(uint32_t) +
                          static_cast<size_t>(partCount) * (sizeof(uint8_t) + 2 * sizeof(uint32_t));
    if (static_cast<size_t>(end - cursor) < needed) return false;

    out.vertices.resize(vertexCount);
    for (SkinVertex& v : out.vertices) take(cursor, end, v);
    out.indices.resize(indexCount);
    for (uint32_t& i : out.indices) take(cursor, end, i);
    out.parts.resize(partCount);
    for (MeshPart& p : out.parts) {
        uint8_t region = 0;
        take(cursor, end, region);
        take(cursor, end, p.firstIndex);
        take(cursor, end, p.indexCount);
        p.region = static_cast<BodyRegion>(region);
        if (static_cast<size_t>(p.firstIndex) + p.indexCount > out.indices.size()) return false;
    }
    for (uint32_t i : out.indices) {
        if (i >= out.vertices.size()) return false;
    }

    if (version >= 2) {
        uint32_t morphCount = 0;
        if (!take(cursor, end, morphCount)) return false;
        if (morphCount > kMorphCount) return false;  // refuse, never grow (P1)
        out.morphs.resize(morphCount);
        for (MorphTarget& t : out.morphs) {
            uint8_t morph = 0;
            uint32_t deltaCount = 0;
            if (!take(cursor, end, morph) || !take(cursor, end, t.scale) ||
                !take(cursor, end, deltaCount)) {
                return false;
            }
            if (morph >= kMorphCount) return false;
            if (static_cast<size_t>(end - cursor) < deltaCount * sizeof(MorphDelta)) return false;
            t.morph = static_cast<Morph>(morph);
            t.deltas.resize(deltaCount);
            for (MorphDelta& d : t.deltas) {
                take(cursor, end, d);
                if (d.vertex >= out.vertices.size()) return false;
            }
        }
    }
    return true;
}

bool writeSkinnedMeshFile(const char* path, const SkinnedMeshData& mesh) {
    std::vector<uint8_t> blob;
    serializeSkinnedMesh(mesh, blob);
    FILE* f = fopen(path, "wb");
    if (f == nullptr) return false;
    const bool ok = fwrite(blob.data(), 1, blob.size(), f) == blob.size();
    fclose(f);
    return ok;
}

bool readSkinnedMeshFile(const char* path, SkinnedMeshData& out) {
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
    if (!readOk || !deserializeSkinnedMesh(blob.data(), blob.size(), out)) {
        MGE_LOGE("mesh_io", "bad skinned mesh file: %s", path);
        return false;
    }
    return true;
}

}  // namespace mge
