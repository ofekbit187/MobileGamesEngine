#include "mge/people/dna.h"

namespace mge {

namespace {

constexpr size_t idx(Trait t) { return static_cast<size_t>(t); }

constexpr TraitRange kRanges[kTraitCount] = {
    {1.50f, 2.05f},  // Height (m)
    {0.75f, 1.50f},  // Bulk
    {0.34f, 0.56f},  // Shoulders (m)
    {0.24f, 0.40f},  // Hips (m)
    {0.46f, 0.54f},  // LegRatio
    {0.42f, 0.46f},  // ArmRatio
    {0.90f, 1.10f},  // HeadScale
    {0.00f, 1.00f},  // SkinTone
};

float clampToRange(float v, const TraitRange& r) {
    return v < r.lo ? r.lo : (v > r.hi ? r.hi : v);
}

}  // namespace

TraitRange traitRange(Trait trait) { return kRanges[idx(trait)]; }

Genome randomGenome(DnaRng& rng) {
    Genome g;
    for (size_t t = 0; t < kTraitCount; ++t) {
        g.alleles[0][t] = rng.range(kRanges[t].lo, kRanges[t].hi);
        g.alleles[1][t] = rng.range(kRanges[t].lo, kRanges[t].hi);
    }
    return g;
}

Genome recombine(const Genome& mother, const Genome& father, DnaRng& rng) {
    Genome child;
    for (size_t t = 0; t < kTraitCount; ++t) {
        child.alleles[0][t] = mother.alleles[rng.next() & 1][t];
        child.alleles[1][t] = father.alleles[rng.next() & 1][t];
        // Bounded mutation: rarely, nudge one allele a few percent of the
        // trait's range — sibling variety and slow generational drift.
        if (rng.unit() < 0.15f) {
            const float span = kRanges[t].hi - kRanges[t].lo;
            const size_t which = rng.next() & 1;
            child.alleles[which][t] = clampToRange(
                child.alleles[which][t] + rng.range(-0.03f, 0.03f) * span, kRanges[t]);
        }
    }
    return child;
}

float traitValue(const Genome& genome, Trait trait) {
    const size_t t = idx(trait);
    const float a = genome.alleles[0][t];
    const float b = genome.alleles[1][t];
    if (trait == Trait::SkinTone) {
        // Darker-dominant blend.
        const float darker = a > b ? a : b;
        const float lighter = a > b ? b : a;
        return 0.6f * darker + 0.4f * lighter;
    }
    return 0.5f * (a + b);
}

HumanoidVariant variantOf(const Genome& genome, Sex sex) {
    HumanoidVariant v;
    const float male = sex == Sex::Male ? 1.0f : 0.0f;
    v.height = traitValue(genome, Trait::Height) + (male != 0.0f ? 0.04f : -0.04f);
    v.bulk = traitValue(genome, Trait::Bulk) + (male != 0.0f ? 0.05f : -0.05f);
    v.shoulderWidth = traitValue(genome, Trait::Shoulders) + (male != 0.0f ? 0.03f : -0.02f);
    v.hipWidth = traitValue(genome, Trait::Hips) + (male != 0.0f ? -0.01f : 0.03f);
    v.legRatio = traitValue(genome, Trait::LegRatio);
    v.armRatio = traitValue(genome, Trait::ArmRatio);
    v.headScale = traitValue(genome, Trait::HeadScale);
    // Skin tone through the shipped palette (texture genes join the DNA when
    // the texture pipeline lands).
    const float tone = traitValue(genome, Trait::SkinTone);
    const float light[3] = {0.87f, 0.70f, 0.56f};
    const float dark[3] = {0.45f, 0.30f, 0.20f};
    for (int c = 0; c < 3; ++c) v.skin[c] = light[c] + (dark[c] - light[c]) * tone;
    v.skin[3] = 1.0f;
    return v;
}

}  // namespace mge
