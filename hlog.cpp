#include "hlog.h"

#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <mutex>

#include "htime.h"  // for get_datetime

#define SECONDS_PER_DAY     86400

static char     s_logfile[256] = DEFAULT_LOG_FILE;
static int      s_loglevel = DEFAULT_LOG_LEVEL;
static bool     s_logcolor = false;
static int      s_remain_days = DEFAULT_LOG_REMAIN_DAYS;

static FILE*    s_logfp = NULL;
static char     s_logbuf[LOG_BUFSIZE];
static char     s_cur_logfile[256] = {0};
static std::mutex s_mutex;

static void ts_logfile(time_t ts, char* buf, int len) {
    struct tm* tm = localtime(&ts);
    snprintf(buf, len, "%s-%04d-%02d-%02d.log",
            s_logfile,
            tm->tm_year+1900,
            tm->tm_mon+1,
            tm->tm_mday);
}

int hlog_set_file(const char* logfile) {
    if (logfile == NULL || strlen(logfile) == 0)    return -10;

    strncpy(s_logfile, logfile, sizeof(s_logfile));
    // remove suffix .log
    char* suffix = strrchr(s_logfile, '.');
    if (suffix && strcmp(suffix, ".log") == 0) {
        *suffix = '\0';
    }

    time_t ts_now = time(NULL);
    ts_logfile(ts_now, s_cur_logfile, sizeof(s_cur_logfile));
    if (s_logfp) {
        fclose(s_logfp);
        s_logfp = NULL;
    }
    s_logfp = fopen(s_cur_logfile, "a");

    // remove logfile before s_remain_days
    if (s_remain_days > 0) {
        time_t ts_rm  = ts_now - s_remain_days * SECONDS_PER_DAY;
        char rm_logfile[256] = {0};
        ts_logfile(ts_rm, rm_logfile, sizeof(rm_logfile));
        remove(rm_logfile);
    }

    return s_logfp ? 0 : -1;
}

void hlog_set_level(int level) {
    s_loglevel = level;
}

void hlog_set_remain_days(int days) {
    s_remain_days = days;
}

void hlog_enable_color(bool enable) {
    s_logcolor = enable;
}

int hlog_printf(int level, const char* fmt, ...) {
    if (level < s_loglevel)
        return -10;

    const char* pcolor = "";
    const char* plevel = "";
#define CASE_LOG(id, str, clr) \
    case id: plevel = str; pcolor = clr; break;

    switch (level) {
        FOREACH_LOG(CASE_LOG)
    }
#undef CASE_LOG

    if (!s_logcolor)
        pcolor = "";

    std::lock_guard<std::mutex> locker(s_mutex);

    if (!s_logfp) {
        if (hlog_set_file(s_logfile) != 0)
            return -20;
    }

    if (ftell(s_logfp) > MAX_LOG_FILESIZE) {
        fclose(s_logfp);
        s_logfp = fopen(s_cur_logfile, "w");
        if (!s_logfp)
            return -30;
    }

    datetime_t now = get_datetime();
    int len = snprintf(s_logbuf, LOG_BUFSIZE, "%s[%04d-%02d-%02d %02d:%02d:%02d.%03d][%s]: ",
        pcolor, now.year, now.month, now.day, now.hour, now.min, now.sec, now.ms, plevel);

    va_list ap;
    va_start(ap, fmt);
    len += vsnprintf(s_logbuf + len, LOG_BUFSIZE-len, fmt, ap);
    va_end(ap);

    fprintf(s_logfp, "%s\n", s_logbuf);
    if (s_logcolor) {
        fprintf(s_logfp, CL_CLR);
    }

    fflush(s_logfp);

    return len;
}
