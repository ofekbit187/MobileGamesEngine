#pragma once

// DNA & heredity (task 9.4, PEOPLE.md §3). The variant data file that drives
// every humanoid body IS the phenotype, so the genome is two inherited copies
// (haplotypes) of those same traits. Resolving them produces the person's
// HumanoidVariant — children resemble parents BY CONSTRUCTION because their
// body is computed from the parents' genomes; siblings differ by
// recombination seed. Everything is deterministic: the same seeds always
// yield the same people.

#include <cstdint>

#include "mge/character/humanoid.h"

namespace mge {

enum class Sex : uint8_t { Female = 0, Male = 1 };

enum class Trait : uint8_t {
    Height = 0,
    Bulk,
    Shoulders,
    Hips,
    LegRatio,
    ArmRatio,
    HeadScale,
    SkinTone,  // 0 light .. 1 dark; darker is slightly dominant
    Count,
};
constexpr size_t kTraitCount = static_cast<size_t>(Trait::Count);

struct TraitRange {
    float lo, hi;
};
TraitRange traitRange(Trait trait);

struct Genome {
    float alleles[2][kTraitCount] = {};  // [0] maternal, [1] paternal
};

// Deterministic tiny RNG (same LCG family the AI uses) — trees must replay
// identically from a seed; never std::random here.
struct DnaRng {
    uint32_t state = 1;
    explicit DnaRng(uint32_t seed = 1) : state(seed ? seed : 1) {}
    uint32_t next() {
        state = state * 1664525u + 1013904223u;
        return state;
    }
    float unit() { return static_cast<float>(next() >> 8) / 16777216.0f; }
    float range(float lo, float hi) { return lo + unit() * (hi - lo); }
    uint32_t below(uint32_t n) { return n != 0 ? next() % n : 0; }
};

// A founder / married-in genome: alleles uniform within trait ranges.
Genome randomGenome(DnaRng& rng);

// Conception: each parent passes one of their two alleles per trait, plus a
// small bounded mutation — the source of sibling variety and slow drift.
Genome recombine(const Genome& mother, const Genome& father, DnaRng& rng);

// Blended phenotype value for one trait (pre-sex adjustments). Skin tone
// resolves darker-dominant; everything else blends evenly.
float traitValue(const Genome& genome, Trait trait);

// The person's body: phenotype + small sex offsets, skin tone mapped through
// the shipped palette. Feeds directly into buildSkeleton/buildHumanoidVisual.
HumanoidVariant variantOf(const Genome& genome, Sex sex);

}  // namespace mge
