#pragma once

// The on-device game: the template-game hamlet — real touch controls,
// humanoid player + AI NPCs, live positional audio — rendered by the engine
// and PRESENTED THROUGH A REAL VULKAN SWAPCHAIN (task 2.1 complete): the
// frame never leaves the GPU. Two degradation steps keep a failure visible
// instead of fatal: no swapchain -> the old readback + window blit; no
// Vulkan at all -> a parchment heartbeat.

#include <android/native_window.h>

#include "mge/audio/mixer.h"
#include "mge/framework/engine.h"

namespace mge {

class DeviceGame {
public:
    ~DeviceGame();

    // Engine must be initialized; call when the surface exists. The window
    // is where the Vulkan surface comes from (P3: the Android call lives in
    // this module, never in the engine).
    bool start(Engine& engine, AudioMixer& mixer, ANativeWindow* window,
               uint32_t surfaceWidth, uint32_t surfaceHeight);
    void stop();
    bool running() const { return impl_ != nullptr; }

    // One display frame: simulate AI/audio, render, present.
    void frame(double dtSeconds, ANativeWindow* window);
    // Surface resized/rotated: rebuild the swapchain against it.
    void onSurfaceResized(uint32_t width, uint32_t height);

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

}  // namespace mge
