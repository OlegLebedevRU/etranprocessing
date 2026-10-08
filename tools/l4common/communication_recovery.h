#pragma once
#include "update_state.h"

/* Fixed window1 policy owned by L4Superv. This is an executor core, not a native
 * SCM adapter, a timer thread, a durable recovery plan or an authorization.
 * The adapter must authenticate/pin the original signed pair/config snapshots,
 * bind original worker Job+epoch and supervisor epoch, and durably arbitrate ONE
 * rollback against the worker. No downloaded commands, replacement MQTT client,
 * ordinary supervisor orchestration, marker clearing or implicit retries here.
 * Callbacks MUST honor their remaining budget. This core detects late return,
 * but cannot preempt blocked WinAPI; independent native execution protection is
 * an adapter requirement, not established by modeled callback tests. */
typedef struct {
    DWORD verify_ms,worker_ms,lock_ms,proxy_ms,broker_prepare_ms,mosquitto_ms,channel_ms,total_ms;
} L4CommunicationBudget;
typedef enum {
    L4_COMM_STOP_PROXY, L4_COMM_RESTORE_PROXY, L4_COMM_START_PROXY, L4_COMM_PROBE_PROXY,
    L4_COMM_PROXY_CHANNELS,
    L4_COMM_STOP_MOSQUITTO, L4_COMM_RESTORE_MOSQUITTO, L4_COMM_START_MOSQUITTO,
    L4_COMM_PROBE_MOSQUITTO, L4_COMM_FINAL_BARRIER
} L4CommunicationStep;
typedef struct {
    ULONGLONG (*monotonic)(void* context);
    ULONGLONG (*utc)(void* context);
    /* Check exact current marker and immutable signed recovery ownership.
     * First check is read-only WITHOUT deployment.lock; second is under lock. */
    bool (*verify)(const L4UpdateState* expected,DWORD remaining_ms,void* context);
    /* Stop only the original owned Job and observe original process + ALL Job
     * children exit. Missing/reused PID is never permission to kill that PID. */
    bool (*worker_exit)(DWORD remaining_ms,void* context);
    bool (*lock)(DWORD remaining_ms,void* context);
    void (*unlock)(void* context);
    /* STOP includes STOPPED + actual process exit; RESTORE includes pinned old
     * image AND fixed config/ACL snapshots. PROBE is fresh actual service/PID
     * readiness. PROXY_CHANNELS checks actual local signal endpoints/certificate;
     * it MUST NOT depend on current broker or IoT host availability (the broker
     * may be broken). FINAL_BARRIER uses existing transport: fresh REQ/RSP+EVT/EVA. */
    bool (*step)(L4CommunicationStep step,DWORD remaining_ms,void* context);
    void* context;
} L4CommunicationRecoveryOps;
typedef enum { L4_COMM_FAILED, L4_COMM_VERIFIED, L4_COMM_CONNECTIVITY_UNCONFIRMED } L4CommunicationOutcome;
typedef struct {L4CommunicationOutcome outcome;DWORD error;unsigned completed;} L4CommunicationResult;
typedef struct {volatile LONG spent;} L4CommunicationAttempt;

/* Sum covers TWO verifications/channel checks, worker exit, lock and full pair phases.
 * Proxy phases share proxy_ms; broker stop/restore share broker_prepare_ms;
 * broker start/probe share EXACTLY 300000ms, without spending it on stop/restore. All budgets
 * explicit, each 100..300000, total >= sum and <=3600000; no guessed defaults. */
bool l4_communication_budget_valid(const L4CommunicationBudget* budget);
/* Native composition additionally reserves one verify_ms for plan/claim and one
 * for terminal decision publication; both are INSIDE total_ms. */
bool l4_communication_native_budget_valid(const L4CommunicationBudget* budget);
/* Exclusive one-shot attempt, even on failure. boot_reconciled is evidence from
 * the native startup adapter, never an RPC flag; otherwise UTC deadline required.
 * The adapter must durably claim before invoking this process-local executor.
 * No unlock before worker exit, no Mosquitto action before proxy+local signal checks.
 * Final barrier failure keeps locally verified old pair, returns unconfirmed;
 * caller must retain active marker and report evidence, never announce success. */
bool l4_communication_recover(L4CommunicationAttempt* attempt,const L4UpdateState* expected,
    const L4CommunicationBudget* budget,const L4CommunicationRecoveryOps* ops,
    bool boot_reconciled,L4CommunicationResult* result);
