#include "mge/core/memory.h"
#include "test_framework.h"

using namespace mge;

MGE_TEST(budget_charge_and_refuse) {
    BudgetRegistry registry;
    const BudgetId id = registry.registerBudget("test", 1000);

    MGE_CHECK(registry.charge(id, 600));
    MGE_CHECK(registry.charge(id, 400));
    // Over cap: must refuse and charge nothing.
    MGE_CHECK(!registry.charge(id, 1));
    MGE_CHECK(registry.stats(id).usedBytes == 1000);

    registry.release(id, 500);
    MGE_CHECK(registry.stats(id).usedBytes == 500);
    MGE_CHECK(registry.stats(id).peakBytes == 1000);
    MGE_CHECK(registry.charge(id, 500));
}

MGE_TEST(arena_alloc_reset) {
    BudgetRegistry registry;
    const BudgetId id = registry.registerBudget("arena", 4096);
    Arena arena(registry, id, 4096);
    MGE_CHECK(arena.valid());
    MGE_CHECK(registry.stats(id).usedBytes == 4096);  // block charged up front

    void* a = arena.alloc(100);
    void* b = arena.alloc(100);
    MGE_CHECK(a != nullptr && b != nullptr && a != b);
    MGE_CHECK(arena.usedBytes() >= 200);

    // Alignment honored.
    void* c = arena.alloc(1, 64);
    MGE_CHECK(reinterpret_cast<uintptr_t>(c) % 64 == 0);

    // Exhaustion returns nullptr, never grows.
    MGE_CHECK(arena.alloc(8192) == nullptr);

    arena.reset();
    MGE_CHECK(arena.usedBytes() == 0);
    MGE_CHECK(arena.alloc(4000) != nullptr);
}

MGE_TEST(arena_refused_when_over_budget) {
    BudgetRegistry registry;
    const BudgetId id = registry.registerBudget("small", 100);
    Arena arena(registry, id, 4096);  // bigger than budget
    MGE_CHECK(!arena.valid());
    MGE_CHECK(arena.alloc(16) == nullptr);
}

namespace {
struct Probe {
    static int liveCount;
    int value;
    explicit Probe(int v) : value(v) { ++liveCount; }
    ~Probe() { --liveCount; }
};
int Probe::liveCount = 0;
}  // namespace

MGE_TEST(pool_acquire_release) {
    BudgetRegistry registry;
    const BudgetId id = registry.registerBudget("pool", 1 << 16);
    {
        Pool<Probe> pool(registry, id, 4);
        MGE_CHECK(pool.valid());

        Probe* p0 = pool.acquire(10);
        Probe* p1 = pool.acquire(11);
        MGE_CHECK(p0 != nullptr && p1 != nullptr);
        MGE_CHECK(p0->value == 10 && p1->value == 11);
        MGE_CHECK(Probe::liveCount == 2);

        Probe* p2 = pool.acquire(12);
        Probe* p3 = pool.acquire(13);
        MGE_CHECK(p2 != nullptr && p3 != nullptr);
        // Exhausted: refuse, don't grow.
        MGE_CHECK(pool.acquire(14) == nullptr);

        pool.release(p1);
        MGE_CHECK(Probe::liveCount == 3);
        Probe* p4 = pool.acquire(15);  // reuses the freed slot
        MGE_CHECK(p4 != nullptr && p4->value == 15);

        pool.release(p0);
        pool.release(p2);
        pool.release(p3);
        pool.release(p4);
        MGE_CHECK(Probe::liveCount == 0);
    }
    // Pool destruction returns its budget charge.
    MGE_CHECK(registry.stats(id).usedBytes == 0);
}
