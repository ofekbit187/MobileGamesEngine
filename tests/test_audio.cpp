// Phase 10: the audio mixer, proven headlessly — this environment has no
// sound device, so correctness is samples on buffers: energy, pan asymmetry,
// resampling, ducking depth, capacity refusal, clamp safety.

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>

#include "mge/audio/mixer.h"
#include "test_framework.h"

using namespace mge;

namespace {

WavData tone(uint32_t sampleRate, uint16_t channels, double seconds, int16_t amplitude,
             float hz = 220.0f) {
    WavData data;
    data.sampleRate = sampleRate;
    data.channels = channels;
    const size_t frames = static_cast<size_t>(sampleRate * seconds);
    data.samples.resize(frames * channels);
    for (size_t f = 0; f < frames; ++f) {
        const float s = std::sin(2.0f * kPi * hz * static_cast<float>(f) / sampleRate);
        for (uint16_t c = 0; c < channels; ++c) {
            data.samples[f * channels + c] = static_cast<int16_t>(s * amplitude);
        }
    }
    return data;
}

double rms(const int16_t* samples, size_t count, int channel = -1) {
    double sum = 0;
    size_t n = 0;
    for (size_t i = 0; i < count; ++i) {
        if (channel >= 0 && static_cast<int>(i & 1) != channel) continue;
        sum += static_cast<double>(samples[i]) * samples[i];
        ++n;
    }
    return n > 0 ? std::sqrt(sum / n) : 0;
}

}  // namespace

MGE_TEST(mixer_plays_mixes_and_finishes) {
    BudgetRegistry budgets;
    AudioMixer mixer(budgets, 48000, 8);
    AudioClip clip;
    MGE_CHECK(clip.adopt(tone(48000, 1, 0.1, 8000), mixer));

    // Silence before anything plays.
    int16_t out[512 * 2];
    mixer.mix(out, 512);
    MGE_CHECK_NEAR(static_cast<float>(rms(out, 1024)), 0.0f, 1e-3f);

    AudioPlayParams params;
    params.bus = AudioBus::Sfx;
    const AudioVoiceId voice = mixer.play(clip, params);
    MGE_CHECK(voice != kInvalidAudioVoice);
    MGE_CHECK(mixer.isPlaying(voice));
    mixer.mix(out, 512);
    const double solo = rms(out, 1024);
    MGE_CHECK(solo > 1000);  // audible energy

    // Two voices carry more energy than one (additive mix; a different
    // frequency so the tones can't phase-cancel).
    AudioClip clipB;
    MGE_CHECK(clipB.adopt(tone(48000, 1, 0.1, 8000, 347.0f), mixer));
    const AudioVoiceId voice2 = mixer.play(clipB, params);
    MGE_CHECK(voice2 != kInvalidAudioVoice);
    int16_t out2[512 * 2];
    mixer.mix(out2, 512);
    MGE_CHECK(rms(out2, 1024) > solo * 1.2);

    // A 0.1 s clip finishes on its own; the slot frees.
    for (int i = 0; i < 20; ++i) mixer.mix(out, 512);
    MGE_CHECK(!mixer.isPlaying(voice));
    MGE_CHECK(mixer.activeVoices() == 0);

    // Looped voices never finish until stopped.
    params.loop = true;
    const AudioVoiceId looped = mixer.play(clip, params);
    for (int i = 0; i < 40; ++i) mixer.mix(out, 512);
    MGE_CHECK(mixer.isPlaying(looped));
    MGE_CHECK(mixer.stop(looped));
    MGE_CHECK(!mixer.isPlaying(looped));
}

MGE_TEST(mixer_resamples_other_rates) {
    BudgetRegistry budgets;
    AudioMixer mixer(budgets, 48000, 4);
    // A 22050 Hz clip (voice takes come at any rate) must last the same
    // wall-clock time at 48000: 0.1 s = 4800 output frames.
    AudioClip clip;
    MGE_CHECK(clip.adopt(tone(22050, 1, 0.1, 8000), mixer));
    AudioPlayParams params;
    const AudioVoiceId voice = mixer.play(clip, params);
    MGE_CHECK(voice != kInvalidAudioVoice);
    int16_t out[480 * 2];
    int framesAlive = 0;
    for (int block = 0; block < 20; ++block) {
        if (mixer.isPlaying(voice)) ++framesAlive;
        mixer.mix(out, 480);  // 10 ms per block
    }
    MGE_CHECK(framesAlive >= 9 && framesAlive <= 11);  // ~0.1 s of playback
}

MGE_TEST(mixer_positional_pan_and_attenuation) {
    BudgetRegistry budgets;
    AudioMixer mixer(budgets, 48000, 4);
    AudioClip clip;
    MGE_CHECK(clip.adopt(tone(48000, 1, 1.0, 8000), mixer));

    mixer.setListener({0, 0, 0}, {0, 0, -1});  // facing -Z; +X is the right ear
    AudioPlayParams params;
    params.positional = true;
    params.loop = true;
    params.refDistance = 1.0f;
    params.maxDistance = 20.0f;

    // Source to the LEFT: left channel carries more energy.
    params.position = {-4, 0, 0};
    AudioVoiceId voice = mixer.play(clip, params);
    int16_t out[1024 * 2];
    mixer.mix(out, 1024);
    const double left = rms(out, 2048, 0);
    const double right = rms(out, 2048, 1);
    MGE_CHECK(left > right * 2.0);
    mixer.stop(voice);

    // Same source far away is much quieter than near.
    params.position = {0, 0, -2};
    voice = mixer.play(clip, params);
    mixer.mix(out, 1024);
    const double near = rms(out, 2048);
    mixer.stop(voice);
    params.position = {0, 0, -18};
    voice = mixer.play(clip, params);
    mixer.mix(out, 1024);
    const double far = rms(out, 2048);
    mixer.stop(voice);
    MGE_CHECK(near > far * 3.0);

    // Beyond maxDistance: silent.
    params.position = {0, 0, -50};
    voice = mixer.play(clip, params);
    mixer.mix(out, 1024);
    MGE_CHECK(rms(out, 2048) < 10);
    mixer.stop(voice);
}

MGE_TEST(mixer_buses_duck_music_under_speech) {
    BudgetRegistry budgets;
    AudioMixer mixer(budgets, 48000, 4);
    AudioClip music, speech;
    MGE_CHECK(music.adopt(tone(48000, 1, 2.0, 8000, 110.0f), mixer));
    MGE_CHECK(speech.adopt(tone(48000, 1, 0.5, 8000, 440.0f), mixer));
    mixer.setMusicDucking(0.3f);

    AudioPlayParams musicParams;
    musicParams.bus = AudioBus::Music;
    musicParams.loop = true;
    MGE_CHECK(mixer.play(music, musicParams) != kInvalidAudioVoice);
    int16_t out[1024 * 2];
    for (int i = 0; i < 8; ++i) mixer.mix(out, 1024);  // settle
    const double musicAlone = rms(out, 2048);

    // A voice line starts: music glides down toward the ducked level.
    AudioPlayParams speechParams;
    speechParams.bus = AudioBus::Voice;
    const AudioVoiceId line = mixer.play(speech, speechParams);
    MGE_CHECK(line != kInvalidAudioVoice);
    for (int i = 0; i < 24; ++i) mixer.mix(out, 1024);
    MGE_CHECK(mixer.currentMusicDuck() < 0.4f);

    // The line ends: music recovers.
    while (mixer.isPlaying(line)) mixer.mix(out, 1024);
    for (int i = 0; i < 40; ++i) mixer.mix(out, 1024);
    MGE_CHECK(mixer.currentMusicDuck() > 0.9f);
    const double musicAfter = rms(out, 2048);
    MGE_CHECK(musicAfter > musicAlone * 0.7);

    // Bus gain zero silences that bus outright.
    mixer.setBusGain(AudioBus::Music, 0.0f);
    mixer.mix(out, 1024);
    MGE_CHECK(rms(out, 2048) < 10);
}

MGE_TEST(mixer_capacity_and_budget_refuse) {
    BudgetRegistry budgets;
    AudioMixer mixer(budgets, 48000, 2);  // tiny on purpose
    AudioClip clip;
    MGE_CHECK(clip.adopt(tone(48000, 1, 1.0, 4000), mixer));
    AudioPlayParams params;
    params.loop = true;
    MGE_CHECK(mixer.play(clip, params) != kInvalidAudioVoice);
    MGE_CHECK(mixer.play(clip, params) != kInvalidAudioVoice);
    MGE_CHECK(mixer.play(clip, params) == kInvalidAudioVoice);  // refuse (P1)
    MGE_CHECK(mixer.activeVoices() == 2);

    // The audio budget refuses an over-cap clip; the loaded set stands.
    WavData giant;
    giant.sampleRate = 48000;
    giant.channels = 2;
    giant.samples.resize((64u * 1024 * 1024) / 2 + 1024);  // just past the cap
    AudioClip tooBig;
    MGE_CHECK(!tooBig.adopt(std::move(giant), mixer));
    MGE_CHECK(!tooBig.loaded());

    // Overdriven voices clamp to full scale instead of wrapping: the output
    // is loud and saturated, never garbage.
    mixer.setMasterGain(8.0f);
    int16_t out[256 * 2];
    mixer.mix(out, 256);
    MGE_CHECK(rms(out, 512) > 20000);  // pinned near full scale
    int peak = 0;
    for (int16_t s : out) peak = std::max(peak, std::abs(static_cast<int>(s)));
    MGE_CHECK(peak >= 32767);
}
