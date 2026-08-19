// Headless host runner: boots the engine without Android and demonstrates the
// Phase 1 exit criteria that don't need a device — a stable fixed-step loop
// through lifecycle events, a live memory-budget dashboard, and zero
// steady-state heap allocations in the frame loop (P1).

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <new>

#include <cmath>

#include "mge/audio/mixer.h"
#include "mge/core/log.h"
#include "mge/framework/engine.h"

// Global allocation counter: proves the frame loop is allocation-free in
// steady state. Engine-internal arenas/pools charge budgets instead of
// hitting the heap per frame.
static std::atomic<long> gAllocCount{0};

void* operator new(std::size_t size) {
    gAllocCount.fetch_add(1, std::memory_order_relaxed);
    if (void* p = std::malloc(size)) return p;
    throw std::bad_alloc();
}

void* operator new[](std::size_t size) {
    gAllocCount.fetch_add(1, std::memory_order_relaxed);
    if (void* p = std::malloc(size)) return p;
    throw std::bad_alloc();
}

void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

int main() {
    mge::Engine engine;
    mge::EngineConfig config;
    if (!engine.init(config)) {
        MGE_LOGE("host", "engine init failed");
        return 1;
    }

    engine.onSurfaceCreated(1080, 2400);
    engine.onResume();

    // The audio mixer joins the P1 gate (task 10.5): a looping positional
    // voice mixes every frame inside the allocation counter's window.
    mge::BudgetRegistry audioBudgets;
    mge::AudioMixer mixer(audioBudgets);
    mge::AudioClip hum;
    {
        mge::WavData data;
        data.sampleRate = 22050;
        data.channels = 1;
        data.samples.resize(2205);
        for (size_t i = 0; i < data.samples.size(); ++i) {
            data.samples[i] = static_cast<int16_t>(
                6000.0f * std::sin(2.0f * mge::kPi * 110.0f * i / 22050.0f));
        }
        if (!hum.adopt(std::move(data), mixer)) return 1;
    }
    mge::AudioPlayParams humParams;
    humParams.loop = true;
    humParams.positional = true;
    humParams.position = {3, 0, -2};
    if (mixer.play(hum, humParams) == mge::kInvalidAudioVoice) return 1;
    static int16_t mixBuffer[800 * 2];  // one 60 Hz frame of 48 kHz stereo

    constexpr int kWarmupFrames = 60;
    constexpr int kSteadyFrames = 600;
    constexpr double kFrameDt = 1.0 / 60.0;

    for (int i = 0; i < kWarmupFrames; ++i) engine.tick(kFrameDt);

    // Exercise the Android lifecycle mid-run: pause/resume + surface loss.
    engine.onPause();
    engine.tick(kFrameDt);
    engine.onResume();
    engine.onSurfaceLost();
    engine.tick(kFrameDt);
    engine.onSurfaceCreated(1080, 2400);

    const long allocsBefore = gAllocCount.load();
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < kSteadyFrames; ++i) {
        engine.tick(kFrameDt);
        mixer.mix(mixBuffer, 800);  // the audio pull rides the same gate
    }
    const auto end = std::chrono::steady_clock::now();
    const long steadyAllocs = gAllocCount.load() - allocsBefore;

    const double totalMs = std::chrono::duration<double, std::milli>(end - start).count();

    printf("\n=== MobileGamesEngine host prototype ===\n");
    printf("frames: %llu  sim steps: %llu\n",
           static_cast<unsigned long long>(engine.stats().frameCount),
           static_cast<unsigned long long>(engine.stats().simStepCount));
    printf("steady-state: %d frames in %.2f ms (%.1f us/frame)\n", kSteadyFrames, totalMs,
           totalMs * 1000.0 / kSteadyFrames);
    printf("steady-state heap allocations: %ld  (target: 0)\n", steadyAllocs);
    printf("frame arena high water: %zu bytes\n", engine.stats().frameArenaHighWaterBytes);
    printf("\n--- memory budgets ---\n%s\n", engine.memoryReport().c_str());

    engine.shutdown();

    if (steadyAllocs != 0) {
        printf("FAIL: steady-state frame loop allocated on the heap\n");
        return 1;
    }
    printf("OK\n");
    return 0;
}
