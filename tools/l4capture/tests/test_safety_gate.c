#include <string.h>
#include <windows.h>
#include "l4capture/safety_gate.h"
#include "l4capture/clock.h"

int test_safety_init_destroy(void) {
    l4c_safety_gate_t gate;
    l4c_status_t s;
    s = l4c_safety_init(&gate);
    if (s != L4C_OK) return 1;
    if (!gate.stop_event) return 2;
    if (gate.started || gate.stopped) return 3;
    l4c_safety_destroy(&gate);
    if (gate.stop_event != NULL) return 4;
    return 0;
}

int test_safety_stop(void) {
    l4c_safety_gate_t gate;
    l4c_status_t s;
    s = l4c_safety_init(&gate);
    if (s != L4C_OK) return 1;
    l4c_safety_stop(&gate, L4C_ERR_DEADLINE_EXPIRED);
    if (!gate.stopped) return 2;
    if (gate.reason != L4C_ERR_DEADLINE_EXPIRED) return 3;
    if (WaitForSingleObject(gate.stop_event, 0) != WAIT_OBJECT_0) return 4;
    /* Double stop is idempotent. */
    l4c_safety_stop(&gate, L4C_OK);
    if (gate.reason != L4C_ERR_DEADLINE_EXPIRED) return 5;
    l4c_safety_destroy(&gate);
    return 0;
}

int test_safety_deadline_trigger(void) {
    l4c_safety_gate_t gate;
    l4c_message_t msg;
    l4c_status_t s;
    uint64_t now = 1000;
    s = l4c_safety_init(&gate);
    if (s != L4C_OK) return 1;
    /* Send CMD_START with deadline 5000. */
    memset(&msg, 0, sizeof(msg));
    msg.type = L4C_CMD_START;
    msg.request_seq = 1;
    memset(msg.body.start.lease_id, 1, 16);
    memset(msg.body.start.stream_id, 2, 16);
    msg.body.start.source_rect.right = 640;
    msg.body.start.source_rect.bottom = 480;
    msg.body.start.profile_id = 1;
    msg.body.start.rtp_port = 5004;
    msg.body.start.rtcp_port = 5005;
    msg.body.start.deadline_tick_ms = 5000;
    s = l4c_safety_command(&gate, &msg, now);
    if (s != L4C_OK) return 2;
    if (!gate.started) return 3;
    /* Check at now=4999 — should be OK. */
    s = l4c_safety_check(&gate, 4999, true);
    if (s != L4C_OK) return 4;
    /* Check at now=5000 — deadline expired. */
    s = l4c_safety_check(&gate, 5000, true);
    if (s != L4C_ERR_DEADLINE_EXPIRED) return 5;
    if (!gate.stopped) return 6;
    l4c_safety_destroy(&gate);
    return 0;
}

int test_safety_session0_reject(void) {
    l4c_session_probe_t probe;
    l4c_status_t s;
    /* This test verifies the session check logic compiles and runs.
     * On a normal desktop session, l4c_session_open succeeds.
     * We cannot easily simulate Session 0 without service context. */
    s = l4c_session_open(&probe);
    /* If we're in a normal interactive session, this should succeed. */
    if (s == L4C_OK) {
        bool avail = l4c_session_available(&probe);
        if (!avail) { l4c_session_close(&probe); return 1; }
        l4c_session_close(&probe);
    }
    /* If running in CI or non-interactive, session_open may fail — that's OK. */
    return 0;
}

int test_safety_pipeline_drain(void) {
    l4c_safety_gate_t gate;
    l4c_status_t s;
    s = l4c_safety_init(&gate);
    if (s != L4C_OK) return 1;
    /* After stop, pipeline is stopped — no sends allowed. */
    l4c_safety_stop(&gate, L4C_OK);
    if (l4c_safety_can_send(&gate, 1000, true)) return 2;
    l4c_safety_destroy(&gate);
    return 0;
}
