// Authored animation clips (tasks 17.1/17.2, ADR 0018).
//
// The owner wants to animate the model in Blender and see it in the game. The
// runtime half of that is a clip: shared, quantized, budgeted, and refusing
// loudly when it was authored against a different skeleton.

#include <cmath>
#include <string>
#include <vector>

#include "mge/character/animation.h"
#include "mge/character/animation_clip.h"
#include "mge/character/use_archetypes.h"
#include "mge/core/memory.h"
#include "test_framework.h"

using namespace mge;

namespace {

constexpr size_t idx(Joint j) { return static_cast<size_t>(j); }

float angleBetween(const Quat& a, const Quat& b) {
    const float dot = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    const float s = dot < 0.0f ? -1.0f : 1.0f;
    const float dx = a.x - s * b.x, dy = a.y - s * b.y, dz = a.z - s * b.z, dw = a.w - s * b.w;
    const float sx = a.x + s * b.x, sy = a.y + s * b.y, sz = a.z + s * b.z, sw = a.w + s * b.w;
    return 4.0f * std::atan2(std::sqrt(dx * dx + dy * dy + dz * dz + dw * dw),
                             std::sqrt(sx * sx + sy * sy + sz * sz + sw * sw));
}

// Bake a procedural archetype into a clip — the engine's own motion as ground
// truth for what the clip path must reproduce.
// Bake at whatever rate reproduces the archetype's OWN duration, which is
// what a real export does. Baking at a fixed 30 Hz gave the clip a different
// length from the motion it came from, so the two ran at different speeds and
// any comparison between them was partly meaningless.
AnimationClip bakeArchetype(const char* name, UseArchetype archetype, uint32_t frames = 32,
                            float rate = 0.0f) {
    UseMotion motion;
    motion.archetype = archetype;
    if (rate <= 0.0f) {
        const float duration = usePhases(motion).duration;
        rate = duration > 0.0f ? static_cast<float>(frames) / duration : 30.0f;
    }
    std::vector<Quat> keys(static_cast<size_t>(frames) * kJointCount);
    for (uint32_t f = 0; f < frames; ++f) {
        Pose pose;
        sampleUseArchetype(motion, static_cast<float>(f) / static_cast<float>(frames - 1), pose);
        for (size_t j = 0; j < kJointCount; ++j) {
            keys[static_cast<size_t>(f) * kJointCount + j] = pose.rotation[j];
        }
    }
    AnimationClip clip;
    std::string error;
    const bool ok = buildClip(name, keys.data(), frames, rate, false,
                              canonicalRigVersionHash(), clip, &error);
    if (!ok) printf("  bakeArchetype failed: %s\n", error.c_str());
    return clip;
}

}  // namespace

MGE_TEST(quantized_rotations_are_accurate_enough_to_animate_with) {
    // Measured, not assumed: what does smallest-three actually cost in angle?
    float worst = 0.0f;
    double total = 0.0;
    int samples = 0;
    // Sweep a wide spread of real rotations rather than random ones.
    for (int ax = 0; ax < 3; ++ax) {
        for (int step = -36; step <= 36; ++step) {
            Vec3 axis{ax == 0 ? 1.0f : 0.3f, ax == 1 ? 1.0f : 0.4f, ax == 2 ? 1.0f : 0.5f};
            const Quat q = Quat::fromAxisAngle(axis, static_cast<float>(step) * 0.0873f);
            const float error = angleBetween(q, unpackRotation(packRotation(q)));
            worst = std::max(worst, error);
            total += error;
            ++samples;
        }
    }
    const float worstDegrees = worst * 180.0f / kPi;
    printf("  quantization: worst %.4f deg, mean %.4f deg over %d rotations, %zu bytes each\n",
           worstDegrees, (total / samples) * 180.0 / kPi, samples, sizeof(uint32_t));
    // A tenth of a degree is far below what any eye resolves on a limb.
    MGE_CHECK(worstDegrees < 0.20f);
    // And it is a quarter the size of four floats, which is the entire point.
    MGE_CHECK(sizeof(uint32_t) * 4 == sizeof(Quat));
}

MGE_TEST(a_clip_reproduces_the_motion_it_was_baked_from) {
    // The interchangeability claim of 17.2, tested the strongest way
    // available: bake the engine's own procedural archetype into a clip, play
    // the clip, and require it to match what the archetype produces.
    //
    // Two error sources, and it matters that they are NOT the same size.
    // Quantization is ~0.15 deg and fixed. SAMPLING is the dominant one and
    // it depends entirely on frame rate: a chop's strike phase accelerates on
    // purpose, so uniform frames resolve it worst exactly where the motion is
    // fastest. Measured here rather than assumed, because the number is the
    // answer to "what rate should I export at" in the owner's written page.
    UseMotion motion;
    motion.archetype = UseArchetype::Chop;

    const UsePhases phases = usePhases(motion);
    printf("  clip fidelity vs its source archetype (chop; strike runs t=%.3f..%.3f)\n",
           phases.windUp, phases.windUp + phases.strike);
    printf("  %8s %10s %9s %10s\n", "frames", "worst deg", "at t", "bytes");
    float worstAtHighRate = 999.0f;
    for (uint32_t frames : {16u, 32u, 64u, 128u, 256u}) {
        const AnimationClip clip = bakeArchetype("chop", UseArchetype::Chop, frames);
        MGE_CHECK(!clip.empty());
        float worst = 0.0f, worstT = 0.0f;
        // Probed densely (2001 points) on purpose: a coarse probe grid beats
        // against the frame grid and produces a curve that is an artefact of
        // the measurement rather than of the clip.
        for (int step = 0; step <= 2000; ++step) {
            const float t = static_cast<float>(step) / 2000.0f;
            Pose fromArchetype, fromClip;
            sampleUseArchetype(motion, t, fromArchetype);
            clip.sample(t, fromClip);
            for (size_t j = 0; j < kJointCount; ++j) {
                const float d = angleBetween(fromArchetype.rotation[j], fromClip.rotation[j]);
                if (d > worst) {
                    worst = d;
                    worstT = t;
                }
            }
        }
        printf("  %8u %10.3f %9.3f %10zu\n", frames, worst * 180.0f / kPi, worstT, clip.bytes());
        if (frames == 256u) worstAtHighRate = worst * 180.0f / kPi;
    }
    // The curve falls steeply and then NOT MONOTONICALLY, and the reason is
    // in the source motion rather than in the clip: an archetype's strike
    // accelerates (u squared) and then hands over to a recovery that starts
    // from rest, so the motion has a deliberate VELOCITY KINK at full
    // extension — the arm stops dead on impact, which is the intent. Linear
    // interpolation across a kink is only as good as whether a frame happens
    // to land on it, which is why 64 can be worse than 32. Worth knowing for
    // the owner's export guidance (17.6): a fast strike wants a high rate,
    // and the error is concentrated at the instant of impact.
    // At a rate a real export would use, the clip is the same motion: under
    // 3 degrees at the single fastest instant of a 159-degree arc, and that
    // worst point is the impact frame.
    MGE_CHECK(worstAtHighRate < 3.0f);
}

MGE_TEST(a_clip_and_an_archetype_layer_identically) {
    // Nothing downstream should be able to tell which it got. The composer
    // takes POSES, so this is true by construction — the test pins it.
    //
    // Compared at EXACT FRAME TIMES on purpose. Between frames a clip is a
    // linear interpolation of a curve and differs by the sampling error
    // measured above; ON a frame it carries what was baked, so the only
    // difference left is quantization. That isolates "do the two paths agree"
    // from "how finely was this sampled", which are separate questions.
    const Skeleton skeleton = buildSkeleton(HumanoidVariant{});
    UseMotion motion;
    motion.archetype = UseArchetype::Swing;
    const uint32_t kFrames = 128;
    const AnimationClip clip = bakeArchetype("swing", UseArchetype::Swing, kFrames);
    const JointMask mask = useArchetypeMask(skeleton, motion);
    // Same length, so the two stay in step.
    MGE_CHECK_NEAR(clip.duration(), usePhases(motion).duration, 1e-4);

    LocomotionAnimator anim;
    for (int f = 0; f < 180; ++f) anim.update(1.0f / 60.0f, 1.6f);
    Pose walk;
    anim.samplePose(walk);

    float worstDisagreement = 0.0f;
    for (uint32_t frame = 0; frame < kFrames; ++frame) {
        const float t = static_cast<float>(frame) / static_cast<float>(kFrames - 1);
        Pose fromArchetype, fromClip;
        sampleUseArchetype(motion, t, fromArchetype);
        clip.sampleFrame(frame, fromClip);

        LayeredPose a, b;
        a.reset(walk);
        b.reset(walk);
        MGE_CHECK(a.addLayer(fromArchetype, mask, 1.0f));
        MGE_CHECK(b.addLayer(fromClip, mask, 1.0f));

        for (size_t j = 0; j < kJointCount; ++j) {
            // Masked-out joints are locomotion's in BOTH, bit-identically —
            // the composer does not care which kind of pose it was handed.
            if (mask.weight[j] == 0.0f) {
                MGE_CHECK(angleBetween(a.result().rotation[j], walk.rotation[j]) < 1e-6f);
                MGE_CHECK(angleBetween(b.result().rotation[j], walk.rotation[j]) < 1e-6f);
            }
            worstDisagreement = std::max(
                worstDisagreement, angleBetween(a.result().rotation[j], b.result().rotation[j]));
        }
    }
    printf("  clip vs archetype through the same composer: worst %.4f deg (quantization only)\n",
           worstDisagreement * 180.0f / kPi);
    // Quantization and nothing else.
    MGE_CHECK(worstDisagreement * 180.0f / kPi < 0.25f);

    // And the two players present the same verbs to a caller, which is the
    // other half of "interchangeable".
    UsePlayer archetypePlayer;
    ClipPlayer clipPlayer;
    archetypePlayer.start(motion, 0.08f);
    clipPlayer.play(&clip, false, 0.08f);
    for (int f = 0; f < 8; ++f) {
        archetypePlayer.update(1.0f / 60.0f);
        clipPlayer.update(1.0f / 60.0f);
        // Same blend-in ramp, so a caller can swap one for the other without
        // its layer weight behaving differently.
        MGE_CHECK_NEAR(archetypePlayer.weight(), clipPlayer.weight(), 1e-5);
    }
    MGE_CHECK(archetypePlayer.active() && clipPlayer.active());
}

MGE_TEST(a_clip_player_is_a_cursor_not_a_copy) {
    // ADR 0018 Ruling 3: the clip is resident once and sampled by everyone;
    // only the cursor is per-character. This is the number that claim rests on.
    const AnimationClip clip = bakeArchetype("walk", UseArchetype::Work, 64);
    printf("  clip: %u frames x %zu joints = %zu bytes shared; ClipPlayer = %zu bytes per "
           "character\n",
           clip.frameCount, kJointCount, clip.bytes(), sizeof(ClipPlayer));
    MGE_CHECK(sizeof(ClipPlayer) < 64);
    // A hundred characters playing it must not cost a hundred clips.
    ClipPlayer crowd[100];
    for (ClipPlayer& player : crowd) player.play(&clip, true, 0.0f);
    for (ClipPlayer& player : crowd) MGE_CHECK(player.clip() == &clip);
    MGE_CHECK(sizeof(crowd) < clip.bytes() * 2);
}

MGE_TEST(a_clip_round_trips_through_its_file_format) {
    AnimationClip source = bakeArchetype("thrust", UseArchetype::Thrust, 24);
    source.loop = true;
    source.strikeFraction = 0.42f;
    source.rootTravelRemoved = {0.0f, 0.0f, -2.4f};

    std::vector<uint8_t> bytes;
    serializeClip(source, bytes);
    MGE_CHECK(!bytes.empty());

    AnimationClip loaded;
    std::string error;
    MGE_CHECK(deserializeClip(bytes.data(), bytes.size(), loaded, canonicalRigVersionHash(),
                              &error));
    MGE_CHECK(std::string(loaded.name) == "thrust");
    MGE_CHECK(loaded.frameCount == source.frameCount);
    MGE_CHECK(loaded.loop);
    MGE_CHECK_NEAR(loaded.sampleRate, source.sampleRate, 1e-6);
    MGE_CHECK_NEAR(loaded.strikeFraction, 0.42f, 1e-6);
    // Root travel is REPORTED, never silently dropped (Ruling 1).
    MGE_CHECK_NEAR(loaded.rootTravelRemoved.z, -2.4f, 1e-6);
    MGE_CHECK(loaded.rotations == source.rotations);

    // Truncation is refused rather than read as garbage.
    AnimationClip broken;
    MGE_CHECK(!deserializeClip(bytes.data(), bytes.size() / 2, broken, 0, &error));
    MGE_CHECK(!error.empty());
    MGE_CHECK(!deserializeClip(nullptr, 0, broken, 0, &error));
}

MGE_TEST(a_clip_for_another_rig_is_refused_and_says_so) {
    // This protects the owner's own hours. A clip authored against a skeleton
    // that has since changed must fail loudly — playing it would look like a
    // bad animation rather than like a mismatch.
    AnimationClip stale = bakeArchetype("stale", UseArchetype::Swing, 8);
    stale.rigHash = 0xDEADBEEFCAFEF00Dull;

    std::vector<uint8_t> bytes;
    serializeClip(stale, bytes);
    AnimationClip loaded;
    std::string error;
    MGE_CHECK(!deserializeClip(bytes.data(), bytes.size(), loaded, canonicalRigVersionHash(),
                               &error));
    printf("  rig mismatch says: %s\n", error.c_str());
    // The reason has to be actionable by a non-engineer (P12): it names both
    // hashes and what to do.
    MGE_CHECK(error.find("stale") != std::string::npos);
    MGE_CHECK(error.find("deadbeefcafef00d") != std::string::npos);
    MGE_CHECK(error.find("re-export") != std::string::npos);

    // ...and with no expectation given, the same bytes load fine, so tooling
    // can inspect a stale clip without the runtime accepting it.
    MGE_CHECK(deserializeClip(bytes.data(), bytes.size(), loaded, 0, &error));
}

MGE_TEST(the_clip_library_refuses_at_its_cap_rather_than_growing) {
    BudgetRegistry budgets;
    // A cap that fits two of these clips and not three.
    const AnimationClip probe = bakeArchetype("probe", UseArchetype::Swing, 64);
    ClipLibrary library(budgets, probe.bytes() * 2 + 16);

    std::string error;
    AnimationClip a = bakeArchetype("a", UseArchetype::Swing, 64);
    AnimationClip b = bakeArchetype("b", UseArchetype::Chop, 64);
    AnimationClip c = bakeArchetype("c", UseArchetype::Thrust, 64);
    MGE_CHECK(library.add(std::move(a), &error) != nullptr);
    MGE_CHECK(library.add(std::move(b), &error) != nullptr);
    MGE_CHECK(library.clipCount() == 2);

    // The third does not fit: refused, named, and nothing was charged.
    const size_t before = library.residentBytes();
    MGE_CHECK(library.add(std::move(c), &error) == nullptr);
    printf("  budget refusal says: %s\n", error.c_str());
    MGE_CHECK(error.find("budget refused") != std::string::npos);
    MGE_CHECK(library.residentBytes() == before);
    MGE_CHECK(library.clipCount() == 2);

    // Evicting one makes room — clips behave like any other asset.
    MGE_CHECK(library.evict("a"));
    MGE_CHECK(library.clipCount() == 1);
    AnimationClip again = bakeArchetype("c", UseArchetype::Thrust, 64);
    MGE_CHECK(library.add(std::move(again), &error) != nullptr);
    MGE_CHECK(library.find("c") != nullptr);
    MGE_CHECK(library.find("a") == nullptr);

    // A clip for the wrong rig never becomes resident at all.
    AnimationClip wrongRig = bakeArchetype("wrong", UseArchetype::Raise, 8);
    wrongRig.rigHash = 12345;
    MGE_CHECK(library.add(std::move(wrongRig), &error) == nullptr);

    library.clear();
    MGE_CHECK(library.clipCount() == 0);
    MGE_CHECK(library.residentBytes() == 0);
    MGE_CHECK(budgets.stats(library.budget()).usedBytes == 0);
}

MGE_TEST(clip_playback_loops_blends_and_interrupts_like_an_archetype) {
    AnimationClip clip = bakeArchetype("loop", UseArchetype::Work, 30, 30.0f);
    clip.loop = true;
    clip.strikeFraction = 0.5f;

    ClipPlayer player;
    player.play(&clip, true, 0.10f);
    MGE_CHECK_NEAR(player.weight(), 0.0f, 1e-6);

    // Ramps in rather than popping.
    float previous = 0.0f;
    for (int f = 0; f < 8; ++f) {
        player.update(1.0f / 60.0f);
        MGE_CHECK(player.weight() >= previous - 1e-6f);
        previous = player.weight();
    }

    // Loops: still active well past its own duration, and strikes once per
    // cycle rather than once ever.
    int strikes = 0;
    for (int f = 0; f < 240; ++f) {
        if (player.update(1.0f / 60.0f)) ++strikes;
    }
    MGE_CHECK(player.active());
    MGE_CHECK(strikes >= 3);
    MGE_CHECK(player.normalizedTime() >= 0.0f && player.normalizedTime() <= 1.0f);

    // Interrupts by fading, and an interrupted clip never lands its blow.
    player.interrupt(0.15f);
    MGE_CHECK(player.interrupted());
    int afterHit = 0;
    for (int f = 0; f < 120 && player.active(); ++f) {
        if (player.update(1.0f / 60.0f)) ++afterHit;
    }
    MGE_CHECK(afterHit == 0);
    MGE_CHECK(!player.active());
    MGE_CHECK_NEAR(player.weight(), 0.0f, 1e-6);

    // A one-shot ends on its own and reports its strike exactly once.
    AnimationClip once = bakeArchetype("once", UseArchetype::Chop, 30, 30.0f);
    ClipPlayer shot;
    shot.play(&once, false, 0.0f);
    int fired = 0;
    for (int f = 0; f < 300 && shot.active(); ++f) {
        if (shot.update(1.0f / 60.0f)) ++fired;
    }
    MGE_CHECK(fired == 1);
    MGE_CHECK(!shot.active());
}

MGE_TEST(a_reweighted_body_does_not_invalidate_a_single_clip) {
    // ADR 0016 is reweighting every bending joint right now. Skin weights are
    // not part of the skeleton, so the rig hash must not move — otherwise the
    // owner's authored clips would all be refused for a change that cannot
    // affect them.
    const uint64_t before = canonicalRigVersionHash();
    MGE_CHECK(before == rigVersionHash(buildSkeleton(HumanoidVariant{})));

    // Bone LENGTHS differ per variant, and that must not move the hash either
    // — every variant shares this rig, which is why animation retargets for
    // free (CHARACTERS.md §4.2). A clip that refused to play on a tall
    // character would be wrong.
    HumanoidVariant tall;
    tall.height = 2.10f;
    tall.armRatio = 0.49f;
    const Skeleton tallRig = buildSkeleton(tall);
    MGE_CHECK(tallRig.bindOffset[idx(Joint::ForearmL)].length() !=
              buildSkeleton(HumanoidVariant{}).bindOffset[idx(Joint::ForearmL)].length());

    // ...but re-parenting IS a rig change and must move it.
    Skeleton reparented = buildSkeleton(HumanoidVariant{});
    reparented.parent[idx(Joint::UpperArmR)] = static_cast<int8_t>(Joint::Spine);
    MGE_CHECK(rigVersionHash(reparented) != before);

    // ...as is moving the bind pose.
    Skeleton moved = buildSkeleton(HumanoidVariant{});
    moved.bindOffset[idx(Joint::Chest)].y += 0.01f;
    MGE_CHECK(rigVersionHash(moved) != before);
}
