#ifndef LOGGER_H
#define LOGGER_H

#include <stdio.h>

/* 日志级别 */
typedef enum {
    LOG_DEBUG = 0,
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR,
    LOG_FATAL
} log_level_t;

/* 初始化日志系统
 * log_file: 日志文件路径，NULL则只输出到控制台
 * level: 最低日志级别
 * max_size: 单个日志文件最大大小(字节)，0表示不限制
 * 返回: 0成功，-1失败
 */
int logger_init(const char *log_file, log_level_t level, long max_size);

/* 关闭日志系统 */
void logger_close(void);

/* 设置日志级别 */
void logger_set_level(log_level_t level);

/* 日志输出函数（内部使用） */
void logger_log(log_level_t level, const char *file, int line, const char *fmt, ...);

/* 便捷宏 */
#define LOG_DEBUG(fmt, ...) logger_log(LOG_DEBUG, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...)  logger_log(LOG_INFO,  __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...)  logger_log(LOG_WARN,  __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) logger_log(LOG_ERROR, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define LOG_FATAL(fmt, ...) logger_log(LOG_FATAL, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

/* 格式化日志（不带文件行号） */
void logger_printf(log_level_t level, const char *fmt, ...);

#endif /* LOGGER_H */
