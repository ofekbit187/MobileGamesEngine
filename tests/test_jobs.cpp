#include <atomic>

#include "mge/core/jobs.h"
#include "test_framework.h"

using namespace mge;

MGE_TEST(jobs_run_and_drain) {
    JobSystem jobs;
    std::atomic<int> counter{0};
    for (int i = 0; i < 100; ++i) {
        jobs.submit(Lane::StreamingIO, [&counter] { counter.fetch_add(1); });
    }
    jobs.drain(Lane::StreamingIO);
    MGE_CHECK(counter.load() == 100);
    MGE_CHECK(jobs.pendingCount(Lane::StreamingIO) == 0);
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
