#pragma once

// Input capture (task 1.8): the platform thread pushes timestamped touch
// events into a fixed-capacity single-producer/single-consumer ring; the
// simulation drains it at the top of each tick. Allocation-free on both sides
// (P1). Overflow drops the newest event and counts it — input loss is
// detectable, memory use is not negotiable.

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace mge {

enum class TouchAction : uint8_t {
    Down = 0,
    Up,
    Move,
    Cancel,
};

struct TouchEvent {
    int32_t pointerId = 0;
    TouchAction action = TouchAction::Down;
    float x = 0.0f;
    float y = 0.0f;
    int64_t timestampNs = 0;
};

// SPSC ring: exactly one producer thread (the platform input thread) and one
// consumer thread (the simulation). Capacity must be a power of two.
class InputQueue {
public:
    static constexpr size_t kCapacity = 256;

    // Producer side. Returns false (and counts a drop) when the queue is full.
    bool push(const TouchEvent& event) {
        const uint32_t head = head_.load(std::memory_order_relaxed);
        const uint32_t tail = tail_.load(std::memory_order_acquire);
        if (head - tail >= kCapacity) {
            droppedCount_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        events_[head & (kCapacity - 1)] = event;
        head_.store(head + 1, std::memory_order_release);
        return true;
    }

    // Consumer side. Returns false when the queue is empty.
    bool pop(TouchEvent& out) {
        const uint32_t tail = tail_.load(std::memory_order_relaxed);
        const uint32_t head = head_.load(std::memory_order_acquire);
        if (tail == head) return false;
        out = events_[tail & (kCapacity - 1)];
        tail_.store(tail + 1, std::memory_order_release);
        return true;
    }

    size_t pendingCount() const {
        return head_.load(std::memory_order_acquire) - tail_.load(std::memory_order_acquire);
    }

    uint64_t droppedCount() const { return droppedCount_.load(std::memory_order_relaxed); }

private:
    TouchEvent events_[kCapacity];
    std::atomic<uint32_t> head_{0};
    std::atomic<uint32_t> tail_{0};
    std::atomic<uint64_t> droppedCount_{0};
};

}  // namespace mge
