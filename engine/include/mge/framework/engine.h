#pragma once

// The engine object the Application layer drives (tasks 1.1/1.2 contract).
// Platform shells (Android activity, host runner) own the real clock and
// surface; they feed lifecycle events and elapsed time in, the engine does the
// rest. Nothing in here knows Android exists (P3 boundary).

#include <cstdint>
#include <memory>
#include <string>

#include "mge/core/frame_clock.h"
#include "mge/core/jobs.h"
#include "mge/core/memory.h"

namespace mge {

struct EngineConfig {
    double fixedStepSeconds = 1.0 / 60.0;
    size_t frameArenaBytes = 4u * 1024u * 1024u;       // per-frame scratch, reset every frame
    size_t persistentBudgetBytes = 64u * 1024u * 1024u;  // long-lived engine allocations
};

struct EngineStats {
    uint64_t frameCount = 0;
    uint64_t simStepCount = 0;
    double lastFrameDtSeconds = 0.0;
    size_t frameArenaHighWaterBytes = 0;  // worst per-frame scratch usage seen
};

class Engine {
public:
    Engine();
    ~Engine();

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    bool init(const EngineConfig& config);
    void shutdown();
    bool initialized() const { return initialized_; }

    // -- Lifecycle, forwarded by the platform shell (P3) --
    void onSurfaceCreated(int widthPx, int heightPx);
    void onSurfaceChanged(int widthPx, int heightPx);
    void onSurfaceLost();
    void onPause();
    void onResume();

    // One platform frame: advances zero or more fixed simulation steps, then
    // prepares a render with interpolation alpha. While paused this is a no-op.
    void tick(double dtSeconds);

    const EngineStats& stats() const { return stats_; }
    BudgetRegistry& budgets() { return budgets_; }
    JobSystem& jobs() { return *jobs_; }
    Arena& frameArena() { return *frameArena_; }

    std::string memoryReport() const;

private:
    void simulateStep(double stepSeconds);
    void render(float alpha);

    EngineConfig config_;
    FrameClock clock_;
    BudgetRegistry budgets_;
    BudgetId frameBudget_ = kInvalidBudget;
    BudgetId persistentBudget_ = kInvalidBudget;
    std::unique_ptr<Arena> frameArena_;
    std::unique_ptr<JobSystem> jobs_;
    EngineStats stats_;

    bool initialized_ = false;
    bool paused_ = false;
    bool surfaceReady_ = false;
    int surfaceWidth_ = 0;
    int surfaceHeight_ = 0;
};

}  // namespace mge
