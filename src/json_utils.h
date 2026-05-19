#ifndef JSON_UTILS_H
#define JSON_UTILS_H

#include "sqlite_db.h"

/* 将单个充电桩转为JSON */
int charger_to_json(const charger_info_t *c, char *buf, int buf_size);

/* 将充电桩数组转为JSON数组 */
int chargers_to_json_array(const charger_info_t *arr, int count, char *buf, int buf_size);

/* 将统计信息转为JSON */
int stats_to_json(const charger_stats_t *stats, char *buf, int buf_size);

#endif /* JSON_UTILS_H */
