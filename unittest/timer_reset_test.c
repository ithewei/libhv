/*
 * Unit tests for timer reset and lifetime handling in libhv.
 *
 * Covers:
 * 1. Resetting a timer that is not due.
 * 2. Resetting an expiration already waiting for dispatch.
 * 3. Rearming from inside the timer's own callback.
 * 4. Cancelling a timer after resetting it in its own callback.
 * 5. Cancelling another pending timer after resetting it (both different and same priority queue).
 * 6. Repeated reset operations before dispatch settles (both keep and cancel).
 * 7. Repeat-timer behavior (one-shot, multiple repetitions, and repeating self-rearm).
 * 8. Replacement allocation and cleanup during callback.
 */

#include "hevent.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static void make_timer_due(hloop_t* loop, htimer_t* timer, uint64_t due_time) {
    heap_remove(&loop->timers, &timer->node);
    timer->next_timeout = due_time;
    heap_insert(&loop->timers, &timer->node);
}

/* Set deadline shortly before current loop time so the timer is due without
 * triggering repeated catch-up iterations on repeating timers. */
static void make_timer_due_now(hloop_t* loop, htimer_t* timer) {
    hloop_update_time(loop);
    uint64_t now = loop->cur_hrtime;
    uint64_t due = now > 1000 ? now - 1000 : 0;
    make_timer_due(loop, timer, due);
}

/* Set deadline shortly before current loop time with an explicit microsecond
 * offset to enforce deterministic minheap ordering. */
static void make_timer_due_ordered(hloop_t* loop, htimer_t* timer, uint64_t offset_us) {
    hloop_update_time(loop);
    uint64_t now = loop->cur_hrtime;
    uint64_t base = now > 10000 ? now - 10000 : 0;
    make_timer_due(loop, timer, base + offset_us);
}

/* ========================================================================= */
/* Case 1: Reset a timer that is not due                                     */
/* ========================================================================= */

static int s_case1_target_calls = 0;
static int s_case1_unrelated_calls = 0;

static void case1_target_cb(htimer_t* timer) {
    (void)timer;
    s_case1_target_calls++;
}

static void case1_unrelated_cb(htimer_t* timer) {
    (void)timer;
    s_case1_unrelated_calls++;
}

static void test_reset_timer_not_due(void) {
    s_case1_target_calls = 0;
    s_case1_unrelated_calls = 0;

    hloop_t* loop = hloop_new(0);
    require(loop != NULL);
    loop->status = HLOOP_STATUS_RUNNING;

    htimer_t* unrelated = htimer_add(loop, case1_unrelated_cb, 50000, 1);
    require(unrelated != NULL);

    htimer_t* target = htimer_add(loop, case1_target_cb, 20000, 1);
    require(target != NULL);

    require(hloop_ntimers(loop) == 2);
    require(hloop_nactives(loop) == 2);
    uint64_t old_target_deadline = target->next_timeout;
    uint64_t unrelated_deadline = unrelated->next_timeout;

    // Reset target to a new, further deadline (40000 ms)
    htimer_reset(target, 40000);

    // Reset does not create an additional timer or change unrelated timers
    require(hloop_ntimers(loop) == 2);
    require(hloop_nactives(loop) == 2);
    // Unrelated timer deadline remains unchanged
    require(unrelated->next_timeout == unrelated_deadline);
    // Target replacement deadline matches the newly requested timeout
    require(((htimeout_t*)target)->timeout == 40000);
    require(target->next_timeout > old_target_deadline);
    require(target->next_timeout >= old_target_deadline + 20000000ULL - 100000ULL);
    require(!target->destroy);
    require(!target->pending);
    require(!target->pending_reset);

    // Step 1: Check non-delivery while deadline is in the future.
    // Neither timer is due.
    hloop_process_events(loop, 0);
    require(s_case1_target_calls == 0);
    require(s_case1_unrelated_calls == 0);
    require(hloop_ntimers(loop) == 2);
    require(hloop_nactives(loop) == 2);

    // Step 2: Advance clock to replacement deadline; target fires while unrelated does not.
    make_timer_due_now(loop, target);
    hloop_process_events(loop, 0);
    require(s_case1_target_calls == 1);
    require(s_case1_unrelated_calls == 0);
    require(hloop_ntimers(loop) == 1);
    require(hloop_nactives(loop) == 1);

    // Step 3: Advance clock for unrelated timer; unrelated fires.
    make_timer_due_now(loop, unrelated);
    hloop_process_events(loop, 0);
    require(s_case1_unrelated_calls == 1);
    require(hloop_ntimers(loop) == 0);
    require(hloop_nactives(loop) == 0);

    hloop_free(&loop);
}

/* ========================================================================= */
/* Case 2: Reset an expiration already waiting for dispatch                  */
/* ========================================================================= */

struct case2_ctx {
    htimer_t* ctrl;
    htimer_t* target;
    htimer_t* unrelated;
    int ctrl_calls;
    int target_calls;
    int unrelated_calls;
    bool target_was_pending_in_ctrl;
    bool target_pending_after_reset;
    bool target_pending_reset_after_reset;
};

static void case2_ctrl_cb(htimer_t* timer) {
    struct case2_ctx* ctx = (struct case2_ctx*)timer->userdata;
    ctx->ctrl_calls++;

    // Target expiration is waiting for dispatch
    ctx->target_was_pending_in_ctrl = ctx->target->pending;
    require(ctx->target->pending == 1);
    require(ctx->unrelated->pending == 1);

    // Reset target timer to a future deadline
    htimer_reset(ctx->target, 10000);

    // Lifetime reference is preserved; pending_reset flag is set
    ctx->target_pending_after_reset = ctx->target->pending;
    ctx->target_pending_reset_after_reset = ctx->target->pending_reset;
    require(ctx->target->pending == 1);
    require(ctx->target->pending_reset == 1);
    require(ctx->target->active == 1);
    require(ctx->target->destroy == 0);
}

static void case2_target_cb(htimer_t* timer) {
    struct case2_ctx* ctx = (struct case2_ctx*)timer->userdata;
    ctx->target_calls++;
}

static void case2_unrelated_cb(htimer_t* timer) {
    struct case2_ctx* ctx = (struct case2_ctx*)timer->userdata;
    ctx->unrelated_calls++;
}

static void test_reset_pending_expiration(void) {
    struct case2_ctx ctx = {0};
    hloop_t* loop = hloop_new(0);
    require(loop != NULL);
    loop->status = HLOOP_STATUS_RUNNING;

    ctx.ctrl = htimer_add(loop, case2_ctrl_cb, 1000, 1);
    ctx.target = htimer_add(loop, case2_target_cb, 1000, 1);
    ctx.unrelated = htimer_add(loop, case2_unrelated_cb, 1000, 1);
    require(ctx.ctrl && ctx.target && ctx.unrelated);

    ctx.ctrl->userdata = &ctx;
    ctx.target->userdata = &ctx;
    ctx.unrelated->userdata = &ctx;

    // Priorities ensure ctrl runs first, then target would run, then unrelated
    hevent_set_priority(ctx.ctrl, HEVENT_HIGHEST_PRIORITY);
    hevent_set_priority(ctx.target, HEVENT_NORMAL_PRIORITY);
    hevent_set_priority(ctx.unrelated, HEVENT_LOWEST_PRIORITY);

    make_timer_due_now(loop, ctx.ctrl);
    make_timer_due_now(loop, ctx.target);
    make_timer_due_now(loop, ctx.unrelated);

    // First dispatch
    hloop_process_events(loop, 0);

    require(ctx.ctrl_calls == 1);
    require(ctx.target_was_pending_in_ctrl);
    require(ctx.target_pending_after_reset);
    require(ctx.target_pending_reset_after_reset);
    require(ctx.target_calls == 0);    // old due occurrence suppressed
    require(ctx.unrelated_calls == 1); // unrelated due timer ran exactly once

    // Old pending reference released after dispatch completes
    require(ctx.target->pending == 0);
    require(ctx.target->pending_reset == 0);
    require(ctx.target->active == 1); // target remains scheduled
    require(ctx.target->destroy == 0);
    require(hloop_ntimers(loop) == 1);
    require(hloop_nactives(loop) == 1);

    // Advance clock to replacement deadline
    make_timer_due_now(loop, ctx.target);
    hloop_process_events(loop, 0);

    require(ctx.target_calls == 1); // fires at replacement deadline
    require(hloop_ntimers(loop) == 0);
    require(hloop_nactives(loop) == 0);

    hloop_free(&loop);
}

/* ========================================================================= */
/* Case 3: Rearm from inside the timer's own callback                        */
/* ========================================================================= */

struct case3_ctx {
    int calls;
    bool pending_in_first_cb;
    bool pending_reset_after_rearm;
    bool pending_after_rearm;
};

static void case3_cb(htimer_t* timer) {
    struct case3_ctx* ctx = (struct case3_ctx*)timer->userdata;
    ctx->calls++;
    if (ctx->calls == 1) {
        ctx->pending_in_first_cb = timer->pending;
        require(timer->pending == 1);
        htimer_reset(timer, 5000);
        ctx->pending_after_rearm = timer->pending;
        ctx->pending_reset_after_rearm = timer->pending_reset;
        require(timer->pending == 1);
        require(timer->pending_reset == 1);
        require(timer->active == 1);
        require(timer->destroy == 0);
    }
}

static void test_rearm_inside_own_callback(void) {
    struct case3_ctx ctx = {0};
    hloop_t* loop = hloop_new(0);
    require(loop != NULL);
    loop->status = HLOOP_STATUS_RUNNING;

    uint32_t init_timers = hloop_ntimers(loop);
    uint32_t init_actives = hloop_nactives(loop);

    htimer_t* timer = htimer_add(loop, case3_cb, 1000, 1);
    require(timer != NULL);
    timer->userdata = &ctx;

    make_timer_due_now(loop, timer);

    // First dispatch: callback fires, rearms itself
    hloop_process_events(loop, 0);

    require(ctx.calls == 1); // first callback completes once
    require(ctx.pending_in_first_cb);
    require(ctx.pending_after_rearm);
    require(ctx.pending_reset_after_rearm);

    // After dispatch finishes, old occurrence marker must be cleared
    require(timer->pending == 0);
    require(timer->pending_reset == 0);
    require(timer->active == 1);
    require(timer->destroy == 0);
    require(hloop_ntimers(loop) == init_timers + 1);
    require(hloop_nactives(loop) == init_actives + 1);

    // Second occurrence: make due and dispatch
    make_timer_due_now(loop, timer);
    hloop_process_events(loop, 0);

    require(ctx.calls == 2); // next occurrence delivered once
    require(hloop_ntimers(loop) == init_timers);
    require(hloop_nactives(loop) == init_actives);
    require(loop->npendings == 0);

    hloop_free(&loop);
}

/* ========================================================================= */
/* Case 4: Cancel a timer after resetting it in its own callback             */
/* ========================================================================= */

struct case4_ctx {
    int calls;
    long alloc_before;
    long alloc_in_cb_after_del;
    bool pending_before_del;
};

static void case4_cb(htimer_t* timer) {
    struct case4_ctx* ctx = (struct case4_ctx*)timer->userdata;
    ctx->calls++;

    require(timer->pending == 1);
    htimer_reset(timer, 5000);
    require(timer->pending == 1);
    require(timer->pending_reset == 1);

    ctx->pending_before_del = timer->pending;

    // Delete timer after resetting it
    htimer_del(timer);

    // Memory must still be valid; dispatch still owns the pending reference
    ctx->alloc_in_cb_after_del = hv_alloc_cnt() - hv_free_cnt();
    // Application does NOT access timer after htimer_del
}

static void test_cancel_after_rearm_in_own_callback(void) {
    struct case4_ctx ctx = {0};
    hloop_t* loop = hloop_new(0);
    require(loop != NULL);
    loop->status = HLOOP_STATUS_RUNNING;

    ctx.alloc_before = hv_alloc_cnt() - hv_free_cnt();

    htimer_t* timer = htimer_add(loop, case4_cb, 1000, 1);
    require(timer != NULL);
    timer->userdata = &ctx;

    require(hv_alloc_cnt() - hv_free_cnt() == ctx.alloc_before + 1);

    make_timer_due_now(loop, timer);

    hloop_process_events(loop, 0);

    require(ctx.calls == 1);
    require(ctx.pending_before_del);
    // In callback, alloc count was still ctx.alloc_before + 1 (not freed prematurely)
    require(ctx.alloc_in_cb_after_del == ctx.alloc_before + 1);

    // Now dispatch has finished: allocation released exactly once
    require(hv_alloc_cnt() - hv_free_cnt() == ctx.alloc_before);
    require(hloop_ntimers(loop) == 0);
    require(hloop_nactives(loop) == 0);
    require(loop->npendings == 0);

    // No later callback is delivered
    hloop_process_events(loop, 0);
    require(ctx.calls == 1);

    hloop_free(&loop);
}

/* ========================================================================= */
/* Case 5: Cancel another pending timer after resetting it                   */
/* ========================================================================= */

struct case5_ctx {
    htimer_t* ctrl;
    htimer_t* target;
    htimer_t* unrelated;
    int ctrl_calls;
    int target_calls;
    int unrelated_calls;
    long alloc_before;
    long alloc_in_ctrl_after_del;
};

static void case5_ctrl_cb(htimer_t* timer) {
    struct case5_ctx* ctx = (struct case5_ctx*)timer->userdata;
    ctx->ctrl_calls++;

    require(ctx->target->pending == 1);
    htimer_reset(ctx->target, 5000);
    require(ctx->target->pending == 1);
    require(ctx->target->pending_reset == 1);

    htimer_del(ctx->target);
    // Target is still referenced by dispatch; must not be freed yet
    ctx->alloc_in_ctrl_after_del = hv_alloc_cnt() - hv_free_cnt();
}

static void case5_target_cb(htimer_t* timer) {
    struct case5_ctx* ctx = (struct case5_ctx*)timer->userdata;
    ctx->target_calls++;
}

static void case5_unrelated_cb(htimer_t* timer) {
    struct case5_ctx* ctx = (struct case5_ctx*)timer->userdata;
    ctx->unrelated_calls++;
}

// 5a: Target in a later priority queue
static void test_cancel_pending_timer_different_priority(void) {
    struct case5_ctx ctx = {0};
    hloop_t* loop = hloop_new(0);
    require(loop != NULL);
    loop->status = HLOOP_STATUS_RUNNING;

    ctx.alloc_before = hv_alloc_cnt() - hv_free_cnt();

    ctx.ctrl = htimer_add(loop, case5_ctrl_cb, 1000, 1);
    ctx.target = htimer_add(loop, case5_target_cb, 1000, 1);
    ctx.unrelated = htimer_add(loop, case5_unrelated_cb, 1000, 1);
    require(ctx.ctrl && ctx.target && ctx.unrelated);

    ctx.ctrl->userdata = &ctx;
    ctx.target->userdata = &ctx;
    ctx.unrelated->userdata = &ctx;

    hevent_set_priority(ctx.ctrl, HEVENT_HIGHEST_PRIORITY);
    hevent_set_priority(ctx.target, HEVENT_NORMAL_PRIORITY);
    hevent_set_priority(ctx.unrelated, HEVENT_LOWEST_PRIORITY);

    make_timer_due_now(loop, ctx.ctrl);
    make_timer_due_now(loop, ctx.target);
    make_timer_due_now(loop, ctx.unrelated);

    hloop_process_events(loop, 0);

    require(ctx.ctrl_calls == 1);
    require(ctx.target_calls == 0);    // neither old nor replacement runs
    require(ctx.unrelated_calls == 1); // other pending callbacks remain intact

    require(ctx.alloc_in_ctrl_after_del == ctx.alloc_before + 3);
    require(hv_alloc_cnt() - hv_free_cnt() == ctx.alloc_before);
    require(hloop_ntimers(loop) == 0);
    require(hloop_nactives(loop) == 0);
    require(loop->npendings == 0);

    hloop_process_events(loop, 0);
    require(ctx.target_calls == 0);

    hloop_free(&loop);
}

// 5b: Target in the SAME priority queue (dispatcher already cached next pointer)
static void test_cancel_pending_timer_same_priority(void) {
    struct case5_ctx ctx = {0};
    hloop_t* loop = hloop_new(0);
    require(loop != NULL);
    loop->status = HLOOP_STATUS_RUNNING;

    ctx.alloc_before = hv_alloc_cnt() - hv_free_cnt();

    ctx.unrelated = htimer_add(loop, case5_unrelated_cb, 1000, 1);
    ctx.target = htimer_add(loop, case5_target_cb, 1000, 1);
    ctx.ctrl = htimer_add(loop, case5_ctrl_cb, 1000, 1);
    require(ctx.ctrl && ctx.target && ctx.unrelated);

    ctx.ctrl->userdata = &ctx;
    ctx.target->userdata = &ctx;
    ctx.unrelated->userdata = &ctx;

    hevent_set_priority(ctx.unrelated, HEVENT_HIGHEST_PRIORITY);
    hevent_set_priority(ctx.target, HEVENT_HIGHEST_PRIORITY);
    hevent_set_priority(ctx.ctrl, HEVENT_HIGHEST_PRIORITY);

    // Heap order: unrelated dequeued first, target second, ctrl third.
    // Resulting pendings order (LIFO): ctrl -> target -> unrelated.
    make_timer_due_ordered(loop, ctx.unrelated, 1);
    make_timer_due_ordered(loop, ctx.target, 2);
    make_timer_due_ordered(loop, ctx.ctrl, 3);

    hloop_process_events(loop, 0);

    require(ctx.ctrl_calls == 1);
    require(ctx.target_calls == 0);
    require(ctx.unrelated_calls == 1);

    require(ctx.alloc_in_ctrl_after_del == ctx.alloc_before + 3);
    require(hv_alloc_cnt() - hv_free_cnt() == ctx.alloc_before);
    require(hloop_ntimers(loop) == 0);
    require(hloop_nactives(loop) == 0);
    require(loop->npendings == 0);

    hloop_free(&loop);
}

/* ========================================================================= */
/* Case 6: Repeat reset operations before dispatch settles                  */
/* ========================================================================= */

struct case6_ctx {
    htimer_t* ctrl;
    htimer_t* target;
    int ctrl_calls;
    int target_calls;
    bool cancel_at_end;
};

static void case6_ctrl_cb(htimer_t* timer) {
    struct case6_ctx* ctx = (struct case6_ctx*)timer->userdata;
    ctx->ctrl_calls++;

    require(ctx->target->pending == 1);

    // Multiple resets
    htimer_reset(ctx->target, 2000);
    require(ctx->target->pending == 1);
    require(ctx->target->pending_reset == 1);

    htimer_reset(ctx->target, 4000);
    require(ctx->target->pending == 1);
    require(ctx->target->pending_reset == 1);

    htimer_reset(ctx->target, 8000);
    require(ctx->target->pending == 1);
    require(ctx->target->pending_reset == 1);

    if (ctx->cancel_at_end) {
        htimer_del(ctx->target);
    }
}

static void case6_target_cb(htimer_t* timer) {
    struct case6_ctx* ctx = (struct case6_ctx*)timer->userdata;
    ctx->target_calls++;
}

static void test_repeated_resets_keep_rearm(void) {
    struct case6_ctx ctx = {0};
    ctx.cancel_at_end = false;

    hloop_t* loop = hloop_new(0);
    require(loop != NULL);
    loop->status = HLOOP_STATUS_RUNNING;

    long alloc_before = hv_alloc_cnt() - hv_free_cnt();

    ctx.ctrl = htimer_add(loop, case6_ctrl_cb, 1000, 1);
    ctx.target = htimer_add(loop, case6_target_cb, 1000, 1);
    ctx.ctrl->userdata = &ctx;
    ctx.target->userdata = &ctx;

    hevent_set_priority(ctx.ctrl, HEVENT_HIGHEST_PRIORITY);
    hevent_set_priority(ctx.target, HEVENT_NORMAL_PRIORITY);

    make_timer_due_now(loop, ctx.ctrl);
    make_timer_due_now(loop, ctx.target);

    hloop_process_events(loop, 0);

    require(ctx.ctrl_calls == 1);
    require(ctx.target_calls == 0); // old occurrence suppressed
    require(ctx.target->pending == 0);
    require(ctx.target->pending_reset == 0);
    require(ctx.target->active == 1);
    require(((htimeout_t*)ctx.target)->timeout == 8000); // final requested deadline

    // Exactly 1 timer in heap (no duplicate queue membership)
    require(loop->timers.nelts == 1);
    require(hloop_ntimers(loop) == 1);
    require(hloop_nactives(loop) == 1);

    // Fire at replacement deadline
    make_timer_due_now(loop, ctx.target);
    hloop_process_events(loop, 0);

    require(ctx.target_calls == 1); // exactly one occurrence delivered
    require(hloop_ntimers(loop) == 0);
    require(hloop_nactives(loop) == 0);
    require(hv_alloc_cnt() - hv_free_cnt() == alloc_before);

    hloop_free(&loop);
}

static void test_repeated_resets_cancel(void) {
    struct case6_ctx ctx = {0};
    ctx.cancel_at_end = true;

    hloop_t* loop = hloop_new(0);
    require(loop != NULL);
    loop->status = HLOOP_STATUS_RUNNING;

    long alloc_before = hv_alloc_cnt() - hv_free_cnt();

    ctx.ctrl = htimer_add(loop, case6_ctrl_cb, 1000, 1);
    ctx.target = htimer_add(loop, case6_target_cb, 1000, 1);
    ctx.ctrl->userdata = &ctx;
    ctx.target->userdata = &ctx;

    hevent_set_priority(ctx.ctrl, HEVENT_HIGHEST_PRIORITY);
    hevent_set_priority(ctx.target, HEVENT_NORMAL_PRIORITY);

    make_timer_due_now(loop, ctx.ctrl);
    make_timer_due_now(loop, ctx.target);

    hloop_process_events(loop, 0);

    require(ctx.ctrl_calls == 1);
    require(ctx.target_calls == 0);
    require(hloop_ntimers(loop) == 0);
    require(hloop_nactives(loop) == 0);
    require(hv_alloc_cnt() - hv_free_cnt() == alloc_before);

    hloop_free(&loop);
}

/* ========================================================================= */
/* Case 7: Repeat-timer behavior                                             */
/* ========================================================================= */

struct case7_ctx {
    htimer_t* ctrl;
    htimer_t* target;
    int ctrl_calls;
    int target_calls;
    int repeat_remaining_after_suppress;
};

static void case7_ctrl_cb(htimer_t* timer) {
    struct case7_ctx* ctx = (struct case7_ctx*)timer->userdata;
    ctx->ctrl_calls++;

    require(ctx->target->pending == 1);
    htimer_reset(ctx->target, 5000);
    require(ctx->target->pending == 1);
    require(ctx->target->pending_reset == 1);
    ctx->repeat_remaining_after_suppress = ctx->target->repeat;
}

static void case7_target_cb(htimer_t* timer) {
    struct case7_ctx* ctx = (struct case7_ctx*)timer->userdata;
    ctx->target_calls++;
}

static void test_repeat_timer_multiple_repetitions(void) {
    struct case7_ctx ctx = {0};
    hloop_t* loop = hloop_new(0);
    require(loop != NULL);
    loop->status = HLOOP_STATUS_RUNNING;

    long alloc_before = hv_alloc_cnt() - hv_free_cnt();

    ctx.ctrl = htimer_add(loop, case7_ctrl_cb, 1000, 1);
    ctx.target = htimer_add(loop, case7_target_cb, 1000, 3);
    require(ctx.target->repeat == 3);

    ctx.ctrl->userdata = &ctx;
    ctx.target->userdata = &ctx;

    hevent_set_priority(ctx.ctrl, HEVENT_HIGHEST_PRIORITY);
    hevent_set_priority(ctx.target, HEVENT_NORMAL_PRIORITY);

    make_timer_due_now(loop, ctx.ctrl);
    make_timer_due_now(loop, ctx.target);

    // Iteration 1: ctrl resets target; 1st occurrence suppressed
    hloop_process_events(loop, 0);

    require(ctx.ctrl_calls == 1);
    require(ctx.target_calls == 0); // suppressed
    require(ctx.repeat_remaining_after_suppress == 2);
    require(ctx.target->repeat == 2);
    require(ctx.target->active == 1);
    require(ctx.target->destroy == 0);
    require(hloop_ntimers(loop) == 1);

    // Iteration 2: 2nd scheduled occurrence
    make_timer_due_now(loop, ctx.target);
    hloop_process_events(loop, 0);

    require(ctx.target_calls == 1);
    require(ctx.target->repeat == 1);
    require(hloop_ntimers(loop) == 1);

    // Iteration 3: 3rd scheduled occurrence
    make_timer_due_now(loop, ctx.target);
    hloop_process_events(loop, 0);

    require(ctx.target_calls == 2); // exactly 2 delivered occurrences total
    require(hloop_ntimers(loop) == 0);
    require(hloop_nactives(loop) == 0);
    require(hv_alloc_cnt() - hv_free_cnt() == alloc_before);

    hloop_free(&loop);
}

struct case7_self_rearm_ctx {
    int calls;
    int repeat_after_rearm;
    bool was_pending;
};

static void case7_self_rearm_cb(htimer_t* timer) {
    struct case7_self_rearm_ctx* ctx = (struct case7_self_rearm_ctx*)timer->userdata;
    ctx->calls++;
    if (ctx->calls == 1) {
        ctx->was_pending = timer->pending;
        require(timer->pending == 1);
        // Reset repeating timer to a new deadline from inside its own callback
        htimer_reset(timer, 5000);
        ctx->repeat_after_rearm = timer->repeat;
        require(timer->repeat == 2); // repeat was decremented for this occurrence, preserved across reset
        require(timer->pending == 1);
        require(timer->pending_reset == 1);
    }
}

static void test_repeat_timer_self_rearm(void) {
    struct case7_self_rearm_ctx ctx = {0};
    hloop_t* loop = hloop_new(0);
    require(loop != NULL);
    loop->status = HLOOP_STATUS_RUNNING;

    long alloc_before = hv_alloc_cnt() - hv_free_cnt();

    htimer_t* timer = htimer_add(loop, case7_self_rearm_cb, 1000, 3);
    timer->userdata = &ctx;

    // Occurrence 1 fires and rearms itself from inside the callback
    make_timer_due_now(loop, timer);
    hloop_process_events(loop, 0);
    require(ctx.calls == 1);
    require(ctx.was_pending);
    require(ctx.repeat_after_rearm == 2);
    require(timer->repeat == 2);
    require(timer->pending == 0);
    require(timer->pending_reset == 0);
    require(timer->active == 1);
    require(hloop_ntimers(loop) == 1);

    // Occurrence 2 fires
    make_timer_due_now(loop, timer);
    hloop_process_events(loop, 0);
    require(ctx.calls == 2);
    require(timer->repeat == 1);
    require(hloop_ntimers(loop) == 1);

    // Occurrence 3 fires
    make_timer_due_now(loop, timer);
    hloop_process_events(loop, 0);
    require(ctx.calls == 3);
    require(hloop_ntimers(loop) == 0);
    require(hloop_nactives(loop) == 0);
    require(hv_alloc_cnt() - hv_free_cnt() == alloc_before);

    hloop_free(&loop);
}

/* ========================================================================= */
/* Case 8: Replacement allocation and cleanup                                */
/* ========================================================================= */

struct case8a_ctx {
    hloop_t* loop;
    htimer_t* timer_a;
    htimer_t* timer_b;
    int a_calls;
    int b_calls;
    uintptr_t addr_a;
    uintptr_t addr_b;
    bool addresses_differ;
};

static void case8a_b_cb(htimer_t* timer) {
    struct case8a_ctx* ctx = (struct case8a_ctx*)timer->userdata;
    ctx->b_calls++;
}

static void case8a_a_cb(htimer_t* timer) {
    struct case8a_ctx* ctx = (struct case8a_ctx*)timer->userdata;
    ctx->a_calls++;
    ctx->addr_a = (uintptr_t)(void*)timer;

    htimer_reset(timer, 2000);
    htimer_del(timer);

    // While timer_a is still owned by dispatch, create replacement timer_b
    ctx->timer_b = htimer_add(ctx->loop, case8a_b_cb, 3000, 1);
    require(ctx->timer_b != NULL);
    ctx->timer_b->userdata = ctx;
    ctx->addr_b = (uintptr_t)(void*)ctx->timer_b;

    // Storage still owned by dispatch is not offered for reuse
    ctx->addresses_differ = (ctx->addr_a != ctx->addr_b);
    require_msg(ctx->addresses_differ, "replacement timer must not reuse executing timer storage");
    // Replacement timer retains its own callback and deadline
    require(((hevent_t*)ctx->timer_b)->cb == (hevent_cb)case8a_b_cb);
    require(((htimeout_t*)ctx->timer_b)->timeout == 3000);
}

static void test_replacement_allocation_in_executing_callback(void) {
    struct case8a_ctx ctx = {0};
    hloop_t* loop = hloop_new(0);
    require(loop != NULL);
    loop->status = HLOOP_STATUS_RUNNING;
    ctx.loop = loop;

    long alloc_before = hv_alloc_cnt() - hv_free_cnt();

    ctx.timer_a = htimer_add(loop, case8a_a_cb, 1000, 1);
    ctx.timer_a->userdata = &ctx;

    make_timer_due_now(loop, ctx.timer_a);

    // Dispatch timer_a
    hloop_process_events(loop, 0);

    require(ctx.a_calls == 1);
    require(ctx.addr_a != 0);
    require(ctx.addr_b != 0);
    // Verified while both allocations were alive inside the callback
    require(ctx.addresses_differ);

    // timer_a freed by dispatch settlement; timer_b active in loop
    require(hloop_ntimers(loop) == 1);
    require(hloop_nactives(loop) == 1);
    require(hv_alloc_cnt() - hv_free_cnt() == alloc_before + 1);

    // Dispatch replacement timer_b
    make_timer_due_now(loop, ctx.timer_b);
    hloop_process_events(loop, 0);

    require(ctx.b_calls == 1);
    require(hloop_ntimers(loop) == 0);
    require(hloop_nactives(loop) == 0);
    require(hv_alloc_cnt() - hv_free_cnt() == alloc_before);

    hloop_free(&loop);
}

struct case8b_ctx {
    hloop_t* loop;
    htimer_t* ctrl;
    htimer_t* target;
    htimer_t* replacement;
    int ctrl_calls;
    int target_calls;
    int replacement_calls;
    uintptr_t target_addr;
    uintptr_t replacement_addr;
    bool addresses_differ;
};

static void case8b_target_cb(htimer_t* timer) {
    struct case8b_ctx* ctx = (struct case8b_ctx*)timer->userdata;
    ctx->target_calls++;
}

static void case8b_replacement_cb(htimer_t* timer) {
    struct case8b_ctx* ctx = (struct case8b_ctx*)timer->userdata;
    ctx->replacement_calls++;
}

static void case8b_ctrl_cb(htimer_t* timer) {
    struct case8b_ctx* ctx = (struct case8b_ctx*)timer->userdata;
    ctx->ctrl_calls++;
    ctx->target_addr = (uintptr_t)(void*)ctx->target;

    htimer_reset(ctx->target, 2000);
    htimer_del(ctx->target);

    ctx->replacement = htimer_add(ctx->loop, case8b_replacement_cb, 4000, 1);
    require(ctx->replacement != NULL);
    ctx->replacement->userdata = ctx;
    ctx->replacement_addr = (uintptr_t)(void*)ctx->replacement;

    ctx->addresses_differ = (ctx->target_addr != ctx->replacement_addr);
    require_msg(ctx->addresses_differ, "replacement timer must not reuse pending timer storage");
    require(((hevent_t*)ctx->replacement)->cb == (hevent_cb)case8b_replacement_cb);
    require(((htimeout_t*)ctx->replacement)->timeout == 4000);
}

static void test_replacement_allocation_in_controller_callback(void) {
    struct case8b_ctx ctx = {0};
    hloop_t* loop = hloop_new(0);
    require(loop != NULL);
    loop->status = HLOOP_STATUS_RUNNING;
    ctx.loop = loop;

    long alloc_before = hv_alloc_cnt() - hv_free_cnt();

    ctx.ctrl = htimer_add(loop, case8b_ctrl_cb, 1000, 1);
    ctx.target = htimer_add(loop, case8b_target_cb, 1000, 1);
    ctx.ctrl->userdata = &ctx;
    ctx.target->userdata = &ctx;

    hevent_set_priority(ctx.ctrl, HEVENT_HIGHEST_PRIORITY);
    hevent_set_priority(ctx.target, HEVENT_NORMAL_PRIORITY);

    make_timer_due_now(loop, ctx.ctrl);
    make_timer_due_now(loop, ctx.target);

    hloop_process_events(loop, 0);

    require(ctx.ctrl_calls == 1);
    require(ctx.target_calls == 0);
    require(ctx.target_addr != 0);
    require(ctx.replacement_addr != 0);
    // Verified while both allocations were alive inside the callback
    require(ctx.addresses_differ);

    // ctrl and target are freed; replacement is scheduled
    require(hloop_ntimers(loop) == 1);
    require(hloop_nactives(loop) == 1);
    require(hv_alloc_cnt() - hv_free_cnt() == alloc_before + 1);

    // Dispatch replacement
    make_timer_due_now(loop, ctx.replacement);
    hloop_process_events(loop, 0);

    require(ctx.replacement_calls == 1);
    require(hloop_ntimers(loop) == 0);
    require(hloop_nactives(loop) == 0);
    require(hv_alloc_cnt() - hv_free_cnt() == alloc_before);

    hloop_free(&loop);
}

/* ========================================================================= */
/* Main test runner                                                          */
/* ========================================================================= */

int main(void) {
    printf("Running timer_reset_test cases...\n");

    test_reset_timer_not_due();
    test_reset_pending_expiration();
    test_rearm_inside_own_callback();
    test_cancel_after_rearm_in_own_callback();
    test_cancel_pending_timer_different_priority();
    test_cancel_pending_timer_same_priority();
    test_repeated_resets_keep_rearm();
    test_repeated_resets_cancel();
    test_repeat_timer_multiple_repetitions();
    test_repeat_timer_self_rearm();
    test_replacement_allocation_in_executing_callback();
    test_replacement_allocation_in_controller_callback();

    printf("timer_reset_test passed\n");
    return 0;
}
