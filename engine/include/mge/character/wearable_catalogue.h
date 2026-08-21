#pragma once

// The wearable catalogue (task 13.12) — garments as DATA.
//
// P12's end state for this area: "adding a wearable touches no `.cpp` and no
// `CMakeLists`. Anything that does is a defect in the mechanism." Until now it
// did: a garment was a `WearableKind` enumerator plus three `switch` arms plus
// an entry in the bake tool's array, so the hundredth wearable cost an
// engineer exactly as much as the first.
//
// A wearable is now a `.mgewear` file next to its mesh. The engine reads the
// directory; nothing about a garment is compiled in. The format follows the
// house convention set by `assets/standards/skin_texture.mgestd` — one
// `key value...` per line, `#` comments, blank lines ignored, deterministic
// and diffable like every other content file here.
//
//     # Tunic — everyday mid-layer torso garment.
//     version       1
//     id            tunic
//     name          Tunic
//     mesh          garment_tunic
//     layer         1
//     covers        torso arms
//     thickness_mm  11
//     color         0.55 0.42 0.28 1
//
// `WearableKind` still exists and still means what it meant. It is now simply
// the name of the first few catalogue entries rather than the definition of
// what a garment can be — the six shipped garments are ordinary catalogue
// rows that happen to also have an enumerator. Code outside this area (the
// device build, the demos, the body session's tests) keeps compiling
// unchanged, and a NEW garment needs no enumerator at all.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "mge/character/held_items.h"
#include "mge/character/humanoid.h"

namespace mge {

// A garment as the catalogue holds it. Everything the fitting, masking and
// layering machinery needs to treat a garment it has never heard of exactly
// like one it ships with.
struct WearableDef {
    std::string id;        // stable, lowercase, unique — how data refers to it
    std::string name;      // human-facing
    std::string mesh;      // asset stem: <mesh>.mgeskin and <mesh>.mgefit
    uint32_t covers = 0;   // BodyRegion bits this garment masks
    uint8_t layer = 1;     // 0 base / 1 mid / 2 outer
    float thicknessMm = 11.0f;
    float color[4] = {1, 1, 1, 1};
    bool held = false;     // held items attach rigidly; they are not fitted
    HeldItemDef heldItem;  // meaningful only when `held` — grip, anchors, offset

    bool valid() const { return !id.empty() && !mesh.empty(); }
};

// Why a `.mgewear` was refused. The pipeline never half-loads a garment: a
// file the engine cannot fully understand is not a garment with defaults, it
// is a mistake to report (P12 — the artist gets the reason, not an engineer).
struct WearableParseResult {
    bool ok = false;
    size_t line = 0;      // 1-based, 0 when not line-specific
    std::string message;  // written for whoever wrote the file
};

// Parses one `.mgewear`'s text. Separate from file reading so the format has
// tests that do not need a filesystem.
WearableParseResult parseWearableDef(const std::string& text, WearableDef& out);

// The catalogue: every `.mgewear` in the character asset directory, plus the
// shipped built-ins, loaded once and shared process-wide.
class WearableCatalogue {
public:
    static constexpr size_t kMaxEntries = 128;  // caps refuse, never grow (P1)

    size_t size() const { return entries_.size(); }
    const WearableDef& at(size_t index) const { return entries_[index]; }

    // Index of a garment by its data id, or `npos`.
    static constexpr size_t npos = static_cast<size_t>(-1);
    size_t find(const char* id) const;

    // The shipped six, addressed by the enum that names them.
    size_t indexOf(WearableKind kind) const;
    const WearableDef* def(WearableKind kind) const;

    // Files that were present but refused, with their reasons — surfaced by
    // `mge_wearable_gates` so a bad `.mgewear` is visible without a debugger.
    const std::vector<std::string>& refusals() const { return refusals_; }

    void loadFromDirectory(const char* dir);
    void clear();

    // Bumped on every load. Caches keyed by catalogue INDEX must rebuild when
    // this changes: a reload can reorder rows, so a stale index does not just
    // miss — it silently names a different garment.
    uint32_t generation() const { return generation_; }

private:
    std::vector<WearableDef> entries_;
    std::vector<std::string> refusals_;
    uint32_t generation_ = 0;
};

// The process-wide catalogue, loaded from `characterAssetDir()` on first use.
const WearableCatalogue& wearableCatalogue();

// Re-reads the catalogue — for tools and tests that point the asset directory
// somewhere else. Not for the frame path.
void reloadWearableCatalogue();

// The id a shipped kind is stored under. This is the ONLY place the six
// built-ins are named in code, and it exists so old call sites keep working;
// a new garment is reached by its data id and needs no entry here.
const char* wearableKindId(WearableKind kind);

}  // namespace mge
