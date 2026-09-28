#ifndef L4D_INPUT_GATE_H
#define L4D_INPUT_GATE_H

#include <stdbool.h>
#include <stdint.h>

typedef struct l4d_input_gate_ctx {
    bool is_desktop_source;
    bool has_active_lease;
    uint64_t deadline_tick_ms;
    uint64_t stream_instance_id;
    uint64_t expected_stream_id;
    uint64_t geometry_generation;
    uint64_t expected_geometry_gen;
    const char *requested_profile;
    uint16_t actual_width;
    uint16_t actual_height;
    bool kiosk_mode_enabled;
    bool kiosk_running;
    bool kiosk_in_focus;
} l4d_input_gate_ctx_t;

bool l4d_input_gate_check(const l4d_input_gate_ctx_t *ctx);

#endif /* L4D_INPUT_GATE_H */
