#ifndef SQLITE_DB_H
#define SQLITE_DB_H

#include <sqlite3.h>

/* 充电桩信息 */
typedef struct {
    int     id;
    char    name[64];
    char    location[128];
    char    area[32];
    double  power;          /* 额定功率 kW */
    int     status;         /* 0-空闲 1-占用 2-故障 */
    char    update_time[32];
} charger_info_t;

/* 统计信息 */
typedef struct {
    int total;
    int idle;
    int busy;
    int fault;
} charger_stats_t;

/* 用户信息 */
typedef struct {
    int     id;
    char    username[64];
    char    password[128];  /* 存储哈希值 */
    char    nickname[64];
    char    create_time[32];
} user_info_t;

/* 会话信息 */
typedef struct {
    char    token[64];
    int     user_id;
    char    username[64];
    int     expire_time;
} session_t;

/* 初始化数据库，创建表和初始数据 */
int db_init(const char *db_path);

/* 查询所有充电桩 */
int db_query_chargers(charger_info_t *out, int max_count);

/* 按区域筛选 */
int db_query_by_area(const char *area, charger_info_t *out, int max_count);

/* 按状态筛选 */
int db_query_by_status(int status, charger_info_t *out, int max_count);

/* 获取统计数据 */
int db_get_stats(charger_stats_t *stats);

/* 更新充电桩状态 */
int db_update_status(int charger_id, int new_status);

/* ========== 用户相关 ========== */

/* 用户注册 */
int db_user_register(const char *username, const char *password, const char *nickname);

/* 用户登录，返回用户ID，失败返回-1 */
int db_user_login(const char *username, const char *password);

/* 根据用户ID获取用户信息 */
int db_user_get_by_id(int user_id, user_info_t *user);

/* 创建会话 */
int db_session_create(const char *token, int user_id, const char *username, int expire_time);

/* 验证会话，返回用户ID */
int db_session_verify(const char *token);

/* 删除会话 */
int db_session_delete(const char *token);

/* 清理过期会话 */
int db_session_cleanup(void);

/* 关闭数据库 */
void db_close(void);

#endif /* SQLITE_DB_H */
