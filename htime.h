#ifndef HW_TIME_H_
#define HW_TIME_H_

#include <time.h>

#include "hdef.h"
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

void msleep(unsigned long ms);

#ifdef _WIN32
inline void sleep(unsigned int s) {
    Sleep(s*1000);
}
#endif

uint64 gettick();

int month_atoi(const char* month);
const char* month_itoa(int month);

datetime_t get_datetime();
datetime_t get_compile_datetime();

#endif  // HW_TIME_H_
