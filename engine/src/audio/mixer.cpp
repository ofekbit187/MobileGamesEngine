#include "mge/audio/mixer.h"

#include <cmath>
#include <cstring>

#include "mge/core/log.h"

namespace mge {

namespace {

constexpr const char* kTag = "audio";
constexpr size_t kAudioBudgetCap = 64u * 1024 * 1024;

float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }

}  // namespace

// ----------------------------------------------------------------- clips ---

bool AudioClip::load(const char* wavPath, AudioMixer& mixer) {
    WavData data;
    if (!loadWav(wavPath, data)) return false;
    return adopt(std::move(data), mixer);
}

bool AudioClip::adopt(WavData&& data, AudioMixer& mixer) {
    unload();
    const size_t bytes = data.samples.size() * sizeof(int16_t);
    if (!mixer.budgets().charge(mixer.audioBudget(), bytes)) {
        MGE_LOGW(kTag, "audio budget refused a %zu KiB clip", bytes / 1024);
        return false;  // refuse, never grow (P1)
    }
    data_ = std::move(data);
    budgets_ = &mixer.budgets();
    budget_ = mixer.audioBudget();
    chargedBytes_ = bytes;
    return true;
}

void AudioClip::unload() {
    if (budgets_ != nullptr && chargedBytes_ > 0) {
        budgets_->release(budget_, chargedBytes_);
    }
    data_ = WavData{};
    budgets_ = nullptr;
    chargedBytes_ = 0;
}

// ----------------------------------------------------------------- mixer ---

AudioMixer::AudioMixer(BudgetRegistry& budgets, uint32_t sampleRate, uint32_t maxVoices)
    : budgets_(budgets),
      audioBudget_(budgets.registerBudget("audio", kAudioBudgetCap)),
      sampleRate_(sampleRate),
      slots_(maxVoices),
      scratch_(kMaxMixFrames * 2, 0.0f) {}

AudioMixer::Slot* AudioMixer::slotOf(AudioVoiceId id) {
    if (id == kInvalidAudioVoice) return nullptr;
    const uint32_t index = (id & 0xFFu) - 1;
    if (index >= slots_.size()) return nullptr;
    Slot& slot = slots_[index];
    return slot.used && slot.generation == (id >> 8) ? &slot : nullptr;
}

const AudioMixer::Slot* AudioMixer::slotOf(AudioVoiceId id) const {
    return const_cast<AudioMixer*>(this)->slotOf(id);
}

AudioVoiceId AudioMixer::play(const AudioClip& clip, const AudioPlayParams& params) {
    if (!clip.loaded()) return kInvalidAudioVoice;
    std::lock_guard<std::mutex> lock(mutex_);
    for (uint32_t i = 0; i < slots_.size(); ++i) {
        Slot& slot = slots_[i];
        if (slot.used) continue;
        slot.used = true;
        slot.clip = &clip;
        slot.cursor = 0;
        slot.params = params;
        return (i + 1) | (static_cast<uint32_t>(slot.generation) << 8);
    }
    MGE_LOGW(kTag, "all %zu mixer voices busy — play refused", slots_.size());
    return kInvalidAudioVoice;  // refuse, never grow (P1)
}

bool AudioMixer::stop(AudioVoiceId id) {
    std::lock_guard<std::mutex> lock(mutex_);
    Slot* slot = slotOf(id);
    if (slot == nullptr) return false;
    slot->used = false;
    ++slot->generation;
    return true;
}

bool AudioMixer::isPlaying(AudioVoiceId id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return slotOf(id) != nullptr;
}

bool AudioMixer::setVoicePosition(AudioVoiceId id, const Vec3& position) {
    std::lock_guard<std::mutex> lock(mutex_);
    Slot* slot = slotOf(id);
    if (slot == nullptr) return false;
    slot->params.position = position;
    return true;
}

uint32_t AudioMixer::activeVoices() const {
    std::lock_guard<std::mutex> lock(mutex_);
    uint32_t count = 0;
    for (const Slot& slot : slots_) count += slot.used ? 1 : 0;
    return count;
}

void AudioMixer::setBusGain(AudioBus bus, float gain) {
    std::lock_guard<std::mutex> lock(mutex_);
    busGain_[static_cast<size_t>(bus)] = gain;
}

void AudioMixer::setMasterGain(float gain) {
    std::lock_guard<std::mutex> lock(mutex_);
    masterGain_ = gain;
}

void AudioMixer::setListener(const Vec3& position, const Vec3& forward) {
    std::lock_guard<std::mutex> lock(mutex_);
    listenerPosition_ = position;
    listenerForward_ = forward.normalized();
}

void AudioMixer::mix(int16_t* out, uint32_t frames) {
    if (frames > kMaxMixFrames) frames = kMaxMixFrames;
    std::lock_guard<std::mutex> lock(mutex_);
    std::memset(scratch_.data(), 0, frames * 2 * sizeof(float));

    // Ducking (task 10.3): music glides toward duckTarget_ while the Voice
    // bus speaks, and back to 1 when it stops — no clicks.
    bool voiceSpeaking = false;
    for (const Slot& slot : slots_) {
        if (slot.used && slot.params.bus == AudioBus::Voice) voiceSpeaking = true;
    }
    const float duckGoal = voiceSpeaking ? duckTarget_ : 1.0f;
    duck_ += (duckGoal - duck_) * 0.15f;

    const Vec3 up{0, 1, 0};
    const Vec3 right = listenerForward_.cross(up).normalized();

    for (Slot& slot : slots_) {
        if (!slot.used) continue;
        const WavData& source = slot.clip->data();
        const double step = static_cast<double>(source.sampleRate) / sampleRate_;
        const size_t sourceFrames = source.samples.size() / source.channels;
        if (sourceFrames == 0) {
            slot.used = false;
            ++slot.generation;
            continue;
        }

        // Voice gain: bus x per-voice x positional attenuation, pan L/R.
        float gain = slot.params.gain * busGain_[static_cast<size_t>(slot.params.bus)];
        if (slot.params.bus == AudioBus::Music) gain *= duck_;
        float left = 0.7071f, right01 = 0.7071f;  // centered, constant power
        if (slot.params.positional) {
            Vec3 delta = slot.params.position - listenerPosition_;
            const float distance = delta.length();
            const float span = slot.params.maxDistance - slot.params.refDistance;
            const float attenuation =
                span > 0 ? clamp01((slot.params.maxDistance - distance) / span) : 1.0f;
            gain *= attenuation;
            if (distance > 0.001f) {
                const float pan = clamp01(0.5f + 0.5f * (delta * (1.0f / distance)).dot(right));
                left = std::cos(pan * kPi * 0.5f);
                right01 = std::sin(pan * kPi * 0.5f);
            }
        }
        gain *= masterGain_;

        for (uint32_t f = 0; f < frames; ++f) {
            size_t frame0 = static_cast<size_t>(slot.cursor);
            if (frame0 >= sourceFrames) {
                if (!slot.params.loop) break;
                slot.cursor -= static_cast<double>(sourceFrames);
                frame0 = static_cast<size_t>(slot.cursor);
                if (frame0 >= sourceFrames) break;  // degenerate step
            }
            // Linear resampling between neighboring source frames.
            const size_t frame1 =
                frame0 + 1 < sourceFrames ? frame0 + 1
                                          : (slot.params.loop ? 0 : frame0);
            const float t = static_cast<float>(slot.cursor - frame0);
            float mono = 0;
            for (uint16_t c = 0; c < source.channels; ++c) {
                const float s0 = source.samples[frame0 * source.channels + c];
                const float s1 = source.samples[frame1 * source.channels + c];
                mono += s0 + (s1 - s0) * t;
            }
            mono /= source.channels;
            scratch_[f * 2 + 0] += mono * gain * left;
            scratch_[f * 2 + 1] += mono * gain * right01;
            slot.cursor += step;
        }
        if (!slot.params.loop && slot.cursor >= static_cast<double>(sourceFrames)) {
            slot.used = false;  // finished
            ++slot.generation;
        }
    }

    // Clamp the sum into the output (many voices may add past full scale).
    for (uint32_t i = 0; i < frames * 2; ++i) {
        float v = scratch_[i];
        if (v > 32767.0f) v = 32767.0f;
        if (v < -32768.0f) v = -32768.0f;
        out[i] = static_cast<int16_t>(v);
    }
}

}  // namespace mge
