#include "mge/core/frame_clock.h"
#include "test_framework.h"

using namespace mge;

MGE_TEST(fixed_step_accumulation) {
    FrameClock clock(1.0 / 60.0);

    // Half a step: no simulation yet, alpha reflects partial progress.
    MGE_CHECK(clock.advance(1.0 / 120.0) == 0);
    MGE_CHECK_NEAR(clock.alpha(), 0.5, 1e-9);

    // Second half arrives: exactly one step, alpha near zero.
    MGE_CHECK(clock.advance(1.0 / 120.0) == 1);
    MGE_CHECK_NEAR(clock.alpha(), 0.0, 1e-9);

    // A 3-step chunk of time yields 3 steps.
    MGE_CHECK(clock.advance(3.0 / 60.0) == 3);
}

MGE_TEST(hitch_is_capped) {
    FrameClock clock(1.0 / 60.0);
    // A 2-second hitch must not spiral: capped at maxSteps and backlog dropped.
    const int steps = clock.advance(2.0, 8);
    MGE_CHECK(steps == 8);
    MGE_CHECK(clock.advance(0.0) == 0);
    MGE_CHECK(clock.alpha() < 1.0);
}
