#include "htime.h"

#include <stdio.h>
#include <string.h>

void msleep(unsigned long ms) {
#ifdef _MSC_VER
    Sleep(ms);
#else
    usleep(ms*1000);
#endif
}

uint64 gettick() {
#ifdef _MSC_VER
    return GetTickCount();
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec + tv.tv_usec/1000;
#endif
}

datetime_t get_datetime() {
    datetime_t  dt;
#ifdef _MSC_VER
    SYSTEMTIME tm;
    GetLocalTime(&tm);
    dt.year     = tm.wYear;
    dt.month    = tm.wMonth;
    dt.day      = tm.wDay;
    dt.hour     = tm.wHour;
    dt.min      = tm.wMinute;
    dt.sec      = tm.wSecond;
    dt.ms       = tm.wMilliseconds;
#else
    struct timeval tv;
    struct tm* tm = NULL;
    gettimeofday(&tv, NULL);
    time_t tt = tv.tv_sec;
    tm = localtime(&tt);

    dt.year     = tm->tm_year + 1900;
    dt.month    = tm->tm_mon  + 1;
    dt.day      = tm->tm_mday;
    dt.hour     = tm->tm_hour;
    dt.min      = tm->tm_min;
    dt.sec      = tm->tm_sec;
    dt.ms       = tv.tv_usec/1000;
#endif
    return dt;
}

static const char* s_month[] = {"January", "February", "March", "April", "May", "June",
    "July", "August", "September", "October", "November", "December"};

int month_atoi(const char* month) {
    for (int i = 0; i < ARRAY_SIZE(s_month); ++i) {
        if (strnicmp(month, s_month[i], strlen(month)) == 0)
            return i+1;
    }
    return 0;
}

const char* month_itoa(int month) {
    if (month < 1 || month > 12) {
        return NULL;
    }
    return s_month[month-1];
}

datetime_t get_compile_datetime() {
    static datetime_t dt;
    char month[32];
    sscanf(__DATE__, "%s %d %d", month, &dt.day, &dt.year);
    sscanf(__TIME__, "%d %d %d", &dt.hour, &dt.min, &dt.sec);
    dt.month = month_atoi(month);
    return dt;
}
