#include "mge/framework/engine.h"

#include "mge/core/log.h"

namespace mge {

namespace {
constexpr const char* kTag = "engine";
}

Engine::Engine() = default;

Engine::~Engine() {
    if (initialized_) shutdown();
}

bool Engine::init(const EngineConfig& config) {
    if (initialized_) return true;
    config_ = config;
    clock_ = FrameClock(config.fixedStepSeconds);

    frameBudget_ = budgets_.registerBudget("frame", config.frameArenaBytes);
    persistentBudget_ = budgets_.registerBudget("persistent", config.persistentBudgetBytes);

    frameArena_ = std::make_unique<Arena>(budgets_, frameBudget_, config.frameArenaBytes);
    if (!frameArena_->valid()) {
        MGE_LOGE(kTag, "failed to create frame arena (%zu bytes)", config.frameArenaBytes);
        return false;
    }
    jobs_ = std::make_unique<JobSystem>();

    initialized_ = true;
    MGE_LOGI(kTag, "engine initialized (fixed step %.4fs, frame arena %zu KiB)",
             config.fixedStepSeconds, config.frameArenaBytes / 1024);
    return true;
}

void Engine::shutdown() {
    if (!initialized_) return;
    jobs_->drainAll();
    jobs_.reset();
    frameArena_.reset();
    initialized_ = false;
    MGE_LOGI(kTag, "engine shut down after %llu frames / %llu sim steps",
             static_cast<unsigned long long>(stats_.frameCount),
             static_cast<unsigned long long>(stats_.simStepCount));
}

void Engine::onSurfaceCreated(int widthPx, int heightPx) {
    surfaceReady_ = true;
    surfaceWidth_ = widthPx;
    surfaceHeight_ = heightPx;
    MGE_LOGI(kTag, "surface created %dx%d", widthPx, heightPx);
}

void Engine::onSurfaceChanged(int widthPx, int heightPx) {
    surfaceWidth_ = widthPx;
    surfaceHeight_ = heightPx;
}

void Engine::onSurfaceLost() {
    surfaceReady_ = false;
    MGE_LOGI(kTag, "surface lost");
}

void Engine::onPause() {
    paused_ = true;
    MGE_LOGI(kTag, "paused");
}

void Engine::onResume() {
    paused_ = false;
    MGE_LOGI(kTag, "resumed");
}

void Engine::tick(double dtSeconds) {
    if (!initialized_ || paused_) return;

    stats_.lastFrameDtSeconds = dtSeconds;

    const int steps = clock_.advance(dtSeconds);
    for (int i = 0; i < steps; ++i) {
        simulateStep(clock_.fixedStepSeconds());
        ++stats_.simStepCount;
    }

    if (surfaceReady_) {
        render(static_cast<float>(clock_.alpha()));
    }

    if (frameArena_->usedBytes() > stats_.frameArenaHighWaterBytes) {
        stats_.frameArenaHighWaterBytes = frameArena_->usedBytes();
    }
    frameArena_->reset();
    ++stats_.frameCount;
}

void Engine::simulateStep(double stepSeconds) {
    // Simulation systems land here in phase order: input intents, characters,
    // streaming tick, UI update. For the prototype the step only exercises the
    // per-frame arena the way real systems will.
    (void)stepSeconds;
    void* scratch = frameArena_->alloc(256);
    (void)scratch;
}

void Engine::render(float alpha) {
    // Graphics engine submission goes here (Phase 2). Prototype: no-op.
    (void)alpha;
}

std::string Engine::memoryReport() const {
    std::string out;
    out.reserve(512);
    budgets_.report(out);
    return out;
}

}  // namespace mge
