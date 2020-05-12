#ifndef HV_TIME_H_
#define HV_TIME_H_

#include "hplatform.h"
#include "hdef.h"

BEGIN_EXTERN_C

#define SECONDS_PER_HOUR    3600
#define SECONDS_PER_DAY     86400   // 24*3600
#define SECONDS_PER_WEEK    604800  // 7*24*3600

#define IS_LEAP_YEAR(year) (((year)%4 == 0 && (year)%100 != 0) ||\
                            (year)%100 == 0)

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

static inline void usleep(unsigned int us) {
    Sleep(us/1000);
}

#ifdef _MSC_VER
/* @see winsock2.h
// Structure used in select() call, taken from the BSD file sys/time.h
struct timeval {
    long    tv_sec;
    long    tv_usec;
};
*/

struct timezone {
    int tz_minuteswest; /* of Greenwich */
    int tz_dsttime;     /* type of dst correction to apply */
};

#include <sys/timeb.h>
static inline int gettimeofday(struct timeval *tv, struct timezone *tz) {
    struct _timeb tb;
    _ftime(&tb);
    if (tv) {
        tv->tv_sec =  (long)tb.time;
        tv->tv_usec = tb.millitm * 1000;
    }
    if (tz) {
        tz->tz_minuteswest = tb.timezone;
        tz->tz_dsttime = tb.dstflag;
    }
    return 0;
}
#endif
#endif

static inline unsigned long long timestamp_ms() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * (unsigned long long)1000 + tv.tv_usec/1000;
}

void msleep(unsigned int ms);
// ms
unsigned int gettick();

// us
unsigned long long gethrtime_us();

datetime_t datetime_now();
time_t     datetime_mktime(datetime_t* dt);

#define DATETIME_FMT        "%04d-%02d-%02d %02d:%02d:%02d.%03d"
#define DATETIME_FMT_BUFLEN 24
char* datetime_fmt(datetime_t* dt, char* buf);

#define GMTIME_FMT          "%.3s, %02d %.3s %04d %02d:%02d:%02d GMT"
#define GMTIME_FMT_BUFLEN   30
char* gmtime_fmt(time_t time, char* buf);

datetime_t* datetime_past(datetime_t* dt, int days DEFAULT(1));
datetime_t* datetime_future(datetime_t* dt, int days DEFAULT(1));

/*
 * minute   hour    day     week    month       action
 * 0~59     0~23    1~31    0~6     1~12
 *  30      -1      -1      -1      -1          cron.hourly
 *  30      1       -1      -1      -1          cron.daily
 *  30      1       15      -1      -1          cron.monthly
 *  30      1       -1       7      -1          cron.weekly
 *  30      1        1      -1      10          cron.yearly
 */
time_t calc_next_timeout(int minute, int hour, int day, int week, int month);

int days_of_month(int month, int year);

int month_atoi(const char* month);
const char* month_itoa(int month);

int weekday_atoi(const char* weekday);
const char* weekday_itoa(int weekday);

datetime_t hv_compile_datetime();

END_EXTERN_C

#endif // HV_TIME_H_
