#pragma once

// Fixed-step accumulator (task 3.1's timing core, needed by the loop from day
// one): simulation advances in fixed steps, rendering interpolates between the
// last two states using alpha().

namespace mge {

class FrameClock {
public:
    explicit FrameClock(double fixedStepSeconds = 1.0 / 60.0)
        : fixedStep_(fixedStepSeconds) {}

    // Feeds real elapsed time; returns how many fixed steps to simulate now.
    // Steps are capped so a hitch (or debugger pause) can't spiral the loop.
    int advance(double dtSeconds, int maxSteps = 8) {
        if (dtSeconds < 0.0) dtSeconds = 0.0;
        accumulator_ += dtSeconds;
        int steps = 0;
        while (accumulator_ >= fixedStep_ && steps < maxSteps) {
            accumulator_ -= fixedStep_;
            ++steps;
        }
        if (steps == maxSteps && accumulator_ >= fixedStep_) {
            // Dropping the backlog keeps the game real-time after a hitch.
            accumulator_ = 0.0;
        }
        return steps;
    }

    // Interpolation factor in [0,1): how far between fixed steps the frame is.
    double alpha() const { return accumulator_ / fixedStep_; }

    double fixedStepSeconds() const { return fixedStep_; }

private:
    double fixedStep_;
    double accumulator_ = 0.0;
};

}  // namespace mge
