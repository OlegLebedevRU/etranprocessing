#ifndef L4CAPTURE_IDLE_REFRESH_H
#define L4CAPTURE_IDLE_REFRESH_H

#include <stdbool.h>
#include <stdint.h>

/* A refresh requires a fresh capture. This helper only schedules that capture. */
static inline bool l4c_idle_refresh_due(uint64_t now_ms, uint64_t last_dxgi_frame_ms,
                                         uint64_t last_refresh_ms, uint32_t interval_ms) {
    if (!interval_ms || !last_dxgi_frame_ms || now_ms < last_dxgi_frame_ms ||
        now_ms - last_dxgi_frame_ms < interval_ms) return false;
    if (last_refresh_ms &&
        (now_ms < last_refresh_ms || now_ms - last_refresh_ms < interval_ms)) return false;
    return true;
}

/* Three startup recovery chances for a viewer joining after the first IDR. */
static inline bool l4c_bootstrap_refresh_due(uint64_t now_ms, uint64_t stream_start_ms,
                                              uint32_t completed_attempts) {
    static const uint32_t offsets_ms[] = { 1500u, 3500u, 7500u };
    if (!stream_start_ms || completed_attempts >= 3u || now_ms < stream_start_ms ||
        now_ms - stream_start_ms > 10000u) return false;
    return now_ms - stream_start_ms >= offsets_ms[completed_attempts];
}

#endif
