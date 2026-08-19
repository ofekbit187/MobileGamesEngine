#include <thread>

#include "mge/core/input.h"
#include "mge/framework/engine.h"
#include "test_framework.h"

using namespace mge;

MGE_TEST(input_queue_fifo) {
    InputQueue queue;
    for (int i = 0; i < 10; ++i) {
        TouchEvent e;
        e.pointerId = i;
        e.timestampNs = i * 1000;
        MGE_CHECK(queue.push(e));
    }
    MGE_CHECK(queue.pendingCount() == 10);

    TouchEvent out;
    for (int i = 0; i < 10; ++i) {
        MGE_CHECK(queue.pop(out));
        MGE_CHECK(out.pointerId == i);  // strict FIFO
    }
    MGE_CHECK(!queue.pop(out));
}

MGE_TEST(input_queue_overflow_drops_and_counts) {
    InputQueue queue;
    TouchEvent e;
    for (size_t i = 0; i < InputQueue::kCapacity; ++i) MGE_CHECK(queue.push(e));
    // Full: pushes fail, get counted, and never overwrite queued events.
    MGE_CHECK(!queue.push(e));
    MGE_CHECK(!queue.push(e));
    MGE_CHECK(queue.droppedCount() == 2);
    MGE_CHECK(queue.pendingCount() == InputQueue::kCapacity);
}

MGE_TEST(input_queue_cross_thread) {
    InputQueue queue;
    constexpr int kEvents = 10000;

    std::thread producer([&queue] {
        TouchEvent e;
        for (int i = 0; i < kEvents; ++i) {
            e.pointerId = i;
            while (!queue.push(e)) {
            }  // spin until the consumer catches up
        }
    });

    int received = 0;
    int lastId = -1;
    bool ordered = true;
    TouchEvent out;
    while (received < kEvents) {
        if (queue.pop(out)) {
            if (out.pointerId != lastId + 1) ordered = false;
            lastId = out.pointerId;
            ++received;
        }
    }
    producer.join();
    MGE_CHECK(ordered);
    MGE_CHECK(received == kEvents);
}

MGE_TEST(engine_drains_input_on_tick) {
    Engine engine;
    MGE_CHECK(engine.init(EngineConfig{}));
    engine.onSurfaceCreated(100, 100);

    TouchEvent e;
    e.pointerId = 7;
    e.action = TouchAction::Move;
    e.x = 50.0f;
    e.y = 60.0f;
    engine.pushTouchEvent(e);
    engine.pushTouchEvent(e);

    engine.tick(1.0 / 60.0);
    MGE_CHECK(engine.stats().inputEventCount == 2);
    MGE_CHECK(engine.inputQueue().pendingCount() == 0);
    MGE_CHECK(engine.lastTouch().pointerId == 7);

    // Paused engines must not consume input (it belongs to the next resume).
    engine.onPause();
    engine.pushTouchEvent(e);
    engine.tick(1.0 / 60.0);
    MGE_CHECK(engine.inputQueue().pendingCount() == 1);
}
