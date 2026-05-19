#include "json_utils.h"
#include <stdio.h>
#include <string.h>

/* 状态枚举到字符串 */
static const char *status_str(int s)
{
    switch (s) {
    case 0: return "idle";
    case 1: return "busy";
    case 2: return "fault";
    default: return "unknown";
    }
}

/* 状态中文描述 */
static const char *status_cn(int s)
{
    switch (s) {
    case 0: return "空闲";
    case 1: return "占用";
    case 2: return "故障";
    default: return "未知";
    }
}

int charger_to_json(const charger_info_t *c, char *buf, int buf_size)
{
    return snprintf(buf, buf_size,
        "{\"id\":%d,\"name\":\"%s\",\"location\":\"%s\","
        "\"area\":\"%s\",\"power\":%.1f,\"status\":\"%s\","
        "\"status_cn\":\"%s\",\"update_time\":\"%s\"}",
        c->id, c->name, c->location,
        c->area, c->power, status_str(c->status),
        status_cn(c->status), c->update_time);
}

int chargers_to_json_array(const charger_info_t *arr, int count, char *buf, int buf_size)
{
    int offset = 0;
    int ret;

    ret = snprintf(buf + offset, buf_size - offset, "{\"code\":0,\"data\":[");
    if (ret < 0 || ret >= buf_size - offset) return -1;
    offset += ret;

    for (int i = 0; i < count; i++) {
        if (i > 0) {
            ret = snprintf(buf + offset, buf_size - offset, ",");
            if (ret < 0 || ret >= buf_size - offset) return -1;
            offset += ret;
        }
        ret = charger_to_json(&arr[i], buf + offset, buf_size - offset);
        if (ret < 0 || ret >= buf_size - offset) return -1;
        offset += ret;
    }

    ret = snprintf(buf + offset, buf_size - offset, "],\"count\":%d}", count);
    if (ret < 0 || ret >= buf_size - offset) return -1;
    offset += ret;

    return offset;
}

int stats_to_json(const charger_stats_t *stats, char *buf, int buf_size)
{
    return snprintf(buf, buf_size,
        "{\"code\":0,\"data\":{\"total\":%d,\"idle\":%d,"
        "\"busy\":%d,\"fault\":%d}}",
        stats->total, stats->idle, stats->busy, stats->fault);
}
