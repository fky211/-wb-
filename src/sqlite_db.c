#include "sqlite_db.h"
#include "logger.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

static sqlite3 *g_db = NULL;

/* 生成当前时间字符串 */
static void get_time_str(char *buf, int size)
{
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    strftime(buf, size, "%Y-%m-%d %H:%M:%S", t);
}

/* 简单密码哈希 (DJB2算法) */
static unsigned long hash_password(const char *str)
{
    unsigned long hash = 5381;
    int c;
    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c;
    }
    return hash;
}

/* 创建充电桩表 */
static int create_table(void)
{
    const char *sql =
        "CREATE TABLE IF NOT EXISTS chargers ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "name TEXT NOT NULL,"
        "location TEXT NOT NULL,"
        "area TEXT NOT NULL,"
        "power REAL NOT NULL DEFAULT 60.0,"
        "status INTEGER NOT NULL DEFAULT 0,"
        "update_time TEXT NOT NULL"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_area ON chargers(area);"
        "CREATE INDEX IF NOT EXISTS idx_status ON chargers(status);";

    char *err = NULL;
    if (sqlite3_exec(g_db, sql, NULL, NULL, &err) != SQLITE_OK) {
        LOG_ERROR("Create table failed: %s", err);
        sqlite3_free(err);
        return -1;
    }
    return 0;
}

/* 插入充电桩示例数据 */
static int insert_sample_data(void)
{
    sqlite3_stmt *stmt;
    const char *cnt_sql = "SELECT COUNT(*) FROM chargers;";
    if (sqlite3_prepare_v2(g_db, cnt_sql, -1, &stmt, NULL) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW && sqlite3_column_int(stmt, 0) > 0) {
            sqlite3_finalize(stmt);
            return 0;
        }
        sqlite3_finalize(stmt);
    }

    const char *insert_sql =
        "INSERT INTO chargers (name, location, area, power, status, update_time) VALUES "
        "('A001', '朝阳区建国路88号', '朝阳区', 120.0, 0, datetime('now','localtime')),"
        "('A002', '朝阳区望京SOHO', '朝阳区', 60.0, 1, datetime('now','localtime')),"
        "('A003', '海淀区中关村大街1号', '海淀区', 120.0, 0, datetime('now','localtime')),"
        "('A004', '海淀区西二旗地铁站', '海淀区', 60.0, 0, datetime('now','localtime')),"
        "('A005', '西城区金融街购物中心', '西城区', 120.0, 2, datetime('now','localtime')),"
        "('B001', '丰台区丽泽SOHO', '丰台区', 60.0, 0, datetime('now','localtime')),"
        "('B002', '丰台区南三环西路', '丰台区', 120.0, 1, datetime('now','localtime')),"
        "('B003', '东城区王府井大街', '东城区', 60.0, 0, datetime('now','localtime')),"
        "('B004', '通州区万达广场', '通州区', 120.0, 0, datetime('now','localtime')),"
        "('B005', '大兴区亦庄开发区', '大兴区', 60.0, 1, datetime('now','localtime'));";

    char *err = NULL;
    if (sqlite3_exec(g_db, insert_sql, NULL, NULL, &err) != SQLITE_OK) {
        LOG_ERROR("Insert sample data failed: %s", err);
        sqlite3_free(err);
        return -1;
    }
    LOG_INFO("Inserted 10 sample chargers");
    return 0;
}

/* 创建用户表和会话表 */
static int create_user_tables(void)
{
    const char *sql =
        "CREATE TABLE IF NOT EXISTS users ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "username TEXT UNIQUE NOT NULL,"
        "password TEXT NOT NULL,"
        "nickname TEXT,"
        "create_time TEXT NOT NULL"
        ");"
        "CREATE TABLE IF NOT EXISTS sessions ("
        "token TEXT PRIMARY KEY,"
        "user_id INTEGER NOT NULL,"
        "username TEXT NOT NULL,"
        "expire_time INTEGER NOT NULL"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_username ON users(username);"
        "CREATE INDEX IF NOT EXISTS idx_session_expire ON sessions(expire_time);";

    char *err = NULL;
    if (sqlite3_exec(g_db, sql, NULL, NULL, &err) != SQLITE_OK) {
        LOG_ERROR("Create user tables failed: %s", err);
        sqlite3_free(err);
        return -1;
    }
    return 0;
}

/* 插入默认用户 */
static int insert_default_user(void)
{
    sqlite3_stmt *stmt;
    const char *cnt_sql = "SELECT COUNT(*) FROM users;";
    if (sqlite3_prepare_v2(g_db, cnt_sql, -1, &stmt, NULL) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW && sqlite3_column_int(stmt, 0) > 0) {
            sqlite3_finalize(stmt);
            return 0;
        }
        sqlite3_finalize(stmt);
    }

    char time_buf[32];
    get_time_str(time_buf, sizeof(time_buf));

    const char *sql = "INSERT INTO users (username, password, nickname, create_time) VALUES (?, ?, ?, ?);";
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return -1;
    }

    char hash_str[32];
    snprintf(hash_str, sizeof(hash_str), "%lu", hash_password("admin123"));

    sqlite3_bind_text(stmt, 1, "admin", -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, hash_str, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, "管理员", -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, time_buf, -1, SQLITE_STATIC);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc == SQLITE_DONE) {
        LOG_INFO("Default user created: admin/admin123");
    }
    return (rc == SQLITE_DONE) ? 0 : -1;
}

int db_init(const char *db_path)
{
    if (sqlite3_open(db_path, &g_db) != SQLITE_OK) {
        LOG_ERROR("Cannot open database: %s", sqlite3_errmsg(g_db));
        return -1;
    }

    sqlite3_exec(g_db, "PRAGMA journal_mode=WAL;", NULL, NULL, NULL);
    LOG_DEBUG("SQLite WAL mode enabled");

    if (create_table() != 0) return -1;
    if (insert_sample_data() != 0) return -1;
    if (create_user_tables() != 0) return -1;
    if (insert_default_user() != 0) return -1;

    LOG_INFO("Database initialized: %s", db_path);
    return 0;
}

/* 通用查询，从stmt中读取charger_info_t */
static int read_charger_row(sqlite3_stmt *stmt, charger_info_t *c)
{
    c->id = sqlite3_column_int(stmt, 0);
    strncpy(c->name, (const char *)sqlite3_column_text(stmt, 1), sizeof(c->name) - 1);
    strncpy(c->location, (const char *)sqlite3_column_text(stmt, 2), sizeof(c->location) - 1);
    strncpy(c->area, (const char *)sqlite3_column_text(stmt, 3), sizeof(c->area) - 1);
    c->power = sqlite3_column_double(stmt, 4);
    c->status = sqlite3_column_int(stmt, 5);
    strncpy(c->update_time, (const char *)sqlite3_column_text(stmt, 6), sizeof(c->update_time) - 1);
    return 0;
}

int db_query_chargers(charger_info_t *out, int max_count)
{
    const char *sql = "SELECT id,name,location,area,power,status,update_time FROM chargers ORDER BY id;";
    sqlite3_stmt *stmt;
    int count = 0;

    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        LOG_ERROR("Query chargers failed: %s", sqlite3_errmsg(g_db));
        return -1;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW && count < max_count) {
        read_charger_row(stmt, &out[count]);
        count++;
    }

    sqlite3_finalize(stmt);
    LOG_DEBUG("Query chargers: %d results", count);
    return count;
}

int db_query_by_area(const char *area, charger_info_t *out, int max_count)
{
    const char *sql = "SELECT id,name,location,area,power,status,update_time "
                      "FROM chargers WHERE area=? ORDER BY id;";
    sqlite3_stmt *stmt;
    int count = 0;

    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        LOG_ERROR("Query by area failed: %s", sqlite3_errmsg(g_db));
        return -1;
    }

    sqlite3_bind_text(stmt, 1, area, -1, SQLITE_STATIC);

    while (sqlite3_step(stmt) == SQLITE_ROW && count < max_count) {
        read_charger_row(stmt, &out[count]);
        count++;
    }

    sqlite3_finalize(stmt);
    return count;
}

int db_query_by_status(int status, charger_info_t *out, int max_count)
{
    const char *sql = "SELECT id,name,location,area,power,status,update_time "
                      "FROM chargers WHERE status=? ORDER BY id;";
    sqlite3_stmt *stmt;
    int count = 0;

    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        LOG_ERROR("Query by status failed: %s", sqlite3_errmsg(g_db));
        return -1;
    }

    sqlite3_bind_int(stmt, 1, status);

    while (sqlite3_step(stmt) == SQLITE_ROW && count < max_count) {
        read_charger_row(stmt, &out[count]);
        count++;
    }

    sqlite3_finalize(stmt);
    LOG_DEBUG("Query by status=%d: %d results", status, count);
    return count;
}

int db_get_stats(charger_stats_t *stats)
{
    const char *sql = "SELECT "
                      "COUNT(*),"
                      "SUM(CASE WHEN status=0 THEN 1 ELSE 0 END),"
                      "SUM(CASE WHEN status=1 THEN 1 ELSE 0 END),"
                      "SUM(CASE WHEN status=2 THEN 1 ELSE 0 END) "
                      "FROM chargers;";
    sqlite3_stmt *stmt;

    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        LOG_ERROR("Stats query failed: %s", sqlite3_errmsg(g_db));
        return -1;
    }

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        stats->total = sqlite3_column_int(stmt, 0);
        stats->idle  = sqlite3_column_int(stmt, 1);
        stats->busy  = sqlite3_column_int(stmt, 2);
        stats->fault = sqlite3_column_int(stmt, 3);
    }

    sqlite3_finalize(stmt);
    return 0;
}

int db_update_status(int charger_id, int new_status)
{
    const char *sql = "UPDATE chargers SET status=?, update_time=datetime('now','localtime') WHERE id=?;";
    sqlite3_stmt *stmt;

    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        LOG_ERROR("Update status failed: %s", sqlite3_errmsg(g_db));
        return -1;
    }

    sqlite3_bind_int(stmt, 1, new_status);
    sqlite3_bind_int(stmt, 2, charger_id);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return (rc == SQLITE_DONE) ? 0 : -1;
}

/* ========== 用户认证相关 ========== */

int db_user_register(const char *username, const char *password, const char *nickname)
{
    sqlite3_stmt *stmt;
    const char *check_sql = "SELECT id FROM users WHERE username=?;";
    if (sqlite3_prepare_v2(g_db, check_sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            sqlite3_finalize(stmt);
            return -2;
        }
        sqlite3_finalize(stmt);
    }

    char time_buf[32];
    get_time_str(time_buf, sizeof(time_buf));

    const char *sql = "INSERT INTO users (username, password, nickname, create_time) VALUES (?, ?, ?, ?);";
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return -1;
    }

    char hash_str[32];
    snprintf(hash_str, sizeof(hash_str), "%lu", hash_password(password));

    sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, hash_str, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, nickname ? nickname : username, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, time_buf, -1, SQLITE_STATIC);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return (rc == SQLITE_DONE) ? 0 : -1;
}

int db_user_login(const char *username, const char *password)
{
    sqlite3_stmt *stmt;
    const char *sql = "SELECT id, password FROM users WHERE username=?;";
    int user_id = -1;

    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return -1;
    }

    sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        user_id = sqlite3_column_int(stmt, 0);
        const char *stored_hash = (const char *)sqlite3_column_text(stmt, 1);
        char hash_str[32];
        snprintf(hash_str, sizeof(hash_str), "%lu", hash_password(password));

        if (strcmp(stored_hash, hash_str) != 0) {
            user_id = -1;
        }
    }

    sqlite3_finalize(stmt);
    return user_id;
}

int db_user_get_by_id(int user_id, user_info_t *user)
{
    sqlite3_stmt *stmt;
    const char *sql = "SELECT id, username, nickname, create_time FROM users WHERE id=?;";

    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return -1;
    }

    sqlite3_bind_int(stmt, 1, user_id);

    int rc = -1;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        user->id = sqlite3_column_int(stmt, 0);
        strncpy(user->username, (const char *)sqlite3_column_text(stmt, 1), sizeof(user->username) - 1);
        strncpy(user->nickname, (const char *)sqlite3_column_text(stmt, 2), sizeof(user->nickname) - 1);
        strncpy(user->create_time, (const char *)sqlite3_column_text(stmt, 3), sizeof(user->create_time) - 1);
        rc = 0;
    }

    sqlite3_finalize(stmt);
    return rc;
}

int db_session_create(const char *token, int user_id, const char *username, int expire_time)
{
    sqlite3_stmt *stmt;
    const char *sql = "INSERT OR REPLACE INTO sessions (token, user_id, username, expire_time) VALUES (?, ?, ?, ?);";

    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return -1;
    }

    sqlite3_bind_text(stmt, 1, token, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, user_id);
    sqlite3_bind_text(stmt, 3, username, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 4, expire_time);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return (rc == SQLITE_DONE) ? 0 : -1;
}

int db_session_verify(const char *token)
{
    sqlite3_stmt *stmt;
    const char *sql = "SELECT user_id FROM sessions WHERE token=? AND expire_time>?;";
    int user_id = -1;
    int now = (int)time(NULL);

    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return -1;
    }

    sqlite3_bind_text(stmt, 1, token, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, now);

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        user_id = sqlite3_column_int(stmt, 0);
    }

    sqlite3_finalize(stmt);
    return user_id;
}

int db_session_delete(const char *token)
{
    sqlite3_stmt *stmt;
    const char *sql = "DELETE FROM sessions WHERE token=?;";

    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return -1;
    }

    sqlite3_bind_text(stmt, 1, token, -1, SQLITE_STATIC);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return (rc == SQLITE_DONE) ? 0 : -1;
}

int db_session_cleanup(void)
{
    int now = (int)time(NULL);
    char sql[128];
    snprintf(sql, sizeof(sql), "DELETE FROM sessions WHERE expire_time < %d;", now);

    char *err = NULL;
    if (sqlite3_exec(g_db, sql, NULL, NULL, &err) != SQLITE_OK) {
        sqlite3_free(err);
        return -1;
    }
    return 0;
}

void db_close(void)
{
    if (g_db) {
        sqlite3_close(g_db);
        g_db = NULL;
    }
}
