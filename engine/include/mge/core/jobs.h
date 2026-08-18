#pragma once

// Job system (task 1.4). Dedicated lanes keep streaming I/O and asset decode
// off the frame-critical path. v0 uses one worker thread per lane and
// std::function jobs; the fixed-size job struct (fully allocation-free
// submission, P1) replaces std::function when the streaming system lands.

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace mge {

enum class Lane : uint8_t {
    Simulation = 0,
    StreamingIO,
    Decode,
    Render,
    Count,
};

class JobSystem {
public:
    JobSystem();
    ~JobSystem();

    JobSystem(const JobSystem&) = delete;
    JobSystem& operator=(const JobSystem&) = delete;

    void submit(Lane lane, std::function<void()> job);

    // Blocks until the lane's queue is empty and its worker is idle.
    void drain(Lane lane);
    void drainAll();

    size_t pendingCount(Lane lane) const;

private:
    struct LaneState {
        std::deque<std::function<void()>> queue;
        mutable std::mutex mutex;
        std::condition_variable wake;
        std::condition_variable idle;
        bool busy = false;
        std::thread worker;
    };

    void workerLoop(LaneState& lane);

    LaneState lanes_[static_cast<size_t>(Lane::Count)];
    std::atomic<bool> shuttingDown_{false};
};

}  // namespace mge
