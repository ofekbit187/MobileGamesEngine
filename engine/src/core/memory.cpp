#include "mge/core/memory.h"

#include <cstdio>
#include <new>

#include "mge/core/log.h"

namespace mge {

BudgetId BudgetRegistry::registerBudget(const char* name, size_t capBytes) {
    std::lock_guard<std::mutex> lock(registerMutex_);
    auto entry = std::make_unique<Entry>();
    entry->name = name;
    entry->cap = capBytes;
    entries_.push_back(std::move(entry));
    return static_cast<BudgetId>(entries_.size() - 1);
}

bool BudgetRegistry::charge(BudgetId id, size_t bytes) {
    if (id >= entries_.size()) return false;
    Entry& e = *entries_[id];
    size_t current = e.used.load(std::memory_order_relaxed);
    while (true) {
        const size_t next = current + bytes;
        if (next > e.cap) {
            MGE_LOGW("memory", "budget '%s' refused %zu bytes (used %zu / cap %zu)",
                     e.name.c_str(), bytes, current, e.cap);
            return false;
        }
        if (e.used.compare_exchange_weak(current, next, std::memory_order_relaxed)) {
            size_t peak = e.peak.load(std::memory_order_relaxed);
            while (next > peak &&
                   !e.peak.compare_exchange_weak(peak, next, std::memory_order_relaxed)) {
            }
            return true;
        }
    }
}

void BudgetRegistry::release(BudgetId id, size_t bytes) {
    if (id >= entries_.size()) return;
    entries_[id]->used.fetch_sub(bytes, std::memory_order_relaxed);
}

BudgetStats BudgetRegistry::stats(BudgetId id) const {
    BudgetStats s;
    if (id >= entries_.size()) return s;
    const Entry& e = *entries_[id];
    s.name = e.name.c_str();
    s.capBytes = e.cap;
    s.usedBytes = e.used.load(std::memory_order_relaxed);
    s.peakBytes = e.peak.load(std::memory_order_relaxed);
    return s;
}

size_t BudgetRegistry::budgetCount() const { return entries_.size(); }

size_t BudgetRegistry::totalUsedBytes() const {
    size_t total = 0;
    for (const auto& e : entries_) total += e->used.load(std::memory_order_relaxed);
    return total;
}

size_t BudgetRegistry::totalCapBytes() const {
    size_t total = 0;
    for (const auto& e : entries_) total += e->cap;
    return total;
}

void BudgetRegistry::report(std::string& out) const {
    char line[160];
    snprintf(line, sizeof(line), "%-16s %12s %12s %12s\n", "budget", "used", "peak", "cap");
    out += line;
    for (const auto& e : entries_) {
        snprintf(line, sizeof(line), "%-16s %12zu %12zu %12zu\n", e->name.c_str(),
                 e->used.load(std::memory_order_relaxed), e->peak.load(std::memory_order_relaxed),
                 e->cap);
        out += line;
    }
    snprintf(line, sizeof(line), "%-16s %12zu %12s %12zu\n", "TOTAL", totalUsedBytes(), "",
             totalCapBytes());
    out += line;
}

Arena::Arena(BudgetRegistry& registry, BudgetId budget, size_t capacityBytes)
    : registry_(registry), budget_(budget) {
    if (registry_.charge(budget_, capacityBytes)) {
        block_ = static_cast<uint8_t*>(::operator new(capacityBytes));
        capacity_ = capacityBytes;
    } else {
        MGE_LOGE("memory", "arena creation refused: %zu bytes over budget", capacityBytes);
    }
}

Arena::~Arena() {
    if (block_ != nullptr) {
        ::operator delete(block_);
        registry_.release(budget_, capacity_);
    }
}

void* Arena::alloc(size_t sizeBytes, size_t alignment) {
    // Align the actual address — the block base is only guaranteed to be
    // aligned to the default new alignment, so aligning the offset alone
    // would break for stricter alignments.
    const uintptr_t base = reinterpret_cast<uintptr_t>(block_);
    const uintptr_t current = base + offset_;
    const uintptr_t alignedAddr = (current + (alignment - 1)) & ~(uintptr_t(alignment) - 1);
    const size_t alignedOffset = static_cast<size_t>(alignedAddr - base);
    if (alignedOffset + sizeBytes > capacity_) return nullptr;
    offset_ = alignedOffset + sizeBytes;
    return block_ + alignedOffset;
}

void Arena::reset() { offset_ = 0; }

RingAllocator::RingAllocator(BudgetRegistry& registry, BudgetId budget, size_t capacityBytes)
    : registry_(registry), budget_(budget) {
    if (registry_.charge(budget_, capacityBytes)) {
        block_ = static_cast<uint8_t*>(::operator new(capacityBytes));
        capacity_ = capacityBytes;
    } else {
        MGE_LOGE("memory", "ring allocator creation refused: %zu bytes over budget",
                 capacityBytes);
    }
}

RingAllocator::~RingAllocator() {
    if (block_ != nullptr) {
        ::operator delete(block_);
        registry_.release(budget_, capacity_);
    }
}

void* RingAllocator::alloc(size_t sizeBytes, size_t alignment) {
    if (block_ == nullptr || sizeBytes == 0 || sizeBytes > capacity_) return nullptr;
    const uintptr_t base = reinterpret_cast<uintptr_t>(block_);

    const auto alignFrom = [&](uint64_t stream) {
        const size_t phys = static_cast<size_t>(stream % capacity_);
        const uintptr_t addr = base + phys;
        const uintptr_t alignedAddr = (addr + (alignment - 1)) & ~(uintptr_t(alignment) - 1);
        return stream + (alignedAddr - addr);
    };

    uint64_t start = alignFrom(head_);
    if (static_cast<size_t>(start % capacity_) + sizeBytes > capacity_) {
        // Doesn't fit contiguously before the wrap point: pad to the start of
        // the buffer and re-align there.
        start = alignFrom(head_ + (capacity_ - static_cast<size_t>(head_ % capacity_)));
        if (static_cast<size_t>(start % capacity_) + sizeBytes > capacity_) return nullptr;
    }
    const uint64_t newHead = start + sizeBytes;
    if (newHead - tail_ > capacity_) return nullptr;  // would overwrite live data
    head_ = newHead;
    return block_ + static_cast<size_t>(start % capacity_);
}

void RingAllocator::retire(Marker upTo) {
    if (upTo > tail_) tail_ = (upTo < head_) ? upTo : head_;
}

}  // namespace mge
