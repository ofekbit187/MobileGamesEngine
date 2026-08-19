#pragma once

// Job system (task 1.4). Dedicated lanes keep streaming I/O and asset decode
// off the frame-critical path. Submission is allocation-free (P1): a job is a
// plain function pointer plus a small inline payload, queued into a
// fixed-capacity per-lane ring. A full lane refuses the job — callers defer
// or drop, queues never grow.

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <thread>
#include <type_traits>

namespace mge {

enum class Lane : uint8_t {
    Simulation = 0,
    StreamingIO,
    Decode,
    Render,
    Count,
};

// A queued unit of work: fn(payload). The payload is inline storage for a
// small trivially-copyable callable or argument block.
struct Job {
    static constexpr size_t kPayloadBytes = 48;

    void (*fn)(void*) = nullptr;
    alignas(16) unsigned char payload[kPayloadBytes];
};

class JobSystem {
public:
    static constexpr size_t kLaneCapacity = 1024;  // jobs per lane, fixed

    JobSystem();
    ~JobSystem();

    JobSystem(const JobSystem&) = delete;
    JobSystem& operator=(const JobSystem&) = delete;

    // Enqueues a callable. The callable must be trivially copyable and fit in
    // Job::kPayloadBytes (both checked at compile time) — capture references
    // and raw pointers, not owning objects. Returns false when the lane is
    // full (nothing is queued).
    template <typename F>
    bool submit(Lane lane, F&& callable) {
        using Fn = std::decay_t<F>;
        static_assert(std::is_trivially_copyable_v<Fn>,
                      "job callables must be trivially copyable (no owning captures)");
        static_assert(sizeof(Fn) <= Job::kPayloadBytes, "job callable too large for payload");
        static_assert(alignof(Fn) <= alignof(Job), "job callable over-aligned");
        Job job;
        job.fn = [](void* p) { (*reinterpret_cast<Fn*>(p))(); };
        ::memcpy(job.payload, &callable, sizeof(Fn));
        return enqueue(lane, job);
    }

    // Blocks until the lane's queue is empty and its worker is idle.
    void drain(Lane lane);
    void drainAll();

    size_t pendingCount(Lane lane) const;

private:
    struct LaneState {
        Job jobs[kLaneCapacity];
        size_t head = 0;  // next slot to write
        size_t tail = 0;  // next slot to read
        size_t count = 0;
        mutable std::mutex mutex;
        std::condition_variable wake;
        std::condition_variable idle;
        bool busy = false;
        std::thread worker;
    };

    bool enqueue(Lane lane, const Job& job);
    void workerLoop(LaneState& lane);

    LaneState lanes_[static_cast<size_t>(Lane::Count)];
    std::atomic<bool> shuttingDown_{false};
};

}  // namespace mge
