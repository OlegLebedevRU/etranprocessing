#ifndef L4C_TELEMETRY_H
#define L4C_TELEMETRY_H
#include "types.h"

enum { L4C_CAPTURE_GDI = 1, L4C_CAPTURE_DXGI = 2 };
enum { L4C_ENCODER_OPENH264_SOFTWARE = 1, L4C_ENCODER_MF_HARDWARE = 2 };
enum { L4C_PROFILE_480P = 1, L4C_PROFILE_540P = 2, L4C_PROFILE_720P = 3 };
enum { L4C_NOMINAL = 0, L4C_FPS_THROTTLED = 1, L4C_DEGRADED_540P = 2, L4C_DEGRADED_480P = 3 };
enum { L4C_FALLBACK_NONE = 0, L4C_DXGI_ACCESS_LOST = 1, L4C_MFT_UNAVAILABLE = 2,
       L4C_WIN7_LEGACY = 3, L4C_HIGH_LOAD = 4 };
#define L4C_FALLBACK_MFT_UNAVAILABLE L4C_MFT_UNAVAILABLE

/* Producer -> IPC -> l4desk adapter (L4C-05). Wire freeze: EVENT_METRICS = 30 байт.
 * READY: video_capture_backend, video_encoder_backend, video_hw_accel (encoder == 2).
 * START/local report: video_profile_requested; actual profile выводится из READY.
 * DEGRADED: video_degradation_state, video_fallback_reason.
 * METRICS: измеренные fps/bitrate/p95/ресурсы (не плейсхолдеры).
 * Неподдержанные wire-поля — только в local inventory log. */
typedef struct {
    uint16_t fps;
    uint32_t bitrate_kbps;
    uint32_t raw_drops, encoder_drops, transport_drops;
    uint16_t encode_p95_ms, queue_depth;
    uint32_t private_bytes_kb, gdi_handles;
} l4c_metrics_t;

#define L4C_TELEMETRY_P95_MAX 256u

/* Реальные ресурсы процесса (Win32). */
typedef struct {
    uint32_t private_bytes_kb;
    uint32_t working_set_kb;
    uint32_t gdi_handles;
    uint32_t user_handles;
    uint32_t kernel_handles;
    bool valid;
} l4c_process_resources_t;

/* Окно перцентиля времени кодирования (1 с). */
typedef struct {
    uint32_t samples[L4C_TELEMETRY_P95_MAX];
    uint32_t count;
} l4c_p95_window_t;

/* Окно измеренного FPS/битрейта (1 с). */
typedef struct {
    uint64_t start_ms;
    uint32_t au_count;
    uint32_t au_bytes;
} l4c_rate_window_t;

/* Слепок платформы для Startup Inventory Header (§8 / §4.4). */
typedef struct {
    uint32_t os_major, os_minor, os_build;
    char service_pack[64];
    char os_arch[8];
    char os_product[32];
    uint32_t cpu_cores;
    char cpu_model[64];
    uint64_t ram_total_kb, ram_avail_kb;
    int32_t display_w, display_h;
    l4c_rect_t source_rect;
    bool dpi_aware;
    uint8_t profile_requested;
    uint16_t profile_actual;
    uint16_t capture_backend, encoder_backend;
    uint16_t fallback_reason;
    uint8_t start_fps;
    uint16_t bitrate_min_kbps, bitrate_target_kbps, bitrate_max_kbps;
    uint32_t lease_or_loop_ms;
} l4c_inventory_snapshot_t;

/* Динамическое разрешение psapi (Win7 / Embedded без жёсткой зависимости). */
l4c_status_t l4c_telemetry_init(void);
void l4c_telemetry_fini(void);

/* Private Bytes / WS / GDI / User / Kernel handles. false при недоступном API. */
bool l4c_telemetry_process_resources(l4c_process_resources_t *out);

/* Платформенный инвентарь (ОС, CPU, RAM, дисплей). */
bool l4c_telemetry_fill_platform_inventory(l4c_inventory_snapshot_t *out);

void l4c_p95_window_reset(l4c_p95_window_t *w);
bool l4c_p95_window_push(l4c_p95_window_t *w, uint32_t sample_ms);
bool l4c_p95_window_eval(const l4c_p95_window_t *w, uint16_t *out_p95_ms);

void l4c_rate_window_reset(l4c_rate_window_t *w, uint64_t now_ms);
void l4c_rate_window_add_au(l4c_rate_window_t *w, uint32_t au_bytes);
/* window_ms обычно 1000. fps = AU/окно, bitrate = (bytes*8)/window_ms. */
bool l4c_rate_window_eval(const l4c_rate_window_t *w, uint64_t now_ms, uint32_t window_ms,
                          uint16_t *out_fps, uint32_t *out_bitrate_kbps);
#endif
