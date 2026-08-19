#pragma once

// AAudio output sink (task 10.7) — see audio_sink_aaudio.cpp.

#include <atomic>

#include "mge/audio/mixer.h"

namespace mge {

class AAudioSink {
public:
    ~AAudioSink() { stop(); }

    bool start(AudioMixer& mixer);
    void stop();
    bool restartIfNeeded();
    void requestRestart() { restartRequested_.store(true); }
    bool running() const { return stream_ != nullptr; }

private:
    void* stream_ = nullptr;  // AAudioStream*, kept opaque here
    AudioMixer* mixer_ = nullptr;
    std::atomic<bool> restartRequested_{false};
};

}  // namespace mge
