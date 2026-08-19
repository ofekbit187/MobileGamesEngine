#include <atomic>

#include "mge/core/jobs.h"
#include "test_framework.h"

using namespace mge;

MGE_TEST(jobs_run_and_drain) {
    JobSystem jobs;
    std::atomic<int> counter{0};
    for (int i = 0; i < 100; ++i) {
        MGE_CHECK(jobs.submit(Lane::StreamingIO, [&counter] { counter.fetch_add(1); }));
    }
    jobs.drain(Lane::StreamingIO);
    MGE_CHECK(counter.load() == 100);
    MGE_CHECK(jobs.pendingCount(Lane::StreamingIO) == 0);
}

MGE_TEST(jobs_full_lane_refuses) {
    JobSystem jobs;
    std::atomic<bool> started{false};
    std::atomic<bool> release{false};
    std::atomic<int> ran{0};

    // Block the lane so submissions pile up; wait until the blocker is
    // actually running so the queue drains nothing while we fill it.
    jobs.submit(Lane::Decode, [&started, &release] {
        started.store(true);
        while (!release.load()) {
        }
    });
    while (!started.load()) {
    }
    // Fill the queue to capacity; every submission must be accepted...
    size_t accepted = 0;
    while (jobs.submit(Lane::Decode, [&ran] { ran.fetch_add(1); })) ++accepted;
    // ...and once full, submission refuses without growing (P1).
    MGE_CHECK(accepted == JobSystem::kLaneCapacity);
    MGE_CHECK(!jobs.submit(Lane::Decode, [&ran] { ran.fetch_add(1); }));

    release.store(true);
    jobs.drain(Lane::Decode);
    MGE_CHECK(ran.load() == static_cast<int>(accepted));
}

MGE_TEST(lanes_are_independent) {
    JobSystem jobs;
    std::atomic<bool> ioBlocked{true};
    std::atomic<int> renderDone{0};

    // Park the I/O lane...
    jobs.submit(Lane::StreamingIO, [&ioBlocked] {
        while (ioBlocked.load()) {
        }
    });
    // ...render lane must still make progress.
    jobs.submit(Lane::Render, [&renderDone] { renderDone.store(1); });
    jobs.drain(Lane::Render);
    MGE_CHECK(renderDone.load() == 1);

    ioBlocked.store(false);
    jobs.drain(Lane::StreamingIO);
}

MGE_TEST(ordered_within_lane) {
    JobSystem jobs;
    std::atomic<int> sequence{0};
    bool ordered = true;
    for (int i = 0; i < 50; ++i) {
        jobs.submit(Lane::Decode, [i, &sequence, &ordered] {
            if (sequence.fetch_add(1) != i) ordered = false;
        });
    }
    jobs.drain(Lane::Decode);
    MGE_CHECK(ordered);
}
