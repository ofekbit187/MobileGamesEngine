// Phase 6: save/load round-trip, atomicity under injected process death
// (the kill-test suite), checksum rejection, schema migration, and the
// delta-apply path through streaming.

#include <cstdio>
#include <cstdlib>
#include <string>

#include "mge/framework/save.h"
#include "mge/graphics/primitives.h"
#include "mge/streaming/streaming_manager.h"
#include "test_framework.h"

using namespace mge;

namespace {

std::string tmpDir() {
    std::string dir = "/tmp";
    if (const char* t = getenv("TMPDIR")) dir = t;
    return dir;
}

SaveSnapshot makeSnapshot() {
    SaveSnapshot snapshot;
    snapshot.player.position = {12.5f, 0.9f, -40.0f};
    snapshot.player.yaw = 1.25f;
    snapshot.player.health = 0.62f;
    snapshot.playtimeSeconds = 3600.5;
    snapshot.deltas.recordRemoved(7, 3);
    snapshot.deltas.recordMoved(7, 5, {1, 2, 3}, 0.5f);
    SpawnedEntity spawned;
    spawned.asset = assetIdFromName("prop/crate");
    spawned.position = {4, 0.5f, 6};
    snapshot.deltas.recordSpawned(9, spawned);
    SavedCollection backpack;
    backpack.id = assetIdFromName("player.backpack");
    backpack.items.push_back({assetIdFromName("item/apple"), 5, {0.8f, 0.2f, 0.2f, 1}});
    snapshot.collections.push_back(backpack);
    return snapshot;
}

}  // namespace

MGE_TEST(save_roundtrip) {
    SaveManager saves(tmpDir().c_str());
    MGE_CHECK(saves.save("test_slot", makeSnapshot()));

    SaveSnapshot loaded;
    MGE_CHECK(saves.load("test_slot", loaded));
    MGE_CHECK_NEAR(loaded.player.position.x, 12.5f, 1e-6);
    MGE_CHECK_NEAR(loaded.player.health, 0.62f, 1e-6);
    MGE_CHECK_NEAR(loaded.playtimeSeconds, 3600.5, 1e-6);
    MGE_CHECK(loaded.deltas.isRemoved(7, 3));
    MGE_CHECK(!loaded.deltas.isRemoved(7, 4));
    const MovedPlacement* moved = loaded.deltas.findMoved(7, 5);
    MGE_CHECK(moved != nullptr);
    MGE_CHECK_NEAR(moved->position.y, 2.0f, 1e-6);
    const ChunkDelta* delta9 = loaded.deltas.find(9);
    MGE_CHECK(delta9 != nullptr && delta9->spawned.size() == 1);
    MGE_CHECK(loaded.collections.size() == 1);
    MGE_CHECK(loaded.collections[0].items[0].count == 5);

    // Slot metadata (task 6.5).
    SaveSlotInfo info;
    MGE_CHECK(saves.slotInfo("test_slot", info));
    MGE_CHECK(info.valid);
    MGE_CHECK(info.schemaVersion == kSaveSchemaVersion);
    MGE_CHECK(info.timestampUnix > 0);
    MGE_CHECK_NEAR(info.playtimeSeconds, 3600.5, 1e-6);

    std::vector<SaveSlotInfo> slots;
    saves.listSlots(slots);
    bool found = false;
    for (const SaveSlotInfo& slot : slots) {
        if (slot.name == "test_slot") found = true;
    }
    MGE_CHECK(found);
    saves.removeSlot("test_slot");
}

MGE_TEST(characters_persist_in_save_files) {
    // Task 8.9: SavedCharacter records ride the same atomic, checksummed
    // save file as everything else — schema v3.
    SaveManager saves(tmpDir().c_str());
    SaveSnapshot snapshot = makeSnapshot();
    SavedCharacter guard;
    guard.persistentId = 11;
    guard.health = 0.8f;
    guard.faction = 1;
    guard.controller = static_cast<uint8_t>(ControllerKind::Ai);
    guard.items.push_back({assetIdFromName("item/bread"), 3, {1, 1, 1, 1}});
    auto& held = guard.equipment[static_cast<size_t>(EquipSlot::HeldMain)];
    held.item = {assetIdFromName("item/sword"), 1, {1, 1, 1, 1}};
    held.sheathed = 1;
    snapshot.characters.push_back(guard);

    MGE_CHECK(saves.save("char_slot", snapshot));
    SaveSnapshot loaded;
    MGE_CHECK(saves.load("char_slot", loaded));
    MGE_CHECK(loaded.characters.size() == 1);
    const SavedCharacter& r = loaded.characters[0];
    MGE_CHECK(r.persistentId == 11);
    MGE_CHECK_NEAR(r.health, 0.8f, 1e-6f);
    MGE_CHECK(r.faction == 1);
    MGE_CHECK(r.items.size() == 1 && r.items[0].count == 3);
    MGE_CHECK(r.equipment[static_cast<size_t>(EquipSlot::HeldMain)].item.asset ==
              assetIdFromName("item/sword"));
    MGE_CHECK(r.equipment[static_cast<size_t>(EquipSlot::HeldMain)].sheathed == 1);
    saves.removeSlot("char_slot");
}

MGE_TEST(kill_test_death_mid_save_preserves_previous) {
    SaveManager saves(tmpDir().c_str());

    // A good save exists.
    SaveSnapshot original = makeSnapshot();
    MGE_CHECK(saves.save("kill_slot", original));

    // A later save "dies" at various points during the write — including
    // after 0 bytes, mid-header, and mid-payload — always before the rename.
    SaveSnapshot changed = makeSnapshot();
    changed.player.health = 0.11f;
    for (long killAt : {0L, 10L, 60L, 200L}) {
        saves.testKillDuringWrite(killAt);
        MGE_CHECK(!saves.save("kill_slot", changed));  // the "crash"
    }
    saves.testKillDuringWrite(-1);

    // The previous save is fully intact — not corrupted, not half-new.
    SaveSnapshot recovered;
    MGE_CHECK(saves.load("kill_slot", recovered));
    MGE_CHECK_NEAR(recovered.player.health, 0.62f, 1e-6);

    // And a clean save afterwards still works (tmp garbage tolerated).
    MGE_CHECK(saves.save("kill_slot", changed));
    MGE_CHECK(saves.load("kill_slot", recovered));
    MGE_CHECK_NEAR(recovered.player.health, 0.11f, 1e-6);
    saves.removeSlot("kill_slot");
}

MGE_TEST(corrupted_save_rejected_cleanly) {
    SaveManager saves(tmpDir().c_str());
    MGE_CHECK(saves.save("corrupt_slot", makeSnapshot()));

    // Flip a byte in the payload region.
    const std::string path = tmpDir() + "/corrupt_slot.mgesave";
    FILE* f = fopen(path.c_str(), "r+b");
    fseek(f, 80, SEEK_SET);
    const int byte = fgetc(f);
    fseek(f, 80, SEEK_SET);
    fputc(byte ^ 0xFF, f);
    fclose(f);

    SaveSnapshot loaded;
    MGE_CHECK(!saves.load("corrupt_slot", loaded));  // checksum gate
    SaveSlotInfo info;
    MGE_CHECK(saves.slotInfo("corrupt_slot", info));
    MGE_CHECK(!info.valid);
    saves.removeSlot("corrupt_slot");
}

MGE_TEST(schema_migration_v1_to_v2) {
    // Hand-craft a v1 save (no player health field) and load it: the
    // migration chain must fill health with the default.
    SaveManager saves(tmpDir().c_str());
    struct PlayerV1 {
        Vec3 position;
        float yaw;
    } playerV1{{5, 0, 5}, 0.7f};

    std::vector<uint8_t> payload;
    auto append = [&](const void* d, size_t n) {
        const uint8_t* b = static_cast<const uint8_t*>(d);
        payload.insert(payload.end(), b, b + n);
    };
    append(&playerV1, sizeof(playerV1));
    const uint32_t zero = 0;
    append(&zero, sizeof(zero));  // no chunk deltas
    append(&zero, sizeof(zero));  // no collections

    struct {
        char magic[4] = {'M', 'G', 'E', 'S'};
        uint32_t fileVersion = 1;
        uint32_t schemaVersion = 1;  // OLD schema
        uint32_t pad = 0;
        uint64_t payloadSize;
        uint64_t payloadChecksum;
        uint64_t timestampUnix = 12345;
        double playtimeSeconds = 10;
    } header;
    header.payloadSize = payload.size();
    header.payloadChecksum = fnv1a(payload.data(), payload.size());

    const std::string path = tmpDir() + "/old_slot.mgesave";
    FILE* f = fopen(path.c_str(), "wb");
    fwrite(&header, sizeof(header), 1, f);
    fwrite(payload.data(), 1, payload.size(), f);
    fclose(f);

    SaveSnapshot loaded;
    MGE_CHECK(saves.load("old_slot", loaded));
    MGE_CHECK_NEAR(loaded.player.position.x, 5.0f, 1e-6);
    MGE_CHECK_NEAR(loaded.player.health, 1.0f, 1e-6);  // migration default
    MGE_CHECK(loaded.characters.empty());              // v2->v3: none recorded

    // A save from the future is rejected, never misread.
    header.schemaVersion = 999;
    f = fopen(path.c_str(), "wb");
    fwrite(&header, sizeof(header), 1, f);
    fwrite(payload.data(), 1, payload.size(), f);
    fclose(f);
    MGE_CHECK(!saves.load("old_slot", loaded));
    saves.removeSlot("old_slot");
}

// --- delta-apply through streaming (task 6.4) -------------------------------

namespace {
std::string bakeDeltaWorld() {
    WorldBaker baker(32.0f);
    LodMesh box;
    box.lods.push_back(makeBox({1, 1, 1}));
    box.computeBounds();
    const AssetId crate = baker.addMeshAsset("prop/crate", box);
    // Chunk (0,0): two crates at known placements.
    baker.place(crate, {5, 0.5f, 5}, 0, 1, 1, 1);    // placement 0
    baker.place(crate, {10, 0.5f, 10}, 0, 1, 1, 1);  // placement 1
    const std::string path = tmpDir() + "/mge_delta.mgeworld";
    baker.write(path.c_str());
    return path;
}

void settle(StreamingManager& sm, AsyncIO& io, const Vec3& pos, int rounds = 12) {
    for (int i = 0; i < rounds; ++i) {
        sm.update(pos);
        io.drain();
    }
}
}  // namespace

MGE_TEST(streaming_applies_and_records_deltas) {
    const std::string worldPath = bakeDeltaWorld();

    WorldDeltaLog deltas;
    // --- session 1: play, mutate, "save" (keep the log) ---
    {
        World world(128);
        AssetRegistry assets;
        AsyncIO io;
        BudgetRegistry budgets;
        StreamingManager sm;
        sm.setDeltaLog(&deltas);
        MGE_CHECK(sm.init(worldPath.c_str(), world, assets, io, budgets, StreamingConfig{}));
        settle(sm, io, {16, 0, 16});
        MGE_CHECK(world.entities().liveCount() == 2);

        // Remove crate 0, move crate 1, drop a new dynamic crate.
        EntityId first = kInvalidEntity, second = kInvalidEntity;
        world.forEachRenderable([&](EntityId e, const TransformComponent& t,
                                    const ModelComponent&) {
            if (t.position.x < 7) first = e;
            else second = e;
        });
        MGE_CHECK(sm.removeStreamedEntity(first));
        MGE_CHECK(sm.moveStreamedEntity(second, {20, 0.5f, 20}, 1.0f));
        const float color[4] = {1, 0, 0, 1};
        const EntityId dropped =
            sm.spawnDynamic(assetIdFromName("prop/crate"), {8, 0.5f, 8}, 0.3f, color);
        MGE_CHECK(dropped != kInvalidEntity);
        MGE_CHECK(world.entities().liveCount() == 2);  // 1 shipped + 1 dynamic
        sm.shutdown();
    }

    // --- persist through an actual save file ---
    SaveManager saves(tmpDir().c_str());
    SaveSnapshot snapshot;
    snapshot.deltas = deltas;
    MGE_CHECK(saves.save("delta_slot", snapshot));
    SaveSnapshot loadedSnapshot;
    MGE_CHECK(saves.load("delta_slot", loadedSnapshot));
    saves.removeSlot("delta_slot");

    // --- session 2: fresh world + loaded deltas -> changes are back ---
    {
        World world(128);
        AssetRegistry assets;
        AsyncIO io;
        BudgetRegistry budgets;
        StreamingManager sm;
        sm.setDeltaLog(&loadedSnapshot.deltas);
        MGE_CHECK(sm.init(worldPath.c_str(), world, assets, io, budgets, StreamingConfig{}));
        settle(sm, io, {16, 0, 16});

        // 1 shipped survivor (moved) + 1 dynamic spawn; the removed crate is gone.
        MGE_CHECK(world.entities().liveCount() == 2);
        bool movedFound = false, droppedFound = false;
        world.forEachRenderable([&](EntityId, const TransformComponent& t,
                                    const ModelComponent&) {
            if (t.position.x > 19) movedFound = true;
            if (t.position.x > 7 && t.position.x < 9) droppedFound = true;
        });
        MGE_CHECK(movedFound);
        MGE_CHECK(droppedFound);
        sm.shutdown();
    }
}
