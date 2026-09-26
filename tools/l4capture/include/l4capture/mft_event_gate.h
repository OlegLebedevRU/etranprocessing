#ifndef L4CAPTURE_MFT_EVENT_GATE_H
#define L4CAPTURE_MFT_EVENT_GATE_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    L4C_MFT_EVENT_INPUT = 1,
    L4C_MFT_EVENT_OUTPUT = 2
} l4c_mft_event_kind_t;

typedef struct {
    uint8_t input_tokens;
    uint8_t output_tokens;
} l4c_mft_event_gate_t;

static inline void l4c_mft_event_gate_record(l4c_mft_event_gate_t *gate,
                                              l4c_mft_event_kind_t event) {
    uint8_t *tokens;
    if (!gate) return;
    tokens = event == L4C_MFT_EVENT_INPUT ? &gate->input_tokens : &gate->output_tokens;
    if (*tokens < 2) ++*tokens;
}

static inline bool l4c_mft_event_gate_take_input(l4c_mft_event_gate_t *gate) {
    if (!gate || !gate->input_tokens) return false;
    --gate->input_tokens;
    return true;
}

static inline bool l4c_mft_event_gate_take_output(l4c_mft_event_gate_t *gate) {
    if (!gate || !gate->output_tokens) return false;
    --gate->output_tokens;
    return true;
}

static inline bool l4c_mft_deadline_expired(uint64_t start_ms, uint64_t now_ms,
                                            uint32_t budget_ms) {
    return now_ms - start_ms >= budget_ms;
}

#endif
