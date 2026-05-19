#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>
#include "http_server.h"
#include "sqlite_db.h"
#include "json_utils.h"
#include "logger.h"

#define DEFAULT_PORT    8080
#define DB_PATH         "charger.db"
#define LOG_PATH        "logs/server.log"
#define SESSION_COOKIE  "SESSION_TOKEN"
#define SESSION_TTL     86400  /* 24小时 */

static http_server_t g_server;

/* 信号处理 */
static void signal_handler(int sig)
{
    (void)sig;
    LOG_INFO("Received signal %d, shutting down...", sig);
    http_server_stop(&g_server);
}

/* 构建完整HTTP JSON响应到resp_buf，返回总长度 */
static int build_json_response(char *resp_buf, int buf_size, int status,
                               const char *json_body, int json_len)
{
    const char *status_text;
    switch (status) {
    case 200: status_text = "OK"; break;
    case 400: status_text = "Bad Request"; break;
    case 500: status_text = "Internal Server Error"; break;
    default: status_text = "Unknown"; break;
    }

    int hdr_len = snprintf(resp_buf, buf_size,
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: application/json; charset=utf-8\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        "Access-Control-Allow-Origin: *\r\n\r\n",
        status, status_text, json_len);

    if (hdr_len < 0 || hdr_len + json_len >= buf_size) return -1;

    memcpy(resp_buf + hdr_len, json_body, json_len);
    return hdr_len + json_len;
}

/* API: 获取所有充电桩 */
static int api_get_chargers(const http_request_t *req, char *resp_buf, int buf_size)
{
    charger_info_t chargers[64];
    char json_buf[4096];
    int count;

    /* 检查是否有区域筛选 */
    char area_buf[32] = {0};
    if (strlen(req->query) > 0) {
        char *p = strstr(req->query, "area=");
        if (p) {
            sscanf(p + 5, "%31[^&]", area_buf);
        }
    }

    if (area_buf[0]) {
        count = db_query_by_area(area_buf, chargers, 64);
    } else {
        count = db_query_chargers(chargers, 64);
    }

    if (count < 0) {
        int json_len = snprintf(json_buf, sizeof(json_buf),
            "{\"code\":-1,\"msg\":\"query failed\"}");
        return build_json_response(resp_buf, buf_size, 500, json_buf, json_len);
    }

    int json_len = chargers_to_json_array(chargers, count, json_buf, sizeof(json_buf));
    if (json_len < 0) {
        json_len = snprintf(json_buf, sizeof(json_buf),
            "{\"code\":-1,\"msg\":\"json build failed\"}");
        return build_json_response(resp_buf, buf_size, 500, json_buf, json_len);
    }

    return build_json_response(resp_buf, buf_size, 200, json_buf, json_len);
}

/* API: 获取空闲充电桩 */
static int api_get_idle_chargers(const http_request_t *req, char *resp_buf, int buf_size)
{
    (void)req;
    charger_info_t chargers[64];
    char json_buf[4096];

    int count = db_query_by_status(0, chargers, 64);
    if (count < 0) {
        int json_len = snprintf(json_buf, sizeof(json_buf),
            "{\"code\":-1,\"msg\":\"query failed\"}");
        return build_json_response(resp_buf, buf_size, 500, json_buf, json_len);
    }

    int json_len = chargers_to_json_array(chargers, count, json_buf, sizeof(json_buf));
    if (json_len < 0) {
        json_len = snprintf(json_buf, sizeof(json_buf),
            "{\"code\":-1,\"msg\":\"json build failed\"}");
        return build_json_response(resp_buf, buf_size, 500, json_buf, json_len);
    }

    return build_json_response(resp_buf, buf_size, 200, json_buf, json_len);
}

/* API: 获取统计信息 */
static int api_get_stats(const http_request_t *req, char *resp_buf, int buf_size)
{
    (void)req;
    charger_stats_t stats;
    char json_buf[512];

    if (db_get_stats(&stats) != 0) {
        int json_len = snprintf(json_buf, sizeof(json_buf),
            "{\"code\":-1,\"msg\":\"query failed\"}");
        return build_json_response(resp_buf, buf_size, 500, json_buf, json_len);
    }

    int json_len = stats_to_json(&stats, json_buf, sizeof(json_buf));
    return build_json_response(resp_buf, buf_size, 200, json_buf, json_len);
}

/* API: 更新充电桩状态 */
static int api_update_status(const http_request_t *req, char *resp_buf, int buf_size)
{
    char json_buf[256];
    int id = -1, status = -1;

    /* 从query解析参数: id=1&status=0 */
    if (strlen(req->query) > 0) {
        char *p_id = strstr(req->query, "id=");
        char *p_st = strstr(req->query, "status=");
        if (p_id) sscanf(p_id + 3, "%d", &id);
        if (p_st) sscanf(p_st + 7, "%d", &status);
    }

    if (id < 0 || status < 0 || status > 2) {
        int json_len = snprintf(json_buf, sizeof(json_buf),
            "{\"code\":-1,\"msg\":\"invalid params, need id and status(0-2)\"}");
        return build_json_response(resp_buf, buf_size, 400, json_buf, json_len);
    }

    if (db_update_status(id, status) != 0) {
        int json_len = snprintf(json_buf, sizeof(json_buf),
            "{\"code\":-1,\"msg\":\"update failed\"}");
        return build_json_response(resp_buf, buf_size, 500, json_buf, json_len);
    }

    int json_len = snprintf(json_buf, sizeof(json_buf),
        "{\"code\":0,\"msg\":\"ok\"}");
    return build_json_response(resp_buf, buf_size, 200, json_buf, json_len);
}

/* ========== 用户认证相关 ========== */

/* 生成随机token */
static void generate_token(char *buf, int size)
{
    static const char charset[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    srand((unsigned int)time(NULL) ^ (unsigned int)getpid());
    for (int i = 0; i < size - 1; i++) {
        buf[i] = charset[rand() % (sizeof(charset) - 1)];
    }
    buf[size - 1] = '\0';
}

/* 从Cookie中提取SESSION_TOKEN */
static int extract_token(const http_request_t *req, char *token_buf, int buf_size)
{
    if (strlen(req->cookie) == 0) return -1;

    /* 查找SESSION_TOKEN= */
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "%s=", SESSION_COOKIE);
    char *p = strstr(req->cookie, pattern);
    if (p) {
        p += strlen(pattern);
        /* 提取token值，到分号或字符串结尾 */
        int i = 0;
        while (*p && *p != ';' && *p != ' ' && i < buf_size - 1) {
            token_buf[i++] = *p++;
        }
        token_buf[i] = '\0';
        return (i > 0) ? 0 : -1;
    }
    return -1;
}

/* 构建带Set-Cookie的JSON响应 */
static int build_json_response_with_cookie(char *resp_buf, int buf_size, int status,
                                           const char *json_body, int json_len,
                                           const char *cookie)
{
    const char *status_text;
    switch (status) {
    case 200: status_text = "OK"; break;
    case 400: status_text = "Bad Request"; break;
    case 401: status_text = "Unauthorized"; break;
    case 500: status_text = "Internal Server Error"; break;
    default: status_text = "Unknown"; break;
    }

    int hdr_len;
    if (cookie && cookie[0]) {
        hdr_len = snprintf(resp_buf, buf_size,
            "HTTP/1.1 %d %s\r\n"
            "Content-Type: application/json; charset=utf-8\r\n"
            "Content-Length: %d\r\n"
            "Set-Cookie: %s\r\n"
            "Connection: close\r\n"
            "Access-Control-Allow-Origin: *\r\n\r\n",
            status, status_text, json_len, cookie);
    } else {
        hdr_len = snprintf(resp_buf, buf_size,
            "HTTP/1.1 %d %s\r\n"
            "Content-Type: application/json; charset=utf-8\r\n"
            "Content-Length: %d\r\n"
            "Connection: close\r\n"
            "Access-Control-Allow-Origin: *\r\n\r\n",
            status, status_text, json_len);
    }

    if (hdr_len < 0 || hdr_len + json_len >= buf_size) return -1;

    memcpy(resp_buf + hdr_len, json_body, json_len);
    return hdr_len + json_len;
}

/* API: 用户注册 */
static int api_user_register(const http_request_t *req, char *resp_buf, int buf_size)
{
    char json_buf[512];
    char username[64] = {0};
    char password[128] = {0};
    char nickname[64] = {0};

    /* 解析参数 */
    if (strlen(req->query) > 0) {
        char *p;
        p = strstr(req->query, "username=");
        if (p) sscanf(p + 9, "%63[^&]", username);
        p = strstr(req->query, "password=");
        if (p) sscanf(p + 9, "%127[^&]", password);
        p = strstr(req->query, "nickname=");
        if (p) sscanf(p + 9, "%63[^&]", nickname);
    }

    if (!username[0] || !password[0]) {
        int json_len = snprintf(json_buf, sizeof(json_buf),
            "{\"code\":-1,\"msg\":\"username and password required\"}");
        return build_json_response(resp_buf, buf_size, 400, json_buf, json_len);
    }

    int rc = db_user_register(username, password, nickname[0] ? nickname : NULL);
    if (rc == -2) {
        int json_len = snprintf(json_buf, sizeof(json_buf),
            "{\"code\":-1,\"msg\":\"username already exists\"}");
        return build_json_response(resp_buf, buf_size, 400, json_buf, json_len);
    }
    if (rc != 0) {
        int json_len = snprintf(json_buf, sizeof(json_buf),
            "{\"code\":-1,\"msg\":\"register failed\"}");
        return build_json_response(resp_buf, buf_size, 500, json_buf, json_len);
    }

    int json_len = snprintf(json_buf, sizeof(json_buf),
        "{\"code\":0,\"msg\":\"register success\"}");
    return build_json_response(resp_buf, buf_size, 200, json_buf, json_len);
}

/* API: 用户登录 */
static int api_user_login(const http_request_t *req, char *resp_buf, int buf_size)
{
    char json_buf[512];
    char username[64] = {0};
    char password[128] = {0};

    /* 解析参数 */
    if (strlen(req->query) > 0) {
        char *p;
        p = strstr(req->query, "username=");
        if (p) sscanf(p + 9, "%63[^&]", username);
        p = strstr(req->query, "password=");
        if (p) sscanf(p + 9, "%127[^&]", password);
    }

    if (!username[0] || !password[0]) {
        int json_len = snprintf(json_buf, sizeof(json_buf),
            "{\"code\":-1,\"msg\":\"username and password required\"}");
        return build_json_response(resp_buf, buf_size, 400, json_buf, json_len);
    }

    int user_id = db_user_login(username, password);
    if (user_id < 0) {
        int json_len = snprintf(json_buf, sizeof(json_buf),
            "{\"code\":-1,\"msg\":\"invalid username or password\"}");
        return build_json_response(resp_buf, buf_size, 401, json_buf, json_len);
    }

    /* 生成session token */
    char token[64];
    generate_token(token, sizeof(token));

    int expire_time = (int)time(NULL) + SESSION_TTL;
    if (db_session_create(token, user_id, username, expire_time) != 0) {
        int json_len = snprintf(json_buf, sizeof(json_buf),
            "{\"code\":-1,\"msg\":\"session create failed\"}");
        return build_json_response(resp_buf, buf_size, 500, json_buf, json_len);
    }

    /* 获取用户信息 */
    user_info_t user;
    db_user_get_by_id(user_id, &user);

    char cookie[128];
    snprintf(cookie, sizeof(cookie), "%s=%s; Path=/; HttpOnly", SESSION_COOKIE, token);

    int json_len = snprintf(json_buf, sizeof(json_buf),
        "{\"code\":0,\"msg\":\"login success\",\"data\":{\"user_id\":%d,\"username\":\"%s\",\"nickname\":\"%s\"}}",
        user_id, user.username, user.nickname);

    return build_json_response_with_cookie(resp_buf, buf_size, 200, json_buf, json_len, cookie);
}

/* API: 用户登出 */
static int api_user_logout(const http_request_t *req, char *resp_buf, int buf_size)
{
    char json_buf[256];
    char token[64] = {0};

    extract_token(req, token, sizeof(token));

    if (token[0]) {
        db_session_delete(token);
    }

    char cookie[128];
    snprintf(cookie, sizeof(cookie), "%s=; Path=/; HttpOnly; Max-Age=0", SESSION_COOKIE);

    int json_len = snprintf(json_buf, sizeof(json_buf),
        "{\"code\":0,\"msg\":\"logout success\"}");

    return build_json_response_with_cookie(resp_buf, buf_size, 200, json_buf, json_len, cookie);
}

/* API: 获取当前用户信息 */
static int api_user_info(const http_request_t *req, char *resp_buf, int buf_size)
{
    char json_buf[512];
    char token[64] = {0};

    extract_token(req, token, sizeof(token));

    if (!token[0]) {
        int json_len = snprintf(json_buf, sizeof(json_buf),
            "{\"code\":-1,\"msg\":\"not logged in\"}");
        return build_json_response(resp_buf, buf_size, 401, json_buf, json_len);
    }

    int user_id = db_session_verify(token);
    if (user_id < 0) {
        int json_len = snprintf(json_buf, sizeof(json_buf),
            "{\"code\":-1,\"msg\":\"session expired\"}");
        return build_json_response(resp_buf, buf_size, 401, json_buf, json_len);
    }

    user_info_t user;
    if (db_user_get_by_id(user_id, &user) != 0) {
        int json_len = snprintf(json_buf, sizeof(json_buf),
            "{\"code\":-1,\"msg\":\"user not found\"}");
        return build_json_response(resp_buf, buf_size, 500, json_buf, json_len);
    }

    int json_len = snprintf(json_buf, sizeof(json_buf),
        "{\"code\":0,\"data\":{\"user_id\":%d,\"username\":\"%s\",\"nickname\":\"%s\",\"create_time\":\"%s\"}}",
        user.id, user.username, user.nickname, user.create_time);

    return build_json_response(resp_buf, buf_size, 200, json_buf, json_len);
}

int main(int argc, char *argv[])
{
    int port = DEFAULT_PORT;

    if (argc > 1) {
        port = atoi(argv[1]);
        if (port <= 0 || port > 65535) {
            fprintf(stderr, "Invalid port: %s\n", argv[1]);
            return 1;
        }
    }

    /* 初始化日志系统 */
    if (logger_init(LOG_PATH, LOG_DEBUG, 10 * 1024 * 1024) != 0) {
        fprintf(stderr, "Logger initialization failed, using console only\n");
        logger_init(NULL, LOG_DEBUG, 0);
    }

    LOG_INFO("========================================");
    LOG_INFO("  Charger Monitor Web Server Starting");
    LOG_INFO("========================================");

    /* 初始化数据库 */
    if (db_init(DB_PATH) != 0) {
        LOG_FATAL("Database initialization failed");
        logger_close();
        return 1;
    }
    LOG_INFO("Database initialized: %s", DB_PATH);

    /* 初始化HTTP服务器 */
    if (http_server_init(&g_server, port) != 0) {
        LOG_FATAL("HTTP server initialization failed");
        db_close();
        logger_close();
        return 1;
    }

    /* 注册路由 */
    http_server_add_route(&g_server, "/api/chargers", api_get_chargers);
    http_server_add_route(&g_server, "/api/chargers/idle", api_get_idle_chargers);
    http_server_add_route(&g_server, "/api/stats", api_get_stats);
    http_server_add_route(&g_server, "/api/update", api_update_status);

    /* 用户认证路由 */
    http_server_add_route(&g_server, "/api/user/register", api_user_register);
    http_server_add_route(&g_server, "/api/user/login", api_user_login);
    http_server_add_route(&g_server, "/api/user/logout", api_user_logout);
    http_server_add_route(&g_server, "/api/user/info", api_user_info);

    /* 设置信号处理 */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    LOG_INFO("Server listening on http://0.0.0.0:%d", port);
    LOG_INFO("Log file: %s", LOG_PATH);

    /* 运行服务器 */
    http_server_run(&g_server);

    /* 清理 */
    http_server_stop(&g_server);
    db_close();

    LOG_INFO("Server stopped.");
    logger_close();

    return 0;
}
