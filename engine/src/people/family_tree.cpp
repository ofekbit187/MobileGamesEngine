#include "mge/people/family_tree.h"

#include <cstdio>
#include <cstring>
#include <type_traits>

#include "mge/core/log.h"
#include "mge/framework/save.h"  // fnv1a

namespace mge {

namespace {

constexpr const char* kTag = "people";

// ---- shipped culture packs (name pools only; rules are the owner's) -------

const char* const kAngloFemale[] = {
    "Adela", "Agnes", "Alice", "Amice", "Avelina", "Beatrice", "Cecily", "Clemence",
    "Edith", "Eleanor", "Ella", "Emma", "Eva", "Giselle", "Gunnora", "Hawise",
    "Ida", "Isabel", "Joan", "Juliana", "Katherine", "Lucy", "Mabel", "Margaret",
    "Margery", "Matilda", "Maud", "Mirabel", "Petronilla", "Rohesia", "Sabina",
    "Susanna", "Sybil", "Theodora", "Wynflaed", "Ysolt"};
const char* const kAngloMale[] = {
    "Adam", "Alard", "Aldous", "Arnold", "Baldwin", "Bardolph", "Bartholomew",
    "Bennet", "Cedric", "Drogo", "Edmund", "Elias", "Everard", "Fulk", "Geoffrey",
    "Gilbert", "Godwin", "Gregory", "Hamo", "Henry", "Hugh", "Jocelin", "John",
    "Lambert", "Miles", "Nicholas", "Odo", "Osbert", "Peter", "Ralf", "Randal",
    "Robert", "Roger", "Simon", "Thomas", "Walter"};
const char* const kAngloFamily[] = {
    "Ashdown", "Blackwood", "Carpenter", "Cartwright", "Fletcher", "Forester",
    "Grimsby", "Harrow", "Hollowell", "Ironsmith", "Kentwell", "Lanham", "Mercer",
    "Northgate", "Oakhurst", "Pemberton", "Quill", "Ravenshaw", "Stanmore",
    "Thatcher", "Underhill", "Wakefield", "Weaver", "Yardley"};

// Hebrew pools (P11: Hebrew is native, people included).
const char* const kHebrewFemale[] = {
    "\xd7\x90\xd7\x91\xd7\x99\xd7\x92\xd7\x99\xd7\x9c",      // Avigail
    "\xd7\x91\xd7\xaa\xd7\x99\xd7\x94",                      // Batya
    "\xd7\x93\xd7\x91\xd7\x95\xd7\xa8\xd7\x94",              // Dvora
    "\xd7\x93\xd7\x99\xd7\xa0\xd7\x94",                      // Dina
    "\xd7\x97\xd7\xa0\xd7\x94",                              // Hanna
    "\xd7\x99\xd7\x94\xd7\x95\xd7\x93\xd7\x99\xd7\xaa",      // Yehudit
    "\xd7\x99\xd7\xa2\xd7\x9c",                              // Yael
    "\xd7\x9c\xd7\x90\xd7\x94",                              // Leah
    "\xd7\x9e\xd7\xa8\xd7\x99\xd7\x9d",                      // Miriam
    "\xd7\xa0\xd7\xa2\xd7\x9e\xd7\x99",                      // Naomi
    "\xd7\xa2\xd7\x93\xd7\x94",                              // Ada
    "\xd7\xa6\xd7\x99\xd7\xa4\xd7\x95\xd7\xa8\xd7\x94",      // Tzipora
    "\xd7\xa8\xd7\x91\xd7\xa7\xd7\x94",                      // Rivka
    "\xd7\xa8\xd7\x97\xd7\x9c",                              // Rachel
    "\xd7\xa9\xd7\xa8\xd7\x94",                              // Sarah
    "\xd7\xaa\xd7\x9e\xd7\xa8"};                             // Tamar
const char* const kHebrewMale[] = {
    "\xd7\x90\xd7\x91\xd7\xa8\xd7\x94\xd7\x9d",              // Avraham
    "\xd7\x90\xd7\x9c\xd7\xa2\xd7\x96\xd7\xa8",              // Elazar
    "\xd7\x91\xd7\x95\xd7\xa2\xd7\x96",                      // Boaz
    "\xd7\x91\xd7\xa0\xd7\x99\xd7\x9e\xd7\x99\xd7\x9f",      // Binyamin
    "\xd7\x92\xd7\x93\xd7\xa2\xd7\x95\xd7\x9f",              // Gideon
    "\xd7\x93\xd7\x95\xd7\x93",                              // David
    "\xd7\x99\xd7\x94\xd7\x95\xd7\xa0\xd7\xaa\xd7\x9f",      // Yehonatan
    "\xd7\x99\xd7\x95\xd7\x90\xd7\x91",                      // Yoav
    "\xd7\x99\xd7\xa6\xd7\x97\xd7\xa7",                      // Yitzhak
    "\xd7\x99\xd7\xa8\xd7\x9e\xd7\x99\xd7\x94\xd7\x95",      // Yirmiyahu
    "\xd7\x9e\xd7\x90\xd7\x99\xd7\xa8",                      // Meir
    "\xd7\xa0\xd7\xaa\xd7\x9f",                              // Natan
    "\xd7\xa2\xd7\x96\xd7\xa8\xd7\x90",                      // Ezra
    "\xd7\xa2\xd7\x9e\xd7\x95\xd7\xa1",                      // Amos
    "\xd7\xa9\xd7\x9e\xd7\x95\xd7\x90\xd7\x9c",              // Shmuel
    "\xd7\xa9\xd7\x9c\xd7\x9e\xd7\x94"};                     // Shlomo
const char* const kHebrewFamily[] = {
    "\xd7\x91\xd7\x9f-\xd7\xa2\xd7\x96\xd7\xa8\xd7\x90",     // Ben-Ezra
    "\xd7\x91\xd7\xa8-\xd7\x90\xd7\x99\xd7\x9c\xd7\x9f",     // Bar-Ilan
    "\xd7\x94\xd7\x9b\xd7\x94\xd7\x9f",                      // HaCohen
    "\xd7\x94\xd7\x9c\xd7\x95\xd7\x99",                      // HaLevi
    "\xd7\x9e\xd7\x96\xd7\xa8\xd7\x97\xd7\x99",              // Mizrahi
    "\xd7\xa0\xd7\x91\xd7\x95\xd7\x9f",                      // Navon
    "\xd7\xa1\xd7\x95\xd7\xa4\xd7\xa8",                      // Sofer
    "\xd7\xa2\xd7\x9e\xd7\xa8\xd7\x9d",                      // Amram
    "\xd7\xa6\xd7\x95\xd7\xa8",                              // Tzur
    "\xd7\xa7\xd7\x93\xd7\x9d",                              // Kedem
    "\xd7\xa8\xd7\x95\xd7\x96\xd7\x9f",                      // Rozen
    "\xd7\xa9\xd7\xa0\xd7\x99"};                             // Shani

const CulturePack kAnglo{"anglo",
                         kAngloFemale, sizeof(kAngloFemale) / sizeof(kAngloFemale[0]),
                         kAngloMale, sizeof(kAngloMale) / sizeof(kAngloMale[0]),
                         kAngloFamily, sizeof(kAngloFamily) / sizeof(kAngloFamily[0])};
const CulturePack kHebrew{"hebrew",
                          kHebrewFemale, sizeof(kHebrewFemale) / sizeof(kHebrewFemale[0]),
                          kHebrewMale, sizeof(kHebrewMale) / sizeof(kHebrewMale[0]),
                          kHebrewFamily, sizeof(kHebrewFamily) / sizeof(kHebrewFamily[0])};

// ---- .mgetree framing (ADR 0005) ------------------------------------------

constexpr char kTreeMagic[4] = {'M', 'G', 'E', 'T'};
constexpr uint32_t kTreeVersion = 1;

struct TreeHeader {
    char magic[4];
    uint32_t version;
    uint32_t recordSize;  // reader validates layout compatibility
    uint32_t count;
    uint32_t seed;
    uint32_t pad;
    char cultureId[16];
    uint64_t checksum;  // FNV-1a over the record block
};

static_assert(std::is_trivially_copyable<PersonRecord>::value,
              "PersonRecord is a raw disk record");

}  // namespace

const CulturePack& cultureAnglo() { return kAnglo; }
const CulturePack& cultureHebrew() { return kHebrew; }

const CulturePack* findCulture(const char* id) {
    if (strcmp(id, kAnglo.id) == 0) return &kAnglo;
    if (strcmp(id, kHebrew.id) == 0) return &kHebrew;
    return nullptr;
}

// ---- name picking (ruling 1: first names unique within the family) --------

bool FamilyTree::pickFirstName(Sex sex, DnaRng& rng, uint16_t& out) {
    std::vector<uint8_t>& used = sex == Sex::Female ? usedFemale_ : usedMale_;
    const size_t count = used.size();
    if (count == 0) return false;
    const size_t start = rng.below(static_cast<uint32_t>(count));
    for (size_t i = 0; i < count; ++i) {
        const size_t candidate = (start + i) % count;
        if (!used[candidate]) {
            used[candidate] = 1;
            out = static_cast<uint16_t>(candidate);
            return true;
        }
    }
    return false;  // pool exhausted: refuse (no duplicate names in a family)
}

uint16_t FamilyTree::pickFamilyName(DnaRng& rng) {
    const size_t count = usedFamily_.size();
    const size_t start = rng.below(static_cast<uint32_t>(count));
    for (size_t i = 0; i < count; ++i) {
        const size_t candidate = (start + i) % count;
        if (!usedFamily_[candidate]) {
            usedFamily_[candidate] = 1;
            return static_cast<uint16_t>(candidate);
        }
    }
    // Family names may legitimately repeat once the pool runs dry.
    return static_cast<uint16_t>(start);
}

// ---- the voice brief (PEOPLE.md §6): derived from the body, deterministic --

void FamilyTree::composeVoiceDesc(PersonRecord& person, DnaRng& rng) const {
    const HumanoidVariant v = variantOf(person.genome, person.sex);
    const char* pitch;
    if (person.sex == Sex::Male) {
        pitch = v.bulk > 1.25f ? "deep" : (v.height > 1.85f ? "low" : "mid-low");
    } else {
        pitch = v.bulk > 1.15f ? "warm alto" : (v.height > 1.80f ? "even" : "bright");
    }
    static const char* const kTextures[] = {"gravelly", "clear", "soft", "husky",
                                            "dry", "resonant", "breathy", "smooth"};
    const char* texture = kTextures[rng.below(8)];
    const char* pace = v.height > 1.90f ? "unhurried" : (v.height < 1.60f ? "quick" : "steady");
    snprintf(person.voiceDesc, kVoiceDescBytes, "%s, %s, %s pace", pitch, texture, pace);
}

// ---- generation ------------------------------------------------------------

bool FamilyTree::generate(const TreeGenParams& params, const CulturePack& culture) {
    culture_ = &culture;
    params_ = params;
    people_.clear();
    usedFemale_.assign(culture.femaleCount, 0);
    usedMale_.assign(culture.maleCount, 0);
    usedFamily_.assign(culture.familyCount, 0);
    if (params.generations == 0) return false;

    DnaRng rng(params.seed);

    const auto addPerson = [&](Sex sex, uint8_t generation) -> int32_t {
        uint16_t firstName = 0;
        if (!pickFirstName(sex, rng, firstName)) return -1;
        PersonRecord person;
        person.sex = sex;
        person.generation = generation;
        person.firstName = firstName;
        people_.push_back(person);
        return static_cast<int32_t>(people_.size() - 1);
    };

    // Founders: the couple the family name flows from.
    const int32_t father0 = addPerson(Sex::Male, 0);
    const int32_t mother0 = addPerson(Sex::Female, 0);
    if (father0 < 0 || mother0 < 0) return false;
    people_[father0].familyName = people_[father0].birthFamilyName = pickFamilyName(rng);
    people_[father0].genome = randomGenome(rng);
    people_[mother0].birthFamilyName = pickFamilyName(rng);
    people_[mother0].familyName = people_[father0].familyName;  // marriage ruling
    people_[mother0].marriedIn = 1;
    people_[mother0].genome = randomGenome(rng);
    people_[father0].spouse = static_cast<uint16_t>(mother0);
    people_[mother0].spouse = static_cast<uint16_t>(father0);

    struct Couple {
        uint16_t father, mother;
    };
    std::vector<Couple> couples{{static_cast<uint16_t>(father0), static_cast<uint16_t>(mother0)}};

    for (uint8_t gen = 1; gen < params.generations; ++gen) {
        std::vector<Couple> next;
        for (const Couple& couple : couples) {
            const uint32_t childCount = 1 + rng.below(params.maxChildrenPerCouple);
            for (uint32_t c = 0; c < childCount; ++c) {
                const Sex sex = (rng.next() & 1) != 0 ? Sex::Male : Sex::Female;
                const int32_t child = addPerson(sex, gen);
                if (child < 0) break;  // name pool exhausted: refuse more children
                people_[child].mother = couple.mother;
                people_[child].father = couple.father;
                // Ruling: children always take the father's family name.
                people_[child].familyName = people_[couple.father].familyName;
                people_[child].birthFamilyName = people_[child].familyName;
                people_[child].genome =
                    recombine(people_[couple.mother].genome, people_[couple.father].genome, rng);

                // Middle generations marry and continue the tree.
                if (gen + 1 < params.generations) {
                    const Sex spouseSex = sex == Sex::Male ? Sex::Female : Sex::Male;
                    const int32_t spouse = addPerson(spouseSex, gen);
                    if (spouse < 0) continue;  // child stays unmarried
                    people_[spouse].marriedIn = 1;
                    people_[spouse].birthFamilyName = pickFamilyName(rng);
                    people_[spouse].familyName = people_[spouse].birthFamilyName;
                    people_[spouse].genome = randomGenome(rng);
                    people_[child].spouse = static_cast<uint16_t>(spouse);
                    people_[spouse].spouse = static_cast<uint16_t>(child);
                    // Marriage ruling: the woman takes her husband's name.
                    if (sex == Sex::Male) {
                        people_[spouse].familyName = people_[child].familyName;
                    } else {
                        people_[child].familyName = people_[spouse].familyName;
                    }
                    const uint16_t father =
                        sex == Sex::Male ? static_cast<uint16_t>(child)
                                         : static_cast<uint16_t>(spouse);
                    const uint16_t mother =
                        sex == Sex::Male ? static_cast<uint16_t>(spouse)
                                         : static_cast<uint16_t>(child);
                    next.push_back({father, mother});
                }
            }
        }
        couples = std::move(next);
    }

    // Record-only ancestors (ruling 5): older generations are dead history.
    const uint8_t firstLiving =
        params.generations > params.livingGenerations
            ? static_cast<uint8_t>(params.generations - params.livingGenerations)
            : 0;
    for (size_t i = 0; i < people_.size(); ++i) {
        people_[i].personId = params.personIdBase + static_cast<uint32_t>(i);
        people_[i].alive = people_[i].generation >= firstLiving ? 1 : 0;
        composeVoiceDesc(people_[i], rng);
    }
    return true;
}

// ---- queries ---------------------------------------------------------------

int32_t FamilyTree::indexOfId(uint32_t personId) const {
    for (size_t i = 0; i < people_.size(); ++i) {
        if (people_[i].personId == personId) return static_cast<int32_t>(i);
    }
    return -1;
}

void FamilyTree::childrenOf(uint16_t index, std::vector<uint16_t>& out) const {
    for (size_t i = 0; i < people_.size(); ++i) {
        if (people_[i].mother == index || people_[i].father == index) {
            out.push_back(static_cast<uint16_t>(i));
        }
    }
}

void FamilyTree::siblingsOf(uint16_t index, std::vector<uint16_t>& out) const {
    const PersonRecord& person = people_[index];
    if (person.mother == kNoPerson && person.father == kNoPerson) return;
    for (size_t i = 0; i < people_.size(); ++i) {
        if (i == index) continue;
        if ((people_[i].mother == person.mother && person.mother != kNoPerson) ||
            (people_[i].father == person.father && person.father != kNoPerson)) {
            out.push_back(static_cast<uint16_t>(i));
        }
    }
}

const char* FamilyTree::firstNameOf(uint16_t index) const {
    const PersonRecord& person = people_[index];
    return person.sex == Sex::Female ? culture_->femaleNames[person.firstName]
                                     : culture_->maleNames[person.firstName];
}

const char* FamilyTree::familyNameOf(uint16_t index) const {
    return culture_->familyNames[people_[index].familyName];
}

std::string FamilyTree::fullNameOf(uint16_t index) const {
    return std::string(firstNameOf(index)) + " " + familyNameOf(index);
}

// ---- file ------------------------------------------------------------------

bool FamilyTree::writeFile(const char* path) const {
    TreeHeader header{};
    memcpy(header.magic, kTreeMagic, 4);
    header.version = kTreeVersion;
    header.recordSize = static_cast<uint32_t>(sizeof(PersonRecord));
    header.count = static_cast<uint32_t>(people_.size());
    header.seed = params_.seed;
    snprintf(header.cultureId, sizeof(header.cultureId), "%s", culture_->id);
    header.checksum = fnv1a(reinterpret_cast<const uint8_t*>(people_.data()),
                            people_.size() * sizeof(PersonRecord));
    FILE* f = fopen(path, "wb");
    if (f == nullptr) return false;
    bool ok = fwrite(&header, sizeof(header), 1, f) == 1;
    ok = ok && (people_.empty() ||
                fwrite(people_.data(), sizeof(PersonRecord), people_.size(), f) ==
                    people_.size());
    fclose(f);
    return ok;
}

bool FamilyTree::readFile(const char* path) {
    FILE* f = fopen(path, "rb");
    if (f == nullptr) return false;
    TreeHeader header{};
    bool ok = fread(&header, sizeof(header), 1, f) == 1 &&
              memcmp(header.magic, kTreeMagic, 4) == 0 && header.version == kTreeVersion &&
              header.recordSize == sizeof(PersonRecord) && header.count < 65535;
    if (ok) {
        people_.resize(header.count);
        ok = header.count == 0 ||
             fread(people_.data(), sizeof(PersonRecord), header.count, f) == header.count;
    }
    fclose(f);
    if (!ok) {
        MGE_LOGE(kTag, "tree %s: bad header or truncated", path);
        return false;
    }
    if (fnv1a(reinterpret_cast<const uint8_t*>(people_.data()),
              people_.size() * sizeof(PersonRecord)) != header.checksum) {
        MGE_LOGE(kTag, "tree %s failed checksum — rejected", path);
        people_.clear();
        return false;
    }
    const CulturePack* culture = findCulture(header.cultureId);
    if (culture == nullptr) {
        MGE_LOGE(kTag, "tree %s: unknown culture '%s'", path, header.cultureId);
        people_.clear();
        return false;
    }
    culture_ = culture;
    params_.seed = header.seed;
    return true;
}

}  // namespace mge
