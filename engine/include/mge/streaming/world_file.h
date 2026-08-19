#pragma once

// World streaming container v1 (.mgeworld, ADR 0003, task 4.1/4.2).
// WorldBaker authors + writes; WorldFileReader loads only the index tables
// and hands out byte ranges for the priority AsyncIO system to fetch.

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "mge/core/math.h"
#include "mge/framework/asset_registry.h"

namespace mge {

enum class CellKind : uint8_t {
    Exterior = 0,  // grid cell: residency by chunk distance to the player
    Interior,      // interior cell: residency by approach prediction (P10)
};

// On-disk placement record — also the in-memory form after a chunk read.
struct Placement {
    AssetId asset = kInvalidAsset;
    float pos[3] = {0, 0, 0};
    float yaw = 0;
    float color[4] = {1, 1, 1, 1};
};
static_assert(sizeof(Placement) == 40, "placement layout is a file-format contract");

struct WorldChunkInfo {
    int32_t cx = 0;
    int32_t cz = 0;
    CellKind kind = CellKind::Exterior;
    Vec3 anchor{};            // interior: door position
    float enterRadius = 0;    // interior: resident within this distance
    float exitRadius = 0;     // interior: evicted beyond this distance
    uint64_t placementsOffset = 0;
    uint32_t placementCount = 0;
};

struct WorldAssetInfo {
    AssetId id = kInvalidAsset;
    AssetKind kind = AssetKind::Mesh;
    std::string name;
    uint64_t payloadOffset = 0;  // serialized LodMesh blob; 0/0 for virtuals
    uint64_t payloadSize = 0;
    VirtualModelDesc virtualDesc;  // meaningful for virtual assets
};

// ---------------------------------------------------------------------------

class WorldBaker {
public:
    explicit WorldBaker(float chunkSizeMeters = 32.0f) : chunkSize_(chunkSizeMeters) {}

    AssetId addMeshAsset(const char* name, const LodMesh& mesh);
    AssetId addVirtualAsset(const char* name, const VirtualModelDesc& desc);

    // Places into the exterior grid cell containing pos.
    void place(AssetId asset, const Vec3& pos, float yaw, float r, float g, float b);

    // Declares an interior cell keyed by (cx, cz, interiorIndex) with a door
    // anchor; returns a handle for placing content inside it.
    uint32_t addInteriorCell(const Vec3& doorAnchor, float enterRadius, float exitRadius);
    void placeInterior(uint32_t interiorCell, AssetId asset, const Vec3& pos, float yaw,
                       float r, float g, float b);

    bool write(const char* path) const;

    float chunkSize() const { return chunkSize_; }

private:
    struct BakedAsset {
        WorldAssetInfo info;
        std::vector<uint8_t> payload;
    };
    struct BakedInterior {
        Vec3 anchor;
        float enterRadius, exitRadius;
        std::vector<Placement> placements;
    };

    float chunkSize_;
    std::vector<BakedAsset> assets_;
    std::unordered_map<uint64_t, std::vector<Placement>> exteriorChunks_;  // key: packed cx,cz
    std::vector<BakedInterior> interiors_;
};

// ---------------------------------------------------------------------------

class WorldFileReader {
public:
    bool open(const char* path);

    float chunkSizeMeters() const { return chunkSize_; }
    const std::string& path() const { return path_; }

    const std::vector<WorldChunkInfo>& chunks() const { return chunks_; }
    const std::vector<WorldAssetInfo>& assets() const { return assets_; }
    const WorldAssetInfo* findAsset(AssetId id) const;
    // Exterior grid lookup; interiors are found by iterating chunks().
    const WorldChunkInfo* findExterior(int32_t cx, int32_t cz) const;

    // Synchronous reads (tooling/tests). The streaming manager uses the
    // offsets above with AsyncIO instead.
    bool readPlacements(const WorldChunkInfo& chunk, std::vector<Placement>& out) const;
    bool readAssetMesh(const WorldAssetInfo& asset, LodMesh& out) const;

private:
    std::string path_;
    float chunkSize_ = 32.0f;
    std::vector<WorldChunkInfo> chunks_;
    std::vector<WorldAssetInfo> assets_;
    std::unordered_map<uint64_t, size_t> exteriorIndex_;
    std::unordered_map<AssetId, size_t> assetIndex_;
};

}  // namespace mge
