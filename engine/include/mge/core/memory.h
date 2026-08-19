#pragma once

// Memory system (task 1.3) — the P1 enforcement point.
// Every subsystem allocates against a registered budget; budgets report live
// usage and refuse to exceed their cap. Hot paths use arenas and pools so
// steady-state gameplay approaches zero allocations.

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <vector>

namespace mge {

using BudgetId = uint32_t;
constexpr BudgetId kInvalidBudget = UINT32_MAX;

struct BudgetStats {
    const char* name = "";
    size_t capBytes = 0;
    size_t usedBytes = 0;
    size_t peakBytes = 0;
};

// Registry of per-system memory budgets. Charging past the cap fails —
// the caller must degrade (evict, defer, drop LOD), never grow the cap silently.
class BudgetRegistry {
public:
    BudgetId registerBudget(const char* name, size_t capBytes);

    // Returns false (and charges nothing) if the charge would exceed the cap.
    bool charge(BudgetId id, size_t bytes);
    void release(BudgetId id, size_t bytes);

    BudgetStats stats(BudgetId id) const;
    size_t budgetCount() const;
    size_t totalUsedBytes() const;
    size_t totalCapBytes() const;

    // Writes a human-readable dashboard of all budgets into `out`.
    void report(std::string& out) const;

private:
    struct Entry {
        std::string name;
        size_t cap = 0;
        std::atomic<size_t> used{0};
        std::atomic<size_t> peak{0};
    };
    // Entries are append-only and never move (unique_ptr), so charge/release
    // are lock-free after registration.
    std::vector<std::unique_ptr<Entry>> entries_;
    mutable std::mutex registerMutex_;
};

// Linear (bump) allocator over one budget-charged block. Reset per frame:
// this is what makes per-frame work allocation-free (P1).
class Arena {
public:
    Arena(BudgetRegistry& registry, BudgetId budget, size_t capacityBytes);
    ~Arena();

    Arena(const Arena&) = delete;
    Arena& operator=(const Arena&) = delete;

    // Returns nullptr when the arena is exhausted. Alignment must be a power of two.
    void* alloc(size_t sizeBytes, size_t alignment = 16);

    template <typename T>
    T* allocArray(size_t count) {
        return static_cast<T*>(alloc(sizeof(T) * count, alignof(T)));
    }

    void reset();
    size_t usedBytes() const { return offset_; }
    size_t capacityBytes() const { return capacity_; }
    bool valid() const { return block_ != nullptr; }

private:
    BudgetRegistry& registry_;
    BudgetId budget_ = kInvalidBudget;
    uint8_t* block_ = nullptr;
    size_t capacity_ = 0;
    size_t offset_ = 0;
};

// Fixed-capacity object pool with an intrusive free list. Acquire/release are
// O(1) and allocation-free after construction.
template <typename T>
class Pool {
public:
    Pool(BudgetRegistry& registry, BudgetId budget, size_t capacity)
        : registry_(registry), budget_(budget), capacity_(capacity) {
        const size_t bytes = capacity * sizeof(Slot);
        if (registry_.charge(budget_, bytes)) {
            slots_ = static_cast<Slot*>(::operator new(bytes));
            chargedBytes_ = bytes;
            for (size_t i = 0; i < capacity_; ++i) {
                slots_[i].nextFree = (i + 1 < capacity_) ? &slots_[i + 1] : nullptr;
            }
            freeHead_ = &slots_[0];
        }
    }

    ~Pool() {
        if (slots_ != nullptr) {
            ::operator delete(slots_);
            registry_.release(budget_, chargedBytes_);
        }
    }

    Pool(const Pool&) = delete;
    Pool& operator=(const Pool&) = delete;

    // Returns nullptr when the pool is exhausted (budget says no — degrade, don't grow).
    template <typename... Args>
    T* acquire(Args&&... args) {
        if (freeHead_ == nullptr) return nullptr;
        Slot* slot = freeHead_;
        freeHead_ = slot->nextFree;
        ++liveCount_;
        return new (slot->storage) T(static_cast<Args&&>(args)...);
    }

    void release(T* object) {
        if (object == nullptr) return;
        object->~T();
        Slot* slot = reinterpret_cast<Slot*>(object);
        slot->nextFree = freeHead_;
        freeHead_ = slot;
        --liveCount_;
    }

    size_t liveCount() const { return liveCount_; }
    size_t capacity() const { return capacity_; }
    bool valid() const { return slots_ != nullptr; }

private:
    union Slot {
        alignas(T) unsigned char storage[sizeof(T)];
        Slot* nextFree;
    };

    BudgetRegistry& registry_;
    BudgetId budget_ = kInvalidBudget;
    Slot* slots_ = nullptr;
    Slot* freeHead_ = nullptr;
    size_t capacity_ = 0;
    size_t liveCount_ = 0;
    size_t chargedBytes_ = 0;
};

// FIFO ring allocator over one budget-charged block, for streaming staging
// buffers: producers allocate at the head, and whole runs of allocations are
// retired in order once consumed (e.g. after an upload completes). Markers
// name a point in the stream; retire(marker) frees everything older.
class RingAllocator {
public:
    using Marker = uint64_t;

    RingAllocator(BudgetRegistry& registry, BudgetId budget, size_t capacityBytes);
    ~RingAllocator();

    RingAllocator(const RingAllocator&) = delete;
    RingAllocator& operator=(const RingAllocator&) = delete;

    // Returns nullptr when the ring can't fit the request until older data is
    // retired (refuse, never grow — P1). Alignment must be a power of two.
    void* alloc(size_t sizeBytes, size_t alignment = 16);

    // Names the current head. Everything allocated before this call is freed
    // by retire(thatMarker).
    Marker marker() const { return head_; }

    // Frees all allocations older than `upTo` (a value from marker()).
    void retire(Marker upTo);

    size_t usedBytes() const { return static_cast<size_t>(head_ - tail_); }
    size_t capacityBytes() const { return capacity_; }
    bool valid() const { return block_ != nullptr; }

private:
    BudgetRegistry& registry_;
    BudgetId budget_ = kInvalidBudget;
    uint8_t* block_ = nullptr;
    size_t capacity_ = 0;
    // Monotonic stream offsets; physical position is offset % capacity.
    uint64_t head_ = 0;
    uint64_t tail_ = 0;
};

}  // namespace mge
