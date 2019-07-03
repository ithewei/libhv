#ifndef HW_LOG_H_
#define HW_LOG_H_

#define CL_CLR      "\033[0m"       /* 恢复颜色 */
#define CL_BLACK    "\033[30m"      /* 黑色字 */
#define CL_RED      "\e[1;31m"      /* 红色字 */
#define CL_GREEN    "\e[1;32m"      /* 绿色字 */
#define CL_YELLOW   "\e[1;33m"      /* 黄色字 */
#define CL_BLUE     "\033[34m"      /* 蓝色字 */
#define CL_PURPLE   "\e[1;35m"      /* 紫色字 */
#define CL_SKYBLUE  "\e[1;36m"      /* 天蓝字 */
#define CL_WHITE    "\033[37m"      /* 白色字 */

#define CL_BLK_WHT  "\033[40;37m"   /* 黑底白字 */
#define CL_RED_WHT  "\033[41;37m"   /* 红底白字 */
#define CL_GRE_WHT  "\033[42;37m"   /* 绿底白字 */
#define CL_YEW_WHT  "\033[43;37m"   /* 黄底白字 */
#define CL_BLUE_WHT "\033[44;37m"   /* 蓝底白字 */
#define CL_PPL_WHT  "\033[45;37m"   /* 紫底白字 */
#define CL_SKYB_WHT "\033[46;37m"   /* 天蓝底白字 */
#define CL_WHT_BLK  "\033[47;30m"   /* 白底黑字 */

// F(id, str, clr)
#define FOREACH_LOG(F) \
    F(LOG_LEVEL_DEBUG, "DEBUG", CL_WHITE) \
    F(LOG_LEVEL_INFO,  "INFO ", CL_GREEN) \
    F(LOG_LEVEL_WARN,  "WARN ", CL_YELLOW) \
    F(LOG_LEVEL_ERROR, "ERROR", CL_RED) \
    F(LOG_LEVEL_FATAL, "FATAL", CL_RED_WHT)

enum LOG_LEVEL {
    LOG_LEVEL_VERBOSE = 0,
#define ENUM_LOG_LEVEL(id, str, clr) id,
    FOREACH_LOG(ENUM_LOG_LEVEL)
#undef  ENUM_LOG_LEVEL
    LOG_LEVEL_SILENT
};

#define DEFAULT_LOG_FILE            "default"
#define DEFAULT_LOG_LEVEL           LOG_LEVEL_VERBOSE
#define DEFAULT_LOG_REMAIN_DAYS     1
#define LOG_BUFSIZE                 (1<<13)  // 8k
#define MAX_LOG_FILESIZE            (1<<23)  // 8M

int     hlog_set_file(const char* file);
void    hlog_set_level(int level);
void    hlog_set_remain_days(int days);
void    hlog_enable_color(int on);
int     hlog_printf(int level, const char* fmt, ...);

#define hlogd(fmt, ...) hlog_printf(LOG_LEVEL_DEBUG, fmt " [%s:%d:%s]", ## __VA_ARGS__, __FILE__, __LINE__, __FUNCTION__)
#define hlogi(fmt, ...) hlog_printf(LOG_LEVEL_INFO,  fmt " [%s:%d:%s]", ## __VA_ARGS__, __FILE__, __LINE__, __FUNCTION__)
#define hlogw(fmt, ...) hlog_printf(LOG_LEVEL_WARN,  fmt " [%s:%d:%s]", ## __VA_ARGS__, __FILE__, __LINE__, __FUNCTION__)
#define hloge(fmt, ...) hlog_printf(LOG_LEVEL_ERROR, fmt " [%s:%d:%s]", ## __VA_ARGS__, __FILE__, __LINE__, __FUNCTION__)
#define hlogf(fmt, ...) hlog_printf(LOG_LEVEL_FATAL, fmt " [%s:%d:%s]", ## __VA_ARGS__, __FILE__, __LINE__, __FUNCTION__)

// below for android
#include "hplatform.h"
#ifdef OS_ANDROID
#include <android/log.h>
#define LOG_TAG "JNI"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGF(...) __android_log_print(ANDROID_LOG_FATAL, LOG_TAG, __VA_ARGS__)
#else
#define LOGD    hlogd
#define LOGI    hlogi
#define LOGW    hlogw
#define LOGE    hloge
#define LOGF    hlogf
#endif

#endif  // HW_LOG_H_
