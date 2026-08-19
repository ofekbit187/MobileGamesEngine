// Phase 4: world container round-trip and streaming residency behavior.
// Tests may block on I/O (io.drain()) — the game loop never does; here it
// makes the async machinery deterministic.

#include <cstdlib>
#include <string>

#include "mge/graphics/primitives.h"
#include "mge/streaming/streaming_manager.h"
#include "mge/streaming/world_file.h"
#include "test_framework.h"

using namespace mge;

namespace {

std::string tmpWorldPath(const char* name) {
    std::string dir = "/tmp";
    if (const char* t = getenv("TMPDIR")) dir = t;
    return dir + "/" + name;
}

// A 4x4-chunk world: houses everywhere, a shared mesh asset, one virtual
// asset, and one interior cell with a door at (40, 0, 40).
std::string bakeSmallWorld() {
    WorldBaker baker(32.0f);
    LodMesh houseMesh;
    houseMesh.lods.push_back(makeBox({3, 2.5f, 3}));
    houseMesh.computeBounds();
    const AssetId house = baker.addMeshAsset("prop/house", houseMesh);

    VirtualModelDesc stall;
    stall.proportions = {2.4f, 2.1f, 1.8f};
    stall.description = "market stall";
    const AssetId virtualStall = baker.addVirtualAsset("prop/stall", stall);

    for (int cx = 0; cx < 4; ++cx) {
        for (int cz = 0; cz < 4; ++cz) {
            const float x = cx * 32.0f + 10.0f;
            const float z = cz * 32.0f + 12.0f;
            baker.place(house, {x, 1.25f, z}, 0.2f, 0.6f, 0.5f, 0.4f);
            baker.place(virtualStall, {x + 6, 1.05f, z + 4}, 0, 0.85f, 0.55f, 0.18f);
        }
    }

    const uint32_t tavern = baker.addInteriorCell({40, 0, 40}, 6.0f, 14.0f);
    baker.placeInterior(tavern, house, {40, 1.25f, 38}, 0, 0.4f, 0.3f, 0.2f);
    baker.placeInterior(tavern, virtualStall, {42, 1.05f, 40}, 0, 0.85f, 0.55f, 0.18f);

    const std::string path = tmpWorldPath("mge_small.mgeworld");
    baker.write(path.c_str());
    return path;
}

// Drives updates until streaming settles (tests only).
void settle(StreamingManager& sm, AsyncIO& io, const Vec3& pos, int rounds = 12) {
    for (int i = 0; i < rounds; ++i) {
        sm.update(pos);
        io.drain();
    }
}

}  // namespace

MGE_TEST(world_file_roundtrip) {
    const std::string path = bakeSmallWorld();
    WorldFileReader reader;
    MGE_CHECK(reader.open(path.c_str()));
    MGE_CHECK(reader.chunks().size() == 17);  // 16 exterior + 1 interior
    MGE_CHECK(reader.assets().size() == 2);

    const WorldChunkInfo* chunk = reader.findExterior(1, 2);
    MGE_CHECK(chunk != nullptr && chunk->placementCount == 2);
    std::vector<Placement> placements;
    MGE_CHECK(reader.readPlacements(*chunk, placements));
    MGE_CHECK(placements.size() == 2);
    MGE_CHECK(placements[0].asset == assetIdFromName("prop/house"));
    MGE_CHECK_NEAR(placements[0].pos[0], 42.0f, 1e-5);

    const WorldAssetInfo* house = reader.findAsset(assetIdFromName("prop/house"));
    MGE_CHECK(house != nullptr && house->payloadSize > 0);
    LodMesh mesh;
    MGE_CHECK(reader.readAssetMesh(*house, mesh));
    MGE_CHECK(mesh.lods.size() == 1 && mesh.lods[0].vertices.size() == 24);

    const WorldAssetInfo* stall = reader.findAsset(assetIdFromName("prop/stall"));
    MGE_CHECK(stall != nullptr && stall->kind == AssetKind::VirtualModel);
    MGE_CHECK(stall->virtualDesc.description == "market stall");
    MGE_CHECK_NEAR(stall->virtualDesc.proportions.y, 2.1f, 1e-6);

    // One interior with its anchor and radii intact.
    int interiors = 0;
    for (const WorldChunkInfo& c : reader.chunks()) {
        if (c.kind == CellKind::Interior) {
            ++interiors;
            MGE_CHECK_NEAR(c.anchor.x, 40.0f, 1e-5);
            MGE_CHECK_NEAR(c.enterRadius, 6.0f, 1e-6);
        }
    }
    MGE_CHECK(interiors == 1);
}

MGE_TEST(streaming_residency_follows_player) {
    const std::string path = bakeSmallWorld();
    World world(512);
    AssetRegistry assets;
    AsyncIO io;
    BudgetRegistry budgets;
    StreamingManager sm;
    StreamingConfig config;
    config.residentRadius = 1;
    config.evictRadius = 2;
    MGE_CHECK(sm.init(path.c_str(), world, assets, io, budgets, config));

    // Stand in chunk (0,0): a 3x3 neighborhood becomes resident.
    settle(sm, io, {16, 0, 16});
    MGE_CHECK(sm.exteriorState(0, 0) == ChunkState::Resident);
    MGE_CHECK(sm.exteriorState(1, 1) == ChunkState::Resident);
    MGE_CHECK(sm.exteriorState(3, 3) == ChunkState::Cold);
    MGE_CHECK(world.entities().liveCount() > 0);
    MGE_CHECK(sm.stats().residentChunks >= 4);

    // The shared house mesh streamed in and is resident in the registry.
    MGE_CHECK(assets.resident(assetIdFromName("prop/house")));
    // The virtual stall is always resident (generated placeholder).
    MGE_CHECK(assets.resident(assetIdFromName("prop/stall")));

    // Walk to the far corner: old chunks evict, new ones load.
    settle(sm, io, {3 * 32 + 16, 0, 3 * 32 + 16}, 24);
    MGE_CHECK(sm.exteriorState(3, 3) == ChunkState::Resident);
    MGE_CHECK(sm.exteriorState(0, 0) == ChunkState::Cold);
    MGE_CHECK(sm.stats().chunkEvictCount > 0);
    MGE_CHECK(sm.stats().playerColdUpdates == 0);  // flow never broke (P10 metric)

    sm.shutdown();
    // All streaming memory returned.
    MGE_CHECK(sm.stats().budgetCapBytes == 0 || budgets.totalUsedBytes() == 0);
}

MGE_TEST(interior_approach_prediction) {
    const std::string path = bakeSmallWorld();
    World world(512);
    AssetRegistry assets;
    AsyncIO io;
    BudgetRegistry budgets;
    StreamingManager sm;
    StreamingConfig config;
    config.residentRadius = 1;
    config.evictRadius = 2;
    MGE_CHECK(sm.init(path.c_str(), world, assets, io, budgets, config));

    size_t interiorIndex = SIZE_MAX;
    for (size_t i = 0; i < sm.reader().chunks().size(); ++i) {
        if (sm.reader().chunks()[i].kind == CellKind::Interior) interiorIndex = i;
    }
    MGE_CHECK(interiorIndex != SIZE_MAX);

    // Far from the tavern door (40,0,40): interior cold.
    settle(sm, io, {100, 0, 100});
    MGE_CHECK(sm.chunkState(interiorIndex) == ChunkState::Cold);

    // Approaching within prefetch range (enter 6m x factor 2 = 12m): the
    // interior is resident BEFORE the player reaches the door.
    settle(sm, io, {50, 0, 40});
    MGE_CHECK(sm.chunkState(interiorIndex) == ChunkState::Resident);

    // Walking away beyond the exit radius evicts it.
    settle(sm, io, {70, 0, 70}, 24);
    MGE_CHECK(sm.chunkState(interiorIndex) == ChunkState::Cold);

    sm.shutdown();
}

MGE_TEST(streaming_budget_refuses_gracefully) {
    const std::string path = bakeSmallWorld();
    World world(512);
    AssetRegistry assets;
    AsyncIO io;
    BudgetRegistry budgets;
    StreamingManager sm;
    StreamingConfig config;
    config.streamingBudgetBytes = 64;  // absurdly small: everything refused
    MGE_CHECK(sm.init(path.c_str(), world, assets, io, budgets, config));
    settle(sm, io, {16, 0, 16});
    // Nothing loaded, nothing crashed, budget never exceeded.
    MGE_CHECK(sm.stats().residentChunks == 0);
    MGE_CHECK(sm.stats().budgetUsedBytes <= 64);
    sm.shutdown();
}
