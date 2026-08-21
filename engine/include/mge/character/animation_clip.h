#pragma once

// Authored animation clips (tasks 17.1/17.2, ADR 0018, Dictation 8).
//
// *"i want to animate some of the models myself using blender"* — so the
// engine needs a clip: a recorded sequence of joint rotations, authored
// outside, played inside. Everything the engine animated before this was
// procedural.
//
// This is the RUNTIME half. The importer that produces `.mgeanim` from a
// Blender export (17.3) and the rig export that gives the owner something to
// animate against (17.4) are the character asset pipeline's.
//
// FOUR THINGS THE DESIGN IS BUILT AROUND, all from ADR 0018:
//
// 1. THE CLIP IS SHARED; ONLY THE CURSOR IS PER-CHARACTER. A clip is 17
//    joints x frames x a rotation. One copy of that per character is exactly
//    the shape that blows a crowd budget on a phone — the same mistake the
//    body mesh and the skin maps already avoid. So `AnimationClip` is
//    immutable and resident once, `ClipPlayer` holds a POINTER to it plus a
//    few floats, and a village shares one clip.
//
// 2. ROTATIONS ARE QUANTIZED. Smallest-three, 32 bits per joint per frame
//    against 64 for four halves or 128 for four floats. See `packRotation`.
//
// 3. THE LIBRARY IS ON A REGISTERED BUDGET WHOSE CAP REFUSES. No unbounded
//    cache (P1). Past the cap `load` fails and says why; clips evict like any
//    other asset.
//
// 4. A CLIP RECORDS THE RIG IT WAS AUTHORED AGAINST AND REFUSES ON MISMATCH,
//    naming it. This is the owner's own hours being protected: his clips are
//    content addressed to a specific skeleton, exactly as garment bindings
//    are addressed to a specific body. That machinery exists and works, so
//    this reuses its shape (FNV-1a, hash in the header, refuse loudly)
//    rather than inventing a second one.
//
// Clips are IN-PLACE (ADR 0018 Ruling 1). Locomotion is distance-driven,
// which is what stops feet sliding, so a clip carrying root translation would
// fight it. The importer measures the root travel it removed and reports it;
// `rootTravelRemoved` carries that number so a tool can say "I removed 2.4 m"
// rather than leaving the author wondering why their walk plays on the spot.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "mge/character/animation.h"
#include "mge/character/humanoid.h"
#include "mge/core/memory.h"

namespace mge {

// --------------------------------------------------------- quantization ---

// Smallest-three: the largest component of a unit quaternion is reconstructed
// from the other three, so only three need storing — and because q and -q are
// the same rotation, the stored ones can be forced into the range that packs
// tightly. 2 bits say which was dropped, 10 bits each for the rest.
//
// 10 bits over [-1/sqrt(2), +1/sqrt(2)] resolves ~0.0014, which is well under
// a tenth of a degree of rotation error — measured in the tests rather than
// asserted here.
uint32_t packRotation(const Quat& q);
Quat unpackRotation(uint32_t packed);

// ------------------------------------------------------- the rig version ---

// Identity of the skeleton a clip was authored against. Covers what ADR 0018
// names as the two invalidating changes — the joint list and the bind pose —
// and deliberately NOTHING ELSE. In particular it does not cover per-variant
// bone lengths: every humanoid variant shares this rig, which is why an
// animation retargets across them for free (CHARACTERS.md §4.2). A clip that
// hashed a variant would refuse to play on a taller character, which would be
// wrong.
//
// Skin weights are not in it either, so ADR 0016's reweighting of every
// bending joint does not invalidate a single authored clip.
uint64_t rigVersionHash(const Skeleton& skeleton);

// The canonical rig every clip is authored against.
uint64_t canonicalRigVersionHash();

// --------------------------------------------------------------- clip ------

inline constexpr uint32_t kAnimClipMagic = 0x4D494E41u;  // "ANIM"
inline constexpr uint32_t kAnimClipVersion = 1;
inline constexpr size_t kMaxClipNameLength = 47;

// An authored clip. Immutable once loaded, shared by every character playing
// it. Frames are uniformly spaced, which is what makes sampling O(1) with no
// search and no per-track cursor — the shape a baked export produces anyway.
struct AnimationClip {
    char name[kMaxClipNameLength + 1] = {};
    uint64_t rigHash = 0;      // the skeleton this was authored against
    uint32_t frameCount = 0;
    float sampleRate = 30.0f;  // Hz
    bool loop = false;
    // Metres of root translation the importer measured and removed (Ruling 1).
    // Carried so a tool can report it; the runtime never applies it.
    Vec3 rootTravelRemoved{};
    // Where the blow lands, as a fraction of the clip — the same question
    // `UsePhases::strike` answers for a procedural archetype, so gameplay can
    // hang damage on it without caring which kind of motion it got. The
    // importer sets it (17.3); 0.5 is a neutral default until it does.
    float strikeFraction = 0.5f;
    // frameCount * kJointCount packed rotations, frame-major.
    std::vector<uint32_t> rotations;

    bool empty() const { return frameCount == 0 || rotations.empty(); }
    float duration() const {
        return sampleRate > 0.0f ? static_cast<float>(frameCount) / sampleRate : 0.0f;
    }
    size_t bytes() const { return rotations.size() * sizeof(uint32_t); }

    // The pose at a normalized time in [0, 1], interpolated between frames.
    // Allocation-free and const: this is what "sampled by everyone" means.
    void sample(float normalizedTime, Pose& out) const;
    // The pose at an exact frame index (clamped) — no interpolation.
    void sampleFrame(uint32_t frame, Pose& out) const;
};

// Build a clip from full-precision rotations. `frames[f * kJointCount + j]`.
// Used by the importer and by anything baking procedural motion to a clip.
bool buildClip(const char* name, const Quat* frames, uint32_t frameCount, float sampleRate,
               bool loop, uint64_t rigHash, AnimationClip& out, std::string* error = nullptr);

// -------------------------------------------------------------- file I/O ---

void serializeClip(const AnimationClip& clip, std::vector<uint8_t>& out);
// Refuses a rig mismatch when `expectedRigHash` is non-zero, and says which
// hash it found — the owner's clip failing loudly beats it playing wrong.
bool deserializeClip(const uint8_t* data, size_t size, AnimationClip& out,
                     uint64_t expectedRigHash = 0, std::string* error = nullptr);
bool writeClipFile(const char* path, const AnimationClip& clip);
bool readClipFile(const char* path, AnimationClip& out, uint64_t expectedRigHash = 0,
                  std::string* error = nullptr);

// ------------------------------------------------------------- library -----

// Owns every resident clip and charges a registered budget. Fixed capacity:
// past it, `load` REFUSES rather than growing (P1 — there are no unbounded
// caches).
class ClipLibrary {
public:
    static constexpr size_t kMaxClips = 64;
    static constexpr size_t kDefaultCapBytes = 4u << 20;  // 4 MiB

    ClipLibrary(BudgetRegistry& budgets, size_t capBytes = kDefaultCapBytes);

    // Takes ownership of `clip`'s data. Returns nullptr and fills `error` if
    // the rig does not match, the table is full, or the budget refuses.
    const AnimationClip* add(AnimationClip&& clip, std::string* error = nullptr);
    const AnimationClip* load(const char* path, std::string* error = nullptr);

    const AnimationClip* find(const char* name) const;
    bool evict(const char* name);
    void clear();

    size_t clipCount() const { return count_; }
    size_t residentBytes() const { return residentBytes_; }
    BudgetId budget() const { return budget_; }
    uint64_t rigHash() const { return rigHash_; }

private:
    BudgetRegistry& budgets_;
    BudgetId budget_{};
    uint64_t rigHash_ = 0;
    AnimationClip clips_[kMaxClips];
    size_t count_ = 0;
    size_t residentBytes_ = 0;
};

// -------------------------------------------------------------- playback ---

// The per-character half, and it is deliberately tiny: a pointer to a shared
// clip plus a cursor. This is the whole of what a crowd pays per member.
//
// Mirrors `UsePlayer` on purpose — same verbs, same blend-in/blend-out
// behaviour — so that a caller holding one can hold the other without
// learning a second vocabulary. That is task 17.2's requirement stated as an
// API rather than as a comment.
class ClipPlayer {
public:
    void play(const AnimationClip* clip, bool loop, float blendInSeconds = 0.08f);
    // Advances. Returns true on the single frame the clip's strike moment is
    // crossed — the same edge `UsePlayer::update` reports, so gameplay hangs
    // damage on it identically whichever kind of motion is playing.
    bool update(float dt);
    // Blend out rather than stop dead (the 14.4 behaviour, same shape).
    void interrupt(float seconds = 0.15f);

    bool active() const { return state_ != State::Idle; }
    bool interrupted() const { return state_ == State::BlendingOut; }
    float weight() const { return weight_; }
    float normalizedTime() const;
    float elapsed() const { return time_; }
    float strikeMoment() const;
    const AnimationClip* clip() const { return clip_; }

    void samplePose(Pose& out) const;

private:
    enum class State : uint8_t { Idle, Playing, BlendingOut };

    const AnimationClip* clip_ = nullptr;  // NOT owned: resident once, shared
    State state_ = State::Idle;
    bool loop_ = false;
    bool strikeFired_ = false;
    float time_ = 0.0f;
    float weight_ = 0.0f;
    float blendIn_ = 0.08f;
    float blendOutRate_ = 0.0f;
};

}  // namespace mge
