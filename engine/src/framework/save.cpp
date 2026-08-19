#include "mge/framework/save.h"

#include <cstdio>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <unistd.h>

#include "mge/core/log.h"

namespace mge {

namespace {

constexpr const char* kTag = "save";
constexpr char kMagic[4] = {'M', 'G', 'E', 'S'};
constexpr uint32_t kFileVersion = 1;

struct FileHeader {
    char magic[4];
    uint32_t fileVersion;
    uint32_t schemaVersion;
    uint32_t pad;
    uint64_t payloadSize;
    uint64_t payloadChecksum;
    uint64_t timestampUnix;
    double playtimeSeconds;
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

// v1 payloads had no player health; the migration gives them full health.
// (A worked example of the chain — real migrations accrete here.)
bool migrateV1ToV2(SaveSnapshot& snapshot) {
    snapshot.player.health = 1.0f;
    return true;
}

// v2 payloads predate character persistence: no characters to restore.
bool migrateV2ToV3(SaveSnapshot& snapshot) {
    snapshot.characters.clear();
    return true;
}

// v3 characters predate status effects: none recorded.
bool migrateV3ToV4(SaveSnapshot& snapshot) {
    for (SavedCharacter& character : snapshot.characters) character.effects.clear();
    return true;
}

void serializePayload(const SaveSnapshot& snapshot, std::vector<uint8_t>& out) {
    append(out, &snapshot.player, sizeof(PlayerState));
    snapshot.deltas.serialize(out);
    const uint32_t collectionCount = static_cast<uint32_t>(snapshot.collections.size());
    append(out, &collectionCount, sizeof(collectionCount));
    for (const SavedCollection& collection : snapshot.collections) {
        append(out, &collection.id, sizeof(collection.id));
        const uint32_t itemCount = static_cast<uint32_t>(collection.items.size());
        append(out, &itemCount, sizeof(itemCount));
        append(out, collection.items.data(), itemCount * sizeof(SavedItem));
    }
    // v3 section: characters (task 8.9).
    const uint32_t characterCount = static_cast<uint32_t>(snapshot.characters.size());
    append(out, &characterCount, sizeof(characterCount));
    for (const SavedCharacter& character : snapshot.characters) {
        append(out, &character.persistentId, sizeof(character.persistentId));
        append(out, &character.health, sizeof(character.health));
        append(out, &character.maxHealth, sizeof(character.maxHealth));
        append(out, &character.faction, sizeof(character.faction));
        append(out, &character.alive, sizeof(character.alive));
        append(out, &character.controller, sizeof(character.controller));
        append(out, &character.sightRange, sizeof(character.sightRange));
        const uint32_t itemCount = static_cast<uint32_t>(character.items.size());
        append(out, &itemCount, sizeof(itemCount));
        append(out, character.items.data(), itemCount * sizeof(SavedCharacterItem));
        append(out, character.equipment, sizeof(character.equipment));
        // v4 section: status effects (task 9.6).
        const uint32_t effectCount = static_cast<uint32_t>(character.effects.size());
        append(out, &effectCount, sizeof(effectCount));
        append(out, character.effects.data(), effectCount * sizeof(SavedStatusEffect));
    }
}

bool deserializePayload(const uint8_t* data, size_t size, uint32_t schemaVersion,
                        SaveSnapshot& out) {
    const uint8_t* cursor = data;
    size_t remaining = size;

    if (schemaVersion >= 2) {
        if (!read(cursor, remaining, &out.player, sizeof(PlayerState))) return false;
    } else {
        // v1 player record had no health field.
        struct PlayerV1 {
            Vec3 position;
            float yaw;
        } v1{};
        if (!read(cursor, remaining, &v1, sizeof(v1))) return false;
        out.player.position = v1.position;
        out.player.yaw = v1.yaw;
    }
    if (!out.deltas.deserialize(cursor, remaining)) return false;

    uint32_t collectionCount = 0;
    if (!read(cursor, remaining, &collectionCount, sizeof(collectionCount)) ||
        collectionCount > 256) {
        return false;
    }
    out.collections.resize(collectionCount);
    for (SavedCollection& collection : out.collections) {
        uint32_t itemCount = 0;
        if (!read(cursor, remaining, &collection.id, sizeof(collection.id)) ||
            !read(cursor, remaining, &itemCount, sizeof(itemCount)) || itemCount > 4096) {
            return false;
        }
        collection.items.resize(itemCount);
        if (!read(cursor, remaining, collection.items.data(), itemCount * sizeof(SavedItem))) {
            return false;
        }
    }

    if (schemaVersion >= 3) {
        uint32_t characterCount = 0;
        if (!read(cursor, remaining, &characterCount, sizeof(characterCount)) ||
            characterCount > 4096) {
            return false;
        }
        out.characters.resize(characterCount);
        for (SavedCharacter& character : out.characters) {
            uint32_t itemCount = 0;
            if (!read(cursor, remaining, &character.persistentId,
                      sizeof(character.persistentId)) ||
                !read(cursor, remaining, &character.health, sizeof(character.health)) ||
                !read(cursor, remaining, &character.maxHealth, sizeof(character.maxHealth)) ||
                !read(cursor, remaining, &character.faction, sizeof(character.faction)) ||
                !read(cursor, remaining, &character.alive, sizeof(character.alive)) ||
                !read(cursor, remaining, &character.controller, sizeof(character.controller)) ||
                !read(cursor, remaining, &character.sightRange, sizeof(character.sightRange)) ||
                !read(cursor, remaining, &itemCount, sizeof(itemCount)) ||
                itemCount > ItemCollection::kCapacity) {
                return false;
            }
            character.items.resize(itemCount);
            if (!read(cursor, remaining, character.items.data(),
                      itemCount * sizeof(SavedCharacterItem)) ||
                !read(cursor, remaining, character.equipment, sizeof(character.equipment))) {
                return false;
            }
            if (schemaVersion >= 4) {
                uint32_t effectCount = 0;
                if (!read(cursor, remaining, &effectCount, sizeof(effectCount)) ||
                    effectCount > kMaxStatusEffects) {
                    return false;
                }
                character.effects.resize(effectCount);
                if (!read(cursor, remaining, character.effects.data(),
                          effectCount * sizeof(SavedStatusEffect))) {
                    return false;
                }
            }
        }
    }
    return true;
}

}  // namespace

uint64_t fnv1a(const uint8_t* data, size_t size) {
    uint64_t hash = 1469598103934665603ull;
    for (size_t i = 0; i < size; ++i) {
        hash ^= data[i];
        hash *= 1099511628211ull;
    }
    return hash;
}

std::string SaveManager::slotPath(const char* slotName) const {
    return directory_ + "/" + slotName + ".mgesave";
}

bool SaveManager::save(const char* slotName, const SaveSnapshot& snapshot) {
    std::vector<uint8_t> payload;
    payload.reserve(4096);
    serializePayload(snapshot, payload);

    FileHeader header{};
    memcpy(header.magic, kMagic, 4);
    header.fileVersion = kFileVersion;
    header.schemaVersion = kSaveSchemaVersion;
    header.payloadSize = payload.size();
    header.payloadChecksum = fnv1a(payload.data(), payload.size());
    header.timestampUnix = static_cast<uint64_t>(time(nullptr));
    header.playtimeSeconds = snapshot.playtimeSeconds;

    const std::string finalPath = slotPath(slotName);
    const std::string tmpPath = finalPath + ".tmp";

    FILE* f = fopen(tmpPath.c_str(), "wb");
    if (f == nullptr) {
        MGE_LOGE(kTag, "cannot open %s", tmpPath.c_str());
        return false;
    }

    // Kill-test hook: write only N bytes, then "die" before the rename.
    if (killAfterBytes_ >= 0) {
        std::vector<uint8_t> whole;
        append(whole, &header, sizeof(header));
        append(whole, payload.data(), payload.size());
        const size_t toWrite =
            static_cast<size_t>(killAfterBytes_) < whole.size()
                ? static_cast<size_t>(killAfterBytes_)
                : whole.size();
        fwrite(whole.data(), 1, toWrite, f);
        fclose(f);
        return false;  // process "died" mid-save; previous save untouched
    }

    bool ok = fwrite(&header, sizeof(header), 1, f) == 1;
    ok = ok && (payload.empty() ||
                fwrite(payload.data(), 1, payload.size(), f) == payload.size());
    ok = ok && fflush(f) == 0 && fsync(fileno(f)) == 0;
    fclose(f);
    if (!ok) {
        remove(tmpPath.c_str());
        return false;
    }
    // The atomic step: the previous save exists untouched until this succeeds.
    if (rename(tmpPath.c_str(), finalPath.c_str()) != 0) {
        MGE_LOGE(kTag, "rename failed for %s", finalPath.c_str());
        remove(tmpPath.c_str());
        return false;
    }
    return true;
}

bool SaveManager::load(const char* slotName, SaveSnapshot& out) {
    const std::string path = slotPath(slotName);
    FILE* f = fopen(path.c_str(), "rb");
    if (f == nullptr) return false;

    FileHeader header{};
    bool ok = fread(&header, sizeof(header), 1, f) == 1 &&
              memcmp(header.magic, kMagic, 4) == 0 && header.fileVersion == kFileVersion;
    if (ok && header.schemaVersion > kSaveSchemaVersion) {
        MGE_LOGE(kTag, "save %s is from a newer engine (schema %u)", slotName,
                 header.schemaVersion);
        ok = false;
    }
    std::vector<uint8_t> payload;
    if (ok) {
        payload.resize(header.payloadSize);
        ok = fread(payload.data(), 1, payload.size(), f) == payload.size();
    }
    fclose(f);
    if (!ok) return false;

    // Checksum gate: nothing is applied from a torn or corrupted file.
    if (fnv1a(payload.data(), payload.size()) != header.payloadChecksum) {
        MGE_LOGE(kTag, "save %s failed checksum — rejected", slotName);
        return false;
    }

    out = SaveSnapshot{};
    out.playtimeSeconds = header.playtimeSeconds;
    if (!deserializePayload(payload.data(), payload.size(), header.schemaVersion, out)) {
        MGE_LOGE(kTag, "save %s failed to deserialize", slotName);
        return false;
    }

    // Migration chain: upgrade one schema step at a time.
    uint32_t version = header.schemaVersion;
    while (version < kSaveSchemaVersion) {
        bool migrated = false;
        switch (version) {
            case 1: migrated = migrateV1ToV2(out); break;
            case 2: migrated = migrateV2ToV3(out); break;
            case 3: migrated = migrateV3ToV4(out); break;
            default: break;
        }
        if (!migrated) {
            MGE_LOGE(kTag, "no migration path from schema %u", version);
            return false;
        }
        ++version;
    }
    return true;
}

bool SaveManager::slotInfo(const char* slotName, SaveSlotInfo& out) const {
    out = SaveSlotInfo{};
    out.name = slotName;
    FILE* f = fopen(slotPath(slotName).c_str(), "rb");
    if (f == nullptr) return false;
    FileHeader header{};
    bool ok = fread(&header, sizeof(header), 1, f) == 1 &&
              memcmp(header.magic, kMagic, 4) == 0;
    std::vector<uint8_t> payload;
    if (ok) {
        payload.resize(header.payloadSize);
        ok = fread(payload.data(), 1, payload.size(), f) == payload.size();
    }
    fclose(f);
    if (!ok) return false;
    out.schemaVersion = header.schemaVersion;
    out.timestampUnix = header.timestampUnix;
    out.playtimeSeconds = header.playtimeSeconds;
    out.valid = fnv1a(payload.data(), payload.size()) == header.payloadChecksum;
    return true;
}

void SaveManager::listSlots(std::vector<SaveSlotInfo>& out) const {
    DIR* dir = opendir(directory_.c_str());
    if (dir == nullptr) return;
    while (dirent* entry = readdir(dir)) {
        const char* name = entry->d_name;
        const size_t len = strlen(name);
        const char* suffix = ".mgesave";
        const size_t suffixLen = strlen(suffix);
        if (len <= suffixLen || strcmp(name + len - suffixLen, suffix) != 0) continue;
        const std::string stem(name, len - suffixLen);
        SaveSlotInfo info;
        if (slotInfo(stem.c_str(), info)) out.push_back(std::move(info));
    }
    closedir(dir);
}

bool SaveManager::removeSlot(const char* slotName) {
    return remove(slotPath(slotName).c_str()) == 0;
}

}  // namespace mge
