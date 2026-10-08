#pragma once
#include <windows.h>
#include <stdbool.h>
#include "update_state.h"
#include "link_probe_evidence.h"
typedef struct L4ProbeServer L4ProbeServer;
typedef DWORD (*L4ProbeCallback)(DWORD mode,DWORD timeout_ms,HANDLE cancel,void* context);
typedef DWORD (*L4DrainCallback)(const L4UpdateState* expected,DWORD timeout_ms,HANDLE cancel,void* context);
typedef DWORD (*L4EvidenceCallback)(DWORD timeout_ms,HANDLE cancel,void* context,L4LinkProbeEvidence* evidence);
/* v3 mode4 fresh evidence only: actual SYSTEM/session0 caller, unique local
 * request nonce echoed by this exchange, no cached last-success export. */
bool l4_probe_server_start_evidence(const wchar_t* component,L4ProbeCallback callback,L4DrainCallback drain,
    L4DrainCallback recovery,L4EvidenceCallback evidence,void* context,L4ProbeServer** server);
bool l4_probe_evidence_call(DWORD expected_pid,DWORD timeout_ms,L4LinkProbeEvidence* evidence);
/* Local SYS/BA-only pipe; one instance, server PID verified by the caller.
 * Callback must honor timeout/cancel; no MQTT connections in this module. */
bool l4_probe_server_start(const wchar_t* component,L4ProbeCallback callback,void* context,L4ProbeServer** server);
bool l4_probe_server_start_ex(const wchar_t* component,L4ProbeCallback callback,L4DrainCallback drain,void* context,L4ProbeServer** server);
void l4_probe_server_stop(L4ProbeServer* server);
bool l4_probe_call(const wchar_t* component,DWORD expected_pid,DWORD mode,DWORD timeout_ms);
/* Read-only v2 drain; exact operation/window/generation/plan/deadline echo.
 * Caller owns SCM PID/creation checks and repeats state before any stop. */
bool l4_probe_drain_call(const wchar_t* component,DWORD expected_pid,const L4UpdateState* expected,DWORD timeout_ms);

/* v2 mode3 is a read-only query of independently armed window1 recovery. The
 * supervisor adapter must match exact state + immutable recovery/worker/epochs,
 * active independent deadline monitor and durable ownership. Echo alone is NOT
 * proof. NULL handler/old supervisor refuses, never falls back to drain/health.
 * May query the next state before publication; does not arm or extend deadlines.
 * Callers recheck before/after quiescence; PID+creation is separately held. */
bool l4_probe_server_start_update(const wchar_t* component,L4ProbeCallback callback,
    L4DrainCallback drain,L4DrainCallback recovery,void* context,L4ProbeServer** server);
bool l4_probe_recovery_call(DWORD expected_pid,const L4UpdateState* expected,DWORD timeout_ms);
