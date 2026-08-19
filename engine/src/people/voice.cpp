#include "mge/people/voice.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>

#include "mge/core/log.h"

namespace mge {

namespace {

constexpr const char* kTag = "voice";

bool makeDirs(const std::string& path) {
    std::string partial;
    partial.reserve(path.size());
    for (size_t i = 0; i <= path.size(); ++i) {
        if (i == path.size() || path[i] == '/') {
            if (!partial.empty() && partial != "/") {
                if (mkdir(partial.c_str(), 0755) != 0 && errno != EEXIST) return false;
            }
        }
        if (i < path.size()) partial.push_back(path[i]);
    }
    return true;
}

bool writeTextFile(const std::string& path, const std::string& contents) {
    FILE* f = fopen(path.c_str(), "wb");
    if (f == nullptr) return false;
    const bool ok =
        contents.empty() || fwrite(contents.data(), 1, contents.size(), f) == contents.size();
    fclose(f);
    return ok;
}

bool readTextFile(const std::string& path, std::string& out) {
    FILE* f = fopen(path.c_str(), "rb");
    if (f == nullptr) return false;
    fseek(f, 0, SEEK_END);
    const long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    out.resize(size > 0 ? static_cast<size_t>(size) : 0);
    const bool ok = out.empty() || fread(&out[0], 1, out.size(), f) == out.size();
    fclose(f);
    return ok;
}

// line.txt: recognized "key: value" header lines until a blank line, then
// the text. Only known keys count as headers, so a colon inside the spoken
// text can't be swallowed by mistake.
void parseLineFile(const std::string& raw, VoiceLine& out) {
    size_t pos = 0;
    size_t bodyStart = 0;
    while (pos < raw.size()) {
        size_t end = raw.find('\n', pos);
        if (end == std::string::npos) end = raw.size();
        std::string line = raw.substr(pos, end - pos);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) {
            bodyStart = end < raw.size() ? end + 1 : raw.size();  // separator
            break;
        }
        const size_t colon = line.find(':');
        const std::string key = colon != std::string::npos ? line.substr(0, colon) : "";
        if (key != "tone") {
            bodyStart = pos;  // not a header: the body starts here
            break;
        }
        std::string value = line.substr(colon + 1);
        while (!value.empty() && value.front() == ' ') value.erase(value.begin());
        out.tone = value;
        pos = end < raw.size() ? end + 1 : raw.size();
        bodyStart = pos;
    }
    out.text = raw.substr(bodyStart < raw.size() ? bodyStart : raw.size());
    while (!out.text.empty() && (out.text.back() == '\n' || out.text.back() == '\r')) {
        out.text.pop_back();
    }
}

void jsonEscapeInto(const std::string& in, std::string& out) {
    for (char c : in) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': break;
            case '\t': out += "\\t"; break;
            default: out.push_back(c);
        }
    }
}

}  // namespace

std::string personVoiceDir(const char* root, const char* lang, uint32_t personId) {
    char buffer[64];
    snprintf(buffer, sizeof(buffer), "/%s/p%u", lang, personId);
    return std::string(root) + buffer;
}

bool writeVoiceDescription(const char* root, const char* lang, uint32_t personId,
                           const char* description) {
    const std::string dir = personVoiceDir(root, lang, personId);
    if (!makeDirs(dir)) return false;
    return writeTextFile(dir + "/voice.txt", description);
}

bool writeVoiceLine(const char* root, const char* lang, uint32_t personId,
                    const char* lineId, const char* tone, const char* text) {
    const std::string dir = personVoiceDir(root, lang, personId) + "/lines/" + lineId;
    if (!makeDirs(dir)) return false;
    std::string contents;
    if (tone != nullptr && tone[0] != '\0') {
        contents += "tone: ";
        contents += tone;
        contents += "\n\n";
    }
    contents += text;
    contents += "\n";
    return writeTextFile(dir + "/line.txt", contents);
}

bool loadPersonVoice(const char* root, const char* lang, uint32_t personId,
                     PersonVoice& out) {
    out = PersonVoice{};
    out.personId = personId;
    const std::string dir = personVoiceDir(root, lang, personId);
    readTextFile(dir + "/voice.txt", out.description);
    while (!out.description.empty() &&
           (out.description.back() == '\n' || out.description.back() == '\r')) {
        out.description.pop_back();
    }

    const std::string linesDir = dir + "/lines";
    DIR* lines = opendir(linesDir.c_str());
    if (lines == nullptr) return false;
    while (dirent* entry = readdir(lines)) {
        if (entry->d_name[0] == '.') continue;
        const std::string lineDir = linesDir + "/" + entry->d_name;
        std::string raw;
        if (!readTextFile(lineDir + "/line.txt", raw)) continue;  // not a line folder
        VoiceLine line;
        line.id = entry->d_name;
        parseLineFile(raw, line);
        // Delivered takes: every .wav in the line's folder.
        if (DIR* takes = opendir(lineDir.c_str())) {
            while (dirent* take = readdir(takes)) {
                const size_t len = strlen(take->d_name);
                if (len > 4 && strcmp(take->d_name + len - 4, ".wav") == 0) {
                    line.takes.push_back(lineDir + "/" + take->d_name);
                }
            }
            closedir(takes);
        }
        out.lines.push_back(std::move(line));
    }
    closedir(lines);
    return true;
}

bool validateVoices(const char* root, const char* lang, const FamilyTree& tree,
                    std::string* firstMissing) {
    for (uint16_t i = 0; i < tree.people().size(); ++i) {
        if (!tree.people()[i].alive) continue;  // history doesn't speak
        PersonVoice voice;
        if (!loadPersonVoice(root, lang, tree.people()[i].personId, voice) ||
            voice.lines.empty()) {
            if (firstMissing != nullptr) *firstMissing = tree.fullNameOf(i);
            MGE_LOGW(kTag, "person %u (%s) has no %s text lines",
                     tree.people()[i].personId, tree.fullNameOf(i).c_str(), lang);
            return false;
        }
    }
    return true;
}

size_t exportVoiceManifest(const char* root, const char* lang, const FamilyTree& tree,
                           const char* manifestPath) {
    std::string json = "{\n  \"language\": \"";
    json += lang;
    json += "\",\n  \"unrecorded\": [\n";
    size_t unrecorded = 0;
    for (uint16_t i = 0; i < tree.people().size(); ++i) {
        const PersonRecord& person = tree.people()[i];
        if (!person.alive) continue;
        PersonVoice voice;
        if (!loadPersonVoice(root, lang, person.personId, voice)) continue;
        for (const VoiceLine& line : voice.lines) {
            if (!line.takes.empty()) continue;  // recorded: off the work list
            if (unrecorded > 0) json += ",\n";
            json += "    { \"person\": \"p" + std::to_string(person.personId) + "\", ";
            json += "\"name\": \"";
            jsonEscapeInto(tree.fullNameOf(i), json);
            json += "\", \"voice\": \"";
            jsonEscapeInto(voice.description, json);
            json += "\", \"line\": \"";
            jsonEscapeInto(line.id, json);
            json += "\", \"tone\": \"";
            jsonEscapeInto(line.tone, json);
            json += "\", \"text\": \"";
            jsonEscapeInto(line.text, json);
            json += "\" }";
            ++unrecorded;
        }
    }
    json += "\n  ]\n}\n";
    writeTextFile(manifestPath, json);
    return unrecorded;
}

const std::string* pickTake(const VoiceLine& line, uint32_t seed) {
    if (line.takes.empty()) return nullptr;  // subtitle fallback
    return &line.takes[seed % line.takes.size()];
}

}  // namespace mge
