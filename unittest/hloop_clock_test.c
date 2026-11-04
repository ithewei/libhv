#include "hevent.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static int failures;

/* Keep checks active in Release/NDEBUG builds. */
static void check_equal(const char *name, uint64_t actual, uint64_t expected) {
    if (actual != expected) {
        fprintf(stderr, "%s: got %" PRIu64 ", expected %" PRIu64 "\n", name, actual, expected);
        ++failures;
    }
}

static void test_cached_time(void) {
    static const struct {
        uint64_t start_ms;
        uint64_t elapsed_us;
        uint64_t expected_us;
    } cases[] = {
        {100900, 200000, 101100000},  {100900, 99999, 100999999},  {100900, 100000, 101000000},
        {100900, 1100000, 102000000}, {100000, 999999, 100999999}, {100000, 1000000, 101000000},
    };
    unsigned int i;
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        hloop_t loop;
        memset(&loop, 0, sizeof(loop));
        loop.start_ms = cases[i].start_ms;
        loop.start_hrtime = 10000000;
        loop.cur_hrtime = loop.start_hrtime + cases[i].elapsed_us;
        check_equal("cached seconds", hloop_now(&loop), cases[i].expected_us / 1000000);
        check_equal("cached milliseconds", hloop_now_ms(&loop), cases[i].expected_us / 1000);
        check_equal("cached microseconds", hloop_now_us(&loop), cases[i].expected_us);
        check_equal("cached high-resolution time", hloop_now_hrtime(&loop), loop.cur_hrtime);
    }
}

#ifdef HLOOP_CLOCK_TEST_WRAPPED
/* Linux static-library tests replace clock sources without changing host time. */
static uint64_t wall_ms;
static uint64_t hr_us;

unsigned long long __wrap_gethrtime_us(void) {
    return hr_us;
}

int __wrap_gettimeofday(struct timeval *tv, void *tz) {
    (void)tz;
    tv->tv_sec = (time_t)(wall_ms / 1000);
    tv->tv_usec = (long)((wall_ms % 1000) * 1000);
    return 0;
}

time_t __wrap_time(time_t *out) {
    time_t now = (time_t)(wall_ms / 1000);
    if (out) *out = now;
    return now;
}

static void test_second_boundary(void) {
    hloop_t loop;
    uint64_t first, second, third;
    memset(&loop, 0, sizeof(loop));
    loop.start_ms = 100900;
    loop.start_hrtime = 10000000;

    /* The wall sample follows the monotonic sample by 2 ms on the first
     * update. Both clock sources then advance normally across a second. */
    hr_us = loop.start_hrtime + 100000;
    wall_ms = 101002;
    hloop_update_time(&loop);
    first = hloop_now_ms(&loop);
    hr_us = loop.start_hrtime + 1099000;
    wall_ms = 101999;
    hloop_update_time(&loop);
    second = hloop_now_ms(&loop);
    hr_us = loop.start_hrtime + 1100000;
    wall_ms = 102000;
    hloop_update_time(&loop);
    third = hloop_now_ms(&loop);

    if (third < second) {
        fprintf(stderr, "cached time moved backward from %" PRIu64 " to %" PRIu64 " ms\n", second, third);
        ++failures;
    }
    check_equal("first boundary sample", first, 101000);
    check_equal("second boundary sample", second, 101999);
    check_equal("third boundary sample", third, 102000);
    check_equal("unchanged wall-clock origin", loop.start_ms, 100900);
    check_equal("unchanged high-resolution origin", loop.start_hrtime, 10000000);
    check_equal("high-resolution sample", hloop_now_hrtime(&loop), hr_us);
}

static void test_clock_adjustments(void) {
    static const struct {
        int drift_ms;
        int correction_ms;
    } cases[] = {{0, 0}, {1, 0}, {-1, 0}, {999, 0}, {-999, 0}, {1000, 1000}, {-1000, -1000}, {5000, 5000}, {-5000, -5000}};
    unsigned int i;
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        hloop_t loop;
        uint64_t expected_ms = (uint64_t)(101100 + cases[i].correction_ms);
        memset(&loop, 0, sizeof(loop));
        loop.start_ms = 100900;
        loop.start_hrtime = 10000000;
        hr_us = loop.start_hrtime + 200123;
        wall_ms = (uint64_t)(101100 + cases[i].drift_ms);
        hloop_update_time(&loop);

        check_equal("adjusted milliseconds", hloop_now_ms(&loop), expected_ms);
        check_equal("adjusted seconds", hloop_now(&loop), expected_ms / 1000);
        check_equal("adjusted microseconds", hloop_now_us(&loop), expected_ms * 1000 + 123);
        check_equal("wall-clock origin adjustment", loop.start_ms, (uint64_t)(100900 + cases[i].correction_ms));
        check_equal("unchanged high-resolution origin", loop.start_hrtime, 10000000);
        check_equal("high-resolution sample", hloop_now_hrtime(&loop), hr_us);
    }
}
#endif

int main(void) {
#ifdef HLOOP_CLOCK_TEST_WRAPPED
    test_second_boundary();
    test_clock_adjustments();
#endif
    test_cached_time();
    if (failures) return 1;
#ifdef HLOOP_CLOCK_TEST_WRAPPED
    puts("hloop_clock_test passed (cached time and deterministic clock updates)");
#else
    puts("hloop_clock_test passed (cached time; clock-update injection requires Linux and a static library)");
#endif
    return 0;
}
