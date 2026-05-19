#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <pthread.h>

/* 日志级别名称 */
static const char *level_names[] = {
    "DEBUG", "INFO ", "WARN ", "ERROR", "FATAL"
};

/* 日志级别颜色 (ANSI终端颜色) */
static const char *level_colors[] = {
    "\033[36m",  /* DEBUG - 青色 */
    "\033[32m",  /* INFO  - 绿色 */
    "\033[33m",  /* WARN  - 黄色 */
    "\033[31m",  /* ERROR - 红色 */
    "\033[35m"   /* FATAL - 紫色 */
};

#define COLOR_RESET "\033[0m"

/* 日志系统上下文 */
static struct {
    FILE        *fp;            /* 日志文件句柄 */
    log_level_t level;          /* 最低日志级别 */
    long        max_size;       /* 单个文件最大大小 */
    long        current_size;   /* 当前文件大小 */
    char        log_path[256];  /* 日志文件路径 */
    int         file_enabled;   /* 是否输出到文件 */
    int         console_enabled;/* 是否输出到控制台 */
    pthread_mutex_t mutex;      /* 线程锁 */
} g_logger = {
    .fp = NULL,
    .level = LOG_INFO,
    .max_size = 10 * 1024 * 1024,  /* 默认10MB */
    .current_size = 0,
    .file_enabled = 0,
    .console_enabled = 1,
    .mutex = PTHREAD_MUTEX_INITIALIZER
};

/* 获取当前时间字符串，返回写入的字符数 */
static int get_timestamp(char *buf, int size)
{
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    return strftime(buf, size, "%Y-%m-%d %H:%M:%S", t);
}

/* 滚动日志文件 */
static int rotate_log(void)
{
    if (!g_logger.file_enabled || !g_logger.fp) return 0;

    /* 关闭当前文件 */
    fclose(g_logger.fp);
    g_logger.fp = NULL;

    /* 重命名旧文件: xxx.log -> xxx.log.1 */
    char old_path[280];
    snprintf(old_path, sizeof(old_path), "%s.1", g_logger.log_path);
    remove(old_path);  /* 删除旧的备份 */
    rename(g_logger.log_path, old_path);

    /* 重新打开新文件 */
    g_logger.fp = fopen(g_logger.log_path, "a");
    if (!g_logger.fp) {
        fprintf(stderr, "Failed to reopen log file: %s\n", g_logger.log_path);
        g_logger.file_enabled = 0;
        return -1;
    }

    g_logger.current_size = 0;
    return 0;
}

int logger_init(const char *log_file, log_level_t level, long max_size)
{
    pthread_mutex_lock(&g_logger.mutex);

    g_logger.level = level;
    g_logger.console_enabled = 1;

    if (max_size > 0) {
        g_logger.max_size = max_size;
    }

    if (log_file && log_file[0]) {
        strncpy(g_logger.log_path, log_file, sizeof(g_logger.log_path) - 1);
        g_logger.fp = fopen(log_file, "a");
        if (!g_logger.fp) {
            fprintf(stderr, "Failed to open log file: %s\n", log_file);
            pthread_mutex_unlock(&g_logger.mutex);
            return -1;
        }

        /* 获取当前文件大小 */
        fseek(g_logger.fp, 0, SEEK_END);
        g_logger.current_size = ftell(g_logger.fp);

        g_logger.file_enabled = 1;
    }

    pthread_mutex_unlock(&g_logger.mutex);

    LOG_INFO("Logger initialized: level=%s, file=%s",
             level_names[level], log_file ? log_file : "stdout");
    return 0;
}

void logger_close(void)
{
    pthread_mutex_lock(&g_logger.mutex);

    if (g_logger.fp) {
        fclose(g_logger.fp);
        g_logger.fp = NULL;
    }
    g_logger.file_enabled = 0;

    pthread_mutex_unlock(&g_logger.mutex);
}

void logger_set_level(log_level_t level)
{
    g_logger.level = level;
}

void logger_log(log_level_t level, const char *file, int line, const char *fmt, ...)
{
    if (level < g_logger.level) return;

    char timestamp[32];
    get_timestamp(timestamp, sizeof(timestamp));

    /* 提取文件名（去掉路径） */
    const char *filename = strrchr(file, '/');
    if (!filename) filename = strrchr(file, '\\');
    if (filename) filename++;
    else filename = file;

    /* 格式化用户消息 */
    char msg[2048];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);

    pthread_mutex_lock(&g_logger.mutex);

    /* 输出到控制台（带颜色） */
    if (g_logger.console_enabled) {
        fprintf(stderr, "%s[%s]%s %s %s:%d | %s\n",
                level_colors[level], level_names[level], COLOR_RESET,
                timestamp, filename, line, msg);
    }

    /* 输出到文件（不带颜色） */
    if (g_logger.file_enabled && g_logger.fp) {
        int len = fprintf(g_logger.fp, "[%s] %s %s:%d | %s\n",
                         level_names[level], timestamp, filename, line, msg);
        if (len > 0) {
            g_logger.current_size += len;
            /* 检查是否需要滚动 */
            if (g_logger.max_size > 0 && g_logger.current_size >= g_logger.max_size) {
                rotate_log();
            }
        }
        fflush(g_logger.fp);
    }

    pthread_mutex_unlock(&g_logger.mutex);
}

void logger_printf(log_level_t level, const char *fmt, ...)
{
    if (level < g_logger.level) return;

    char timestamp[32];
    get_timestamp(timestamp, sizeof(timestamp));

    char msg[2048];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);

    pthread_mutex_lock(&g_logger.mutex);

    if (g_logger.console_enabled) {
        fprintf(stderr, "%s[%s]%s %s | %s\n",
                level_colors[level], level_names[level], COLOR_RESET,
                timestamp, msg);
    }

    if (g_logger.file_enabled && g_logger.fp) {
        int len = fprintf(g_logger.fp, "[%s] %s | %s\n",
                         level_names[level], timestamp, msg);
        if (len > 0) {
            g_logger.current_size += len;
            if (g_logger.max_size > 0 && g_logger.current_size >= g_logger.max_size) {
                rotate_log();
            }
        }
        fflush(g_logger.fp);
    }

    pthread_mutex_unlock(&g_logger.mutex);
}
