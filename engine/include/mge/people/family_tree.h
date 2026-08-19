#pragma once

// Family trees (tasks 9.1/9.2/9.3/9.5, PEOPLE.md §2): you don't generate
// people, you generate trees. A tree is compact records — names, genomes,
// relation links — bytes while cold (P1/P2); bodies derive from DNA on
// demand. Everything is deterministic from the seed.
//
// Owner rulings (PEOPLE.md §7): first names unique within the family;
// children always take the father's family name; a woman takes her
// husband's family name upon marriage (birth name preserved); trees include
// record-only ancestors (dead history).

#include <cstdint>
#include <string>
#include <vector>

#include "mge/people/dna.h"

namespace mge {

// Culture pack: the name pools a tree draws from. Naming RULES are fixed by
// the owner's ruling and are not per-culture.
struct CulturePack {
    const char* id;
    const char* const* femaleNames;
    size_t femaleCount;
    const char* const* maleNames;
    size_t maleCount;
    const char* const* familyNames;
    size_t familyCount;
};

const CulturePack& cultureAnglo();   // shipped medieval-English pool
const CulturePack& cultureHebrew();  // shipped Hebrew pool (P11)
const CulturePack* findCulture(const char* id);

constexpr uint16_t kNoPerson = UINT16_MAX;
constexpr size_t kVoiceDescBytes = 96;

struct PersonRecord {
    uint32_t personId = 0;  // stable; doubles as the save persistentId (8.9)
    Sex sex = Sex::Female;
    uint8_t generation = 0;
    uint8_t alive = 1;  // record-only ancestors are dead history
    uint8_t marriedIn = 0;  // joined the tree by marriage (own birth family)
    uint16_t firstName = 0;        // index into the culture's per-sex pool
    uint16_t familyName = 0;       // CURRENT family name (marriage ruling)
    uint16_t birthFamilyName = 0;  // birth name, preserved
    uint16_t mother = kNoPerson;
    uint16_t father = kNoPerson;
    uint16_t spouse = kNoPerson;
    Genome genome;
    // Occupation / residence / schedule stubs (task 9.7 — deepen in a later
    // dictation; the data shape is fixed now).
    uint64_t occupationTag = 0;
    uint32_t residenceCell = UINT32_MAX;
    uint8_t scheduleId = 0;
    // The recording brief: how this person's voice sounds (PEOPLE.md §6).
    char voiceDesc[kVoiceDescBytes] = {};
};

struct TreeGenParams {
    uint32_t seed = 1;
    uint32_t personIdBase = 1;   // ids are base..base+count-1; games keep trees disjoint
    uint8_t generations = 3;     // total, including record-only ancestors
    uint8_t livingGenerations = 2;  // the last N generations are alive
    uint8_t maxChildrenPerCouple = 3;
};

class FamilyTree {
public:
    // Deterministic: the same params + culture always produce the same tree.
    // Fails (false) only if the name pools can't cover the tree (refuse, not
    // rename — P1 discipline applied to content).
    bool generate(const TreeGenParams& params, const CulturePack& culture);

    const std::vector<PersonRecord>& people() const { return people_; }
    const CulturePack& culture() const { return *culture_; }
    uint32_t seed() const { return params_.seed; }

    int32_t indexOfId(uint32_t personId) const;
    void childrenOf(uint16_t index, std::vector<uint16_t>& out) const;
    void siblingsOf(uint16_t index, std::vector<uint16_t>& out) const;

    const char* firstNameOf(uint16_t index) const;
    const char* familyNameOf(uint16_t index) const;
    std::string fullNameOf(uint16_t index) const;

    // ---- .mgetree file (ADR 0005): header + raw records + checksum ----
    bool writeFile(const char* path) const;
    bool readFile(const char* path);

private:
    bool pickFirstName(Sex sex, DnaRng& rng, uint16_t& out);
    uint16_t pickFamilyName(DnaRng& rng);
    void composeVoiceDesc(PersonRecord& person, DnaRng& rng) const;

    const CulturePack* culture_ = nullptr;
    TreeGenParams params_{};
    std::vector<PersonRecord> people_;
    std::vector<uint8_t> usedFemale_, usedMale_, usedFamily_;
};

}  // namespace mge
