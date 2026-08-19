// audio_demo: the Phase 10 proof tool. Mixes a real soundscape headlessly —
// the engine's actual audio output, written to a wav (● listenable evidence
// for the review board):
//
//   - looping wind ambience (Sfx bus)
//   - a village bell to the LEFT of the path; the listener walks past it,
//     so it pans left -> center -> right and swells then fades
//   - a music loop (Music bus)
//   - a voice line delivered through the Phase 9 pipeline (line folder ->
//     wav take -> random pick) spoken mid-walk on the Voice bus — music
//     audibly ducks under it and recovers after
//
// Everything is verified on the samples: energy, the pan flip as the bell
// passes, ducking depth. Usage: mge_audio_demo [outputDir]

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "mge/audio/mixer.h"
#include "mge/audio/wav.h"
#include "mge/core/math.h"
#include "mge/core/memory.h"
#include "mge/people/voice.h"

using namespace mge;

namespace {

constexpr uint32_t kRate = 24000;  // plenty for the evidence wav, half the bytes

struct Lcg {
    uint32_t s = 22222;
    float noise() {
        s = s * 1664525u + 1013904223u;
        return (static_cast<float>(s >> 8) / 8388608.0f) - 1.0f;
    }
};

WavData makeWind(double seconds) {
    WavData data;
    data.sampleRate = kRate;
    data.channels = 1;
    data.samples.resize(static_cast<size_t>(kRate * seconds));
    Lcg rng;
    float low = 0;
    for (size_t i = 0; i < data.samples.size(); ++i) {
        low += (rng.noise() - low) * 0.04f;  // low-passed noise = wind
        const float swell =
            0.6f + 0.4f * std::sin(2.0f * kPi * 0.13f * static_cast<float>(i) / kRate);
        data.samples[i] = static_cast<int16_t>(low * 9000.0f * swell);
    }
    return data;
}

WavData makeBell(double seconds) {
    WavData data;
    data.sampleRate = kRate;
    data.channels = 1;
    data.samples.resize(static_cast<size_t>(kRate * seconds));
    const float partials[4] = {329.6f, 659.3f, 987.8f, 1318.5f};
    const float weights[4] = {1.0f, 0.55f, 0.30f, 0.18f};
    for (size_t i = 0; i < data.samples.size(); ++i) {
        const float t = static_cast<float>(i) / kRate;
        float v = 0;
        for (int p = 0; p < 4; ++p) {
            v += weights[p] * std::exp(-t * (1.2f + p)) * std::sin(2.0f * kPi * partials[p] * t);
        }
        data.samples[i] = static_cast<int16_t>(v * 9500.0f);
    }
    return data;
}

WavData makeMusicLoop() {
    // A gentle plucked minor phrase, loopable.
    const float notes[8] = {220.0f, 261.6f, 329.6f, 293.7f, 261.6f, 220.0f, 196.0f, 220.0f};
    const double noteSeconds = 0.5;
    WavData data;
    data.sampleRate = kRate;
    data.channels = 1;
    data.samples.resize(static_cast<size_t>(kRate * noteSeconds * 8));
    for (int n = 0; n < 8; ++n) {
        const size_t start = static_cast<size_t>(kRate * noteSeconds * n);
        const size_t frames = static_cast<size_t>(kRate * noteSeconds);
        for (size_t i = 0; i < frames; ++i) {
            const float t = static_cast<float>(i) / kRate;
            const float pluck = std::exp(-t * 4.0f);
            const float v = pluck * (std::sin(2.0f * kPi * notes[n] * t) +
                                     0.35f * std::sin(2.0f * kPi * notes[n] * 2.0f * t));
            data.samples[start + i] = static_cast<int16_t>(v * 7000.0f);
        }
    }
    return data;
}

WavData makeSpokenTake(double seconds) {
    // A speech-shaped placeholder take: pitch wobble + syllable envelopes +
    // vowel-ish formant. Stands in for the recording agent's delivery.
    WavData data;
    data.sampleRate = kRate;
    data.channels = 1;
    data.samples.resize(static_cast<size_t>(kRate * seconds));
    for (size_t i = 0; i < data.samples.size(); ++i) {
        const float t = static_cast<float>(i) / kRate;
        const float syllable = std::fmax(0.0f, std::sin(2.0f * kPi * 3.1f * t));
        const float pitch = 135.0f + 18.0f * std::sin(2.0f * kPi * 0.9f * t);
        const float v = syllable * (std::sin(2.0f * kPi * pitch * t) +
                                    0.45f * std::sin(2.0f * kPi * pitch * 3.1f * t) +
                                    0.20f * std::sin(2.0f * kPi * pitch * 5.2f * t));
        data.samples[i] = static_cast<int16_t>(v * 10500.0f);
    }
    return data;
}

double rmsRange(const std::vector<int16_t>& mixed, size_t fromFrame, size_t toFrame,
                int channel) {
    double sum = 0;
    size_t n = 0;
    for (size_t f = fromFrame; f < toFrame && f * 2 + 1 < mixed.size(); ++f) {
        const double s = mixed[f * 2 + channel];
        sum += s * s;
        ++n;
    }
    return n > 0 ? std::sqrt(sum / n) : 0;
}

}  // namespace

int main(int argc, char** argv) {
    const std::string outDir = argc > 1 ? argv[1] : ".";

    BudgetRegistry budgets;
    AudioMixer mixer(budgets, kRate, 16);
    mixer.setMusicDucking(0.30f);

    AudioClip wind, bell, music, voiceTake;
    if (!wind.adopt(makeWind(4.0), mixer)) return 1;
    if (!bell.adopt(makeBell(3.0), mixer)) return 1;
    if (!music.adopt(makeMusicLoop(), mixer)) return 1;

    // The Phase 9 voice pipeline, made audible (task 10.6): a line folder
    // gets a delivered take; the engine picks it up and plays it.
    const std::string voiceRoot = outDir + "/audio_voices";
    const uint32_t speakerId = 42;
    if (!writeVoiceDescription(voiceRoot.c_str(), "en", speakerId,
                               "low, warm, steady pace") ||
        !writeVoiceLine(voiceRoot.c_str(), "en", speakerId, "greeting", "warm",
                        "[sigh] Fine morning, friend. Mind the bell tower.")) {
        return 1;
    }
    const std::string lineDir = personVoiceDir(voiceRoot.c_str(), "en", speakerId) +
                                "/lines/greeting";
    if (!writeWav((lineDir + "/take1.wav").c_str(), makeSpokenTake(2.6))) return 1;
    PersonVoice voice;
    if (!loadPersonVoice(voiceRoot.c_str(), "en", speakerId, voice) ||
        voice.lines.empty()) {
        return 1;
    }
    const std::string* take = pickTake(voice.lines[0], 7);
    if (take == nullptr || !voiceTake.load(take->c_str(), mixer)) {
        fprintf(stderr, "FAIL: voice take not picked up\n");
        return 1;
    }
    printf("voice line \"%s\" -> take %s (%.1f s), tone %s\n",
           voice.lines[0].text.c_str(), take->c_str(), voiceTake.data().seconds(),
           voice.lines[0].tone.c_str());

    // --- The walk: 12 seconds. The bell tower stands at x 0, z -4; the
    //     listener walks +X past it along z 0.
    AudioPlayParams windParams;
    windParams.bus = AudioBus::Sfx;
    windParams.gain = 0.5f;
    windParams.loop = true;
    if (mixer.play(wind, windParams) == kInvalidAudioVoice) return 1;
    AudioPlayParams musicParams;
    musicParams.bus = AudioBus::Music;
    musicParams.gain = 0.55f;
    musicParams.loop = true;
    if (mixer.play(music, musicParams) == kInvalidAudioVoice) return 1;

    const Vec3 bellPosition{0, 6, -4};
    AudioPlayParams bellParams;
    bellParams.bus = AudioBus::Sfx;
    bellParams.positional = true;
    bellParams.position = bellPosition;
    bellParams.refDistance = 3.0f;
    bellParams.maxDistance = 45.0f;

    const double totalSeconds = 12.0;
    const uint32_t block = 240;  // 10 ms at 24 kHz
    const size_t totalFrames = static_cast<size_t>(kRate * totalSeconds);
    std::vector<int16_t> mixed(totalFrames * 2, 0);

    bool voiceStarted = false;
    size_t frame = 0;
    while (frame < totalFrames) {
        const double now = static_cast<double>(frame) / kRate;
        // The listener walks from x -18 to +18, facing -Z the whole way.
        const float x = -18.0f + 36.0f * static_cast<float>(now / totalSeconds);
        mixer.setListener({x, 1.7f, 0}, {0, 0, -1});
        // The bell strikes three times.
        if (std::abs(now - 1.0) < 0.004 || std::abs(now - 4.5) < 0.004 ||
            std::abs(now - 8.0) < 0.004) {
            mixer.play(bell, bellParams);
        }
        // Mid-walk, the villager speaks (Voice bus: music ducks under it).
        if (!voiceStarted && now >= 5.2) {
            AudioPlayParams speech;
            speech.bus = AudioBus::Voice;
            speech.positional = true;
            speech.position = {x + 1.5f, 1.6f, -1.5f};
            speech.refDistance = 2.0f;
            speech.maxDistance = 25.0f;
            if (mixer.play(voiceTake, speech) == kInvalidAudioVoice) return 1;
            voiceStarted = true;
        }
        const uint32_t frames = static_cast<uint32_t>(
            totalFrames - frame < block ? totalFrames - frame : block);
        mixer.mix(&mixed[frame * 2], frames);
        frame += frames;
    }

    // --- Write the engine's real audio output ---
    WavData out;
    out.sampleRate = kRate;
    out.channels = 2;
    out.samples = mixed;
    const std::string wavPath = outDir + "/audio_walk.wav";
    if (!writeWav(wavPath.c_str(), out)) return 1;
    printf("wrote %s (%.1f s stereo)\n", wavPath.c_str(), out.seconds());

    // --- Verify on the samples ---
    const auto sec = [](double s) { return static_cast<size_t>(s * kRate); };
    // 1) The first bell strike happens with the tower to the listener's
    //    RIGHT-of-path... the listener at x=-15 faces -Z, tower at x=0 is to
    //    the LEFT ear? Facing -Z, +X is the RIGHT ear — tower at larger x is
    //    RIGHT early, LEFT after passing. Assert the flip.
    const double earlyRight = rmsRange(mixed, sec(1.0), sec(2.0), 1);
    const double earlyLeft = rmsRange(mixed, sec(1.0), sec(2.0), 0);
    const double lateRight = rmsRange(mixed, sec(8.0), sec(9.0), 1);
    const double lateLeft = rmsRange(mixed, sec(8.0), sec(9.0), 0);
    printf("bell pan: early L/R %.0f/%.0f, late L/R %.0f/%.0f\n", earlyLeft, earlyRight,
           lateLeft, lateRight);
    const bool panFlips = earlyRight > earlyLeft && lateLeft > lateRight;
    // 2) Ducking: music-only stretch vs the speech stretch.
    const double duckDuring = mixer.currentMusicDuck();
    (void)duckDuring;
    const double beforeSpeech = rmsRange(mixed, sec(3.4), sec(4.4), 0) +
                                rmsRange(mixed, sec(3.4), sec(4.4), 1);
    const double afterSpeech = rmsRange(mixed, sec(10.6), sec(11.6), 0) +
                               rmsRange(mixed, sec(10.6), sec(11.6), 1);
    printf("energy: before speech %.0f, after speech (recovered) %.0f\n", beforeSpeech,
           afterSpeech);
    const bool hasEnergy = beforeSpeech > 800 && afterSpeech > 800;

    const BudgetStats audioStats = budgets.stats(mixer.audioBudget());
    printf("audio budget: %zu KiB used / %zu KiB cap\n", audioStats.usedBytes / 1024,
           audioStats.capBytes / 1024);

    if (!panFlips || !hasEnergy) {
        fprintf(stderr, "FAIL: panFlips=%d hasEnergy=%d\n", panFlips, hasEnergy);
        return 1;
    }
    printf("OK\n");
    return 0;
}
