#include "htime.h"

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
    return clock() / (double)CLOCKS_PER_SEC * 1000000;
#endif
}

datetime_t get_datetime() {
    datetime_t  dt;
#ifdef OS_WIN
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

#include <string.h>
#ifdef _MSC_VER
    #define strcasecmp stricmp
    #define strncasecmp strnicmp
#else
    #include <strings.h>
    #define stricmp     strcasecmp
    #define strnicmp    strncasecmp
#endif
int month_atoi(const char* month) {
    for (size_t i = 0; i < 12; ++i) {
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
