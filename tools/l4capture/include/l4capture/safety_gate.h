#ifndef L4C_SAFETY_GATE_H
#define L4C_SAFETY_GATE_H
#include <windows.h>
#include "deadline.h"
#include "ipc_protocol.h"
#include "pipeline.h"

typedef struct {
    SRWLOCK lock;
    HANDLE stop_event;
    l4c_deadline_t lease;
    l4c_pipeline_t pipeline;
    uint8_t lease_id[16], stream_id[16];
    bool started, stopped, idr_pending, idr_sent;
    uint64_t last_idr_ms, last_command_seq;
    l4c_status_t reason;
} l4c_safety_gate_t;

typedef struct {
    DWORD session_id;
    HMODULE wts_module;
    FARPROC query_session;
    FARPROC free_memory;
} l4c_session_probe_t;

l4c_status_t l4c_safety_init(l4c_safety_gate_t *gate);
void l4c_safety_destroy(l4c_safety_gate_t *gate);
void l4c_safety_stop(l4c_safety_gate_t *gate, l4c_status_t reason);
l4c_status_t l4c_safety_check(l4c_safety_gate_t *gate, uint64_t now, bool desktop_available);
l4c_status_t l4c_safety_command(l4c_safety_gate_t *gate, const l4c_message_t *message, uint64_t now);
bool l4c_safety_can_send(l4c_safety_gate_t *gate, uint64_t now, bool desktop_available);
bool l4c_safety_take_idr(l4c_safety_gate_t *gate, uint64_t now);
l4c_status_t l4c_safety_reason(l4c_safety_gate_t *gate);
l4c_status_t l4c_check_job(void);
l4c_status_t l4c_session_open(l4c_session_probe_t *probe);
bool l4c_session_available(const l4c_session_probe_t *probe);
void l4c_session_close(l4c_session_probe_t *probe);
#endif