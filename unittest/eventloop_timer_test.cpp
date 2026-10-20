/*
 * Unit tests for C++ EventLoop timer wrapper repeat accounting and lifetime.
 *
 * Covers:
 * Case 1: Finite count two, pending expiration suppressed via controller reset.
 * Case 2: Finite count three, repeated resets of the same pending expiration.
 * Case 3: Reset a finite timer between delivered occurrences.
 * Case 4: One-shot self-rearm inside callback (zero-to-one revival).
 * Case 5: Infinite timer reset and cancellation (INFINITE sentinel preserved).
 */

#include "EventLoop.h"
#include "hevent.h"

#include <memory>
#include <stdio.h>
#include <stdlib.h>

#define require(expr)                                                                    \
    do {                                                                                 \
        if (!(expr)) {                                                                   \
            fprintf(stderr, "REQUIRE FAILED at %s:%d: %s\n", __FILE__, __LINE__, #expr); \
            exit(EXIT_FAILURE);                                                          \
        }                                                                                \
    } while (0)

#define require_msg(expr, msg)                                                                     \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            fprintf(stderr, "REQUIRE FAILED at %s:%d: %s (%s)\n", __FILE__, __LINE__, #expr, msg); \
            exit(EXIT_FAILURE);                                                                    \
        }                                                                                          \
    } while (0)

using namespace hv;

static htimer_t* find_timer_in_heap(struct heap_node* node, TimerID timerID) {
    if (!node) return NULL;
    htimer_t* timer = TIMER_ENTRY(node);
    if ((TimerID)hevent_id(timer) == timerID) return timer;
    htimer_t* found = find_timer_in_heap(node->left, timerID);
    if (found) return found;
    return find_timer_in_heap(node->right, timerID);
}

static htimer_t* find_native_timer(hloop_t* loop, TimerID timerID) {
    if (!loop) return NULL;
    return find_timer_in_heap(loop->timers.root, timerID);
}

static void make_timer_due(hloop_t* loop, htimer_t* timer, uint64_t due_time) {
    heap_remove(&loop->timers, &timer->node);
    timer->next_timeout = due_time;
    heap_insert(&loop->timers, &timer->node);
}

static void make_timer_due_now(hloop_t* loop, htimer_t* timer) {
    hloop_update_time(loop);
    uint64_t now = loop->cur_hrtime;
    uint64_t due = now > 1000 ? now - 1000 : 0;
    make_timer_due(loop, timer, due);
}

static void make_timer_due_ordered(hloop_t* loop, htimer_t* timer, uint64_t offset_us) {
    hloop_update_time(loop);
    uint64_t now = loop->cur_hrtime;
    uint64_t base = now > 10000 ? now - 10000 : 0;
    make_timer_due(loop, timer, base + offset_us);
}

/*
 * Case 1: Finite count two, pending expiration suppressed.
 * Create a target with count two. A higher-priority controller resets it
 * after it becomes pending but before its callback runs. No target callback
 * runs in that dispatch. Its next forced expiration runs the target callback
 * once, then releases both the native schedule and wrapper callback record.
 * The probe expires. Later wrapper reset and cancellation through the old ID
 * perform no work.
 */
static void test_case1_finite_count_two_pending_suppressed() {
    hloop_t* loop = hloop_new(HLOOP_FLAG_RUN_ONCE);
    require(loop != NULL);
    {
        EventLoop evloop(loop);
        int target_cb_count = 0;
        int controller_cb_count = 0;
        TimerID target_id = INVALID_TIMER_ID;
        TimerID controller_id = INVALID_TIMER_ID;

        std::weak_ptr<int> weak_probe;
        {
            auto probe = std::make_shared<int>(100);
            weak_probe = probe;
            target_id = evloop.setTimer(
                40000,
                [&target_cb_count, probe](TimerID id) {
                    (void)id;
                    target_cb_count++;
                },
                2);
        }
        require(target_id != INVALID_TIMER_ID);
        require(!weak_probe.expired());

        htimer_t* target_timer = find_native_timer(loop, target_id);
        require(target_timer != NULL);
        require(htimer_repeat(target_timer) == 2);

        controller_id = evloop.setTimer(
            40000,
            [&controller_cb_count, &evloop, target_id](TimerID id) {
                (void)id;
                require(evloop.isRunning());
                require(evloop.isInLoopThread());
                evloop.resetTimer(target_id, 40000);
                controller_cb_count++;
            },
            1);
        require(controller_id != INVALID_TIMER_ID);

        htimer_t* controller_timer = find_native_timer(loop, controller_id);
        require(controller_timer != NULL);
        hevent_set_priority(controller_timer, HEVENT_HIGHEST_PRIORITY);
        hevent_set_priority(target_timer, HEVENT_NORMAL_PRIORITY);

        make_timer_due_ordered(loop, controller_timer, 0);
        make_timer_due_ordered(loop, target_timer, 100);

        // Dispatch 1: Controller resets pending target; target callback suppressed
        evloop.run();

        require(controller_cb_count == 1);
        require(target_cb_count == 0);
        require(!weak_probe.expired());

        target_timer = find_native_timer(loop, target_id);
        require(target_timer != NULL);
        // After pending reset, native repeat remains 1 and wrapper is synchronized
        require(htimer_repeat(target_timer) == 1);

        // Dispatch 2: Next forced expiration runs target callback once and retires
        make_timer_due_now(loop, target_timer);
        evloop.run();

        require(target_cb_count == 1);
        require(weak_probe.expired());
        require(find_native_timer(loop, target_id) == NULL);

        // Later wrapper reset and cancellation through old ID perform no work
        evloop.resetTimer(target_id, 40000);
        evloop.killTimer(target_id);
        evloop.run();
        require(weak_probe.expired());
        require(find_native_timer(loop, target_id) == NULL);
    }
    hloop_free(&loop);
}

/*
 * Case 2: Finite count three, repeated resets of the same pending expiration.
 * Reset the pending target more than once before its old delivery is settled.
 * Native remaining count stays two; the resets do not consume extra repeats.
 * Two later expirations deliver two callbacks and release the wrapper.
 */
static void test_case2_finite_count_three_repeated_resets() {
    hloop_t* loop = hloop_new(HLOOP_FLAG_RUN_ONCE);
    require(loop != NULL);
    {
        EventLoop evloop(loop);
        int target_cb_count = 0;
        int controller_cb_count = 0;
        TimerID target_id = INVALID_TIMER_ID;
        TimerID controller_id = INVALID_TIMER_ID;

        std::weak_ptr<int> weak_probe;
        {
            auto probe = std::make_shared<int>(200);
            weak_probe = probe;
            target_id = evloop.setTimer(
                40000,
                [&target_cb_count, probe](TimerID id) {
                    (void)id;
                    target_cb_count++;
                },
                3);
        }
        require(target_id != INVALID_TIMER_ID);
        require(!weak_probe.expired());

        htimer_t* target_timer = find_native_timer(loop, target_id);
        require(target_timer != NULL);
        require(htimer_repeat(target_timer) == 3);

        controller_id = evloop.setTimer(
            40000,
            [&controller_cb_count, &evloop, target_id](TimerID id) {
                (void)id;
                require(evloop.isRunning());
                require(evloop.isInLoopThread());
                // Reset pending target twice before old delivery settles
                evloop.resetTimer(target_id, 40000);
                evloop.resetTimer(target_id, 40000);
                controller_cb_count++;
            },
            1);
        require(controller_id != INVALID_TIMER_ID);

        htimer_t* controller_timer = find_native_timer(loop, controller_id);
        require(controller_timer != NULL);
        hevent_set_priority(controller_timer, HEVENT_HIGHEST_PRIORITY);
        hevent_set_priority(target_timer, HEVENT_NORMAL_PRIORITY);

        make_timer_due_ordered(loop, controller_timer, 0);
        make_timer_due_ordered(loop, target_timer, 100);

        // Dispatch 1: Controller suppresses pending target delivery
        evloop.run();

        require(controller_cb_count == 1);
        require(target_cb_count == 0);
        require(!weak_probe.expired());

        target_timer = find_native_timer(loop, target_id);
        require(target_timer != NULL);
        // Native remaining count stays 2; repeated resets did not consume extra repeats
        require(htimer_repeat(target_timer) == 2);

        // Dispatch 2: First remaining expiration
        make_timer_due_now(loop, target_timer);
        evloop.run();

        require(target_cb_count == 1);
        require(!weak_probe.expired());

        target_timer = find_native_timer(loop, target_id);
        require(target_timer != NULL);
        require(htimer_repeat(target_timer) == 1);

        // Dispatch 3: Second remaining expiration; delivers callback 2 and retires
        make_timer_due_now(loop, target_timer);
        evloop.run();

        require(target_cb_count == 2);
        require(weak_probe.expired());
        require(find_native_timer(loop, target_id) == NULL);
    }
    hloop_free(&loop);
}

/*
 * Case 3: Reset a finite timer between delivered occurrences.
 * Deliver the first occurrence of a count-three timer, reset it while it
 * is scheduled but not pending, then complete the two remaining occurrences.
 * All three callbacks should be delivered overall, with normal retirement.
 */
static void test_case3_reset_between_delivered_occurrences() {
    hloop_t* loop = hloop_new(HLOOP_FLAG_RUN_ONCE);
    require(loop != NULL);
    {
        EventLoop evloop(loop);
        int target_cb_count = 0;
        TimerID target_id = INVALID_TIMER_ID;

        std::weak_ptr<int> weak_probe;
        {
            auto probe = std::make_shared<int>(300);
            weak_probe = probe;
            target_id = evloop.setTimer(
                40000,
                [&target_cb_count, probe](TimerID id) {
                    (void)id;
                    target_cb_count++;
                },
                3);
        }
        require(target_id != INVALID_TIMER_ID);
        require(!weak_probe.expired());

        htimer_t* target_timer = find_native_timer(loop, target_id);
        require(target_timer != NULL);
        require(htimer_repeat(target_timer) == 3);

        // Occurrence 1: Deliver first occurrence
        make_timer_due_now(loop, target_timer);
        evloop.run();

        require(target_cb_count == 1);
        require(!weak_probe.expired());

        target_timer = find_native_timer(loop, target_id);
        require(target_timer != NULL);
        require(htimer_repeat(target_timer) == 2);

        // Reset while scheduled but not pending
        evloop.resetTimer(target_id, 40000);
        evloop.run(); // drive queued work

        target_timer = find_native_timer(loop, target_id);
        require(target_timer != NULL);
        require(htimer_repeat(target_timer) == 2);

        // Occurrence 2: Deliver second occurrence
        make_timer_due_now(loop, target_timer);
        evloop.run();

        require(target_cb_count == 2);
        require(!weak_probe.expired());

        target_timer = find_native_timer(loop, target_id);
        require(target_timer != NULL);
        require(htimer_repeat(target_timer) == 1);

        // Occurrence 3: Deliver third occurrence and complete normal retirement
        make_timer_due_now(loop, target_timer);
        evloop.run();

        require(target_cb_count == 3);
        require(weak_probe.expired());
        require(find_native_timer(loop, target_id) == NULL);
    }
    hloop_free(&loop);
}

/*
 * Case 4: One-shot self-rearm.
 * In its first callback, reset the same ID. The wrapper must remain
 * registered after dispatch; the next forced expiration must deliver once
 * more and then release the probe.
 * Also tests zero-to-one revival inside a final live callback.
 */
static void test_case4_oneshot_self_rearm() {
    hloop_t* loop = hloop_new(HLOOP_FLAG_RUN_ONCE);
    require(loop != NULL);
    {
        EventLoop evloop(loop);
        int target_cb_count = 0;
        TimerID target_id = INVALID_TIMER_ID;

        std::weak_ptr<int> weak_probe;
        {
            auto probe = std::make_shared<int>(400);
            weak_probe = probe;
            target_id = evloop.setTimer(
                40000,
                [&target_cb_count, &evloop, probe](TimerID id) {
                    target_cb_count++;
                    if (target_cb_count == 1) {
                        require(evloop.isRunning());
                        require(evloop.isInLoopThread());

                        // Reset performs zero-to-one revival and reinserts into heap
                        evloop.resetTimer(id, 40000);

                        // Inside live callback after reset, repeat is revived from 0 to 1
                        htimer_t* t = find_native_timer(evloop.loop(), id);
                        require(t != NULL);
                        require(htimer_repeat(t) == 1);
                    }
                },
                1);
        }
        require(target_id != INVALID_TIMER_ID);
        require(!weak_probe.expired());

        htimer_t* target_timer = find_native_timer(loop, target_id);
        require(target_timer != NULL);
        require(htimer_repeat(target_timer) == 1);

        // Dispatch 1: Fires callback, callback self-rearms via resetTimer
        make_timer_due_now(loop, target_timer);
        evloop.run();

        require(target_cb_count == 1);
        // The wrapper must remain registered after dispatch
        require(!weak_probe.expired());

        target_timer = find_native_timer(loop, target_id);
        require(target_timer != NULL);
        require(htimer_repeat(target_timer) == 1);

        // Dispatch 2: Next forced expiration delivers once more and releases probe
        make_timer_due_now(loop, target_timer);
        evloop.run();

        require(target_cb_count == 2);
        require(weak_probe.expired());
        require(find_native_timer(loop, target_id) == NULL);
    }
    hloop_free(&loop);
}

/*
 * Case 5: Infinite timer reset and cancellation.
 * Reset a pending infinite timer, suppress its old delivery, allow a later
 * delivery, then cancel it. The INFINITE sentinel must stay unchanged through
 * reset; cancellation must release the wrapper and native schedule.
 */
static void test_case5_infinite_reset_and_cancellation() {
    hloop_t* loop = hloop_new(HLOOP_FLAG_RUN_ONCE);
    require(loop != NULL);
    {
        EventLoop evloop(loop);
        int target_cb_count = 0;
        int controller_cb_count = 0;
        TimerID target_id = INVALID_TIMER_ID;
        TimerID controller_id = INVALID_TIMER_ID;

        std::weak_ptr<int> weak_probe;
        {
            auto probe = std::make_shared<int>(500);
            weak_probe = probe;
            target_id = evloop.setTimer(
                40000,
                [&target_cb_count, probe](TimerID id) {
                    (void)id;
                    target_cb_count++;
                },
                INFINITE);
        }
        require(target_id != INVALID_TIMER_ID);
        require(!weak_probe.expired());

        htimer_t* target_timer = find_native_timer(loop, target_id);
        require(target_timer != NULL);
        require(htimer_repeat(target_timer) == INFINITE);

        controller_id = evloop.setTimer(
            40000,
            [&controller_cb_count, &evloop, target_id](TimerID id) {
                (void)id;
                require(evloop.isRunning());
                require(evloop.isInLoopThread());
                evloop.resetTimer(target_id, 40000);
                controller_cb_count++;
            },
            1);
        require(controller_id != INVALID_TIMER_ID);

        htimer_t* controller_timer = find_native_timer(loop, controller_id);
        require(controller_timer != NULL);
        hevent_set_priority(controller_timer, HEVENT_HIGHEST_PRIORITY);
        hevent_set_priority(target_timer, HEVENT_NORMAL_PRIORITY);

        make_timer_due_ordered(loop, controller_timer, 0);
        make_timer_due_ordered(loop, target_timer, 100);

        // Dispatch 1: Controller suppresses pending infinite delivery
        evloop.run();

        require(controller_cb_count == 1);
        require(target_cb_count == 0);
        require(!weak_probe.expired());

        target_timer = find_native_timer(loop, target_id);
        require(target_timer != NULL);
        // INFINITE sentinel must stay unchanged through reset
        require(htimer_repeat(target_timer) == INFINITE);

        // Dispatch 2: Allow a later delivery
        make_timer_due_now(loop, target_timer);
        evloop.run();

        require(target_cb_count == 1);
        require(!weak_probe.expired());

        target_timer = find_native_timer(loop, target_id);
        require(target_timer != NULL);
        require(htimer_repeat(target_timer) == INFINITE);

        // Cancel it
        evloop.killTimer(target_id);
        evloop.run(); // drive queued cancellation

        require(weak_probe.expired());
        require(find_native_timer(loop, target_id) == NULL);
    }
    hloop_free(&loop);
}

int main() {
    printf("Running test_case1_finite_count_two_pending_suppressed...\n");
    test_case1_finite_count_two_pending_suppressed();

    printf("Running test_case2_finite_count_three_repeated_resets...\n");
    test_case2_finite_count_three_repeated_resets();

    printf("Running test_case3_reset_between_delivered_occurrences...\n");
    test_case3_reset_between_delivered_occurrences();

    printf("Running test_case4_oneshot_self_rearm...\n");
    test_case4_oneshot_self_rearm();

    printf("Running test_case5_infinite_reset_and_cancellation...\n");
    test_case5_infinite_reset_and_cancellation();

    printf("All EventLoop timer tests (Cases 1-5) passed!\n");
    return 0;
}
