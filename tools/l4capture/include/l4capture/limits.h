#ifndef L4C_LIMITS_H
#define L4C_LIMITS_H
#include "types.h"

#define L4C_MAX_PIXELS_AREA UINT64_C(8294400)
#define L4C_MAX_AU_SIZE 2097152u
#define L4C_ADMISSION_MEMORY_LIMIT 134217728u
#define L4C_TARGET_PRIVATE_BYTES_480P 47185920u
#define L4C_IPC_MAX_PAYLOAD 4096u
#define L4C_FORCE_IDR_MIN_INTERVAL_MS 500u
#define L4C_STATUS_QUEUE_CAPACITY 16u
#define L4C_SAFETY_POLL_MS 25u
#define L4C_DESKTOP_POLL_MS 50u
#define L4C_WRITER_TIMEOUT_MS 450u /* Запас до жёсткого предела 500 мс. */

typedef struct {
    uint32_t stride;
    size_t bytes;
} l4c_raster_layout_t;

l4c_status_t l4c_checked_mul(size_t a, size_t b, size_t *out);
l4c_status_t l4c_checked_add(size_t a, size_t b, size_t *out);
l4c_status_t l4c_bgra_layout(uint32_t width, uint32_t height, l4c_raster_layout_t *out);
l4c_status_t l4c_rect_layout(const l4c_rect_t *rect, l4c_raster_layout_t *out);
/* Все управляемые поверхности, включая encoder context и AU, резервируются до malloc. */
l4c_status_t l4c_memory_reserve(size_t *used, size_t bytes);
void l4c_memory_release(size_t *used, size_t bytes);
#endif