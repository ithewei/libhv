#ifndef HW_TIME_H_
#define HW_TIME_H_

#include <time.h>

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

inline unsigned int gettick() {
#ifdef OS_WIN
    return GetTickCount();
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec*1000 + tv.tv_usec/1000;
#endif
}

int month_atoi(const char* month);
const char* month_itoa(int month);

datetime_t get_datetime();
datetime_t get_compile_datetime();

#endif  // HW_TIME_H_
