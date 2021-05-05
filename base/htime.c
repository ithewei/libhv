#include "htime.h"

static const char* s_weekdays[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};

static const char* s_months[] = {"January", "February", "March", "April", "May", "June",
    "July", "August", "September", "October", "November", "December"};

static const uint8_t s_days[] = \
//   1       3       5       7   8       10      12
    {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

unsigned int gettick_ms() {
#ifdef OS_WIN
    return GetTickCount();
#elif HAVE_CLOCK_GETTIME
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000 + tv.tv_usec / 1000;
#endif
}

unsigned long long gethrtime_us() {
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
        return (unsigned long long)(count.QuadPart / (double)s_freq * 1000000);
    }
    return 0;
#elif defined(OS_SOLARIS)
    return gethrtime() / 1000;
#elif HAVE_CLOCK_GETTIME
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec*(unsigned long long)1000000 + ts.tv_nsec / 1000;
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec*(unsigned long long)1000000 + tv.tv_usec;
#endif
}

datetime_t datetime_now() {
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

time_t datetime_mktime(datetime_t* dt) {
    struct tm tm;
    time_t ts;
    time(&ts);
    struct tm* ptm = localtime(&ts);
    memcpy(&tm, ptm, sizeof(struct tm));
    tm.tm_yday  = dt->year   - 1900;
    tm.tm_mon   = dt->month  - 1;
    tm.tm_mday  = dt->day;
    tm.tm_hour  = dt->hour;
    tm.tm_min   = dt->min;
    tm.tm_sec   = dt->sec;
    return mktime(&tm);
}

int days_of_month(int month, int year) {
    if (month < 1 || month > 12) {
        return 0;
    }
    int days = s_days[month-1];
    return (month == 2 && IS_LEAP_YEAR(year)) ? ++days : days;
}

datetime_t* datetime_past(datetime_t* dt, int days) {
    assert(days >= 0);
    int sub = days;
    while (sub) {
        if (dt->day > sub) {
            dt->day -= sub;
            break;
        }
        sub -= dt->day;
        if (--dt->month == 0) {
            dt->month = 12;
            --dt->year;
        }
        dt->day = days_of_month(dt->month, dt->year);
    }
    return dt;
}

datetime_t* datetime_future(datetime_t* dt, int days) {
    assert(days >= 0);
    int sub = days;
    int mdays;
    while (sub) {
        mdays = days_of_month(dt->month, dt->year);
        if (dt->day + sub <= mdays) {
            dt->day += sub;
            break;
        }
        sub -= (mdays - dt->day + 1);
        if (++dt->month > 12) {
            dt->month = 1;
            ++dt->year;
        }
        dt->day = 1;
    }
    return dt;
}

char* duration_fmt(int sec, char* buf) {
    int h, m, s;
    m = sec / 60;
    s = sec % 60;
    h = m / 60;
    m = m % 60;
    sprintf(buf, TIME_FMT, h, m, s);
    return buf;
}

char* datetime_fmt(datetime_t* dt, char* buf) {
    sprintf(buf, DATETIME_FMT,
        dt->year, dt->month, dt->day,
        dt->hour, dt->min, dt->sec);
    return buf;
}

char* gmtime_fmt(time_t time, char* buf) {
    struct tm* tm = gmtime(&time);
    //strftime(buf, GMTIME_FMT_BUFLEN, "%a, %d %b %Y %H:%M:%S GMT", tm);
    sprintf(buf, GMTIME_FMT,
        s_weekdays[tm->tm_wday],
        tm->tm_mday, s_months[tm->tm_mon], tm->tm_year + 1900,
        tm->tm_hour, tm->tm_min, tm->tm_sec);
    return buf;
}

int month_atoi(const char* month) {
    for (size_t i = 0; i < 12; ++i) {
        if (strnicmp(month, s_months[i], strlen(month)) == 0)
            return i+1;
    }
    return 0;
}

const char* month_itoa(int month) {
    assert(month >= 1 && month <= 12);
    return s_months[month-1];
}

int weekday_atoi(const char* weekday) {
    for (size_t i = 0; i < 7; ++i) {
        if (strnicmp(weekday, s_weekdays[i], strlen(weekday)) == 0)
            return i;
    }
    return 0;
}

const char* weekday_itoa(int weekday) {
    assert(weekday >= 0 && weekday <= 7);
    if (weekday == 7) weekday = 0;
    return s_weekdays[weekday];
}

datetime_t hv_compile_datetime() {
    datetime_t dt;
    char month[32];
    sscanf(__DATE__, "%s %d %d", month, &dt.day, &dt.year);
    sscanf(__TIME__, "%d:%d:%d", &dt.hour, &dt.min, &dt.sec);
    dt.month = month_atoi(month);
    return dt;
}

time_t cron_next_timeout(int minute, int hour, int day, int week, int month) {
    enum {
        UNKOWN,
        HOURLY,
        DAILY,
        WEEKLY,
        MONTHLY,
        YEARLY,
    } period_type = UNKOWN;
    struct tm tm;
    time_t tt;
    time(&tt);
    tm = *localtime(&tt);
    time_t tt_round = 0;

    tm.tm_sec = 0;
    if (minute >= 0) {
        period_type = HOURLY;
        tm.tm_min = minute;
    }
    if (hour >= 0) {
        period_type = DAILY;
        tm.tm_hour = hour;
    }
    if (week >= 0) {
        period_type = WEEKLY;
    }
    else if (day > 0) {
        period_type = MONTHLY;
        tm.tm_mday = day;
        if (month > 0) {
            period_type = YEARLY;
            tm.tm_mon = month - 1;
        }
    }

    if (period_type == UNKOWN) {
        return -1;
    }

    tt_round = mktime(&tm);
    if (week >= 0) {
        tt_round = tt + (week-tm.tm_wday)*SECONDS_PER_DAY;
    }
    if (tt_round > tt) {
        return tt_round;
    }

    switch(period_type) {
    case HOURLY:
        tt_round += SECONDS_PER_HOUR;
        return tt_round;
    case DAILY:
        tt_round += SECONDS_PER_DAY;
        return tt_round;
    case WEEKLY:
        tt_round += SECONDS_PER_WEEK;
        return tt_round;
    case MONTHLY:
        if (++tm.tm_mon == 12) {
            tm.tm_mon = 0;
            ++tm.tm_year;
        }
        break;
    case YEARLY:
        ++tm.tm_year;
        break;
    default:
        return -1;
    }

    return mktime(&tm);
}
