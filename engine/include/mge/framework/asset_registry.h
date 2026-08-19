#pragma once

// Asset registry (task 3.4) with virtual models as a first-class asset kind
// (tasks 7.1/7.2). Everything references assets by stable id; resolving can
// legitimately answer "not resident" and callers degrade (P2). A virtual
// model registers with proportions + shape hint + description; fulfilling it
// swaps in real geometry under the SAME id — no scene edits (P5).

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "mge/graphics/mesh_data.h"

namespace mge {

using AssetId = uint64_t;
constexpr AssetId kInvalidAsset = 0;

// FNV-1a — stable across runs, platforms, and sessions.
constexpr AssetId assetIdFromName(const char* name) {
    AssetId hash = 1469598103934665603ull;
    while (*name != '\0') {
        hash ^= static_cast<uint8_t>(*name++);
        hash *= 1099511628211ull;
    }
    return hash == kInvalidAsset ? 1 : hash;
}

enum class AssetKind : uint8_t {
    Mesh = 0,      // real geometry (baked or procedural)
    VirtualModel,  // proportions + description, awaiting fulfillment
};

enum class PlaceholderShape : uint8_t { Box = 0, Cylinder, Capsule };

struct VirtualModelDesc {
    Vec3 proportions{1, 1, 1};  // full extents (w, h, d)
    PlaceholderShape shape = PlaceholderShape::Box;
    std::string description;  // written for the external agent that will build it
    bool collidable = true;
};

struct AssetRecord {
    AssetId id = kInvalidAsset;
    std::string name;
    AssetKind kind = AssetKind::Mesh;
    LodMesh mesh;                 // real geometry, or the generated placeholder volume
    VirtualModelDesc virtualDesc; // meaningful when kind == VirtualModel
};

class AssetRegistry {
public:
    // Returns the stable id (or kInvalidAsset on name collision with different content).
    AssetId registerMesh(const char* name, LodMesh mesh);

    // Registers a virtual model; the engine generates its placeholder volume
    // at the declared proportions immediately, so it renders like any asset.
    AssetId registerVirtualModel(const char* name, VirtualModelDesc desc);

    // Fulfillment (P5): real geometry arrives under a virtual model's id.
    // Every placement resolves to the real mesh from now on; returns false if
    // the id is unknown or not virtual. Logs a warning when the real mesh's
    // bounds deviate badly from the declared proportions.
    bool fulfill(AssetId id, LodMesh mesh);

    const AssetRecord* find(AssetId id) const;  // nullptr = unknown id

    // Streaming residency (P2): a registered mesh whose payload has been
    // dropped is known but not resident — callers degrade, never crash.
    bool resident(AssetId id) const {
        const AssetRecord* record = find(id);
        return record != nullptr && !record->mesh.lods.empty();
    }
    // Drops a mesh asset's payload (keeps the record + id). Virtual models
    // keep their placeholder volume — they are always "resident".
    void unload(AssetId id);

    // Manifest of unfulfilled virtual models for external agents (task 7.5).
    void unfulfilled(std::vector<const AssetRecord*>& out) const;

    size_t count() const { return records_.size(); }

private:
    std::unordered_map<AssetId, std::unique_ptr<AssetRecord>> records_;
};

}  // namespace mge
