#pragma once
#include "update_metadata.h"
typedef struct SetupRemoteService SetupRemoteService;
/* Native SYSTEM/session0 only. Caller holds an owner-authenticated operation64
 * and passes its exact switch10 member/reference AFTER verified pre-stop gate.
 * Adapter repeats raw10, native roots, fixed4 image paths, current marker and
 * SCM/process identity; it does NOT authenticate metadata or grant stop authority.
 * Requires original RUNNING old epoch once; cannot adopt a stopped/foreign/new
 * epoch after restart. Owns approved old/new image pins through close. */
bool setup_remote_service_open(L4Journal* original,ULONGLONG switch_reference,
    const L4ServiceSwitch* authenticated_member,const L4UpdateState* expected,DWORD timeout_ms,
    SetupRemoteService** service);
/* Only exact same owner/64 window1->2 generation+1 already published in native
 * state, bounded by caller's authenticated original recovery deadline. No marker
 * write, deadline extension/rebinding within a window or process re-adoption. */
bool setup_remote_service_rebind_state(SetupRemoteService* service,
    const L4UpdateState* window2,ULONGLONG original_recovery_deadline);
/* Exact retained owned epoch, STOPPED AND process exit, one monotonic budget.
 * No forced termination or epoch substitution. Failure may have requested STOP. */
bool setup_remote_service_stop(SetupRemoteService* service,DWORD timeout_ms);
/* Strict stopped-only common switch/rollback; caller journals intent FIRST.
 * False after native mutation is not proof that SCM remained unchanged. */
bool setup_remote_service_switch(SetupRemoteService* service,bool target,DWORD timeout_ms);
/* No approved old/new survivors, exact stopped SCM configuration, then native
 * start and retain new PID/birth. Failure can leave own pending/running service;
 * caller must durably reconcile/rollback, never fabricate a terminal outcome. */
bool setup_remote_service_start(SetupRemoteService* service,bool target,DWORD timeout_ms);
/* Explicit readback after own successful StartService/pending timeout. Never
 * starts again. Capture requires PID birth within that exact native call's
 * before/after timestamps; later/foreign epochs refuse instead of adoption. */
bool setup_remote_service_observe_start(SetupRemoteService* service,DWORD timeout_ms);
/* Fresh owned RUNNING PID/birth proof for real caller application probe. Not
 * readiness: caller probes this PID, then repeats this API and channel barriers. */
bool setup_remote_service_running(SetupRemoteService* service,DWORD* pid,FILETIME* created);
typedef enum { SETUP_SERVICE_STOPPED_OLD=1,SETUP_SERVICE_STOPPED_TARGET,
    SETUP_SERVICE_RUNNING_OLD,SETUP_SERVICE_RUNNING_TARGET } SetupRemoteServicePhase;
typedef struct { SetupRemoteServicePhase phase;L4UpdateState state;ULONGLONG switch_reference;DWORD pid;FILETIME created; } SetupRemoteServiceObservation;
/* Read-only fresh configuration/status + retained epoch + survivor/marker
 * recheck. STOPPED includes retained process exit. Never captures/adopts an
 * epoch, starts/stops/switches or confers application/channel readiness. */
bool setup_remote_service_observe(SetupRemoteService* service,SetupRemoteServiceObservation* observation);
void setup_remote_service_close(SetupRemoteService* service);
/* These primitives do not append100/101, clear markers or claim crash safety.
 * Executor owns typed action intents/completions and ambiguity recovery. All
 * methods retain original journal borrow; close BEFORE original journal closes. */
