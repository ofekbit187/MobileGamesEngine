// Garments as data (task 13.12) — and the proof P12 actually asks for.
//
// The claim under test is not "the catalogue parses". It is: **a wearable
// nobody compiled in behaves exactly like one that was.** The last test in
// this file adds a garment to a directory at runtime, with no enumerator, no
// `switch` arm and no rebuild, and then requires it to mask, layer and fit
// like the shipped six — because if it does, then adding the hundredth
// wearable is a data change, which is the whole principle.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "mge/character/body_mesh.h"
#include "mge/character/garment_binding.h"
#include "mge/character/wearable_catalogue.h"
#include "test_framework.h"

using namespace mge;

namespace {

// Same convention as the other file-touching tests in this suite.
std::string tmpPath(const char* name) {
    std::string dir = "/tmp";
    if (const char* t = getenv("TMPDIR")) dir = t;
    return dir + "/" + name;
}

const char* kMinimal =
    "version 1\n"
    "id      hood\n"
    "name    Hood\n"
    "mesh    garment_hair_short\n"
    "layer   2\n"
    "covers  scalp\n";

WearableDef parsed(const char* text, bool expectOk = true) {
    WearableDef def;
    const WearableParseResult result = parseWearableDef(text, def);
    if (result.ok != expectOk) {
        printf("  unexpected parse result: ok=%d line=%zu %s\n", result.ok ? 1 : 0,
               result.line, result.message.c_str());
    }
    MGE_CHECK(result.ok == expectOk);
    return def;
}

void refuses(const char* text, const char* expectedWord) {
    WearableDef def;
    const WearableParseResult result = parseWearableDef(text, def);
    MGE_CHECK(!result.ok);
    if (result.ok) return;
    // P12: the message is for whoever wrote the file, so it has to name the
    // thing that is wrong rather than say "parse error".
    MGE_CHECK(result.message.find(expectedWord) != std::string::npos);
    if (result.message.find(expectedWord) == std::string::npos) {
        printf("  message was: %s\n", result.message.c_str());
    }
}

}  // namespace

// ------------------------------------------------------------- format ------

MGE_TEST(a_wearable_file_declares_everything_the_engine_needs) {
    const WearableDef def = parsed(kMinimal);
    MGE_CHECK(def.id == "hood");
    MGE_CHECK(def.name == "Hood");
    MGE_CHECK(def.mesh == "garment_hair_short");
    MGE_CHECK(def.layer == 2);
    MGE_CHECK(def.covers == regionBit(BodyRegion::Scalp));
    MGE_CHECK(def.valid());
}

MGE_TEST(region_groups_spare_an_artist_from_spelling_out_both_sides) {
    const WearableDef def = parsed(
        "version 1\nid x\nmesh m\ncovers legs feet\n");
    MGE_CHECK(def.covers == (kRegionsLegs | kRegionsFeet));

    // And the individual names still work, for a one-sided garment.
    const WearableDef single = parsed("version 1\nid y\nmesh m\ncovers arm_l hand_l\n");
    MGE_CHECK(single.covers ==
              (regionBit(BodyRegion::ArmL) | regionBit(BodyRegion::HandL)));
}

MGE_TEST(comments_and_blank_lines_are_content_not_syntax) {
    const WearableDef def = parsed(
        "# a leading comment\n"
        "\n"
        "version 1\n"
        "\n"
        "# what this is\n"
        "id      cloak\n"
        "mesh    garment_tunic\n"
        "covers  none\n"
        "\n");
    MGE_CHECK(def.id == "cloak");
    MGE_CHECK(def.covers == 0);
}

MGE_TEST(a_bad_wearable_file_says_what_is_wrong_and_where) {
    // Every one of these is a mistake someone will actually make, and the
    // message has to be enough to fix it without reading engine source.
    refuses("id x\nmesh m\ncovers none\n", "version");
    refuses("version 1\nmesh m\ncovers none\n", "id");
    refuses("version 1\nid x\ncovers none\n", "mesh");
    refuses("version 1\nid x\nmesh m\n", "covers");
    refuses("version 1\nid x\nmesh m\ncovers elbow\n", "elbow");
    refuses("version 1\nid x\nmesh m\ncovers none\nlayer 7\n", "layer");
    refuses("version 1\nid x\nmesh m\ncovers none\ncolour 1 1 1 1\n", "colour");
    refuses("version 2\nid x\nmesh m\ncovers none\n", "version 2");
    refuses("version 1\nid x\nmesh m\ncovers none\nthickness_mm huge\n", "thickness_mm");

    // A line number, so a long file is navigable.
    WearableDef def;
    const WearableParseResult result =
        parseWearableDef("version 1\nid x\nmesh m\ncovers none\nnonsense 3\n", def);
    MGE_CHECK(!result.ok);
    MGE_CHECK(result.line == 5);
}

// ---------------------------------------------------------- the shipped ----

MGE_TEST(the_shipped_garments_are_catalogue_rows_like_any_other) {
    const WearableCatalogue& catalogue = wearableCatalogue();
    if (catalogue.size() == 0) return;  // assets not present

    // No `.mgewear` in the shipped set was refused.
    for (const std::string& refusal : catalogue.refusals()) printf("  refused: %s\n",
                                                                  refusal.c_str());
    MGE_CHECK(catalogue.refusals().empty());

    const WearableKind kinds[] = {WearableKind::Tunic,     WearableKind::Armor,
                                  WearableKind::Pants,     WearableKind::Boots,
                                  WearableKind::HairShort, WearableKind::HairLong};
    for (WearableKind kind : kinds) {
        const WearableDef* def = catalogue.def(kind);
        MGE_CHECK(def != nullptr);
        if (def == nullptr) continue;
        MGE_CHECK(def->valid());
        // The mesh the row names is really on disk and really loads.
        MGE_CHECK(!sharedGarment(kind).vertices.empty());
    }
}

MGE_TEST(coverage_now_comes_from_data_rather_than_a_switch) {
    const WearableCatalogue& catalogue = wearableCatalogue();
    if (catalogue.size() == 0) return;

    // The behaviour the rest of the engine depends on, unchanged by the move
    // to data: a tunic masks the torso, trousers the legs, hair nothing.
    MGE_CHECK(garmentCoverage(WearableKind::Tunic) == regionBit(BodyRegion::Torso));
    MGE_CHECK(garmentCoverage(WearableKind::Pants) == kRegionsLegs);
    MGE_CHECK(garmentCoverage(WearableKind::Boots) == kRegionsFeet);
    MGE_CHECK(garmentCoverage(WearableKind::HairShort) == 0);

    // And the same answers by catalogue index, which is the path a garment
    // with no enumerator takes.
    const size_t tunic = catalogue.find("tunic");
    MGE_CHECK(tunic != WearableCatalogue::npos);
    if (tunic != WearableCatalogue::npos) {
        MGE_CHECK(garmentCoverageById(tunic) == garmentCoverage(WearableKind::Tunic));
        MGE_CHECK(sharedGarmentById(tunic).vertices.size() ==
                  sharedGarment(WearableKind::Tunic).vertices.size());
    }
}

MGE_TEST(an_unknown_catalogue_index_answers_empty_rather_than_crashing) {
    // Data can name a garment that is not installed. That is a missing asset,
    // not a crash: the character renders without it.
    MGE_CHECK(sharedGarmentById(9999).vertices.empty());
    MGE_CHECK(sharedGarmentBindingById(9999).empty());
    MGE_CHECK(garmentCoverageById(9999) == 0);
    MGE_CHECK(wearableCatalogue().find("no_such_garment") == WearableCatalogue::npos);
}

// ------------------------------------------------------------ the proof ----

MGE_TEST(a_garment_added_as_pure_data_behaves_like_a_compiled_one) {
    // THE P12 PROOF (task 13.12). A garment that exists only as a file:
    // no enumerator, no `switch` arm, no rebuild, no CMake entry. If this
    // passes, the hundredth wearable costs an artist a text file.
    const WearableCatalogue& shipped = wearableCatalogue();
    if (shipped.size() == 0) return;  // assets not present
    const size_t shippedCount = shipped.size();

    // A scratch asset directory: the shipped assets plus one new `.mgewear`
    // that reuses an existing mesh. Reusing the mesh is the point — what is
    // new here is the DECLARATION, which is all a new garment really is.
    const std::string dir = tmpPath("mge_wearable_catalogue");
    const std::string cmd = "rm -rf '" + dir + "' && mkdir -p '" + dir + "' && cp '" +
                            std::string(characterAssetDir()) + "'/* '" + dir + "'/";
    MGE_CHECK(std::system(cmd.c_str()) == 0);

    const std::string path = dir + "/surcoat.mgewear";
    std::FILE* file = std::fopen(path.c_str(), "wb");
    MGE_CHECK(file != nullptr);
    if (file == nullptr) return;
    // An outer-layer torso garment the engine has never heard of.
    const char* text =
        "# A surcoat, added without touching a single line of C++.\n"
        "version       1\n"
        "id            surcoat\n"
        "name          Surcoat\n"
        "mesh          garment_armour\n"
        "layer         2\n"
        "covers        torso\n"
        "thickness_mm  9\n"
        "color         0.62 0.18 0.20 1\n";
    std::fwrite(text, 1, std::strlen(text), file);
    std::fclose(file);

    const std::string original = characterAssetDir();
    setCharacterAssetDir(dir.c_str());
    reloadWearableCatalogue();

    const WearableCatalogue& reloaded = wearableCatalogue();
    MGE_CHECK(reloaded.refusals().empty());
    MGE_CHECK(reloaded.size() == shippedCount + 1);

    const size_t surcoat = reloaded.find("surcoat");
    MGE_CHECK(surcoat != WearableCatalogue::npos);
    if (surcoat != WearableCatalogue::npos) {
        const WearableDef& def = reloaded.at(surcoat);
        MGE_CHECK(def.name == "Surcoat");
        MGE_CHECK(def.layer == 2);
        // It masks what it says it masks, through the same call the shipped
        // garments go through.
        MGE_CHECK(garmentCoverageById(surcoat) == regionBit(BodyRegion::Torso));
        // Its mesh resolves and its baked binding is found, because the
        // catalogue row names the asset stem and the loaders follow it.
        MGE_CHECK(!sharedGarmentById(surcoat).vertices.empty());
        MGE_CHECK(!sharedGarmentBindingById(surcoat).empty());
    }

    // The shipped six are untouched by the newcomer.
    MGE_CHECK(reloaded.def(WearableKind::Tunic) != nullptr);
    MGE_CHECK(garmentCoverage(WearableKind::Tunic) == regionBit(BodyRegion::Torso));

    setCharacterAssetDir(original.c_str());
    reloadWearableCatalogue();
    MGE_CHECK(wearableCatalogue().size() == shippedCount);
}

MGE_TEST(a_broken_wearable_file_is_refused_without_taking_the_others_down) {
    // One bad file must not cost an artist the whole catalogue — it is
    // reported and skipped, and everything else still loads.
    const WearableCatalogue& shipped = wearableCatalogue();
    if (shipped.size() == 0) return;
    const size_t shippedCount = shipped.size();

    const std::string dir = tmpPath("mge_wearable_broken");
    const std::string cmd = "rm -rf '" + dir + "' && mkdir -p '" + dir + "' && cp '" +
                            std::string(characterAssetDir()) + "'/* '" + dir + "'/";
    MGE_CHECK(std::system(cmd.c_str()) == 0);

    const std::string path = dir + "/broken.mgewear";
    std::FILE* file = std::fopen(path.c_str(), "wb");
    MGE_CHECK(file != nullptr);
    if (file == nullptr) return;
    const char* text = "version 1\nid broken\nmesh m\ncovers elbow\n";
    std::fwrite(text, 1, std::strlen(text), file);
    std::fclose(file);

    const std::string original = characterAssetDir();
    setCharacterAssetDir(dir.c_str());
    reloadWearableCatalogue();

    const WearableCatalogue& reloaded = wearableCatalogue();
    MGE_CHECK(reloaded.size() == shippedCount);         // the good ones survived
    MGE_CHECK(reloaded.refusals().size() == 1);          // and the bad one is named
    if (!reloaded.refusals().empty()) {
        const std::string& why = reloaded.refusals()[0];
        MGE_CHECK(why.find("broken.mgewear") != std::string::npos);
        MGE_CHECK(why.find("elbow") != std::string::npos);
    }

    setCharacterAssetDir(original.c_str());
    reloadWearableCatalogue();
}
