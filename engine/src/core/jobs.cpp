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

bool JobSystem::enqueue(Lane laneId, const Job& job) {
    LaneState& lane = lanes_[static_cast<size_t>(laneId)];
    {
        std::lock_guard<std::mutex> lock(lane.mutex);
        if (lane.count == kLaneCapacity) return false;  // refuse, never grow
        lane.jobs[lane.head] = job;
        lane.head = (lane.head + 1) % kLaneCapacity;
        ++lane.count;
    }
    lane.wake.notify_one();
    return true;
}

void JobSystem::drain(Lane laneId) {
    LaneState& lane = lanes_[static_cast<size_t>(laneId)];
    std::unique_lock<std::mutex> lock(lane.mutex);
    lane.idle.wait(lock, [&lane] { return lane.count == 0 && !lane.busy; });
}

void JobSystem::drainAll() {
    for (size_t i = 0; i < static_cast<size_t>(Lane::Count); ++i) {
        drain(static_cast<Lane>(i));
    }
}

size_t JobSystem::pendingCount(Lane laneId) const {
    const LaneState& lane = lanes_[static_cast<size_t>(laneId)];
    std::lock_guard<std::mutex> lock(lane.mutex);
    return lane.count;
}

void JobSystem::workerLoop(LaneState& lane) {
    while (true) {
        Job job;
        {
            std::unique_lock<std::mutex> lock(lane.mutex);
            lane.wake.wait(lock, [this, &lane] { return shuttingDown_.load() || lane.count > 0; });
            if (shuttingDown_.load() && lane.count == 0) return;
            job = lane.jobs[lane.tail];
            lane.tail = (lane.tail + 1) % kLaneCapacity;
            --lane.count;
            lane.busy = true;
        }
        job.fn(job.payload);
        {
            std::lock_guard<std::mutex> lock(lane.mutex);
            lane.busy = false;
        }
        lane.idle.notify_all();
    }
}

}  // namespace mge
