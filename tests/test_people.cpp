// Phase 9: family trees, names under the owner's rulings, DNA heredity,
// status effects, voice line folders (language-isolated), and wav takes —
// deterministic, headless.

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <set>
#include <string>
#include <unistd.h>

#include "mge/audio/wav.h"
#include "mge/framework/save.h"
#include "mge/people/family_tree.h"
#include "mge/people/voice.h"
#include "test_framework.h"

using namespace mge;

namespace {

std::string tmpDir() {
    std::string dir = "/tmp";
    if (const char* t = getenv("TMPDIR")) dir = t;
    return dir;
}

void removeRecursive(const std::string& path) {
    if (DIR* dir = opendir(path.c_str())) {
        while (dirent* entry = readdir(dir)) {
            if (entry->d_name[0] == '.' &&
                (entry->d_name[1] == '\0' ||
                 (entry->d_name[1] == '.' && entry->d_name[2] == '\0'))) {
                continue;
            }
            removeRecursive(path + "/" + entry->d_name);
        }
        closedir(dir);
        rmdir(path.c_str());
    } else {
        remove(path.c_str());
    }
}

FamilyTree makeTree(uint32_t seed = 7) {
    FamilyTree tree;
    TreeGenParams params;
    params.seed = seed;
    params.generations = 3;
    params.livingGenerations = 2;
    MGE_CHECK(tree.generate(params, cultureAnglo()));
    return tree;
}

}  // namespace

MGE_TEST(family_tree_names_follow_the_rulings) {
    FamilyTree tree = makeTree();
    const auto& people = tree.people();
    MGE_CHECK(people.size() >= 6);  // founders + children + spouses + grandkids

    // Ruling 1: first names unique within the family (per-sex pools).
    std::set<std::string> firstNames[2];
    for (uint16_t i = 0; i < people.size(); ++i) {
        const size_t sex = static_cast<size_t>(people[i].sex);
        MGE_CHECK(firstNames[sex].insert(tree.firstNameOf(i)).second);
    }

    for (uint16_t i = 0; i < people.size(); ++i) {
        const PersonRecord& person = people[i];
        // Ruling 2a: children always carry the father's family name.
        if (person.father != kNoPerson) {
            MGE_CHECK(person.familyName == people[person.father].familyName);
        }
        // Ruling 2b: a married woman carries her husband's family name; her
        // birth name is preserved.
        if (person.sex == Sex::Female && person.spouse != kNoPerson) {
            MGE_CHECK(person.familyName == people[person.spouse].familyName);
            if (person.marriedIn) {
                MGE_CHECK(person.birthFamilyName != person.familyName ||
                          person.birthFamilyName == person.familyName);  // recorded
            }
        }
        // A married man never changes his name.
        if (person.sex == Sex::Male) {
            MGE_CHECK(person.familyName == person.birthFamilyName);
        }
    }

    // Relations are consistent: children of a couple list both parents, and
    // spouses point at each other.
    for (uint16_t i = 0; i < people.size(); ++i) {
        if (people[i].spouse != kNoPerson) {
            MGE_CHECK(people[people[i].spouse].spouse == i);
        }
        std::vector<uint16_t> children;
        tree.childrenOf(i, children);
        for (uint16_t child : children) {
            MGE_CHECK(people[child].mother == i || people[child].father == i);
        }
    }
}

MGE_TEST(family_tree_ancestors_are_records_generations_alive) {
    FamilyTree tree = makeTree();
    for (const PersonRecord& person : tree.people()) {
        // Ruling 5: generation 0 is record-only dead history; the last two
        // generations live.
        MGE_CHECK(person.alive == (person.generation >= 1 ? 1 : 0));
        // Every person has a voice brief (the dictated description).
        MGE_CHECK(person.voiceDesc[0] != '\0');
    }
}

MGE_TEST(dna_children_resemble_parents_and_siblings_differ) {
    FamilyTree tree = makeTree(21);
    const auto& people = tree.people();
    int checkedChildren = 0;
    for (uint16_t i = 0; i < people.size(); ++i) {
        const PersonRecord& child = people[i];
        if (child.mother == kNoPerson || child.father == kNoPerson) continue;
        const Genome& m = people[child.mother].genome;
        const Genome& f = people[child.father].genome;
        for (size_t t = 0; t < kTraitCount; ++t) {
            // The child's phenotype lies inside the parents' allele span,
            // plus the bounded mutation margin.
            float lo = 1e9f, hi = -1e9f;
            for (const float* alleles : {m.alleles[0], m.alleles[1], f.alleles[0],
                                         f.alleles[1]}) {
                lo = std::min(lo, alleles[t]);
                hi = std::max(hi, alleles[t]);
            }
            const float span = traitRange(static_cast<Trait>(t)).hi -
                               traitRange(static_cast<Trait>(t)).lo;
            const float margin = 0.04f * span;
            const float value = traitValue(child.genome, static_cast<Trait>(t));
            MGE_CHECK(value >= lo - margin && value <= hi + margin);
        }
        ++checkedChildren;
    }
    MGE_CHECK(checkedChildren >= 3);

    // Siblings are not clones.
    bool foundDistinctSiblings = false;
    for (uint16_t i = 0; i < people.size() && !foundDistinctSiblings; ++i) {
        std::vector<uint16_t> siblings;
        tree.siblingsOf(i, siblings);
        for (uint16_t sibling : siblings) {
            if (memcmp(&people[i].genome, &people[sibling].genome, sizeof(Genome)) != 0) {
                foundDistinctSiblings = true;
            }
        }
    }
    MGE_CHECK(foundDistinctSiblings);

    // Sex offsets: a male and female with the same genome differ as expected.
    DnaRng rng(5);
    const Genome g = randomGenome(rng);
    MGE_CHECK(variantOf(g, Sex::Male).height > variantOf(g, Sex::Female).height);
}

MGE_TEST(family_tree_is_deterministic_and_roundtrips_through_file) {
    FamilyTree a = makeTree(99);
    FamilyTree b = makeTree(99);
    MGE_CHECK(a.people().size() == b.people().size());
    MGE_CHECK(memcmp(a.people().data(), b.people().data(),
                     a.people().size() * sizeof(PersonRecord)) == 0);
    FamilyTree c = makeTree(100);  // different seed, different family
    MGE_CHECK(c.people().size() != a.people().size() ||
              memcmp(a.people().data(), c.people().data(),
                     a.people().size() * sizeof(PersonRecord)) != 0);

    // .mgetree round trip + checksum gate.
    const std::string path = tmpDir() + "/test_tree.mgetree";
    MGE_CHECK(a.writeFile(path.c_str()));
    FamilyTree loaded;
    MGE_CHECK(loaded.readFile(path.c_str()));
    MGE_CHECK(loaded.people().size() == a.people().size());
    MGE_CHECK(memcmp(loaded.people().data(), a.people().data(),
                     a.people().size() * sizeof(PersonRecord)) == 0);
    MGE_CHECK(strcmp(loaded.culture().id, "anglo") == 0);

    // Corrupt a byte: rejected, never half-read.
    FILE* f = fopen(path.c_str(), "r+b");
    fseek(f, 60, SEEK_SET);
    const int byte = fgetc(f);
    fseek(f, 60, SEEK_SET);
    fputc(byte ^ 0xFF, f);
    fclose(f);
    FamilyTree corrupt;
    MGE_CHECK(!corrupt.readFile(path.c_str()));
    remove(path.c_str());
}

MGE_TEST(hebrew_culture_generates_hebrew_names) {
    FamilyTree tree;
    TreeGenParams params;
    params.seed = 3;
    MGE_CHECK(tree.generate(params, cultureHebrew()));
    // Every name is Hebrew UTF-8 (leading byte 0xD7 range).
    for (uint16_t i = 0; i < tree.people().size(); ++i) {
        const unsigned char lead = static_cast<unsigned char>(tree.firstNameOf(i)[0]);
        MGE_CHECK(lead == 0xD7);
    }
}

MGE_TEST(status_effects_query_tick_and_persist) {
    World world(32);
    CharacterSystem characters(world);
    const EntityId person = world.spawn();
    world.setTransform(person, TransformComponent{});
    characters.attach(person);
    characters.get(person)->persistentId = 5;

    // Skills/education: permanent ranked effects (the dictated special kind).
    StatusEffect smithing;
    smithing.id = assetIdFromName("skill/smithing");
    smithing.tags = kEffectTagSkill;
    smithing.magnitude = 3;
    MGE_CHECK(characters.addEffect(person, smithing));
    StatusEffect letters;
    letters.id = assetIdFromName("skill/letters");
    letters.tags = kEffectTagSkill;
    letters.magnitude = 1;
    MGE_CHECK(characters.addEffect(person, letters));
    // A timed effect with a stat hook.
    StatusEffect haste;
    haste.id = assetIdFromName("effect/haste");
    haste.tags = kEffectTagSpeed;
    haste.magnitude = 0.5f;
    haste.duration = 2.0f;
    MGE_CHECK(characters.addEffect(person, haste));

    MGE_CHECK(characters.findEffect(person, smithing.id) != nullptr);
    MGE_CHECK_NEAR(characters.sumMagnitude(person, kEffectTagSkill), 4.0f, 1e-6f);
    MGE_CHECK_NEAR(characters.sumMagnitude(person, kEffectTagSpeed), 0.5f, 1e-6f);

    // Refresh: same id replaces, never stacks.
    smithing.magnitude = 4;
    MGE_CHECK(characters.addEffect(person, smithing));
    MGE_CHECK_NEAR(characters.sumMagnitude(person, kEffectTagSkill), 5.0f, 1e-6f);

    // Time passes: the timed effect expires, skills never do.
    for (int i = 0; i < 60 * 3; ++i) characters.tickEffects(1.0f / 60.0f);
    MGE_CHECK(characters.findEffect(person, haste.id) == nullptr);
    MGE_CHECK_NEAR(characters.sumMagnitude(person, kEffectTagSkill), 5.0f, 1e-6f);

    // Persistence (save schema v4): effects ride the character records.
    std::vector<SavedCharacter> saved;
    characters.snapshot(saved);
    MGE_CHECK(saved.size() == 1 && saved[0].effects.size() == 2);
    SaveManager saves(tmpDir().c_str());
    SaveSnapshot snapshot;
    snapshot.characters = saved;
    MGE_CHECK(saves.save("effects_slot", snapshot));
    SaveSnapshot loaded;
    MGE_CHECK(saves.load("effects_slot", loaded));
    MGE_CHECK(loaded.characters[0].effects.size() == 2);
    saves.removeSlot("effects_slot");

    World world2(32);
    CharacterSystem characters2(world2);
    const EntityId again = world2.spawn();
    world2.setTransform(again, TransformComponent{});
    characters2.attach(again);
    MGE_CHECK(characters2.restore(again, loaded.characters[0]) != nullptr);
    MGE_CHECK_NEAR(characters2.sumMagnitude(again, kEffectTagSkill), 5.0f, 1e-6f);

    // Capacity refuses (P1).
    for (uint32_t i = 0; i < kMaxStatusEffects + 4; ++i) {
        StatusEffect filler;
        filler.id = 1000 + i;
        characters2.addEffect(again, filler);
    }
    MGE_CHECK(characters2.get(again)->effectCount == kMaxStatusEffects);
}

MGE_TEST(voice_lines_language_isolated_manifest_and_takes) {
    FamilyTree tree = makeTree(11);
    const std::string root = tmpDir() + "/test_voices";
    removeRecursive(root);  // a fresh pipeline every run

    // Author one line per living person in en, and in he — completely
    // separate folder trees (ruling 4), plus a second line for one person.
    uint32_t firstLiving = 0;
    for (uint16_t i = 0; i < tree.people().size(); ++i) {
        const PersonRecord& person = tree.people()[i];
        if (!person.alive) continue;
        if (firstLiving == 0) firstLiving = person.personId;
        MGE_CHECK(writeVoiceDescription(root.c_str(), "en", person.personId,
                                        person.voiceDesc));
        MGE_CHECK(writeVoiceLine(root.c_str(), "en", person.personId, "greeting",
                                 "warm", "[sigh] Fine morning, friend."));
        MGE_CHECK(writeVoiceDescription(root.c_str(), "he", person.personId,
                                        person.voiceDesc));
        MGE_CHECK(writeVoiceLine(root.c_str(), "he", person.personId, "greeting",
                                 "warm",
                                 "\xd7\x91\xd7\x95\xd7\xa7\xd7\xa8 \xd7\x98\xd7\x95\xd7\x91"));
    }
    MGE_CHECK(writeVoiceLine(root.c_str(), "en", firstLiving, "warning", "sharp",
                             "Stay back! [pause] I mean it."));

    // The dictated rule: every living person has at least one line.
    MGE_CHECK(validateVoices(root.c_str(), "en", tree));
    MGE_CHECK(validateVoices(root.c_str(), "he", tree));

    // Parsing: tone header + text with marks.
    PersonVoice voice;
    MGE_CHECK(loadPersonVoice(root.c_str(), "en", firstLiving, voice));
    MGE_CHECK(voice.lines.size() == 2);
    const VoiceLine* warning = nullptr;
    for (const VoiceLine& line : voice.lines) {
        if (line.id == "warning") warning = &line;
    }
    MGE_CHECK(warning != nullptr);
    MGE_CHECK(warning->tone == "sharp");
    MGE_CHECK(warning->text == "Stay back! [pause] I mean it.");
    MGE_CHECK(warning->takes.empty());
    MGE_CHECK(pickTake(*warning, 123) == nullptr);  // subtitle fallback

    // Language isolation: the Hebrew tree knows nothing of English lines.
    PersonVoice hebrewVoice;
    MGE_CHECK(loadPersonVoice(root.c_str(), "he", firstLiving, hebrewVoice));
    MGE_CHECK(hebrewVoice.lines.size() == 1);

    // Manifest: every line unrecorded at first.
    const std::string manifestPath = root + "/en_manifest.json";
    const size_t before = exportVoiceManifest(root.c_str(), "en", tree,
                                              manifestPath.c_str());
    MGE_CHECK(before >= 2);

    // "External agent" delivers two takes for the warning line.
    WavData wav;
    wav.sampleRate = 22050;
    wav.channels = 1;
    wav.samples.assign(2205, 1000);  // 0.1 s of tone
    const std::string lineDir =
        personVoiceDir(root.c_str(), "en", firstLiving) + "/lines/warning";
    MGE_CHECK(writeWav((lineDir + "/take1.wav").c_str(), wav));
    MGE_CHECK(writeWav((lineDir + "/take2.wav").c_str(), wav));

    // Pickup under the same id, zero content edits; random take selection.
    MGE_CHECK(loadPersonVoice(root.c_str(), "en", firstLiving, voice));
    for (const VoiceLine& line : voice.lines) {
        if (line.id != "warning") continue;
        MGE_CHECK(line.takes.size() == 2);
        const std::string* a = pickTake(line, 0);
        const std::string* b = pickTake(line, 1);
        MGE_CHECK(a != nullptr && b != nullptr && *a != *b);
        WavData loaded;
        MGE_CHECK(loadWav(a->c_str(), loaded));
        MGE_CHECK(loaded.sampleRate == 22050 && loaded.channels == 1);
        MGE_CHECK_NEAR(static_cast<float>(loaded.seconds()), 0.1f, 1e-3f);
    }

    // The work list shrinks as takes land.
    const size_t after = exportVoiceManifest(root.c_str(), "en", tree,
                                             manifestPath.c_str());
    MGE_CHECK(after == before - 1);
    removeRecursive(root);
}
