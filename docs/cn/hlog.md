日志

```c

// 标准输出日志
void stdout_logger(int loglevel, const char* buf, int len);

// 标准错误日志
void stderr_logger(int loglevel, const char* buf, int len);

// 文件日志
void file_logger(int loglevel, const char* buf, int len);

// 网络日志（定义在event/nlog.h头文件里）
// void network_logger(int loglevel, const char* buf, int len);

// 创建日志器
logger_t* logger_create();

// 销毁日志器
void logger_destroy(logger_t* logger);

// 设置日志处理器
void logger_set_handler(logger_t* logger, logger_handler fn);

// 设置日志等级
void logger_set_level(logger_t* logger, int level);
// level = [VERBOSE,DEBUG,INFO,WARN,ERROR,FATAL,SILENT]
void logger_set_level_by_str(logger_t* logger, const char* level);

/*
 * 设置日志格式
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
void logger_set_format(logger_t* logger, const char* format);

// 设置日志缓存大小
void logger_set_max_bufsize(logger_t* logger, unsigned int bufsize);

// 启用日志颜色
void logger_enable_color(logger_t* logger, int on);

// 日志打印
int  logger_print(logger_t* logger, int level, const char* fmt, ...);

// 设置日志文件
void logger_set_file(logger_t* logger, const char* filepath);

// 设置日志文件大小
void logger_set_max_filesize(logger_t* logger, unsigned long long filesize);
// 16, 16M, 16MB
void logger_set_max_filesize_by_str(logger_t* logger, const char* filesize);

// 设置日志文件保留天数
void logger_set_remain_days(logger_t* logger, int days);

// 启用每次写日志文件立即刷新到磁盘（即每次都调用fsync，会增加IO耗时，影响性能）
void logger_enable_fsync(logger_t* logger, int on);

// 刷新缓存到磁盘（如对日志文件实时性有必要的，可使用定时器定时刷新到磁盘）
void logger_fsync(logger_t* logger);

// 获取当前日志文件路径
const char* logger_get_cur_file(logger_t* logger);

// hlog: 默认的日志器
logger_t* hv_default_logger();

// 销毁默认的日志器
void      hv_destroy_default_logger(void);

// 对默认日志器hlog的一些便利操作宏
#define hlog                            hv_default_logger()
#define hlog_destory()                  hv_destroy_default_logger()
/* 禁用hv的默认日志 */
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

```

测试代码见 [examples/hloop_test.c](../../examples/hloop_test.c)
