#include "mge/framework/engine.h"
#include "test_framework.h"

using namespace mge;

MGE_TEST(engine_init_tick_shutdown) {
    Engine engine;
    EngineConfig config;
    MGE_CHECK(engine.init(config));
    MGE_CHECK(engine.initialized());

    engine.onSurfaceCreated(1080, 2400);
    engine.onResume();

    for (int i = 0; i < 120; ++i) {
        engine.tick(1.0 / 60.0);
    }
    MGE_CHECK(engine.stats().frameCount == 120);
    // One fixed step per 60Hz frame (within accumulator rounding).
    MGE_CHECK(engine.stats().simStepCount >= 119 && engine.stats().simStepCount <= 121);
    // Frame arena was used and reset every frame.
    MGE_CHECK(engine.stats().frameArenaHighWaterBytes > 0);
    MGE_CHECK(engine.frameArena().usedBytes() == 0);

    engine.shutdown();
    MGE_CHECK(!engine.initialized());
}

MGE_TEST(engine_pause_stops_ticking) {
    Engine engine;
    MGE_CHECK(engine.init(EngineConfig{}));
    engine.onSurfaceCreated(100, 100);

    engine.tick(1.0 / 60.0);
    const uint64_t framesBefore = engine.stats().frameCount;

    engine.onPause();
    engine.tick(1.0 / 60.0);
    engine.tick(1.0 / 60.0);
    MGE_CHECK(engine.stats().frameCount == framesBefore);  // paused: no work

    engine.onResume();
    engine.tick(1.0 / 60.0);
    MGE_CHECK(engine.stats().frameCount == framesBefore + 1);
}

MGE_TEST(engine_survives_surface_loss) {
    Engine engine;
    MGE_CHECK(engine.init(EngineConfig{}));
    engine.onSurfaceCreated(100, 100);
    engine.tick(1.0 / 60.0);

    // Surface gone (Android backgrounding): simulation may continue, render must not.
    engine.onSurfaceLost();
    engine.tick(1.0 / 60.0);
    engine.onSurfaceCreated(200, 200);
    engine.tick(1.0 / 60.0);
    MGE_CHECK(engine.stats().frameCount == 3);
}

MGE_TEST(engine_memory_report_lists_budgets) {
    Engine engine;
    MGE_CHECK(engine.init(EngineConfig{}));
    const std::string report = engine.memoryReport();
    MGE_CHECK(report.find("frame") != std::string::npos);
    MGE_CHECK(report.find("persistent") != std::string::npos);
    MGE_CHECK(report.find("TOTAL") != std::string::npos);
}
