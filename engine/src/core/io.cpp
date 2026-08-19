#include "mge/core/io.h"

#include <cerrno>
#include <fcntl.h>
#include <unistd.h>

#include "mge/core/log.h"

namespace mge {

AsyncIO::AsyncIO() {
    worker_ = std::thread([this] { workerLoop(); });
}

AsyncIO::~AsyncIO() {
    shuttingDown_.store(true);
    {
        std::lock_guard<std::mutex> lock(mutex_);
    }
    wake_.notify_all();
    if (worker_.joinable()) worker_.join();
}

bool AsyncIO::submit(IoRequest* request) {
    if (request == nullptr || request->path == nullptr || request->dest == nullptr ||
        request->size == 0) {
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        PriorityRing& ring = rings_[static_cast<size_t>(request->priority)];
        if (ring.count == kQueueCapacity) return false;  // refuse, never grow
        request->bytesRead = 0;
        request->errorCode = 0;
        request->status_.store(IoStatus::Pending, std::memory_order_release);
        ring.requests[ring.head] = request;
        ring.head = (ring.head + 1) % kQueueCapacity;
        ++ring.count;
        ++totalPending_;
    }
    wake_.notify_one();
    return true;
}

void AsyncIO::pause() {
    std::lock_guard<std::mutex> lock(mutex_);
    paused_ = true;
}

void AsyncIO::resume() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        paused_ = false;
    }
    wake_.notify_one();
}

void AsyncIO::drain() {
    std::unique_lock<std::mutex> lock(mutex_);
    idle_.wait(lock, [this] { return totalPending_ == 0 && !inFlight_; });
}

size_t AsyncIO::pendingCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return totalPending_ + (inFlight_ ? 1 : 0);
}

IoRequest* AsyncIO::popHighestPriorityLocked() {
    for (auto& ring : rings_) {  // rings are ordered Critical..Low
        if (ring.count > 0) {
            IoRequest* request = ring.requests[ring.tail];
            ring.tail = (ring.tail + 1) % kQueueCapacity;
            --ring.count;
            --totalPending_;
            return request;
        }
    }
    return nullptr;
}

void AsyncIO::workerLoop() {
    while (true) {
        IoRequest* request = nullptr;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait(lock, [this] {
                return shuttingDown_.load() || (!paused_ && totalPending_ > 0);
            });
            if (shuttingDown_.load()) return;
            request = popHighestPriorityLocked();
            inFlight_ = true;
        }

        // Perform the read outside the lock: plain syscalls, no heap.
        const int fd = ::open(request->path, O_RDONLY);
        if (fd < 0) {
            request->errorCode = errno;
            request->status_.store(IoStatus::Failed, std::memory_order_release);
        } else {
            size_t total = 0;
            bool failed = false;
            while (total < request->size) {
                const ssize_t n =
                    ::pread(fd, static_cast<uint8_t*>(request->dest) + total,
                            request->size - total, static_cast<off_t>(request->offset + total));
                if (n < 0) {
                    if (errno == EINTR) continue;
                    request->errorCode = errno;
                    failed = true;
                    break;
                }
                if (n == 0) break;  // EOF: short read is a valid result
                total += static_cast<size_t>(n);
            }
            ::close(fd);
            request->bytesRead = total;
            request->status_.store(failed ? IoStatus::Failed : IoStatus::Done,
                                   std::memory_order_release);
        }

        if (request->onComplete != nullptr) request->onComplete(request, request->user);

        {
            std::lock_guard<std::mutex> lock(mutex_);
            inFlight_ = false;
        }
        idle_.notify_all();
    }
}

}  // namespace mge
