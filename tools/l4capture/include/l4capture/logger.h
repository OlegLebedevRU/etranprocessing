#ifndef L4C_LOGGER_H
#define L4C_LOGGER_H
#include "types.h"
#include "telemetry.h"

#ifdef __cplusplus
extern "C" {
#endif

#define L4C_LOG_MAX_FILE_BYTES  5242880u /* 5 MiB */
#define L4C_LOG_MAX_TOTAL_BYTES 10485760u /* 10 MiB */

/* dir_or_null: NULL — каталог модуля (exe). Ротация 5 МиБ × 2 (l4capture.log / .old). */
l4c_status_t l4c_logger_init(const wchar_t *dir_or_null);
void l4c_logger_shutdown(void);

void l4c_logger_line(const char *line);
void l4c_logger_write(const char *fmt, ...);

/* Startup Inventory Header: ОС, CPU, RAM, дисплей, медиа-тракт, лимиты. */
void l4c_logger_startup_inventory(const l4c_inventory_snapshot_t *inv);

/* Служебное/тестовое: размер активного файла и принудительная ротация. */
uint64_t l4c_logger_active_bytes(void);
bool l4c_logger_rotate_if_needed(void);

/* Тестовый scrub: персистентные секреты не попадают в лог. */
void l4c_logger_scrub_secrets(char *line, size_t cap);

#ifdef __cplusplus
}
#endif
#endif /* L4C_LOGGER_H */
