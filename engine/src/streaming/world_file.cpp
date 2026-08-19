#include "mge/streaming/world_file.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "mge/core/log.h"
#include "mge/graphics/mesh_io.h"

namespace mge {

namespace {

constexpr char kMagic[4] = {'M', 'G', 'E', 'W'};
constexpr uint32_t kVersion = 1;
constexpr const char* kTag = "world_file";

struct FileHeader {
    char magic[4];
    uint32_t version;
    float chunkSize;
    uint32_t assetCount;
    uint32_t chunkCount;
    uint64_t assetTableOffset;
    uint64_t nameTableOffset;
    uint64_t nameTableSize;
    uint64_t chunkTableOffset;
};

struct AssetRecordDisk {
    uint64_t id;
    uint8_t kind;  // AssetKind
    uint8_t shape; // PlaceholderShape (virtuals)
    uint16_t pad;
    float proportions[3];
    uint64_t nameOffset;
    uint32_t nameLen;
    uint32_t descLen;
    uint64_t descOffset;
    uint64_t payloadOffset;
    uint64_t payloadSize;
};

struct ChunkRecordDisk {
    int32_t cx;
    int32_t cz;
    uint8_t kind;  // CellKind
    uint8_t pad[3];
    float anchor[3];
    float enterRadius;
    float exitRadius;
    uint64_t placementsOffset;
    uint32_t placementCount;
    uint32_t pad2;
};

uint64_t packCoord(int32_t cx, int32_t cz) {
    return (static_cast<uint64_t>(static_cast<uint32_t>(cx)) << 32) |
           static_cast<uint32_t>(cz);
}

}  // namespace

// --------------------------- WorldBaker ------------------------------------

AssetId WorldBaker::addMeshAsset(const char* name, const LodMesh& mesh) {
    BakedAsset baked;
    baked.info.id = assetIdFromName(name);
    baked.info.kind = AssetKind::Mesh;
    baked.info.name = name;
    serializeMesh(mesh, baked.payload);
    baked.info.payloadSize = baked.payload.size();
    assets_.push_back(std::move(baked));
    return assets_.back().info.id;
}

AssetId WorldBaker::addVirtualAsset(const char* name, const VirtualModelDesc& desc) {
    BakedAsset baked;
    baked.info.id = assetIdFromName(name);
    baked.info.kind = AssetKind::VirtualModel;
    baked.info.name = name;
    baked.info.virtualDesc = desc;
    assets_.push_back(std::move(baked));
    return assets_.back().info.id;
}

void WorldBaker::place(AssetId asset, const Vec3& pos, float yaw, float r, float g, float b) {
    Placement p;
    p.asset = asset;
    p.pos[0] = pos.x;
    p.pos[1] = pos.y;
    p.pos[2] = pos.z;
    p.yaw = yaw;
    p.color[0] = r;
    p.color[1] = g;
    p.color[2] = b;
    const int32_t cx = static_cast<int32_t>(std::floor(pos.x / chunkSize_));
    const int32_t cz = static_cast<int32_t>(std::floor(pos.z / chunkSize_));
    exteriorChunks_[packCoord(cx, cz)].push_back(p);
}

uint32_t WorldBaker::addInteriorCell(const Vec3& doorAnchor, float enterRadius,
                                     float exitRadius) {
    BakedInterior interior;
    interior.anchor = doorAnchor;
    interior.enterRadius = enterRadius;
    interior.exitRadius = exitRadius;
    interiors_.push_back(std::move(interior));
    return static_cast<uint32_t>(interiors_.size() - 1);
}

void WorldBaker::placeInterior(uint32_t interiorCell, AssetId asset, const Vec3& pos, float yaw,
                               float r, float g, float b) {
    if (interiorCell >= interiors_.size()) return;
    Placement p;
    p.asset = asset;
    p.pos[0] = pos.x;
    p.pos[1] = pos.y;
    p.pos[2] = pos.z;
    p.yaw = yaw;
    p.color[0] = r;
    p.color[1] = g;
    p.color[2] = b;
    interiors_[interiorCell].placements.push_back(p);
}

bool WorldBaker::write(const char* path) const {
    // Layout: header | asset table | name table | chunk table | payloads.
    std::vector<AssetRecordDisk> assetTable(assets_.size());
    std::vector<uint8_t> nameTable;
    for (size_t i = 0; i < assets_.size(); ++i) {
        const BakedAsset& baked = assets_[i];
        AssetRecordDisk& rec = assetTable[i];
        rec = AssetRecordDisk{};
        rec.id = baked.info.id;
        rec.kind = static_cast<uint8_t>(baked.info.kind);
        rec.shape = static_cast<uint8_t>(baked.info.virtualDesc.shape);
        rec.proportions[0] = baked.info.virtualDesc.proportions.x;
        rec.proportions[1] = baked.info.virtualDesc.proportions.y;
        rec.proportions[2] = baked.info.virtualDesc.proportions.z;
        rec.nameOffset = nameTable.size();
        rec.nameLen = static_cast<uint32_t>(baked.info.name.size());
        nameTable.insert(nameTable.end(), baked.info.name.begin(), baked.info.name.end());
        rec.descOffset = nameTable.size();
        rec.descLen = static_cast<uint32_t>(baked.info.virtualDesc.description.size());
        nameTable.insert(nameTable.end(), baked.info.virtualDesc.description.begin(),
                         baked.info.virtualDesc.description.end());
    }

    const uint32_t chunkCount =
        static_cast<uint32_t>(exteriorChunks_.size() + interiors_.size());
    std::vector<ChunkRecordDisk> chunkTable;
    chunkTable.reserve(chunkCount);

    FileHeader header{};
    memcpy(header.magic, kMagic, 4);
    header.version = kVersion;
    header.chunkSize = chunkSize_;
    header.assetCount = static_cast<uint32_t>(assets_.size());
    header.chunkCount = chunkCount;
    header.assetTableOffset = sizeof(FileHeader);
    header.nameTableOffset = header.assetTableOffset + assetTable.size() * sizeof(AssetRecordDisk);
    header.nameTableSize = nameTable.size();
    header.chunkTableOffset = header.nameTableOffset + nameTable.size();

    uint64_t payloadCursor =
        header.chunkTableOffset + static_cast<uint64_t>(chunkCount) * sizeof(ChunkRecordDisk);

    for (const auto& [key, placements] : exteriorChunks_) {
        ChunkRecordDisk rec{};
        rec.cx = static_cast<int32_t>(key >> 32);
        rec.cz = static_cast<int32_t>(key & 0xFFFFFFFFu);
        rec.kind = static_cast<uint8_t>(CellKind::Exterior);
        rec.placementsOffset = payloadCursor;
        rec.placementCount = static_cast<uint32_t>(placements.size());
        payloadCursor += placements.size() * sizeof(Placement);
        chunkTable.push_back(rec);
    }
    for (const BakedInterior& interior : interiors_) {
        ChunkRecordDisk rec{};
        // Interiors live outside the grid keyspace; store the grid cell of
        // their anchor for diagnostics.
        rec.cx = static_cast<int32_t>(std::floor(interior.anchor.x / chunkSize_));
        rec.cz = static_cast<int32_t>(std::floor(interior.anchor.z / chunkSize_));
        rec.kind = static_cast<uint8_t>(CellKind::Interior);
        rec.anchor[0] = interior.anchor.x;
        rec.anchor[1] = interior.anchor.y;
        rec.anchor[2] = interior.anchor.z;
        rec.enterRadius = interior.enterRadius;
        rec.exitRadius = interior.exitRadius;
        rec.placementsOffset = payloadCursor;
        rec.placementCount = static_cast<uint32_t>(interior.placements.size());
        payloadCursor += interior.placements.size() * sizeof(Placement);
        chunkTable.push_back(rec);
    }

    // Asset payload offsets follow all placement payloads.
    for (size_t i = 0; i < assets_.size(); ++i) {
        if (!assets_[i].payload.empty()) {
            assetTable[i].payloadOffset = payloadCursor;
            assetTable[i].payloadSize = assets_[i].payload.size();
            payloadCursor += assets_[i].payload.size();
        }
    }

    FILE* f = fopen(path, "wb");
    if (f == nullptr) {
        MGE_LOGE(kTag, "cannot open for write: %s", path);
        return false;
    }
    bool ok = fwrite(&header, sizeof(header), 1, f) == 1;
    ok = ok && (assetTable.empty() ||
                fwrite(assetTable.data(), sizeof(AssetRecordDisk), assetTable.size(), f) ==
                    assetTable.size());
    ok = ok && (nameTable.empty() ||
                fwrite(nameTable.data(), 1, nameTable.size(), f) == nameTable.size());
    ok = ok && (chunkTable.empty() ||
                fwrite(chunkTable.data(), sizeof(ChunkRecordDisk), chunkTable.size(), f) ==
                    chunkTable.size());
    for (const auto& [key, placements] : exteriorChunks_) {
        ok = ok && fwrite(placements.data(), sizeof(Placement), placements.size(), f) ==
                       placements.size();
    }
    for (const BakedInterior& interior : interiors_) {
        ok = ok && fwrite(interior.placements.data(), sizeof(Placement),
                          interior.placements.size(), f) == interior.placements.size();
    }
    for (const BakedAsset& baked : assets_) {
        if (!baked.payload.empty()) {
            ok = ok && fwrite(baked.payload.data(), 1, baked.payload.size(), f) ==
                           baked.payload.size();
        }
    }
    fclose(f);
    return ok;
}

// ------------------------- WorldFileReader ---------------------------------

bool WorldFileReader::open(const char* path) {
    FILE* f = fopen(path, "rb");
    if (f == nullptr) return false;

    FileHeader header{};
    bool ok = fread(&header, sizeof(header), 1, f) == 1 &&
              memcmp(header.magic, kMagic, 4) == 0 && header.version == kVersion;
    if (!ok) {
        MGE_LOGE(kTag, "bad world file: %s", path);
        fclose(f);
        return false;
    }

    path_ = path;
    chunkSize_ = header.chunkSize;

    std::vector<AssetRecordDisk> assetTable(header.assetCount);
    std::vector<uint8_t> nameTable(header.nameTableSize);
    std::vector<ChunkRecordDisk> chunkTable(header.chunkCount);
    ok = fseek(f, static_cast<long>(header.assetTableOffset), SEEK_SET) == 0 &&
         (assetTable.empty() ||
          fread(assetTable.data(), sizeof(AssetRecordDisk), assetTable.size(), f) ==
              assetTable.size());
    ok = ok && (nameTable.empty() ||
                fread(nameTable.data(), 1, nameTable.size(), f) == nameTable.size());
    ok = ok && (chunkTable.empty() ||
                fread(chunkTable.data(), sizeof(ChunkRecordDisk), chunkTable.size(), f) ==
                    chunkTable.size());
    fclose(f);
    if (!ok) {
        MGE_LOGE(kTag, "truncated world tables: %s", path);
        return false;
    }

    assets_.resize(assetTable.size());
    for (size_t i = 0; i < assetTable.size(); ++i) {
        const AssetRecordDisk& rec = assetTable[i];
        WorldAssetInfo& info = assets_[i];
        info.id = rec.id;
        info.kind = static_cast<AssetKind>(rec.kind);
        info.name.assign(reinterpret_cast<const char*>(nameTable.data()) + rec.nameOffset,
                         rec.nameLen);
        info.payloadOffset = rec.payloadOffset;
        info.payloadSize = rec.payloadSize;
        info.virtualDesc.proportions = {rec.proportions[0], rec.proportions[1],
                                        rec.proportions[2]};
        info.virtualDesc.shape = static_cast<PlaceholderShape>(rec.shape);
        info.virtualDesc.description.assign(
            reinterpret_cast<const char*>(nameTable.data()) + rec.descOffset, rec.descLen);
        assetIndex_[info.id] = i;
    }

    chunks_.resize(chunkTable.size());
    for (size_t i = 0; i < chunkTable.size(); ++i) {
        const ChunkRecordDisk& rec = chunkTable[i];
        WorldChunkInfo& info = chunks_[i];
        info.cx = rec.cx;
        info.cz = rec.cz;
        info.kind = static_cast<CellKind>(rec.kind);
        info.anchor = {rec.anchor[0], rec.anchor[1], rec.anchor[2]};
        info.enterRadius = rec.enterRadius;
        info.exitRadius = rec.exitRadius;
        info.placementsOffset = rec.placementsOffset;
        info.placementCount = rec.placementCount;
        if (info.kind == CellKind::Exterior) {
            exteriorIndex_[packCoord(info.cx, info.cz)] = i;
        }
    }
    return true;
}

const WorldAssetInfo* WorldFileReader::findAsset(AssetId id) const {
    auto it = assetIndex_.find(id);
    return it != assetIndex_.end() ? &assets_[it->second] : nullptr;
}

const WorldChunkInfo* WorldFileReader::findExterior(int32_t cx, int32_t cz) const {
    auto it = exteriorIndex_.find(packCoord(cx, cz));
    return it != exteriorIndex_.end() ? &chunks_[it->second] : nullptr;
}

bool WorldFileReader::readPlacements(const WorldChunkInfo& chunk,
                                     std::vector<Placement>& out) const {
    out.resize(chunk.placementCount);
    if (chunk.placementCount == 0) return true;
    FILE* f = fopen(path_.c_str(), "rb");
    if (f == nullptr) return false;
    const bool ok = fseek(f, static_cast<long>(chunk.placementsOffset), SEEK_SET) == 0 &&
                    fread(out.data(), sizeof(Placement), out.size(), f) == out.size();
    fclose(f);
    return ok;
}

bool WorldFileReader::readAssetMesh(const WorldAssetInfo& asset, LodMesh& out) const {
    if (asset.payloadSize == 0) return false;
    std::vector<uint8_t> blob(asset.payloadSize);
    FILE* f = fopen(path_.c_str(), "rb");
    if (f == nullptr) return false;
    const bool ok = fseek(f, static_cast<long>(asset.payloadOffset), SEEK_SET) == 0 &&
                    fread(blob.data(), 1, blob.size(), f) == blob.size();
    fclose(f);
    return ok && deserializeMesh(blob.data(), blob.size(), out);
}

}  // namespace mge
