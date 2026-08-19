#include <cstring>

#include "mge/core/memory.h"
#include "test_framework.h"

using namespace mge;

MGE_TEST(ring_alloc_and_retire) {
    BudgetRegistry registry;
    const BudgetId id = registry.registerBudget("ring", 1024);
    RingAllocator ring(registry, id, 1024);
    MGE_CHECK(ring.valid());

    void* a = ring.alloc(400);
    MGE_CHECK(a != nullptr);
    const RingAllocator::Marker afterA = ring.marker();
    void* b = ring.alloc(400);
    MGE_CHECK(b != nullptr);

    // Full-ish: a third 400-byte block can't fit until something retires.
    MGE_CHECK(ring.alloc(400) == nullptr);

    ring.retire(afterA);  // frees a's run
    void* c = ring.alloc(400);
    MGE_CHECK(c != nullptr);
}

MGE_TEST(ring_wraps_and_reuses) {
    BudgetRegistry registry;
    const BudgetId id = registry.registerBudget("ring", 1024);
    RingAllocator ring(registry, id, 1024);

    // Many rounds of alloc/consume/retire must cycle forever in place.
    for (int round = 0; round < 100; ++round) {
        void* p = ring.alloc(300);
        MGE_CHECK(p != nullptr);
        memset(p, round & 0xFF, 300);  // memory is really writable
        ring.retire(ring.marker());
    }
    MGE_CHECK(ring.usedBytes() == 0);
}

MGE_TEST(ring_contiguity_across_wrap) {
    BudgetRegistry registry;
    const BudgetId id = registry.registerBudget("ring", 1024);
    RingAllocator ring(registry, id, 1024);

    void* a = ring.alloc(700);
    MGE_CHECK(a != nullptr);
    ring.retire(ring.marker());

    // 700 remaining before the wrap point is only ~324 contiguous; a 500-byte
    // block must land at the buffer start, fully contiguous.
    void* b = ring.alloc(500);
    MGE_CHECK(b != nullptr);
    memset(b, 0xAB, 500);
    MGE_CHECK(b == a);  // physical start of the buffer both times
}

MGE_TEST(ring_alignment_honored) {
    BudgetRegistry registry;
    const BudgetId id = registry.registerBudget("ring", 4096);
    RingAllocator ring(registry, id, 4096);

    ring.alloc(7);  // misalign the head
    void* p = ring.alloc(16, 64);
    MGE_CHECK(p != nullptr);
    MGE_CHECK(reinterpret_cast<uintptr_t>(p) % 64 == 0);
}

MGE_TEST(ring_refused_over_budget) {
    BudgetRegistry registry;
    const BudgetId id = registry.registerBudget("tiny", 64);
    RingAllocator ring(registry, id, 1024);
    MGE_CHECK(!ring.valid());
    MGE_CHECK(ring.alloc(16) == nullptr);
}
