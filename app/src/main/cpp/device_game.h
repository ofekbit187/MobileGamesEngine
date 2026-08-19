#pragma once

// The on-device game (first on-glass bring-up): the template-game hamlet —
// real touch controls, humanoid player + AI NPCs, live positional audio —
// rendered through the engine's offscreen Vulkan pass and blitted to the
// ANativeWindow. An honest stopgap for the missing swapchain (task 2.1):
// correct images via readback + blit; the swapchain replaces the blit later
// behind this same interface. If Vulkan is unavailable the screen falls back
// to a parchment heartbeat so a failure is visible, never a black screen.

#include <android/native_window.h>

#include "mge/audio/mixer.h"
#include "mge/framework/engine.h"

namespace mge {

class DeviceGame {
public:
    ~DeviceGame();

    // Engine must be initialized; call when the surface exists.
    bool start(Engine& engine, AudioMixer& mixer, uint32_t surfaceWidth,
               uint32_t surfaceHeight);
    void stop();
    bool running() const { return impl_ != nullptr; }

    // One display frame: simulate AI/audio, render, blit into the window.
    void frame(double dtSeconds, ANativeWindow* window);

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

}  // namespace mge
