#include "http_server.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

/* 设置socket为非阻塞 */
static int set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

/* 设置socket选项 */
static void set_socket_opts(int fd)
{
    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));
}

/* 解析HTTP请求 */
static int parse_http_request(const char *raw, int len, http_request_t *req)
{
    (void)len;
    char method[16] = {0};
    char path[512] = {0};
    char version[16] = {0};

    memset(req, 0, sizeof(*req));

    /* 解析请求行: GET /path?query HTTP/1.1 */
    if (sscanf(raw, "%15s %511s %15s", method, path, version) != 3) {
        return -1;
    }

    strncpy(req->method, method, sizeof(req->method) - 1);

    /* 分离path和query */
    char *q = strchr(path, '?');
    if (q) {
        *q = '\0';
        strncpy(req->query, q + 1, sizeof(req->query) - 1);
    }
    strncpy(req->path, path, sizeof(req->path) - 1);

    /* 解析Connection头 */
    if (strstr(raw, "Connection: keep-alive") || strstr(raw, "Connection: Keep-Alive")) {
        req->keep_alive = 1;
    }

    /* 解析Host头 */
    const char *host_hdr = strstr(raw, "Host: ");
    if (host_hdr) {
        sscanf(host_hdr + 6, "%127[^ \r\n]", req->host);
    }

    /* 解析Cookie头 */
    const char *cookie_hdr = strstr(raw, "Cookie: ");
    if (cookie_hdr) {
        sscanf(cookie_hdr + 8, "%511[^\r\n]", req->cookie);
    }

    return 0;
}

/* 获取Content-Type */
static const char *get_content_type(const char *path)
{
    if (strstr(path, ".html")) return "text/html; charset=utf-8";
    if (strstr(path, ".css"))  return "text/css; charset=utf-8";
    if (strstr(path, ".js"))   return "application/javascript; charset=utf-8";
    if (strstr(path, ".json")) return "application/json; charset=utf-8";
    if (strstr(path, ".png"))  return "image/png";
    if (strstr(path, ".jpg"))  return "image/jpeg";
    return "text/plain; charset=utf-8";
}

/* 处理静态文件请求 */
static int serve_static_file(const char *path, char *resp_buf, int buf_size)
{
    char file_path[512];
    FILE *fp;
    int ret;

    /* 安全检查：不允许路径穿越 */
    if (strstr(path, "..")) {
        ret = snprintf(resp_buf, buf_size,
            "HTTP/1.1 403 Forbidden\r\n"
            "Content-Type: text/plain\r\n"
            "Content-Length: 13\r\n\r\n"
            "403 Forbidden");
        return ret;
    }

    /* 默认首页 */
    if (strcmp(path, "/") == 0 || strcmp(path, "/index.html") == 0) {
        snprintf(file_path, sizeof(file_path), "www/index.html");
    } else {
        snprintf(file_path, sizeof(file_path), "www%s", path);
    }

    fp = fopen(file_path, "rb");
    if (!fp) {
        ret = snprintf(resp_buf, buf_size,
            "HTTP/1.1 404 Not Found\r\n"
            "Content-Type: text/plain\r\n"
            "Content-Length: 13\r\n\r\n"
            "404 Not Found");
        return ret;
    }

    /* 获取文件大小 */
    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    /* 构建响应头 */
    const char *ctype = get_content_type(file_path);
    int offset = snprintf(resp_buf, buf_size,
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %ld\r\n"
        "Connection: close\r\n\r\n",
        ctype, file_size);

    LOG_DEBUG("Serving file: %s, size=%ld, type=%s", file_path, file_size, ctype);

    if (offset + file_size > buf_size) {
        fclose(fp);
        ret = snprintf(resp_buf, buf_size,
            "HTTP/1.1 500 Internal Server Error\r\n"
            "Content-Type: text/plain\r\n"
            "Content-Length: 21\r\n\r\n"
            "500 Internal Server Error");
        return ret;
    }

    /* 读取文件内容 */
    size_t read_bytes = fread(resp_buf + offset, 1, file_size, fp);
    fclose(fp);

    return offset + (int)read_bytes;
}

/* 查找并执行路由处理 */
static int handle_route(http_server_t *srv, const http_request_t *req,
                        char *resp_buf, int buf_size)
{
    /* 查找匹配的路由 */
    for (int i = 0; i < srv->route_count; i++) {
        if (strcmp(req->path, srv->routes[i].path) == 0) {
            return srv->routes[i].handler(req, resp_buf, buf_size);
        }
    }

    /* 未找到路由，尝试静态文件 */
    return serve_static_file(req->path, resp_buf, buf_size);
}

/* 处理客户端数据 */
static void handle_client_data(http_server_t *srv, int client_idx)
{
    client_conn_t *conn = &srv->clients[client_idx];
    http_request_t req;
    char resp_buf[SEND_BUF_SIZE];

    /* 解析HTTP请求 */
    conn->recv_buf[conn->recv_len] = '\0';
    if (parse_http_request(conn->recv_buf, conn->recv_len, &req) != 0) {
        LOG_WARN("Bad request from fd=%d", conn->fd);
        int len = snprintf(resp_buf, sizeof(resp_buf),
            "HTTP/1.1 400 Bad Request\r\n"
            "Content-Type: text/plain\r\n"
            "Content-Length: 15\r\n\r\n"
            "400 Bad Request");
        send(conn->fd, resp_buf, len, 0);
        goto cleanup;
    }

    LOG_INFO("%s %s%s%s", req.method, req.path,
             req.query[0] ? "?" : "", req.query);

    /* 路由处理 */
    int resp_len = handle_route(srv, &req, resp_buf, sizeof(resp_buf));
    if (resp_len > 0) {
        int sent = 0;
        while (sent < resp_len) {
            int n = send(conn->fd, resp_buf + sent, resp_len - sent, 0);
            if (n < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) continue;
                break;
            }
            sent += n;
        }
    }

cleanup:
    /* 关闭连接 */
    epoll_ctl(srv->epoll_fd, EPOLL_CTL_DEL, conn->fd, NULL);
    close(conn->fd);
    conn->in_use = 0;
    conn->fd = -1;
}

/* 接受新连接 */
static void accept_connections(http_server_t *srv)
{
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);

    while (1) {
        int client_fd = accept(srv->listen_fd, (struct sockaddr *)&client_addr, &addr_len);
        if (client_fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            perror("accept");
            break;
        }

        set_nonblocking(client_fd);
        set_socket_opts(client_fd);

        /* 查找空闲客户端槽位 */
        int slot = -1;
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (!srv->clients[i].in_use) {
                slot = i;
                break;
            }
        }

        if (slot < 0) {
            close(client_fd);
            continue;
        }

        /* 初始化客户端连接 */
        srv->clients[slot].fd = client_fd;
        srv->clients[slot].in_use = 1;
        srv->clients[slot].recv_len = 0;

        /* 添加到epoll */
        struct epoll_event ev;
        ev.events = EPOLLIN | EPOLLET;
        ev.data.fd = client_fd;
        epoll_ctl(srv->epoll_fd, EPOLL_CTL_ADD, client_fd, &ev);
    }
}

/* 从客户端读取数据 */
static void read_client_data(http_server_t *srv, int fd)
{
    int slot = -1;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (srv->clients[i].in_use && srv->clients[i].fd == fd) {
            slot = i;
            break;
        }
    }

    if (slot < 0) return;

    client_conn_t *conn = &srv->clients[slot];

    while (1) {
        int space = RECV_BUF_SIZE - conn->recv_len - 1;
        if (space <= 0) break;

        int n = recv(fd, conn->recv_buf + conn->recv_len, space, 0);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            goto cleanup;
        }
        if (n == 0) goto cleanup;

        conn->recv_len += n;
        conn->recv_buf[conn->recv_len] = '\0';

        /* 检查是否收到完整的HTTP请求 */
        if (strstr(conn->recv_buf, "\r\n\r\n")) {
            handle_client_data(srv, slot);
            return;
        }
    }
    return;

cleanup:
    epoll_ctl(srv->epoll_fd, EPOLL_CTL_DEL, fd, NULL);
    close(fd);
    conn->in_use = 0;
    conn->fd = -1;
}

int http_server_init(http_server_t *srv, int port)
{
    struct sockaddr_in addr;

    memset(srv, 0, sizeof(*srv));
    srv->running = 0;
    srv->route_count = 0;

    /* 创建监听socket */
    srv->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (srv->listen_fd < 0) {
        LOG_ERROR("socket() failed: %s", strerror(errno));
        return -1;
    }

    set_nonblocking(srv->listen_fd);
    set_socket_opts(srv->listen_fd);

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(srv->listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        LOG_ERROR("bind() failed: %s", strerror(errno));
        close(srv->listen_fd);
        return -1;
    }

    if (listen(srv->listen_fd, 128) < 0) {
        LOG_ERROR("listen() failed: %s", strerror(errno));
        close(srv->listen_fd);
        return -1;
    }

    /* 创建epoll实例 */
    srv->epoll_fd = epoll_create1(0);
    if (srv->epoll_fd < 0) {
        LOG_ERROR("epoll_create1() failed: %s", strerror(errno));
        close(srv->listen_fd);
        return -1;
    }

    /* 注册监听socket到epoll */
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = srv->listen_fd;
    if (epoll_ctl(srv->epoll_fd, EPOLL_CTL_ADD, srv->listen_fd, &ev) < 0) {
        LOG_ERROR("epoll_ctl() failed: %s", strerror(errno));
        close(srv->epoll_fd);
        close(srv->listen_fd);
        return -1;
    }

    /* 初始化客户端数组 */
    for (int i = 0; i < MAX_CLIENTS; i++) {
        srv->clients[i].fd = -1;
        srv->clients[i].in_use = 0;
    }

    LOG_INFO("HTTP server initialized on port %d", port);
    return 0;
}

int http_server_add_route(http_server_t *srv, const char *path, route_handler_t handler)
{
    if (srv->route_count >= 16) return -1;

    srv->routes[srv->route_count].path = path;
    srv->routes[srv->route_count].handler = handler;
    srv->route_count++;
    return 0;
}

int http_server_run(http_server_t *srv)
{
    struct epoll_event events[MAX_EVENTS];

    srv->running = 1;
    LOG_INFO("Server event loop started");

    while (srv->running) {
        int nfds = epoll_wait(srv->epoll_fd, events, MAX_EVENTS, 1000);
        if (nfds < 0) {
            if (errno == EINTR) continue;
            LOG_ERROR("epoll_wait() failed: %s", strerror(errno));
            break;
        }

        for (int i = 0; i < nfds; i++) {
            if (events[i].data.fd == srv->listen_fd) {
                accept_connections(srv);
            } else {
                read_client_data(srv, events[i].data.fd);
            }
        }
    }

    LOG_INFO("Server event loop stopped");
    return 0;
}

void http_server_stop(http_server_t *srv)
{
    srv->running = 0;

    /* 关闭所有客户端连接 */
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (srv->clients[i].in_use) {
            close(srv->clients[i].fd);
            srv->clients[i].in_use = 0;
        }
    }

    if (srv->epoll_fd >= 0) close(srv->epoll_fd);
    if (srv->listen_fd >= 0) close(srv->listen_fd);
}

int http_send_response(int fd, int status, const char *content_type,
                       const char *body, int body_len, int keep_alive)
{
    char buf[SEND_BUF_SIZE];
    const char *status_text;

    switch (status) {
    case 200: status_text = "OK"; break;
    case 400: status_text = "Bad Request"; break;
    case 404: status_text = "Not Found"; break;
    case 500: status_text = "Internal Server Error"; break;
    default: status_text = "Unknown"; break;
    }

    int len = snprintf(buf, sizeof(buf),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %d\r\n"
        "Connection: %s\r\n"
        "Access-Control-Allow-Origin: *\r\n\r\n",
        status, status_text, content_type, body_len,
        keep_alive ? "keep-alive" : "close");

    if (len + body_len > (int)sizeof(buf)) return -1;

    memcpy(buf + len, body, body_len);
    len += body_len;

    return send(fd, buf, len, 0);
}
