#include <windows.h>
#include "l4capture/clock.h"
uint64_t l4c_now_monotonic_ms(void) {
    return GetTickCount64();
}

uint64_t l4c_pts_session_ms(uint64_t *base_ms, uint64_t capture_pts_ms,
                            uint64_t fallback_now_ms, bool *out_rebased) {
    if (out_rebased) *out_rebased = false;
    if (!base_ms) return 0;
    if (capture_pts_ms == 0) capture_pts_ms = fallback_now_ms;
    if (*base_ms == 0) {
        *base_ms = capture_pts_ms;
        return 0;
    }
    if (capture_pts_ms < *base_ms) {
        *base_ms = capture_pts_ms;
        if (out_rebased) *out_rebased = true;
        return 0;
    }
    return capture_pts_ms - *base_ms;
}