// Streaming scale test (task 4.8): bakes a synthetic world of configurable
// size — unique terrain geometry per chunk, shared props, virtual models,
// interiors — then traverses it continuously with a moving player and proves
// the P2/P10 promises:
//   - runtime memory stays flat inside the streaming budget (P1/P2)
//   - the player's chunk is always resident while walking (no flow breaks, P10)
//   - interiors are resident before the player reaches their doors (P10)
//   - update() never blocks (max update time bounded)
//
// Usage:
//   mge_stream_test bake <world.mgeworld> <chunksPerSide> <terrainRes>
//   mge_stream_test run  <world.mgeworld> <seconds> [budgetMiB]

#include <chrono>
#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <unistd.h>

#include "mge/graphics/primitives.h"
#include "mge/streaming/streaming_manager.h"
#include "mge/streaming/world_file.h"

using namespace mge;

namespace {

constexpr float kChunkSize = 32.0f;

// Deterministic per-chunk terrain patch: a heightfield grid, unique geometry
// for every chunk — this is what makes the world file big, exactly like real
// authored terrain would.
MeshData makeTerrainPatch(int32_t cx, int32_t cz, int resolution) {
    std::mt19937 rng(static_cast<uint32_t>(cx * 73856093 ^ cz * 19349663));
    std::uniform_real_distribution<float> jitter(-0.4f, 0.4f);

    MeshData mesh;
    const int verts = resolution + 1;
    mesh.vertices.reserve(static_cast<size_t>(verts) * verts);
    for (int z = 0; z <= resolution; ++z) {
        for (int x = 0; x <= resolution; ++x) {
            const float fx = static_cast<float>(x) / resolution * kChunkSize;
            const float fz = static_cast<float>(z) / resolution * kChunkSize;
            const float wx = cx * kChunkSize + fx;
            const float wz = cz * kChunkSize + fz;
            const float height = std::sin(wx * 0.05f) * std::cos(wz * 0.045f) * 1.5f + jitter(rng);
            mesh.vertices.push_back({{fx, height, fz}, {0, 1, 0}});
        }
    }
    for (int z = 0; z < resolution; ++z) {
        for (int x = 0; x < resolution; ++x) {
            const uint32_t i = static_cast<uint32_t>(z * verts + x);
            mesh.indices.insert(mesh.indices.end(),
                                {i, i + verts, i + verts + 1, i, i + verts + 1, i + 1});
        }
    }
    mesh.computeBounds();
    return mesh;
}

int bake(const char* path, int chunksPerSide, int terrainRes) {
    WorldBaker baker(kChunkSize);

    LodMesh houseMesh;
    houseMesh.lods.push_back(makeBox({3.2f, 2.6f, 2.8f}));
    houseMesh.lods.push_back(makeBox({3.2f, 2.6f, 2.8f}));  // coarse stand-in LOD
    houseMesh.switchDistances = {60.0f};
    houseMesh.computeBounds();
    const AssetId house = baker.addMeshAsset("prop/house", houseMesh);

    LodMesh towerMesh;
    towerMesh.lods.push_back(makeCylinder(1.2f, 7.0f, 32));
    towerMesh.lods.push_back(makeCylinder(1.2f, 7.0f, 8));
    towerMesh.switchDistances = {50.0f};
    towerMesh.computeBounds();
    const AssetId tower = baker.addMeshAsset("prop/tower", towerMesh);

    LodMesh furnitureMesh;
    furnitureMesh.lods.push_back(makeBox({1.0f, 0.8f, 0.6f}));
    furnitureMesh.computeBounds();
    const AssetId furniture = baker.addMeshAsset("prop/furniture", furnitureMesh);

    VirtualModelDesc stallDesc;
    stallDesc.proportions = {2.4f, 2.1f, 1.8f};
    stallDesc.description = "market stall placeholder";
    const AssetId stall = baker.addVirtualAsset("prop/stall", stallDesc);

    std::mt19937 rng(12345);
    std::uniform_real_distribution<float> offset(4.0f, kChunkSize - 4.0f);
    uint32_t interiorCount = 0;

    for (int cz = 0; cz < chunksPerSide; ++cz) {
        for (int cx = 0; cx < chunksPerSide; ++cx) {
            char terrainName[64];
            snprintf(terrainName, sizeof(terrainName), "terrain/%d_%d", cx, cz);
            LodMesh terrain;
            terrain.lods.push_back(makeTerrainPatch(cx, cz, terrainRes));
            terrain.computeBounds();
            const AssetId terrainId = baker.addMeshAsset(terrainName, terrain);
            baker.place(terrainId, {cx * kChunkSize, 0, cz * kChunkSize}, 0, 0.42f, 0.47f, 0.36f);

            // Scatter props.
            for (int p = 0; p < 6; ++p) {
                const Vec3 pos{cx * kChunkSize + offset(rng), 1.3f, cz * kChunkSize + offset(rng)};
                const AssetId asset = (p % 3 == 0) ? tower : ((p % 3 == 1) ? house : stall);
                baker.place(asset, pos, offset(rng) * 0.1f, 0.6f, 0.55f, 0.45f);
            }

            // Every 16th chunk hosts an interior cell (a "tavern") with a
            // door at the chunk center.
            if ((cx + cz * chunksPerSide) % 16 == 7) {
                const Vec3 door{cx * kChunkSize + 16.0f, 0, cz * kChunkSize + 16.0f};
                const uint32_t cell = baker.addInteriorCell(door, 8.0f, 20.0f);
                for (int i = 0; i < 8; ++i) {
                    baker.placeInterior(cell, furniture,
                                        {door.x + offset(rng) * 0.2f, 0.4f,
                                         door.z + offset(rng) * 0.2f},
                                        0, 0.45f, 0.35f, 0.25f);
                }
                ++interiorCount;
            }
        }
    }

    if (!baker.write(path)) {
        fprintf(stderr, "FAIL: bake write\n");
        return 1;
    }
    FILE* f = fopen(path, "rb");
    fseek(f, 0, SEEK_END);
    const long size = ftell(f);
    fclose(f);
    printf("baked %dx%d chunks (%d interiors) -> %s (%.2f GiB)\n", chunksPerSide, chunksPerSide,
           interiorCount, path, size / (1024.0 * 1024.0 * 1024.0));
    return 0;
}

int run(const char* path, int seconds, size_t budgetMiB) {
    World world(16384);
    AssetRegistry assets;
    AsyncIO io;
    BudgetRegistry budgets;
    StreamingManager sm;
    StreamingConfig config;
    config.residentRadius = 2;
    config.evictRadius = 3;
    config.maxInFlight = 12;
    config.maxInstantiatePerUpdate = 3;
    config.streamingBudgetBytes = budgetMiB * 1024 * 1024;
    if (!sm.init(path, world, assets, io, budgets, config)) return 1;

    const float side = std::sqrt(static_cast<float>(sm.reader().chunks().size())) * kChunkSize;
    const double dt = 1.0 / 60.0;
    const float speed = 8.0f;  // fast sprint
    Vec3 pos{kChunkSize * 2.5f, 0, kChunkSize * 2.5f};

    // Watch the interior nearest the start; mid-run the route detours to walk
    // through its door, proving approach prediction under P10.
    size_t watchedInterior = SIZE_MAX;
    float nearest = 1e30f;
    for (size_t i = 0; i < sm.reader().chunks().size(); ++i) {
        const WorldChunkInfo& chunk = sm.reader().chunks()[i];
        if (chunk.kind != CellKind::Interior) continue;
        const float d = (chunk.anchor - Vec3{kChunkSize * 2.5f, 0, kChunkSize * 2.5f}).length();
        if (d < nearest) {
            nearest = d;
            watchedInterior = i;
        }
    }
    bool interiorWasReadyBeforeDoor = false;
    bool doorVisited = false;

    size_t maxBudgetUsed = 0;
    double maxUpdateMs = 0, totalUpdateMs = 0;
    uint64_t updates = 0;
    float heading = 0.55f;

    const int totalSteps = seconds * 60;
    for (int step = 0; step < totalSteps; ++step) {
        // After 20 s, detour to walk through the watched interior's door.
        if (!doorVisited && watchedInterior != SIZE_MAX && step > 20 * 60) {
            const Vec3 door = sm.reader().chunks()[watchedInterior].anchor;
            const Vec3 toDoor = door - pos;
            if (toDoor.length() < 1.5f) {
                doorVisited = true;  // stepped through; resume wandering
            } else {
                heading = std::atan2(toDoor.z, toDoor.x);
            }
        }
        // Wander diagonally, bouncing off the world edges.
        pos.x += std::cos(heading) * speed * static_cast<float>(dt);
        pos.z += std::sin(heading) * speed * static_cast<float>(dt);
        if (pos.x < kChunkSize || pos.x > side - kChunkSize) heading = kPi - heading;
        if (pos.z < kChunkSize || pos.z > side - kChunkSize) heading = -heading;
        // Curve gently so the route sweeps many chunks.
        heading += 0.0004f;

        const auto start = std::chrono::steady_clock::now();
        sm.update(pos);
        const auto end = std::chrono::steady_clock::now();
        const double ms = std::chrono::duration<double, std::milli>(end - start).count();
        maxUpdateMs = ms > maxUpdateMs ? ms : maxUpdateMs;
        totalUpdateMs += ms;
        ++updates;

        if (watchedInterior != SIZE_MAX) {
            const WorldChunkInfo& interior = sm.reader().chunks()[watchedInterior];
            const float doorDistance = (pos - interior.anchor).length();
            if (doorDistance < interior.enterRadius &&
                sm.chunkState(watchedInterior) == ChunkState::Resident) {
                interiorWasReadyBeforeDoor = true;
            }
        }

        if (sm.stats().budgetUsedBytes > maxBudgetUsed) {
            maxBudgetUsed = sm.stats().budgetUsedBytes;
        }

        // Pace the fast-forward so async I/O gets realistic breathing room
        // (still ~30x faster than real time).
        usleep(400);

        if (step % (60 * 20) == 0) {
            char map[512];
            sm.debugMap(map, sizeof(map), 4);
            const StreamingStats& s = sm.stats();
            printf("t=%4ds pos(%6.0f,%6.0f) resident=%u loads=%" PRIu64 " evicts=%" PRIu64
                   " assets=%u mem=%.1f/%zu MiB entities=%u\n%s",
                   step / 60, pos.x, pos.z, s.residentChunks, s.chunkLoadCount,
                   s.chunkEvictCount, s.residentAssets,
                   s.budgetUsedBytes / (1024.0 * 1024.0), budgetMiB,
                   world.entities().liveCount(), map);
        }
    }

    const StreamingStats& s = sm.stats();
    printf("\n=== streaming scale test ===\n");
    printf("traversed %.1f km over %d simulated seconds\n",
           speed * seconds / 1000.0, seconds);
    printf("chunk loads: %" PRIu64 "  evictions: %" PRIu64 "  bytes streamed: %.2f GiB\n",
           s.chunkLoadCount, s.chunkEvictCount, s.bytesLoaded / (1024.0 * 1024.0 * 1024.0));
    printf("peak streaming memory: %.1f MiB (cap %zu MiB)\n", maxBudgetUsed / (1024.0 * 1024.0),
           budgetMiB);
    printf("update time: avg %.3f ms, max %.2f ms (never blocks on I/O)\n",
           totalUpdateMs / updates, maxUpdateMs);
    printf("player-on-cold-chunk updates (flow breaks): %" PRIu64 "\n", s.playerColdUpdates);
    printf("interior resident before its door was reached: %s\n",
           interiorWasReadyBeforeDoor ? "yes" : "no/never passed one");

    sm.shutdown();
    const bool ok = s.playerColdUpdates == 0 && maxBudgetUsed <= budgetMiB * 1024 * 1024 &&
                    s.chunkEvictCount > 0 && maxUpdateMs < 100.0 &&
                    (watchedInterior == SIZE_MAX || interiorWasReadyBeforeDoor);
    if (!ok) {
        fprintf(stderr, "FAIL: coldUpdates=%" PRIu64 " peakMem=%zu maxUpdateMs=%.2f\n",
                s.playerColdUpdates, maxBudgetUsed, maxUpdateMs);
        return 1;
    }
    printf("OK\n");
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc >= 5 && strcmp(argv[1], "bake") == 0) {
        return bake(argv[2], atoi(argv[3]), atoi(argv[4]));
    }
    if (argc >= 4 && strcmp(argv[1], "run") == 0) {
        const size_t budgetMiB = argc > 4 ? static_cast<size_t>(atoi(argv[4])) : 64;
        return run(argv[2], atoi(argv[3]), budgetMiB);
    }
    fprintf(stderr,
            "usage:\n  %s bake <world> <chunksPerSide> <terrainRes>\n"
            "  %s run <world> <seconds> [budgetMiB]\n",
            argv[0], argv[0]);
    return 2;
}
