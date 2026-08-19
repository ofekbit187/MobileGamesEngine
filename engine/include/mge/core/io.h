#pragma once

// Async priority I/O (task 1.5) — the streaming system's main client. Reads
// run on a dedicated I/O thread, strictly ordered by priority (what the
// player is about to see loads first — P2), FIFO within a priority.
// Submission is allocation-free: requests are caller-owned and queued by
// pointer into fixed-capacity rings; a full ring refuses (P1).
//
// Backend: POSIX pread over real paths. Android app-private files and
// extracted asset-pack files are plain paths, so this backend serves both;
// reading from inside APK/AAsset containers is a later extension.

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <thread>

namespace mge {

enum class IoPriority : uint8_t {
    Critical = 0,  // blocking the player right now (e.g. chunk under their feet)
    High,          // about to be visible
    Normal,        // prefetch
    Low,           // opportunistic
    Count,
};

enum class IoStatus : uint8_t {
    Idle = 0,   // not submitted
    Pending,    // queued or in flight
    Done,       // bytesRead filled, data in dest
    Failed,     // open/read error; see errorCode
};

// Caller-owned request block. Must stay alive and unmodified from submit()
// until status() reports Done or Failed. Reusable after completion.
struct IoRequest {
    // -- inputs --
    const char* path = nullptr;  // must outlive the request
    uint64_t offset = 0;
    void* dest = nullptr;  // caller-owned buffer of at least `size` bytes
    size_t size = 0;
    IoPriority priority = IoPriority::Normal;
    // Optional completion hook, called on the I/O thread. Keep it tiny and
    // thread-safe; heavy work belongs on the Decode lane.
    void (*onComplete)(IoRequest*, void* user) = nullptr;
    void* user = nullptr;

    // -- outputs --
    size_t bytesRead = 0;
    int errorCode = 0;

    IoStatus status() const { return status_.load(std::memory_order_acquire); }

private:
    friend class AsyncIO;
    std::atomic<IoStatus> status_{IoStatus::Idle};
};

class AsyncIO {
public:
    static constexpr size_t kQueueCapacity = 256;  // per priority level

    AsyncIO();
    ~AsyncIO();

    AsyncIO(const AsyncIO&) = delete;
    AsyncIO& operator=(const AsyncIO&) = delete;

    // Queues a read. Returns false when the priority's ring is full or the
    // request is malformed — nothing is queued, status stays Idle.
    bool submit(IoRequest* request);

    // Lifecycle: a paused AsyncIO accepts submissions but processes nothing
    // (Android background states). resume() picks work back up.
    void pause();
    void resume();

    // Blocks until every queued request has completed.
    void drain();

    size_t pendingCount() const;

private:
    struct PriorityRing {
        IoRequest* requests[kQueueCapacity];
        size_t head = 0;
        size_t tail = 0;
        size_t count = 0;
    };

    IoRequest* popHighestPriorityLocked();
    void workerLoop();

    PriorityRing rings_[static_cast<size_t>(IoPriority::Count)];
    size_t totalPending_ = 0;
    bool inFlight_ = false;
    bool paused_ = false;
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::condition_variable idle_;
    std::thread worker_;
    std::atomic<bool> shuttingDown_{false};
};

}  // namespace mge
