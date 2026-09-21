#include "input_gate.h"
#include <string.h>
#include <windows.h>

bool l4d_input_gate_check(const l4d_input_gate_ctx_t *ctx) {
    if (!ctx) return false;

    /* 1. Source must be desktop */
    if (!ctx->is_desktop_source) return false;

    /* 2. Active lease required and not expired */
    if (!ctx->has_active_lease) return false;
    if (ctx->deadline_tick_ms > 0 && GetTickCount64() >= ctx->deadline_tick_ms) return false;

    /* 3. Stream instance must match exactly */
    if (ctx->stream_instance_id == 0 || ctx->stream_instance_id != ctx->expected_stream_id) return false;

    /* 4. Display geometry must be confirmed and unchanged */
    if (ctx->geometry_generation != ctx->expected_geometry_gen) return false;

    /* 5. User MUST have explicitly requested "low" profile */
    if (!ctx->requested_profile || strcmp(ctx->requested_profile, "low") != 0) {
        /* Input for "default" is CATEGORICALLY forbidden */
        return false;
    }

    /* 6. Actual video profile must be strictly base_480p (854x480) */
    if (ctx->actual_width != 854 || ctx->actual_height != 480) {
        return false;
    }

    /* 7. Kiosk Mode: if kiosk is configured and running, it must have focus.
     * If kiosk is NOT running (adaptive fallback), input is NOT blocked. */
    if (ctx->kiosk_mode_enabled && ctx->kiosk_running && !ctx->kiosk_in_focus) {
        return false;
    }

    return true;
}
