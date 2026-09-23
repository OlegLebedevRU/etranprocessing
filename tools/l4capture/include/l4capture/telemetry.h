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

/* Producer -> IPC -> l4desk adapter (L4C-05). Новых внешних полей нет.
 * READY: video_capture_backend, video_encoder_backend, video_hw_accel (encoder == 2).
 * START/local report: video_profile_requested; actual profile выводится из READY.
 * DEGRADED: video_degradation_state, video_fallback_reason.
 * METRICS: video_stream_fps, video_stream_bitrate (H.264 кбит/с за 1 с).
 * Расширение wire payload v1 запрещено: неподдержанные поля только в local report. */
typedef struct {
    uint16_t fps;
    uint32_t bitrate_kbps;
    uint32_t raw_drops, encoder_drops, transport_drops;
    uint16_t encode_p95_ms, queue_depth;
    uint32_t private_bytes_kb, gdi_handles;
} l4c_metrics_t;
#endif