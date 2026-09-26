#include <stdint.h>
#include "l4capture/mft_event_gate.h"

int test_mft_event_gate_reordered(void) {
    l4c_mft_event_gate_t gate = {0};
    l4c_mft_event_gate_record(&gate, L4C_MFT_EVENT_OUTPUT);
    l4c_mft_event_gate_record(&gate, L4C_MFT_EVENT_INPUT);
    if (!l4c_mft_event_gate_take_input(&gate)) return 1;
    if (!l4c_mft_event_gate_take_output(&gate)) return 2;
    if (l4c_mft_event_gate_take_output(&gate)) return 3;
    return 0;
}

int test_mft_event_gate_series(void) {
    l4c_mft_event_gate_t gate = {0};
    l4c_mft_event_gate_record(&gate, L4C_MFT_EVENT_INPUT);
    l4c_mft_event_gate_record(&gate, L4C_MFT_EVENT_INPUT);
    l4c_mft_event_gate_record(&gate, L4C_MFT_EVENT_OUTPUT);
    l4c_mft_event_gate_record(&gate, L4C_MFT_EVENT_OUTPUT);
    if (!l4c_mft_event_gate_take_input(&gate)) return 1;
    if (!l4c_mft_event_gate_take_input(&gate)) return 2;
    if (!l4c_mft_event_gate_take_output(&gate)) return 3;
    if (!l4c_mft_event_gate_take_output(&gate)) return 4;
    return 0;
}

int test_mft_event_gate_deadline(void) {
    if (l4c_mft_deadline_expired(1000, 1099, 100)) return 1;
    if (!l4c_mft_deadline_expired(1000, 1100, 100)) return 2;
    if (!l4c_mft_deadline_expired(1000, 1200, 100)) return 3;
    return 0;
}
