#include "hevent.h"
#include "hsocket.h"

/* Keep checks active in Release builds too. */
static void require(int condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "hloop_timer_fairness_test: %s\n", message);
        exit(EXIT_FAILURE);
    }
}

typedef struct {
    int turn;
    int timers;
    int reads;
    int posts;
} progress_t;

static void on_timer(htimer_t *timer) {
    progress_t *progress = (progress_t *)hevent_userdata(timer);
    ++progress->timers;
}

static void on_read(hio_t *io, void *buf, int size) {
    progress_t *progress = (progress_t *)hevent_userdata(io);
    require(size == 1 && *(char *)buf == 'x', "unexpected socket data");
    require(progress->timers == progress->turn, "timer priority changed relative to I/O");
    ++progress->reads;
}

static void on_post(hevent_t *event) {
    progress_t *progress = (progress_t *)hevent_userdata(event);
    require(progress->timers == progress->turn, "timer priority changed relative to posted callbacks");
    ++progress->posts;
}

static void set_deadline(htimer_t *timer, uint64_t deadline) {
    struct heap *timers = timer->event_type == HEVENT_TYPE_TIMEOUT ? &timer->loop->timers : &timer->loop->realtimers;
    heap_remove(timers, &timer->node);
    timer->next_timeout = deadline;
    heap_insert(timers, &timer->node);
}

/* Socketpair uses loopback TCP on Windows. Establish actual readiness before
 * forcing the timer overdue, without making any libhv event pending by hand. */
static void wait_readable(int fd) {
    fd_set readfds;
    struct timeval timeout;
#ifndef OS_WIN
    require(fd >= 0 && fd < FD_SETSIZE, "descriptor outside select range");
#endif
    FD_ZERO(&readfds);
    FD_SET(fd, &readfds);
    timeout.tv_sec = 2;
    timeout.tv_usec = 0;
    require(select(fd + 1, &readfds, NULL, NULL, &timeout) == 1, "descriptor did not become readable");
}

static void test_progress(int realtime) {
    enum { TURNS = 8 };
    int sockets[2];
    progress_t progress = {0};
    hevent_t event;
    hloop_t *loop = hloop_new(HLOOP_FLAG_RUN_ONCE);
    require(loop != NULL, "hloop_new failed");
    require(Socketpair(AF_INET, SOCK_STREAM, 0, sockets) == 0, "Socketpair failed");
    hio_t *io = hread(loop, sockets[0], NULL, 0, on_read);
    require(io != NULL, "hread failed");
    hevent_set_userdata(io, &progress);
    htimer_t *timer = realtime ? htimer_add_period(loop, on_timer, -1, -1, -1, -1, -1, INFINITE) : htimer_add(loop, on_timer, 1, INFINITE);
    require(timer != NULL, "timer creation failed");
    hevent_set_userdata(timer, &progress);
    /* Period timers normally share the wakeup descriptor's priority. Use an
     * explicit priority so discovery order cannot decide the expected order. */
    hevent_set_priority(timer, HEVENT_HIGHEST_PRIORITY);

    for (progress.turn = 1; progress.turn <= TURNS; ++progress.turn) {
        require(send(sockets[1], "x", 1, 0) == 1, "socket write failed");
        memset(&event, 0, sizeof(event));
        event.cb = on_post;
        event.userdata = &progress;
        hloop_post_event(loop, &event);
        wait_readable(sockets[0]);
        wait_readable(loop->eventfds[0]);

        /* Model a timer that is late on every turn, without timing-sensitive
         * sleeps or changing its positive repeat interval. */
        hloop_update_time(loop);
        set_deadline(timer, (realtime ? hloop_now_us(loop) : loop->cur_hrtime) - 1);
        require(hloop_run(loop) == 0, "hloop_run failed");
        require(progress.timers == progress.turn, "repeating timer did not fire once per turn");
        require(timer->repeat == INFINITE && !timer->destroy, "repeating timer was removed");
    }

    /* Check before removing the timer or allowing a non-overdue turn. The old
     * implementation runs all eight timers but never discovers either reader. */
    printf("%s timer progress: timers=%d reads=%d posts=%d\n", realtime ? "realtime" : "monotonic", progress.timers, progress.reads, progress.posts);
    require(progress.reads == TURNS, "overdue timers starved ready socket reads");
    require(progress.posts == TURNS, "overdue timers starved posted callbacks");
    htimer_del(timer);
    hio_close(io);
    closesocket(sockets[1]);
    hloop_free(&loop);
    puts(realtime ? "realtime timer progress passed" : "monotonic timer progress passed");
}

#ifdef HLOOP_TEST_WRAP
/* Static Linux builds also observe the actual backend timeout. A ready socket
 * alone would hide an accidental positive wait. Linker wrappers leave the
 * real-backend tests above unchanged and require no production test hooks. */
extern int hloop_process_events(hloop_t *loop, int timeout_ms);
extern int __real_iowatcher_poll_events(hloop_t *loop, int timeout_ms);
extern unsigned long long __real_gethrtime_us(void);
extern int __real_usleep(unsigned int usec);

static int probing;
static int poll_calls;
static int poll_timeout;
static int sleep_calls;
static unsigned int sleep_timeout;
static int stop_in_poll;
static unsigned long long clock_us;

unsigned long long __wrap_gethrtime_us(void) {
    return probing ? clock_us : __real_gethrtime_us();
}

int __wrap_iowatcher_poll_events(hloop_t *loop, int timeout_ms) {
    if (!probing) {
        return __real_iowatcher_poll_events(loop, timeout_ms);
    }
    ++poll_calls;
    poll_timeout = timeout_ms;
    ++clock_us;
    if (stop_in_poll) {
        loop->status = HLOOP_STATUS_STOP;
    }
    return 0;
}

int __wrap_usleep(unsigned int usec) {
    if (!probing) {
        return __real_usleep(usec);
    }
    ++sleep_calls;
    sleep_timeout = usec;
    ++clock_us;
    return 0;
}

static void test_timeout(int offset_us, int caller_timeout, int expected_timeout, int with_io, int stop) {
    progress_t progress = {0};
    clock_us = __real_gethrtime_us();
    probing = 1;
    poll_calls = sleep_calls = 0;
    poll_timeout = -1;
    sleep_timeout = (unsigned int)-1;
    stop_in_poll = stop;
    hloop_t *loop = hloop_new(0);
    require(loop != NULL, "probe loop creation failed");
    htimer_t *timer = htimer_add(loop, on_timer, 1, INFINITE);
    require(timer != NULL, "probe timer creation failed");
    hevent_set_userdata(timer, &progress);
    set_deadline(timer, clock_us + offset_us);
    if (with_io) {
        /* Register the real wakeup descriptor, but intercept only its poll. */
        hloop_wakeup(loop);
        require(loop->nios > 0, "probe has no registered descriptor");
    }
    loop->status = HLOOP_STATUS_RUNNING;
    hloop_process_events(loop, caller_timeout);
    if (with_io) {
        require(poll_calls == 1 && sleep_calls == 0, "timer bypassed backend polling");
        require(poll_timeout == expected_timeout, "incorrect backend timeout");
    }
    else {
        require(poll_calls == 0 && sleep_calls == 1, "incorrect no-I/O wait path");
        require(sleep_timeout == (unsigned int)expected_timeout * 1000, "incorrect no-I/O sleep duration");
    }
    require(loop->cur_hrtime == clock_us, "loop time was not refreshed after polling");
    require(progress.timers == (!stop && offset_us <= 1), "unexpected timer callback dispatch");
    hloop_free(&loop);
    probing = 0;
}

static void test_timeouts(void) {
    test_timeout(-1, 100, 0, 1, 0);
    test_timeout(0, 100, 0, 1, 0);
    test_timeout(1, 100, 1, 1, 0);
    test_timeout(999, 100, 1, 1, 0);
    test_timeout(1000, 100, 2, 1, 0);
    test_timeout(1001, 100, 2, 1, 0);
    test_timeout(200000, 100, 100, 1, 0);
    test_timeout(1000, 0, 0, 1, 0);
    test_timeout(-1, 100, 0, 0, 0);
    test_timeout(0, 100, 0, 0, 0);
    test_timeout(-1, 100, 0, 1, 1);
    puts("poll timeout, no-I/O wait, time refresh and STOP checks passed");
}
#endif

int main(void) {
    test_progress(0);
    test_progress(1);
#ifdef HLOOP_TEST_WRAP
    test_timeouts();
#endif
    puts("hloop_timer_fairness_test passed");
    return 0;
}
