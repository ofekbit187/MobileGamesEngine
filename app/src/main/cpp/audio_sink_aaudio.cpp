// AAudio output sink (task 10.7): the platform half of the audio pillar.
// Thin by design (ADR 0006): open a low-latency stereo PCM16 stream and let
// its callback PULL the portable mixer. Engine code never sees AAudio; this
// file lives with the JNI glue at the P3 boundary.

#include "audio_sink_aaudio.h"

#include <aaudio/AAudio.h>

#include "mge/core/log.h"

namespace mge {

namespace {

constexpr const char* kTag = "aaudio";

aaudio_data_callback_result_t dataCallback(AAudioStream*, void* userData, void* audioData,
                                           int32_t numFrames) {
    auto* mixer = static_cast<AudioMixer*>(userData);
    auto* out = static_cast<int16_t*>(audioData);
    // The mixer caps one call at kMaxMixFrames; large bursts loop.
    int32_t done = 0;
    while (done < numFrames) {
        const uint32_t chunk =
            static_cast<uint32_t>(numFrames - done) < AudioMixer::kMaxMixFrames
                ? static_cast<uint32_t>(numFrames - done)
                : AudioMixer::kMaxMixFrames;
        mixer->mix(out + done * 2, chunk);
        done += static_cast<int32_t>(chunk);
    }
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

void errorCallback(AAudioStream*, void* userData, aaudio_result_t error) {
    // Stream died (device change, etc.) — flag for a restart from the app
    // thread; never reopen from inside the dead stream's callback.
    MGE_LOGW(kTag, "stream error %d — restart requested", error);
    static_cast<AAudioSink*>(userData)->requestRestart();
}

}  // namespace

bool AAudioSink::start(AudioMixer& mixer) {
    stop();
    mixer_ = &mixer;

    AAudioStreamBuilder* builder = nullptr;
    if (AAudio_createStreamBuilder(&builder) != AAUDIO_OK) return false;
    AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_I16);
    AAudioStreamBuilder_setChannelCount(builder, 2);
    AAudioStreamBuilder_setSampleRate(builder, static_cast<int32_t>(mixer.sampleRate()));
    AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
    AAudioStreamBuilder_setDataCallback(builder, dataCallback, &mixer);
    AAudioStreamBuilder_setErrorCallback(builder, errorCallback, this);

    AAudioStream* stream = nullptr;
    const aaudio_result_t result = AAudioStreamBuilder_openStream(builder, &stream);
    AAudioStreamBuilder_delete(builder);
    if (result != AAUDIO_OK || stream == nullptr) {
        MGE_LOGE(kTag, "openStream failed: %d", result);
        return false;
    }
    if (AAudioStream_requestStart(stream) != AAUDIO_OK) {
        AAudioStream_close(stream);
        MGE_LOGE(kTag, "requestStart failed");
        return false;
    }
    stream_ = stream;
    restartRequested_.store(false);
    MGE_LOGI(kTag, "stream started at %d Hz", AAudioStream_getSampleRate(stream));
    return true;
}

void AAudioSink::stop() {
    if (stream_ != nullptr) {
        AAudioStream_requestStop(static_cast<AAudioStream*>(stream_));
        AAudioStream_close(static_cast<AAudioStream*>(stream_));
        stream_ = nullptr;
    }
}

// Called from the app thread (e.g. on resume): reopens after a device error.
bool AAudioSink::restartIfNeeded() {
    if (!restartRequested_.load() || mixer_ == nullptr) return false;
    return start(*mixer_);
}

}  // namespace mge
