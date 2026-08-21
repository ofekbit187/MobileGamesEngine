#include "mge/character/animation_clip.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "mge/core/log.h"

namespace mge {

namespace {

constexpr uint64_t kFnvOffset = 1469598103934665603ull;
constexpr uint64_t kFnvPrime = 1099511628211ull;

void hashBytes(const void* data, size_t size, uint64_t& h) {
    const unsigned char* p = static_cast<const unsigned char*>(data);
    for (size_t i = 0; i < size; ++i) {
        h ^= static_cast<uint64_t>(p[i]);
        h *= kFnvPrime;
    }
}

// Through the bit pattern, like the body contract hash: two rigs that differ
// by one ulp ARE different rigs as far as a baked clip is concerned, and
// saying so loudly is the point.
void hashFloat(float f, uint64_t& h) {
    uint32_t bits;
    std::memcpy(&bits, &f, sizeof bits);
    hashBytes(&bits, sizeof bits, h);
}

constexpr float kInvSqrt2 = 0.70710678f;
constexpr uint32_t kComponentMax = 1023;  // 10 bits

float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

}  // namespace

// --------------------------------------------------------- quantization ---

uint32_t packRotation(const Quat& q) {
    const float c[4] = {q.x, q.y, q.z, q.w};
    uint32_t largest = 0;
    float largestAbs = std::fabs(c[0]);
    for (uint32_t i = 1; i < 4; ++i) {
        const float a = std::fabs(c[i]);
        if (a > largestAbs) {
            largestAbs = a;
            largest = i;
        }
    }
    // q and -q are the same rotation, so flip so the dropped component is
    // positive and its sign never has to be stored.
    const float sign = c[largest] < 0.0f ? -1.0f : 1.0f;

    uint32_t packed = largest << 30;
    uint32_t shift = 20;
    for (uint32_t i = 0; i < 4; ++i) {
        if (i == largest) continue;
        const float v = clampf(c[i] * sign, -kInvSqrt2, kInvSqrt2);
        const float unit = (v / kInvSqrt2) * 0.5f + 0.5f;  // [0,1]
        const uint32_t q10 =
            static_cast<uint32_t>(unit * static_cast<float>(kComponentMax) + 0.5f);
        packed |= (q10 & kComponentMax) << shift;
        shift -= 10;
    }
    return packed;
}

Quat unpackRotation(uint32_t packed) {
    const uint32_t largest = packed >> 30;
    float c[4];
    float sumSquares = 0.0f;
    uint32_t shift = 20;
    for (uint32_t i = 0; i < 4; ++i) {
        if (i == largest) continue;
        const uint32_t q10 = (packed >> shift) & kComponentMax;
        const float unit = static_cast<float>(q10) / static_cast<float>(kComponentMax);
        c[i] = (unit * 2.0f - 1.0f) * kInvSqrt2;
        sumSquares += c[i] * c[i];
        shift -= 10;
    }
    c[largest] = std::sqrt(sumSquares < 1.0f ? 1.0f - sumSquares : 0.0f);
    return Quat{c[0], c[1], c[2], c[3]};
}

// ------------------------------------------------------- the rig version ---

uint64_t rigVersionHash(const Skeleton& skeleton) {
    uint64_t h = kFnvOffset;
    // The joint list: count and topology. Adding, removing or re-parenting a
    // joint changes this, which is the first of ADR 0018's two invalidating
    // changes.
    const uint32_t count = static_cast<uint32_t>(kJointCount);
    hashBytes(&count, sizeof count, h);
    for (size_t j = 0; j < kJointCount; ++j) {
        hashBytes(&skeleton.parent[j], sizeof skeleton.parent[j], h);
    }
    // The bind pose: where each joint sits. The second invalidating change.
    for (size_t j = 0; j < kJointCount; ++j) {
        hashFloat(skeleton.bindOffset[j].x, h);
        hashFloat(skeleton.bindOffset[j].y, h);
        hashFloat(skeleton.bindOffset[j].z, h);
    }
    return h;
}

uint64_t canonicalRigVersionHash() {
    // The template variant IS the default-constructed one, and every variant
    // shares this rig — so this is one value for the whole engine, not one
    // per character.
    static const uint64_t hash = rigVersionHash(buildSkeleton(HumanoidVariant{}));
    return hash;
}

// --------------------------------------------------------------- clip ------

void AnimationClip::sampleFrame(uint32_t frame, Pose& out) const {
    if (empty()) {
        for (size_t j = 0; j < kJointCount; ++j) out.rotation[j] = Quat{};
        return;
    }
    const uint32_t f = frame >= frameCount ? frameCount - 1 : frame;
    const size_t base = static_cast<size_t>(f) * kJointCount;
    for (size_t j = 0; j < kJointCount; ++j) {
        out.rotation[j] = unpackRotation(rotations[base + j]);
    }
}

void AnimationClip::sample(float normalizedTime, Pose& out) const {
    if (empty()) {
        for (size_t j = 0; j < kJointCount; ++j) out.rotation[j] = Quat{};
        return;
    }
    if (frameCount == 1) {
        sampleFrame(0, out);
        return;
    }
    // Uniform spacing is what makes this O(1) with no search and no per-track
    // cursor — a baked export produces uniform frames anyway.
    float t = normalizedTime;
    if (loop) {
        t -= std::floor(t);
    } else {
        t = clampf(t, 0.0f, 1.0f);
    }
    const float position = t * static_cast<float>(loop ? frameCount : frameCount - 1);
    uint32_t a = static_cast<uint32_t>(position);
    if (a >= frameCount) a = frameCount - 1;
    const float frac = position - static_cast<float>(a);
    // A looping clip wraps its last frame back to its first; a one-shot holds
    // its final frame instead of snapping to the start.
    uint32_t b = a + 1;
    if (b >= frameCount) b = loop ? 0 : frameCount - 1;

    const size_t baseA = static_cast<size_t>(a) * kJointCount;
    const size_t baseB = static_cast<size_t>(b) * kJointCount;
    for (size_t j = 0; j < kJointCount; ++j) {
        out.rotation[j] = blendRotation(unpackRotation(rotations[baseA + j]),
                                        unpackRotation(rotations[baseB + j]), frac);
    }
}

bool buildClip(const char* name, const Quat* frames, uint32_t frameCount, float sampleRate,
               bool loop, uint64_t rigHash, AnimationClip& out, std::string* error) {
    const auto fail = [&](const char* why) {
        if (error != nullptr) *error = why;
        return false;
    };
    if (name == nullptr || name[0] == '\0') return fail("clip has no name");
    if (std::strlen(name) > kMaxClipNameLength) return fail("clip name is too long");
    if (frames == nullptr || frameCount == 0) return fail("clip has no frames");
    if (sampleRate <= 0.0f) return fail("clip sample rate must be positive");

    out = AnimationClip{};
    std::snprintf(out.name, sizeof out.name, "%s", name);
    out.rigHash = rigHash;
    out.frameCount = frameCount;
    out.sampleRate = sampleRate;
    out.loop = loop;
    out.rotations.resize(static_cast<size_t>(frameCount) * kJointCount);
    for (size_t i = 0; i < out.rotations.size(); ++i) {
        out.rotations[i] = packRotation(frames[i].normalized());
    }
    return true;
}

// -------------------------------------------------------------- file I/O ---

namespace {

template <typename T>
void put(std::vector<uint8_t>& out, const T& value) {
    const uint8_t* p = reinterpret_cast<const uint8_t*>(&value);
    out.insert(out.end(), p, p + sizeof(T));
}

template <typename T>
bool take(const uint8_t*& cursor, const uint8_t* end, T& value) {
    if (static_cast<size_t>(end - cursor) < sizeof(T)) return false;
    std::memcpy(&value, cursor, sizeof(T));
    cursor += sizeof(T);
    return true;
}

}  // namespace

void serializeClip(const AnimationClip& clip, std::vector<uint8_t>& out) {
    out.clear();
    put(out, kAnimClipMagic);
    put(out, kAnimClipVersion);
    out.insert(out.end(), clip.name, clip.name + sizeof clip.name);
    put(out, clip.rigHash);
    put(out, clip.frameCount);
    put(out, static_cast<uint32_t>(kJointCount));
    put(out, clip.sampleRate);
    put(out, static_cast<uint32_t>(clip.loop ? 1u : 0u));
    put(out, clip.rootTravelRemoved.x);
    put(out, clip.rootTravelRemoved.y);
    put(out, clip.rootTravelRemoved.z);
    put(out, clip.strikeFraction);
    put(out, static_cast<uint32_t>(clip.rotations.size()));
    for (uint32_t packed : clip.rotations) put(out, packed);
}

bool deserializeClip(const uint8_t* data, size_t size, AnimationClip& out,
                     uint64_t expectedRigHash, std::string* error) {
    char message[320];
    const auto fail = [&](const char* why) {
        if (error != nullptr) *error = why;
        return false;
    };
    out = AnimationClip{};
    if (data == nullptr) return fail("no clip data");
    const uint8_t* cursor = data;
    const uint8_t* end = data + size;

    uint32_t magic = 0, version = 0, jointCount = 0, loop = 0, count = 0;
    if (!take(cursor, end, magic) || magic != kAnimClipMagic) return fail("not a .mgeanim file");
    if (!take(cursor, end, version) || version != kAnimClipVersion) {
        std::snprintf(message, sizeof message,
                      "clip is format version %u; this engine reads version %u", version,
                      kAnimClipVersion);
        return fail(message);
    }
    if (static_cast<size_t>(end - cursor) < sizeof out.name) return fail("clip header truncated");
    std::memcpy(out.name, cursor, sizeof out.name);
    out.name[kMaxClipNameLength] = '\0';
    cursor += sizeof out.name;

    if (!take(cursor, end, out.rigHash)) return fail("clip header truncated");
    if (!take(cursor, end, out.frameCount)) return fail("clip header truncated");
    if (!take(cursor, end, jointCount)) return fail("clip header truncated");
    if (!take(cursor, end, out.sampleRate)) return fail("clip header truncated");
    if (!take(cursor, end, loop)) return fail("clip header truncated");
    if (!take(cursor, end, out.rootTravelRemoved.x)) return fail("clip header truncated");
    if (!take(cursor, end, out.rootTravelRemoved.y)) return fail("clip header truncated");
    if (!take(cursor, end, out.rootTravelRemoved.z)) return fail("clip header truncated");
    if (!take(cursor, end, out.strikeFraction)) return fail("clip header truncated");
    if (!take(cursor, end, count)) return fail("clip header truncated");
    out.loop = loop != 0;

    if (jointCount != kJointCount) {
        std::snprintf(message, sizeof message,
                      "clip '%s' is for a %u-joint rig; this engine's rig has %zu joints",
                      out.name, jointCount, kJointCount);
        return fail(message);
    }
    // The refusal that protects the author's hours (ADR 0018 Ruling 2). Named,
    // never silent: a clip that plays against the wrong skeleton looks like a
    // bad animation rather than like a mismatch.
    if (expectedRigHash != 0 && out.rigHash != expectedRigHash) {
        std::snprintf(message, sizeof message,
                      "clip '%s' was authored against rig %016llx; this engine's rig is %016llx "
                      "- the skeleton changed, so re-export the rig and re-author or re-import "
                      "the clip",
                      out.name, static_cast<unsigned long long>(out.rigHash),
                      static_cast<unsigned long long>(expectedRigHash));
        return fail(message);
    }
    if (count != static_cast<size_t>(out.frameCount) * kJointCount) {
        return fail("clip frame count does not match its rotation count");
    }
    if (static_cast<size_t>(end - cursor) < count * sizeof(uint32_t)) {
        return fail("clip data truncated");
    }
    out.rotations.resize(count);
    for (uint32_t i = 0; i < count; ++i) {
        if (!take(cursor, end, out.rotations[i])) return fail("clip data truncated");
    }
    return true;
}

bool writeClipFile(const char* path, const AnimationClip& clip) {
    std::vector<uint8_t> bytes;
    serializeClip(clip, bytes);
    std::FILE* file = std::fopen(path, "wb");
    if (file == nullptr) return false;
    const size_t written = std::fwrite(bytes.data(), 1, bytes.size(), file);
    std::fclose(file);
    return written == bytes.size();
}

bool readClipFile(const char* path, AnimationClip& out, uint64_t expectedRigHash,
                  std::string* error) {
    std::FILE* file = std::fopen(path, "rb");
    if (file == nullptr) {
        if (error != nullptr) *error = "cannot open clip file";
        return false;
    }
    std::fseek(file, 0, SEEK_END);
    const long size = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);
    if (size <= 0) {
        std::fclose(file);
        if (error != nullptr) *error = "clip file is empty";
        return false;
    }
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    const size_t read = std::fread(bytes.data(), 1, bytes.size(), file);
    std::fclose(file);
    if (read != bytes.size()) {
        if (error != nullptr) *error = "clip file could not be read";
        return false;
    }
    return deserializeClip(bytes.data(), bytes.size(), out, expectedRigHash, error);
}

// ------------------------------------------------------------- library -----

ClipLibrary::ClipLibrary(BudgetRegistry& budgets, size_t capBytes)
    : budgets_(budgets), rigHash_(canonicalRigVersionHash()) {
    budget_ = budgets_.registerBudget("animation", capBytes);
}

const AnimationClip* ClipLibrary::add(AnimationClip&& clip, std::string* error) {
    char message[320];
    const auto fail = [&](const char* why) -> const AnimationClip* {
        if (error != nullptr) *error = why;
        MGE_LOGW("animation", "%s", why);
        return nullptr;
    };
    if (clip.empty()) return fail("clip has no frames");
    if (clip.rigHash != rigHash_) {
        std::snprintf(message, sizeof message,
                      "clip '%s' was authored against rig %016llx; this engine's rig is %016llx",
                      clip.name, static_cast<unsigned long long>(clip.rigHash),
                      static_cast<unsigned long long>(rigHash_));
        return fail(message);
    }
    if (find(clip.name) != nullptr) {
        std::snprintf(message, sizeof message, "a clip named '%s' is already resident", clip.name);
        return fail(message);
    }
    if (count_ >= kMaxClips) {
        std::snprintf(message, sizeof message, "clip table is full (%zu resident, cap %zu)",
                      count_, kMaxClips);
        return fail(message);
    }
    const size_t bytes = clip.bytes();
    if (!budgets_.charge(budget_, bytes)) {
        const BudgetStats stats = budgets_.stats(budget_);
        std::snprintf(message, sizeof message,
                      "animation budget refused clip '%s' (%zu bytes; %zu of %zu used) - evict a "
                      "clip or raise the cap",
                      clip.name, bytes, stats.usedBytes, stats.capBytes);
        return fail(message);
    }
    clips_[count_] = std::move(clip);
    residentBytes_ += bytes;
    return &clips_[count_++];
}

const AnimationClip* ClipLibrary::load(const char* path, std::string* error) {
    AnimationClip clip;
    if (!readClipFile(path, clip, rigHash_, error)) {
        if (error != nullptr) MGE_LOGW("animation", "%s", error->c_str());
        return nullptr;
    }
    return add(std::move(clip), error);
}

const AnimationClip* ClipLibrary::find(const char* name) const {
    if (name == nullptr) return nullptr;
    for (size_t i = 0; i < count_; ++i) {
        if (std::strcmp(clips_[i].name, name) == 0) return &clips_[i];
    }
    return nullptr;
}

bool ClipLibrary::evict(const char* name) {
    if (name == nullptr) return false;
    for (size_t i = 0; i < count_; ++i) {
        if (std::strcmp(clips_[i].name, name) != 0) continue;
        const size_t bytes = clips_[i].bytes();
        budgets_.release(budget_, bytes);
        residentBytes_ -= bytes;
        // Swap the last one down: clip order is not meaningful, and callers
        // hold pointers only for as long as a clip is resident.
        clips_[i] = std::move(clips_[count_ - 1]);
        clips_[count_ - 1] = AnimationClip{};
        --count_;
        return true;
    }
    return false;
}

void ClipLibrary::clear() {
    budgets_.release(budget_, residentBytes_);
    for (size_t i = 0; i < count_; ++i) clips_[i] = AnimationClip{};
    count_ = 0;
    residentBytes_ = 0;
}

// -------------------------------------------------------------- playback ---

void ClipPlayer::play(const AnimationClip* clip, bool loop, float blendInSeconds) {
    clip_ = clip;
    if (clip_ == nullptr || clip_->empty()) {
        state_ = State::Idle;
        weight_ = 0.0f;
        return;
    }
    state_ = State::Playing;
    loop_ = loop;
    time_ = 0.0f;
    strikeFired_ = false;
    blendIn_ = blendInSeconds > 0.0f ? blendInSeconds : 0.0f;
    weight_ = blendIn_ > 0.0f ? 0.0f : 1.0f;
    blendOutRate_ = 0.0f;
}

bool ClipPlayer::update(float dt) {
    if (state_ == State::Idle || clip_ == nullptr || dt <= 0.0f) return false;

    if (state_ == State::BlendingOut) {
        weight_ -= blendOutRate_ * dt;
        if (weight_ <= 0.0f) {
            weight_ = 0.0f;
            state_ = State::Idle;
        }
        time_ += dt;
        return false;  // an interrupted clip never lands its blow
    }

    const float before = time_;
    time_ += dt;

    if (blendIn_ > 0.0f && weight_ < 1.0f) {
        weight_ += dt / blendIn_;
        if (weight_ > 1.0f) weight_ = 1.0f;
    } else {
        weight_ = 1.0f;
    }

    const float strikeAt = strikeMoment();
    bool crossed = false;
    if (!strikeFired_ && before < strikeAt && time_ >= strikeAt) {
        strikeFired_ = true;
        crossed = true;
    }

    const float length = clip_->duration();
    if (time_ >= length) {
        if (loop_) {
            if (length > 0.0f) time_ -= std::floor(time_ / length) * length;
            strikeFired_ = false;  // a looping clip strikes once per cycle
        } else {
            time_ = length;
            state_ = State::Idle;
            weight_ = 0.0f;
        }
    }
    return crossed;
}

void ClipPlayer::interrupt(float seconds) {
    if (state_ != State::Playing) return;
    state_ = State::BlendingOut;
    if (seconds <= 0.0f) {
        weight_ = 0.0f;
        state_ = State::Idle;
        return;
    }
    blendOutRate_ = weight_ / seconds;
}

float ClipPlayer::normalizedTime() const {
    if (clip_ == nullptr) return 0.0f;
    const float length = clip_->duration();
    if (length <= 0.0f) return 0.0f;
    const float t = time_ / length;
    if (loop_) return t - std::floor(t);
    return t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
}

float ClipPlayer::strikeMoment() const {
    return clip_ == nullptr ? 0.0f : clip_->strikeFraction * clip_->duration();
}

void ClipPlayer::samplePose(Pose& out) const {
    if (clip_ == nullptr) {
        for (size_t j = 0; j < kJointCount; ++j) out.rotation[j] = Quat{};
        return;
    }
    clip_->sample(normalizedTime(), out);
}

}  // namespace mge
