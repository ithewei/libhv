#ifndef HW_TIME_H_
#define HW_TIME_H_

#include "hplatform.h"

typedef struct datetime_s {
    int year;
    int month;
    int day;
    int hour;
    int min;
    int sec;
    int ms;
} datetime_t;

#ifdef OS_WIN
inline void sleep(unsigned int s) {
    Sleep(s*1000);
}
#endif

inline void msleep(unsigned int ms) {
#ifdef OS_WIN
    Sleep(ms);
#else
    usleep(ms*1000);
#endif
}

// ms
inline unsigned int gettick() {
#ifdef OS_WIN
    return GetTickCount();
#else
    return clock()*(unsigned long long)1000 / CLOCKS_PER_SEC;
#endif
}

// us
inline unsigned long long gethrtime() {
#ifdef OS_WIN
    static LONGLONG s_freq = 0;
    if (s_freq == 0) {
        LARGE_INTEGER freq;
        QueryPerformanceFrequency(&freq);
        s_freq = freq.QuadPart;
    }
    if (s_freq != 0) {
        LARGE_INTEGER count;
        QueryPerformanceCounter(&count);
        return count.QuadPart / (double)s_freq * 1000000;
    }
    return 0;
#elif defined(OS_LINUX)
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec*(unsigned long long)1000000 + ts.tv_nsec / 1000;
#else
    return clock()* / (double)CLOCKS_PER_SEC * 1000000;
#endif
}

int month_atoi(const char* month);
const char* month_itoa(int month);

datetime_t get_datetime();
datetime_t get_compile_datetime();

#endif  // HW_TIME_H_
