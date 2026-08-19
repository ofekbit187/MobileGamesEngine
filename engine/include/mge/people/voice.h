#pragma once

// Voices — text lines (tasks 9.8/9.9, PEOPLE.md §6): the P5 fulfillment
// pattern applied to audio, the engine's third fulfillment pipeline.
//
// On disk (ruling 4 — languages NEVER mix: each language is a completely
// separate folder tree under the voice root):
//
//   <root>/<lang>/p<personId>/voice.txt              the recording brief
//   <root>/<lang>/p<personId>/lines/<lineId>/line.txt  text + directions
//   <root>/<lang>/p<personId>/lines/<lineId>/*.wav     agent-delivered takes
//
// line.txt: optional "key: value" header lines (tone: ...), a blank line,
// then the text — with inline delivery marks like [sigh] [laugh] [pause].
// Zero takes recorded = the line plays as a subtitle (text falls back
// through the localization/RTL stack); multiple takes = random pick.

#include <cstdint>
#include <string>
#include <vector>

#include "mge/people/family_tree.h"

namespace mge {

struct VoiceLine {
    std::string id;    // the line folder's name
    std::string tone;  // delivery directions from the header
    std::string text;  // with inline [marks]
    std::vector<std::string> takes;  // full paths of delivered wavs
};

struct PersonVoice {
    uint32_t personId = 0;
    std::string description;  // voice.txt — the agent's recording brief
    std::vector<VoiceLine> lines;
};

std::string personVoiceDir(const char* root, const char* lang, uint32_t personId);

// Authoring helpers (tools/tests/game authoring; create directories as needed).
bool writeVoiceDescription(const char* root, const char* lang, uint32_t personId,
                           const char* description);
bool writeVoiceLine(const char* root, const char* lang, uint32_t personId,
                    const char* lineId, const char* tone, const char* text);

bool loadPersonVoice(const char* root, const char* lang, uint32_t personId,
                     PersonVoice& out);

// The dictated rule: every (living) person has AT LEAST ONE text line.
bool validateVoices(const char* root, const char* lang, const FamilyTree& tree,
                    std::string* firstMissing = nullptr);

// Manifest of unrecorded lines (JSON): person, voice brief, line id, tone,
// text — the external agent's work list. Returns the unrecorded count;
// re-export shrinks as takes are delivered.
size_t exportVoiceManifest(const char* root, const char* lang, const FamilyTree& tree,
                           const char* manifestPath);

// Take selection: null = no takes yet, play the line as a subtitle.
const std::string* pickTake(const VoiceLine& line, uint32_t seed);

}  // namespace mge
