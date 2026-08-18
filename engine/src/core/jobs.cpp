#include "mge/core/jobs.h"

namespace mge {

JobSystem::JobSystem() {
    for (auto& lane : lanes_) {
        lane.worker = std::thread([this, &lane] { workerLoop(lane); });
    }
}

JobSystem::~JobSystem() {
    shuttingDown_.store(true);
    for (auto& lane : lanes_) {
        // Taking the lane mutex guarantees the worker is either before its
        // predicate check (and will see the flag) or inside wait (and will be
        // woken by the notify below) — no lost wakeup.
        { std::lock_guard<std::mutex> lock(lane.mutex); }
        lane.wake.notify_all();
        if (lane.worker.joinable()) lane.worker.join();
    }
}

void JobSystem::submit(Lane laneId, std::function<void()> job) {
    LaneState& lane = lanes_[static_cast<size_t>(laneId)];
    {
        std::lock_guard<std::mutex> lock(lane.mutex);
        lane.queue.push_back(std::move(job));
    }
    lane.wake.notify_one();
}

void JobSystem::drain(Lane laneId) {
    LaneState& lane = lanes_[static_cast<size_t>(laneId)];
    std::unique_lock<std::mutex> lock(lane.mutex);
    lane.idle.wait(lock, [&lane] { return lane.queue.empty() && !lane.busy; });
}

void JobSystem::drainAll() {
    for (size_t i = 0; i < static_cast<size_t>(Lane::Count); ++i) {
        drain(static_cast<Lane>(i));
    }
}

size_t JobSystem::pendingCount(Lane laneId) const {
    const LaneState& lane = lanes_[static_cast<size_t>(laneId)];
    std::lock_guard<std::mutex> lock(lane.mutex);
    return lane.queue.size();
}

void JobSystem::workerLoop(LaneState& lane) {
    while (true) {
        std::function<void()> job;
        {
            std::unique_lock<std::mutex> lock(lane.mutex);
            lane.wake.wait(lock, [this, &lane] { return shuttingDown_ || !lane.queue.empty(); });
            if (shuttingDown_ && lane.queue.empty()) return;
            job = std::move(lane.queue.front());
            lane.queue.pop_front();
            lane.busy = true;
        }
        job();
        {
            std::lock_guard<std::mutex> lock(lane.mutex);
            lane.busy = false;
        }
        lane.idle.notify_all();
    }
}

}  // namespace mge
