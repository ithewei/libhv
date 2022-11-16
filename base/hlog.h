#ifndef HV_LOG_H_
#define HV_LOG_H_

/*
 * hlog is thread-safe
 */

#include <string.h>

#ifdef _WIN32
#define DIR_SEPARATOR       '\\'
#define DIR_SEPARATOR_STR   "\\"
#else
#define DIR_SEPARATOR       '/'
#define DIR_SEPARATOR_STR   "/"
#endif

#ifndef __FILENAME__
// #define __FILENAME__  (strrchr(__FILE__, DIR_SEPARATOR) ? strrchr(__FILE__, DIR_SEPARATOR) + 1 : __FILE__)
#define __FILENAME__  (strrchr(DIR_SEPARATOR_STR __FILE__, DIR_SEPARATOR) + 1)
#endif

#include "hexport.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CLR_CLR         "\033[0m"       /* 恢复颜色 */
#define CLR_BLACK       "\033[30m"      /* 黑色字 */
#define CLR_RED         "\033[31m"      /* 红色字 */
#define CLR_GREEN       "\033[32m"      /* 绿色字 */
#define CLR_YELLOW      "\033[33m"      /* 黄色字 */
#define CLR_BLUE        "\033[34m"      /* 蓝色字 */
#define CLR_PURPLE      "\033[35m"      /* 紫色字 */
#define CLR_SKYBLUE     "\033[36m"      /* 天蓝字 */
#define CLR_WHITE       "\033[37m"      /* 白色字 */

#define CLR_BLK_WHT     "\033[40;37m"   /* 黑底白字 */
#define CLR_RED_WHT     "\033[41;37m"   /* 红底白字 */
#define CLR_GREEN_WHT   "\033[42;37m"   /* 绿底白字 */
#define CLR_YELLOW_WHT  "\033[43;37m"   /* 黄底白字 */
#define CLR_BLUE_WHT    "\033[44;37m"   /* 蓝底白字 */
#define CLR_PURPLE_WHT  "\033[45;37m"   /* 紫底白字 */
#define CLR_SKYBLUE_WHT "\033[46;37m"   /* 天蓝底白字 */
#define CLR_WHT_BLK     "\033[47;30m"   /* 白底黑字 */

// XXX(id, str, clr)
#define LOG_LEVEL_MAP(XXX) \
    XXX(LOG_LEVEL_DEBUG, "DEBUG", CLR_WHITE)     \
    XXX(LOG_LEVEL_INFO,  "INFO ", CLR_GREEN)     \
    XXX(LOG_LEVEL_WARN,  "WARN ", CLR_YELLOW)    \
    XXX(LOG_LEVEL_ERROR, "ERROR", CLR_RED)       \
    XXX(LOG_LEVEL_FATAL, "FATAL", CLR_RED_WHT)

typedef enum {
    LOG_LEVEL_VERBOSE = 0,
#define XXX(id, str, clr) id,
    LOG_LEVEL_MAP(XXX)
#undef  XXX
    LOG_LEVEL_SILENT
} log_level_e;

#define DEFAULT_LOG_FILE            "libhv"
#define DEFAULT_LOG_LEVEL           LOG_LEVEL_INFO
#define DEFAULT_LOG_FORMAT          "%y-%m-%d %H:%M:%S.%z %L %s"
#define DEFAULT_LOG_REMAIN_DAYS     1
#define DEFAULT_LOG_MAX_BUFSIZE     (1<<14)  // 16k
#define DEFAULT_LOG_MAX_FILESIZE    (1<<24)  // 16M

// logger: default file_logger
// network_logger() see event/nlog.h
typedef void (*logger_handler)(int loglevel, const char* buf, int len);

HV_EXPORT void stdout_logger(int loglevel, const char* buf, int len);
HV_EXPORT void stderr_logger(int loglevel, const char* buf, int len);
HV_EXPORT void file_logger(int loglevel, const char* buf, int len);
// network_logger implement see event/nlog.h
// HV_EXPORT void network_logger(int loglevel, const char* buf, int len);

typedef struct logger_s logger_t;
HV_EXPORT logger_t* logger_create();
HV_EXPORT void logger_destroy(logger_t* logger);

HV_EXPORT void logger_set_handler(logger_t* logger, logger_handler fn);
HV_EXPORT void logger_set_level(logger_t* logger, int level);
// level = [VERBOSE,DEBUG,INFO,WARN,ERROR,FATAL,SILENT]
HV_EXPORT void logger_set_level_by_str(logger_t* logger, const char* level);
/*
 * format  = "%y-%m-%d %H:%M:%S.%z %L %s"
 * message = "2020-01-02 03:04:05.067 DEBUG message"
 * %y year
 * %m month
 * %d day
 * %H hour
 * %M min
 * %S sec
 * %z ms
 * %Z us
 * %l First character of level
 * %L All characters of level
 * %s message
 * %% %
 */
HV_EXPORT void logger_set_format(logger_t* logger, const char* format);
HV_EXPORT void logger_set_max_bufsize(logger_t* logger, unsigned int bufsize);
HV_EXPORT void logger_enable_color(logger_t* logger, int on);
HV_EXPORT int  logger_print(logger_t* logger, int level, const char* fmt, ...);

// below for file logger
HV_EXPORT void logger_set_file(logger_t* logger, const char* filepath);
HV_EXPORT void logger_set_max_filesize(logger_t* logger, unsigned long long filesize);
// 16, 16M, 16MB
HV_EXPORT void logger_set_max_filesize_by_str(logger_t* logger, const char* filesize);
HV_EXPORT void logger_set_remain_days(logger_t* logger, int days);
HV_EXPORT void logger_enable_fsync(logger_t* logger, int on);
HV_EXPORT void logger_fsync(logger_t* logger);
HV_EXPORT const char* logger_get_cur_file(logger_t* logger);

// hlog: default logger instance
HV_EXPORT logger_t* hv_default_logger();
HV_EXPORT void      hv_destroy_default_logger(void);

// macro hlog*
#define hlog                            hv_default_logger()
#define hlog_destory()                  hv_destroy_default_logger()
#define hlog_disable()                  logger_set_level(hlog, LOG_LEVEL_SILENT)
#define hlog_set_file(filepath)         logger_set_file(hlog, filepath)
#define hlog_set_level(level)           logger_set_level(hlog, level)
#define hlog_set_level_by_str(level)    logger_set_level_by_str(hlog, level)
#define hlog_set_handler(fn)            logger_set_handler(hlog, fn)
#define hlog_set_format(format)         logger_set_format(hlog, format)
#define hlog_set_max_filesize(filesize) logger_set_max_filesize(hlog, filesize)
#define hlog_set_max_filesize_by_str(filesize) logger_set_max_filesize_by_str(hlog, filesize)
#define hlog_set_remain_days(days)      logger_set_remain_days(hlog, days)
#define hlog_enable_fsync()             logger_enable_fsync(hlog, 1)
#define hlog_disable_fsync()            logger_enable_fsync(hlog, 0)
#define hlog_fsync()                    logger_fsync(hlog)
#define hlog_get_cur_file()             logger_get_cur_file(hlog)

#define hlogd(fmt, ...) logger_print(hlog, LOG_LEVEL_DEBUG, fmt " [%s:%d:%s]\n", ## __VA_ARGS__, __FILENAME__, __LINE__, __FUNCTION__)
#define hlogi(fmt, ...) logger_print(hlog, LOG_LEVEL_INFO,  fmt " [%s:%d:%s]\n", ## __VA_ARGS__, __FILENAME__, __LINE__, __FUNCTION__)
#define hlogw(fmt, ...) logger_print(hlog, LOG_LEVEL_WARN,  fmt " [%s:%d:%s]\n", ## __VA_ARGS__, __FILENAME__, __LINE__, __FUNCTION__)
#define hloge(fmt, ...) logger_print(hlog, LOG_LEVEL_ERROR, fmt " [%s:%d:%s]\n", ## __VA_ARGS__, __FILENAME__, __LINE__, __FUNCTION__)
#define hlogf(fmt, ...) logger_print(hlog, LOG_LEVEL_FATAL, fmt " [%s:%d:%s]\n", ## __VA_ARGS__, __FILENAME__, __LINE__, __FUNCTION__)

// below for android
#if defined(ANDROID) || defined(__ANDROID__)
#include <android/log.h>
#define LOG_TAG "JNI"
#undef  hlogd
#undef  hlogi
#undef  hlogw
#undef  hloge
#undef  hlogf
#define hlogd(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define hlogi(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define hlogw(...) __android_log_print(ANDROID_LOG_WARN,  LOG_TAG, __VA_ARGS__)
#define hloge(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define hlogf(...) __android_log_print(ANDROID_LOG_FATAL, LOG_TAG, __VA_ARGS__)
#endif

// macro alias
#if !defined(LOGD) && !defined(LOGI) && !defined(LOGW) && !defined(LOGE) && !defined(LOGF)
#define LOGD    hlogd
#define LOGI    hlogi
#define LOGW    hlogw
#define LOGE    hloge
#define LOGF    hlogf
#endif

#ifdef __cplusplus
} // extern "C"
#endif

#endif // HV_LOG_H_
