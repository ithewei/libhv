#ifndef HW_TIME_H_
#define HW_TIME_H_

#ifdef __cplusplus
extern "C" {
#endif

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
static inline void sleep(unsigned int s) {
    Sleep(s*1000);
}
#endif

static inline void msleep(unsigned int ms) {
#ifdef OS_WIN
    Sleep(ms);
#else
    usleep(ms*1000);
#endif
}

// ms
static inline unsigned int gettick() {
#ifdef OS_WIN
    return GetTickCount();
#else
    return clock()*(unsigned long long)1000 / CLOCKS_PER_SEC;
#endif
}

// us
unsigned long long gethrtime();

datetime_t get_datetime();
datetime_t get_compile_datetime();

int month_atoi(const char* month);
const char* month_itoa(int month);

#ifdef __cplusplus
} // extern "C"
#endif

#endif  // HW_TIME_H_
