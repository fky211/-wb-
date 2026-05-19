#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

#include <sys/epoll.h>

#define MAX_EVENTS      64
#define MAX_CLIENTS     1024
#define RECV_BUF_SIZE   4096
#define SEND_BUF_SIZE   65536

/* HTTP请求结构 */
typedef struct {
    char method[16];        /* GET/POST */
    char path[256];         /* 请求路径 */
    char query[512];        /* 查询字符串 */
    char host[128];
    char cookie[512];       /* Cookie头 */
    int  keep_alive;
} http_request_t;

/* 客户端连接结构 */
typedef struct {
    int     fd;
    int     in_use;
    char    recv_buf[RECV_BUF_SIZE];
    int     recv_len;
    char    *send_buf;
    int     send_len;
    int     send_offset;
} client_conn_t;

/* 路由处理函数类型 */
typedef int (*route_handler_t)(const http_request_t *req, char *resp_buf, int buf_size);

/* 路由表项 */
typedef struct {
    const char      *path;
    route_handler_t handler;
} route_entry_t;

/* 服务器上下文 */
typedef struct {
    int             listen_fd;
    int             epoll_fd;
    client_conn_t   clients[MAX_CLIENTS];
    route_entry_t   routes[16];
    int             route_count;
    int             running;
} http_server_t;

/* 初始化服务器 */
int http_server_init(http_server_t *srv, int port);

/* 注册路由 */
int http_server_add_route(http_server_t *srv, const char *path, route_handler_t handler);

/* 运行事件循环 */
int http_server_run(http_server_t *srv);

/* 停止服务器 */
void http_server_stop(http_server_t *srv);

/* 发送HTTP响应 */
int http_send_response(int fd, int status, const char *content_type,
                       const char *body, int body_len, int keep_alive);

#endif /* HTTP_SERVER_H */
