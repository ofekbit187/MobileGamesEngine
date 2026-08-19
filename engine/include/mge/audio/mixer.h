#pragma once

// The audio mixer (Phase 10, ADR 0006). Portable core: fixed voice slots,
// buses, resampling, positional attenuation/pan, and music ducking under
// speech. The device (AAudio on Android; tests and demos on the host) PULLS
// mixed frames via mix() — which is allocation-free (P1, enforced by the
// host runner) and safe to call from the audio thread while the game thread
// starts/stops voices and moves the listener.

#include <cstdint>
#include <mutex>
#include <vector>

#include "mge/audio/wav.h"
#include "mge/core/math.h"
#include "mge/core/memory.h"

namespace mge {

class AudioMixer;

// A loaded, budget-charged PCM16 clip. The caller owns it and must keep it
// alive while any voice plays it. Loading goes through the mixer, which owns
// the "audio" budget (refuse past the cap, P1).
class AudioClip {
public:
    AudioClip() = default;
    ~AudioClip() { unload(); }
    AudioClip(const AudioClip&) = delete;
    AudioClip& operator=(const AudioClip&) = delete;

    bool load(const char* wavPath, AudioMixer& mixer);
    // Adopts already-synthesized samples through the same budget gate.
    bool adopt(WavData&& data, AudioMixer& mixer);
    void unload();

    const WavData& data() const { return data_; }
    bool loaded() const { return data_.sampleRate != 0; }

private:
    WavData data_;
    BudgetRegistry* budgets_ = nullptr;
    BudgetId budget_ = 0;
    size_t chargedBytes_ = 0;
};

enum class AudioBus : uint8_t { Music = 0, Sfx, Voice, Count };

using AudioVoiceId = uint32_t;
constexpr AudioVoiceId kInvalidAudioVoice = 0;

struct AudioPlayParams {
    AudioBus bus = AudioBus::Sfx;
    float gain = 1.0f;
    bool loop = false;
    // Positional voices attenuate with distance and pan by azimuth;
    // non-positional ones play centered at full bus gain.
    bool positional = false;
    Vec3 position{};
    float refDistance = 2.0f;   // full volume within
    float maxDistance = 30.0f;  // silent beyond
};

class AudioMixer {
public:
    static constexpr uint32_t kMaxMixFrames = 4096;  // per mix() call

    explicit AudioMixer(BudgetRegistry& budgets, uint32_t sampleRate = 48000,
                        uint32_t maxVoices = 32);

    BudgetRegistry& budgets() { return budgets_; }
    BudgetId audioBudget() const { return audioBudget_; }

    // Starts a voice; refuses (invalid id) when all slots are busy (P1).
    AudioVoiceId play(const AudioClip& clip, const AudioPlayParams& params);
    bool stop(AudioVoiceId id);
    bool isPlaying(AudioVoiceId id) const;
    bool setVoicePosition(AudioVoiceId id, const Vec3& position);
    uint32_t activeVoices() const;

    void setBusGain(AudioBus bus, float gain);
    void setMasterGain(float gain);
    void setListener(const Vec3& position, const Vec3& forward);
    // While the Voice bus speaks, music glides to gain * duckedGain (task
    // 10.3); 1.0 disables ducking.
    void setMusicDucking(float duckedGain) { duckTarget_ = duckedGain; }
    float currentMusicDuck() const { return duck_; }

    // Fills `frames` interleaved STEREO int16 frames. Allocation-free;
    // frames is capped at kMaxMixFrames per call.
    void mix(int16_t* out, uint32_t frames);

    uint32_t sampleRate() const { return sampleRate_; }

private:
    struct Slot {
        bool used = false;
        uint16_t generation = 1;
        const AudioClip* clip = nullptr;
        double cursor = 0;  // source-frame position (fractional: resampling)
        AudioPlayParams params;
    };

    Slot* slotOf(AudioVoiceId id);
    const Slot* slotOf(AudioVoiceId id) const;

    BudgetRegistry& budgets_;
    BudgetId audioBudget_;
    uint32_t sampleRate_;
    std::vector<Slot> slots_;
    std::vector<float> scratch_;  // kMaxMixFrames * 2, fixed at init
    float busGain_[static_cast<size_t>(AudioBus::Count)] = {1, 1, 1};
    float masterGain_ = 1.0f;
    Vec3 listenerPosition_{};
    Vec3 listenerForward_{0, 0, -1};
    float duckTarget_ = 0.35f;
    float duck_ = 1.0f;  // smoothed music multiplier
    mutable std::mutex mutex_;
};

}  // namespace mge
