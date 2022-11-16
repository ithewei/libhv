#include "hlog.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>

//#include "hmutex.h"
#ifdef _WIN32
#pragma warning (disable: 4244) // conversion loss of data
#include <windows.h>
#define hmutex_t            CRITICAL_SECTION
#define hmutex_init         InitializeCriticalSection
#define hmutex_destroy      DeleteCriticalSection
#define hmutex_lock         EnterCriticalSection
#define hmutex_unlock       LeaveCriticalSection
#else
#include <sys/time.h>       // for gettimeofday
#include <pthread.h>
#define hmutex_t            pthread_mutex_t
#define hmutex_init(mutex)  pthread_mutex_init(mutex, NULL)
#define hmutex_destroy      pthread_mutex_destroy
#define hmutex_lock         pthread_mutex_lock
#define hmutex_unlock       pthread_mutex_unlock
#endif

//#include "htime.h"
#define SECONDS_PER_HOUR    3600
#define SECONDS_PER_DAY     86400   // 24*3600
#define SECONDS_PER_WEEK    604800  // 7*24*3600;

static int s_gmtoff = 28800; // 8*3600

struct logger_s {
    logger_handler  handler;
    unsigned int    bufsize;
    char*           buf;

    int             level;
    int             enable_color;
    char            format[64];

    // for file logger
    char                filepath[256];
    unsigned long long  max_filesize;
    int                 remain_days;
    int                 enable_fsync;
    FILE*               fp_;
    char                cur_logfile[256];
    time_t              last_logfile_ts;
    int                 can_write_cnt;

    hmutex_t            mutex_; // thread-safe
};

static void logger_init(logger_t* logger) {
    logger->handler = NULL;
    logger->bufsize = DEFAULT_LOG_MAX_BUFSIZE;
    logger->buf = (char*)malloc(logger->bufsize);

    logger->level = DEFAULT_LOG_LEVEL;
    logger->enable_color = 0;
    // NOTE: format is faster 6% than snprintf
    // logger->format[0] = '\0';
    strncpy(logger->format, DEFAULT_LOG_FORMAT, sizeof(logger->format) - 1);

    logger->fp_ = NULL;
    logger->max_filesize = DEFAULT_LOG_MAX_FILESIZE;
    logger->remain_days = DEFAULT_LOG_REMAIN_DAYS;
    logger->enable_fsync = 1;
    logger_set_file(logger, DEFAULT_LOG_FILE);
    logger->last_logfile_ts = 0;
    logger->can_write_cnt = -1;
    hmutex_init(&logger->mutex_);
}

logger_t* logger_create() {
    // init gmtoff here
    time_t ts = time(NULL);
    struct tm* local_tm = localtime(&ts);
    int local_hour = local_tm->tm_hour;
    struct tm* gmt_tm = gmtime(&ts);
    int gmt_hour = gmt_tm->tm_hour;
    s_gmtoff = (local_hour - gmt_hour) * SECONDS_PER_HOUR;

    logger_t* logger = (logger_t*)malloc(sizeof(logger_t));
    logger_init(logger);
    return logger;
}

void logger_destroy(logger_t* logger) {
    if (logger) {
        if (logger->buf) {
            free(logger->buf);
            logger->buf = NULL;
        }
        if (logger->fp_) {
            fclose(logger->fp_);
            logger->fp_ = NULL;
        }
        hmutex_destroy(&logger->mutex_);
        free(logger);
    }
}

void logger_set_handler(logger_t* logger, logger_handler fn) {
    logger->handler = fn;
}

void logger_set_level(logger_t* logger, int level) {
    logger->level = level;
}

void logger_set_level_by_str(logger_t* logger, const char* szLoglevel) {
    int loglevel = DEFAULT_LOG_LEVEL;
    if (strcmp(szLoglevel, "VERBOSE") == 0) {
        loglevel = LOG_LEVEL_VERBOSE;
    } else if (strcmp(szLoglevel, "DEBUG") == 0) {
        loglevel = LOG_LEVEL_DEBUG;
    } else if (strcmp(szLoglevel, "INFO") == 0) {
        loglevel = LOG_LEVEL_INFO;
    } else if (strcmp(szLoglevel, "WARN") == 0) {
        loglevel = LOG_LEVEL_WARN;
    } else if (strcmp(szLoglevel, "ERROR") == 0) {
        loglevel = LOG_LEVEL_ERROR;
    } else if (strcmp(szLoglevel, "FATAL") == 0) {
        loglevel = LOG_LEVEL_FATAL;
    } else if (strcmp(szLoglevel, "SILENT") == 0) {
        loglevel = LOG_LEVEL_SILENT;
    } else {
        loglevel = DEFAULT_LOG_LEVEL;
    }
    logger->level = loglevel;
}

void logger_set_format(logger_t* logger, const char* format) {
    if (format) {
        strncpy(logger->format, format, sizeof(logger->format) - 1);
    } else {
        logger->format[0] = '\0';
    }
}

void logger_set_remain_days(logger_t* logger, int days) {
    logger->remain_days = days;
}

void logger_set_max_bufsize(logger_t* logger, unsigned int bufsize) {
    logger->bufsize = bufsize;
    logger->buf = (char*)realloc(logger->buf, bufsize);
}

void logger_enable_color(logger_t* logger, int on) {
    logger->enable_color = on;
}

void logger_set_file(logger_t* logger, const char* filepath) {
    strncpy(logger->filepath, filepath, sizeof(logger->filepath) - 1);
    // remove suffix .log
    char* suffix = strrchr(logger->filepath, '.');
    if (suffix && strcmp(suffix, ".log") == 0) {
        *suffix = '\0';
    }
}

void logger_set_max_filesize(logger_t* logger, unsigned long long filesize) {
    logger->max_filesize = filesize;
}

void logger_set_max_filesize_by_str(logger_t* logger, const char* str) {
    int num = atoi(str);
    if (num <= 0) return;
    // 16 16M 16MB
    const char* e = str;
    while (*e != '\0') ++e;
    --e;
    char unit;
    if (*e >= '0' && *e <= '9') unit = 'M';
    else if (*e == 'B')         unit = *(e-1);
    else                        unit = *e;
    unsigned long long filesize = num;
    switch (unit) {
    case 'K': filesize <<= 10; break;
    case 'M': filesize <<= 20; break;
    case 'G': filesize <<= 30; break;
    default:  filesize <<= 20; break;
    }
    logger->max_filesize = filesize;
}

void logger_enable_fsync(logger_t* logger, int on) {
    logger->enable_fsync = on;
}

void logger_fsync(logger_t* logger) {
    hmutex_lock(&logger->mutex_);
    if (logger->fp_) {
        fflush(logger->fp_);
    }
    hmutex_unlock(&logger->mutex_);
}

const char* logger_get_cur_file(logger_t* logger) {
    return logger->cur_logfile;
}

static void logfile_name(const char* filepath, time_t ts, char* buf, int len) {
    struct tm* tm = localtime(&ts);
    snprintf(buf, len, "%s.%04d%02d%02d.log",
            filepath,
            tm->tm_year+1900,
            tm->tm_mon+1,
            tm->tm_mday);
}

static FILE* logfile_shift(logger_t* logger) {
    time_t ts_now = time(NULL);
    int interval_days = logger->last_logfile_ts == 0 ? 0 : (ts_now+s_gmtoff) / SECONDS_PER_DAY - (logger->last_logfile_ts+s_gmtoff) / SECONDS_PER_DAY;
    if (logger->fp_ == NULL || interval_days > 0) {
        // close old logfile
        if (logger->fp_) {
            fclose(logger->fp_);
            logger->fp_ = NULL;
        }
        else {
            interval_days = 30;
        }

        if (logger->remain_days >= 0) {
            char rm_logfile[256] = {0};
            if (interval_days >= logger->remain_days) {
                // remove [today-interval_days, today-remain_days] logfile
                for (int i = interval_days; i >= logger->remain_days; --i) {
                    time_t ts_rm  = ts_now - i * SECONDS_PER_DAY;
                    logfile_name(logger->filepath, ts_rm, rm_logfile, sizeof(rm_logfile));
                    remove(rm_logfile);
                }
            }
            else {
                // remove today-remain_days logfile
                time_t ts_rm  = ts_now - logger->remain_days * SECONDS_PER_DAY;
                logfile_name(logger->filepath, ts_rm, rm_logfile, sizeof(rm_logfile));
                remove(rm_logfile);
            }
        }
    }

    // open today logfile
    if (logger->fp_ == NULL) {
        logfile_name(logger->filepath, ts_now, logger->cur_logfile, sizeof(logger->cur_logfile));
        logger->fp_ = fopen(logger->cur_logfile, "a");
        logger->last_logfile_ts = ts_now;
    }

    // NOTE: estimate can_write_cnt to avoid frequent fseek/ftell
    if (logger->fp_ && --logger->can_write_cnt < 0) {
        fseek(logger->fp_, 0, SEEK_END);
        long filesize = ftell(logger->fp_);
        if (filesize > logger->max_filesize) {
            fclose(logger->fp_);
            logger->fp_ = NULL;
            // ftruncate
            logger->fp_ = fopen(logger->cur_logfile, "w");
            // reopen with O_APPEND for multi-processes
            if (logger->fp_) {
                fclose(logger->fp_);
                logger->fp_ = fopen(logger->cur_logfile, "a");
            }
        }
        else {
            logger->can_write_cnt = (logger->max_filesize - filesize) / logger->bufsize;
        }
    }

    return logger->fp_;
}

static void logfile_write(logger_t* logger, const char* buf, int len) {
    FILE* fp = logfile_shift(logger);
    if (fp) {
        fwrite(buf, 1, len, fp);
        if (logger->enable_fsync) {
            fflush(fp);
        }
    }
}

static int i2a(int i, char* buf, int len) {
    for (int l = len - 1; l >= 0; --l) {
        if (i == 0) {
            buf[l] = '0';
        } else {
            buf[l] = i % 10 + '0';
            i /= 10;
        }
    }
    return len;
}

int logger_print(logger_t* logger, int level, const char* fmt, ...) {
    if (level < logger->level)
        return -10;

    int year,month,day,hour,min,sec,us;
#ifdef _WIN32
    SYSTEMTIME tm;
    GetLocalTime(&tm);
    year     = tm.wYear;
    month    = tm.wMonth;
    day      = tm.wDay;
    hour     = tm.wHour;
    min      = tm.wMinute;
    sec      = tm.wSecond;
    us       = tm.wMilliseconds * 1000;
#else
    struct timeval tv;
    struct tm* tm = NULL;
    gettimeofday(&tv, NULL);
    time_t tt = tv.tv_sec;
    tm = localtime(&tt);
    year     = tm->tm_year + 1900;
    month    = tm->tm_mon  + 1;
    day      = tm->tm_mday;
    hour     = tm->tm_hour;
    min      = tm->tm_min;
    sec      = tm->tm_sec;
    us       = tv.tv_usec;
#endif

    const char* pcolor = "";
    const char* plevel = "";
#define XXX(id, str, clr) \
    case id: plevel = str; pcolor = clr; break;

    switch (level) {
        LOG_LEVEL_MAP(XXX)
    }
#undef XXX

    // lock logger->buf
    hmutex_lock(&logger->mutex_);

    char* buf = logger->buf;
    int bufsize = logger->bufsize;
    int len = 0;

    if (logger->enable_color) {
        len = snprintf(buf, bufsize, "%s", pcolor);
    }

    const char* p = logger->format;
    if (*p) {
        while (*p) {
            if (*p == '%') {
                switch(*++p) {
                case 'y':
                    len += i2a(year, buf + len, 4);
                    break;
                case 'm':
                    len += i2a(month, buf + len, 2);
                    break;
                case 'd':
                    len += i2a(day, buf + len, 2);
                    break;
                case 'H':
                    len += i2a(hour, buf + len, 2);
                    break;
                case 'M':
                    len += i2a(min, buf + len, 2);
                    break;
                case 'S':
                    len += i2a(sec, buf + len, 2);
                    break;
                case 'z':
                    len += i2a(us/1000, buf + len, 3);
                    break;
                case 'Z':
                    len += i2a(us, buf + len, 6);
                    break;
                case 'l':
                    buf[len++] = *plevel;
                    break;
                case 'L':
                    for (int i = 0; i < 5; ++i) {
                        buf[len++] = plevel[i];
                    }
                    break;
                case 's':
                {
                    va_list ap;
                    va_start(ap, fmt);
                    len += vsnprintf(buf + len, bufsize - len, fmt, ap);
                    va_end(ap);
                }
                    break;
                case '%':
                    buf[len++] = '%';
                    break;
                default: break;
                }
            } else {
                buf[len++] = *p;
            }
            ++p;
        }
    } else {
        len += snprintf(buf + len, bufsize - len, "%04d-%02d-%02d %02d:%02d:%02d.%03d %s ",
            year, month, day, hour, min, sec, us/1000,
            plevel);

        va_list ap;
        va_start(ap, fmt);
        len += vsnprintf(buf + len, bufsize - len, fmt, ap);
        va_end(ap);
    }

    if (logger->enable_color) {
        len += snprintf(buf + len, bufsize - len, "%s", CLR_CLR);
    }

    if (logger->handler) {
        logger->handler(level, buf, len);
    }
    else {
        logfile_write(logger, buf, len);
    }

    hmutex_unlock(&logger->mutex_);
    return len;
}

static logger_t* s_logger = NULL;
logger_t* hv_default_logger() {
    if (s_logger == NULL) {
        s_logger = logger_create();
        atexit(hv_destroy_default_logger);
    }
    return s_logger;
}
void hv_destroy_default_logger(void) {
    if (s_logger) {
        logger_fsync(s_logger);
        logger_destroy(s_logger);
        s_logger = NULL;
    }
}

void stdout_logger(int loglevel, const char* buf, int len) {
    fprintf(stdout, "%.*s", len, buf);
}

void stderr_logger(int loglevel, const char* buf, int len) {
    fprintf(stderr, "%.*s", len, buf);
}

void file_logger(int loglevel, const char* buf, int len) {
    logfile_write(hv_default_logger(), buf, len);
}
