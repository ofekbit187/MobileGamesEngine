#include "mge/framework/engine.h"

#include "mge/core/log.h"
#include "mge/framework/camera_controller.h"
#include "mge/framework/character.h"

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
    io_ = std::make_unique<AsyncIO>();
    world_ = std::make_unique<World>(config.worldEntityCapacity);

    initialized_ = true;
    MGE_LOGI(kTag, "engine initialized (fixed step %.4fs, frame arena %zu KiB)",
             config.fixedStepSeconds, config.frameArenaBytes / 1024);
    return true;
}

void Engine::shutdown() {
    if (!initialized_) return;
    io_->resume();  // a paused AsyncIO can't finish its queue
    io_->drain();
    io_.reset();
    jobs_->drainAll();
    jobs_.reset();
    world_.reset();
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
    controls_.configure(static_cast<float>(widthPx), static_cast<float>(heightPx));
    MGE_LOGI(kTag, "surface created %dx%d", widthPx, heightPx);
}

void Engine::onSurfaceChanged(int widthPx, int heightPx) {
    surfaceWidth_ = widthPx;
    surfaceHeight_ = heightPx;
    controls_.configure(static_cast<float>(widthPx), static_cast<float>(heightPx));
}

void Engine::onSurfaceLost() {
    surfaceReady_ = false;
    MGE_LOGI(kTag, "surface lost");
}

void Engine::onPause() {
    paused_ = true;
    if (io_) io_->pause();  // don't burn battery/IO while backgrounded
    MGE_LOGI(kTag, "paused");
}

void Engine::onResume() {
    paused_ = false;
    if (io_) io_->resume();
    MGE_LOGI(kTag, "resumed");
}

void Engine::pushTouchEvent(const TouchEvent& event) { inputQueue_.push(event); }

void Engine::drainInput() {
    TouchEvent event;
    while (inputQueue_.pop(event)) {
        lastTouch_ = event;
        ++stats_.inputEventCount;
        controls_.handle(event);
    }
}

void Engine::tick(double dtSeconds) {
    if (!initialized_ || paused_) return;

    stats_.lastFrameDtSeconds = dtSeconds;
    drainInput();

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
    // Order per architecture: input intents -> player control -> world step.
    // (Streaming tick and UI update join this sequence in their phases.)
    const GameplayIntents intents = controls_.consume();
    lastIntents_ = intents;

    if (world_->entities().isAlive(player_)) {
        const TransformComponent* t = world_->transform(player_);
        const MovementComponent* m = world_->movement(player_);
        if (t != nullptr && m != nullptr) {
            // Same intent contract the AI uses (task 8.2, P9): the controllers
            // differ in how the intent is produced, never in how it acts.
            const float yaw = t->yaw + intents.lookX * config_.turnSensitivity;
            CharacterIntent intent;
            intent.lookDelta = intents.lookX * config_.turnSensitivity;
            intent.move = yawRight(yaw) * intents.moveX + yawForward(yaw) * intents.moveY;
            intent.speed = m->maxSpeed;
            intent.faceMove = false;  // the player faces where the camera looks
            applyIntent(*world_, player_, intent);
        }
    }

    world_->step(stepSeconds);
    if (characters_ != nullptr) {
        // Bodies settle where the world allows, once per fixed step.
        characters_->stepLocomotion(static_cast<float>(stepSeconds));
        characters_->tickEffects(static_cast<float>(stepSeconds));
    }

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
