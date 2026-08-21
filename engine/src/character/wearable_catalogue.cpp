#include "mge/character/wearable_catalogue.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>

#include "mge/character/body_mesh.h"
#include "mge/core/log.h"

namespace mge {

namespace {

// ------------------------------------------------------------- parsing -----

std::string trim(const std::string& s) {
    size_t begin = 0;
    size_t end = s.size();
    while (begin < end && (s[begin] == ' ' || s[begin] == '\t' || s[begin] == '\r')) ++begin;
    while (end > begin && (s[end - 1] == ' ' || s[end - 1] == '\t' || s[end - 1] == '\r')) --end;
    return s.substr(begin, end - begin);
}

void splitTokens(const std::string& line, std::vector<std::string>& out) {
    out.clear();
    size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
        if (i >= line.size()) break;
        const size_t start = i;
        while (i < line.size() && line[i] != ' ' && line[i] != '\t') ++i;
        out.push_back(line.substr(start, i - start));
    }
}

// A region name, or one of the group aliases an artist actually thinks in.
// `arms` means both arms; nobody writes a one-sleeved tunic by accident.
bool regionBitsFor(const std::string& word, uint32_t& out) {
    static const struct {
        const char* name;
        uint32_t bits;
    } kNames[] = {
        {"scalp", regionBit(BodyRegion::Scalp)},
        {"face", regionBit(BodyRegion::Face)},
        {"neck", regionBit(BodyRegion::Neck)},
        {"torso", regionBit(BodyRegion::Torso)},
        {"arm_l", regionBit(BodyRegion::ArmL)},
        {"arm_r", regionBit(BodyRegion::ArmR)},
        {"hand_l", regionBit(BodyRegion::HandL)},
        {"hand_r", regionBit(BodyRegion::HandR)},
        {"leg_l", regionBit(BodyRegion::LegL)},
        {"leg_r", regionBit(BodyRegion::LegR)},
        {"foot_l", regionBit(BodyRegion::FootL)},
        {"foot_r", regionBit(BodyRegion::FootR)},
        // Groups.
        {"arms", kRegionsArms},
        {"hands", kRegionsHands},
        {"legs", kRegionsLegs},
        {"feet", kRegionsFeet},
        {"head", kRegionsHead},
        {"none", 0},
    };
    for (const auto& entry : kNames) {
        if (word == entry.name) {
            out = entry.bits;
            return true;
        }
    }
    return false;
}

bool parseFloat(const std::string& token, float& out) {
    char* end = nullptr;
    const double value = std::strtod(token.c_str(), &end);
    if (end == token.c_str() || *end != '\0') return false;
    out = static_cast<float>(value);
    return true;
}

bool parseUint(const std::string& token, unsigned long& out) {
    char* end = nullptr;
    out = std::strtoul(token.c_str(), &end, 10);
    return end != token.c_str() && *end == '\0';
}

WearableParseResult fail(size_t line, const std::string& message) {
    WearableParseResult result;
    result.ok = false;
    result.line = line;
    result.message = message;
    return result;
}

}  // namespace

// ------------------------------------------------------------ the format ---

WearableParseResult parseWearableDef(const std::string& text, WearableDef& out) {
    out = WearableDef{};
    bool sawVersion = false;
    bool sawCovers = false;
    std::vector<std::string> tokens;

    size_t pos = 0;
    size_t lineNumber = 0;
    while (pos <= text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        const std::string raw = trim(text.substr(pos, end - pos));
        pos = end + 1;
        ++lineNumber;

        if (raw.empty() || raw[0] == '#') {
            if (end >= text.size()) break;
            continue;
        }
        splitTokens(raw, tokens);
        if (tokens.empty()) {
            if (end >= text.size()) break;
            continue;
        }
        const std::string& key = tokens[0];

        if (key == "version") {
            unsigned long version = 0;
            if (tokens.size() != 2 || !parseUint(tokens[1], version)) {
                return fail(lineNumber, "version needs one whole number, e.g. `version 1`");
            }
            if (version != 1) {
                return fail(lineNumber,
                            "this engine reads .mgewear version 1; found version " +
                                tokens[1]);
            }
            sawVersion = true;
        } else if (key == "id") {
            if (tokens.size() != 2) return fail(lineNumber, "id needs exactly one word");
            out.id = tokens[1];
        } else if (key == "name") {
            std::string name;
            for (size_t i = 1; i < tokens.size(); ++i) {
                if (i > 1) name += ' ';
                name += tokens[i];
            }
            out.name = name;
        } else if (key == "mesh") {
            if (tokens.size() != 2) {
                return fail(lineNumber,
                            "mesh needs exactly one asset stem, e.g. `mesh garment_tunic` "
                            "(the engine appends .mgeskin and .mgefit)");
            }
            out.mesh = tokens[1];
        } else if (key == "layer") {
            unsigned long layer = 0;
            if (tokens.size() != 2 || !parseUint(tokens[1], layer) || layer > 2) {
                return fail(lineNumber, "layer must be 0 (base), 1 (mid) or 2 (outer)");
            }
            out.layer = static_cast<uint8_t>(layer);
        } else if (key == "covers") {
            if (tokens.size() < 2) {
                return fail(lineNumber,
                            "covers needs at least one region, or `none`. Regions: scalp "
                            "face neck torso arm_l arm_r hand_l hand_r leg_l leg_r foot_l "
                            "foot_r, or the groups arms hands legs feet head");
            }
            uint32_t bits = 0;
            for (size_t i = 1; i < tokens.size(); ++i) {
                uint32_t one = 0;
                if (!regionBitsFor(tokens[i], one)) {
                    return fail(lineNumber, "`" + tokens[i] +
                                                "` is not a body region or region group");
                }
                bits |= one;
            }
            out.covers = bits;
            sawCovers = true;
        } else if (key == "thickness_mm") {
            float mm = 0;
            if (tokens.size() != 2 || !parseFloat(tokens[1], mm) || mm < 0 || mm > 100) {
                return fail(lineNumber, "thickness_mm must be a number between 0 and 100");
            }
            out.thicknessMm = mm;
        } else if (key == "color") {
            if (tokens.size() != 5) {
                return fail(lineNumber, "color needs four numbers: r g b a, each 0..1");
            }
            for (int c = 0; c < 4; ++c) {
                if (!parseFloat(tokens[static_cast<size_t>(c) + 1], out.color[c])) {
                    return fail(lineNumber, "color components must be numbers 0..1");
                }
            }
        } else if (key == "held") {
            if (tokens.size() != 2 || (tokens[1] != "true" && tokens[1] != "false")) {
                return fail(lineNumber, "held must be `true` or `false`");
            }
            out.held = tokens[1] == "true";
        } else {
            return fail(lineNumber, "unknown key `" + key +
                                        "`. Known keys: version id name mesh layer covers "
                                        "thickness_mm color held");
        }
        if (end >= text.size()) break;
    }

    if (!sawVersion) return fail(0, "missing `version 1` — every content file declares one");
    if (out.id.empty()) return fail(0, "missing `id` — the stable name data refers to");
    if (out.mesh.empty() && !out.held) {
        return fail(0, "missing `mesh` — a fitted garment needs geometry");
    }
    if (!sawCovers && !out.held) {
        return fail(0,
                    "missing `covers` — say which body regions this garment masks, or "
                    "`covers none` if it masks nothing (hair over a scalp that still shows)");
    }
    if (out.name.empty()) out.name = out.id;

    WearableParseResult result;
    result.ok = true;
    return result;
}

// ---------------------------------------------------------- the catalogue --

const char* wearableKindId(WearableKind kind) {
    switch (kind) {
        case WearableKind::Tunic: return "tunic";
        case WearableKind::Armor: return "armour";
        case WearableKind::Pants: return "trousers";
        case WearableKind::Boots: return "boots";
        case WearableKind::HairShort: return "hair_short";
        case WearableKind::HairLong: return "hair_long";
        case WearableKind::Sword: return "sword";
    }
    return "";
}

size_t WearableCatalogue::find(const char* id) const {
    if (id == nullptr) return npos;
    for (size_t i = 0; i < entries_.size(); ++i) {
        if (entries_[i].id == id) return i;
    }
    return npos;
}

size_t WearableCatalogue::indexOf(WearableKind kind) const {
    return find(wearableKindId(kind));
}

const WearableDef* WearableCatalogue::def(WearableKind kind) const {
    const size_t index = indexOf(kind);
    return index == npos ? nullptr : &entries_[index];
}

void WearableCatalogue::clear() {
    entries_.clear();
    refusals_.clear();
}

void WearableCatalogue::loadFromDirectory(const char* dir) {
    clear();
    if (dir == nullptr) return;

    // Sorted, so the catalogue is the same on every machine and a diff of a
    // report is a diff of the content (determinism, MODELING.md §3.7).
    std::vector<std::string> files;
    DIR* handle = ::opendir(dir);
    if (handle == nullptr) {
        MGE_LOGE("wearables", "no wearable directory at %s", dir);
        return;
    }
    while (const dirent* entry = ::readdir(handle)) {
        const std::string name = entry->d_name;
        if (name.size() > 8 && name.compare(name.size() - 8, 8, ".mgewear") == 0) {
            files.push_back(name);
        }
    }
    ::closedir(handle);
    std::sort(files.begin(), files.end());

    for (const std::string& file : files) {
        if (entries_.size() >= kMaxEntries) {
            refusals_.push_back(file + ": catalogue is full (" +
                                std::to_string(kMaxEntries) + " wearables)");
            continue;
        }
        const std::string path = std::string(dir) + "/" + file;
        std::FILE* handle2 = std::fopen(path.c_str(), "rb");
        if (handle2 == nullptr) {
            refusals_.push_back(file + ": could not be opened");
            continue;
        }
        std::fseek(handle2, 0, SEEK_END);
        const long size = std::ftell(handle2);
        std::fseek(handle2, 0, SEEK_SET);
        std::string text;
        if (size > 0) {
            text.resize(static_cast<size_t>(size));
            const size_t read = std::fread(&text[0], 1, text.size(), handle2);
            text.resize(read);
        }
        std::fclose(handle2);

        WearableDef def;
        const WearableParseResult parsed = parseWearableDef(text, def);
        if (!parsed.ok) {
            const std::string where =
                parsed.line > 0 ? ":" + std::to_string(parsed.line) : "";
            refusals_.push_back(file + where + ": " + parsed.message);
            MGE_LOGE("wearables", "%s%s: %s", file.c_str(), where.c_str(),
                     parsed.message.c_str());
            continue;
        }
        if (find(def.id.c_str()) != npos) {
            refusals_.push_back(file + ": id `" + def.id + "` is already taken");
            continue;
        }
        entries_.push_back(def);
    }
}

const WearableCatalogue& wearableCatalogue() {
    static WearableCatalogue catalogue = [] {
        WearableCatalogue loaded;
        loaded.loadFromDirectory(characterAssetDir());
        return loaded;
    }();
    return catalogue;
}

void reloadWearableCatalogue() {
    // The shared instance is const to everyone else; reloading is a tooling
    // operation, so it casts here rather than handing out a mutable catalogue.
    WearableCatalogue& mutableCatalogue = const_cast<WearableCatalogue&>(wearableCatalogue());
    mutableCatalogue.loadFromDirectory(characterAssetDir());
}

}  // namespace mge
