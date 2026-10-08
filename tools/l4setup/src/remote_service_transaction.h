#pragma once
#include "remote_service.h"
#define L4_RECORD_SERVICE_ACTION_INTENT 100u
#define L4_RECORD_SERVICE_ACTION_DONE 101u
#define L4_RECORD_SERVICE_RESTORE_INTENT 104u
#define L4_RECORD_SERVICE_RESTORE_DONE 105u
#define L4_RECORD_SERVICE_ACTION_ABANDON 106u
#define L4_RECORD_SERVICE_CONFIG_ABANDON 107u
typedef enum { SETUP_SERVICE_RESTORE_STOP=1,SETUP_SERVICE_RESTORE_SWITCH=2,
    SETUP_SERVICE_RESTORE_START=3,SETUP_SERVICE_RESTORE_VERIFY=4 } SetupServiceRestore;
typedef enum { SETUP_SERVICE_ACTION_STOP=1,SETUP_SERVICE_ACTION_SWITCH=2,SETUP_SERVICE_ACTION_START=3 } SetupServiceAction;
typedef struct SetupServiceTransaction SetupServiceTransaction;
/* Single-hop forward SCM journal only; no readiness/commit/rollback authority.
 * Borrow original locked journal, held owner-authenticated pinned operation64,
 * and four independently owned native service objects for the whole lifetime.
 * Caller owns verified pre-stop/watchdog and window publication. */
bool setup_service_transaction_open(L4Journal* original,const SetupOperationPlan* operation,
    SetupRemoteService* services[4],SetupServiceTransaction** transaction);
/* Intent100 is flushed before native mutation, done101 only after fresh readback.
 * Per-service STOP->SWITCH_TARGET->START_TARGET; one unresolved intent globally.
 * False can mean native mutation occurred: consult recovery_required, do not
 * retry native action. Exact completed retries do readback, never mutate. */
bool setup_service_transaction_execute(SetupServiceTransaction* transaction,unsigned service,
    SetupServiceAction action,const L4UpdateState* active,DWORD timeout_ms);
/* Readback only of retained owned epoch. A pending own START may finish its
 * existing bounded startup observation, but StartService is never repeated.
 * A process restart/lost handles cannot manufacture completion or adopt PID. */
bool setup_service_transaction_reconcile(SetupServiceTransaction* transaction,
    const L4UpdateState* active,DWORD timeout_ms);
bool setup_service_transaction_recovery_required(const SetupServiceTransaction* transaction);
/* Read-only replay of owned unresolved intent: 0,21,100 or104. Not permission
 * to repeat a mutation. Rollback may invoke only its corresponding audit. */
bool setup_service_transaction_pending(SetupServiceTransaction* transaction,DWORD* kind);
/* Exact original borrowed objects, complete durable START101 for all four,
 * freshly observed target owned epochs under current window2. Caller must
 * retain/rebind ALL four native objects to window2. No app/channel/terminal
 * readiness: repeat before/after real application probes and fresh barrier. */
bool setup_service_transaction_complete(SetupServiceTransaction* transaction,L4Journal* original,
    const SetupOperationPlan* operation,SetupRemoteService* services[4],
    const L4UpdateState* window2,DWORD timeout_ms);
/* Continuing admitted SYSTEM worker + fresh original Job/task/parent authority
 * and independent recovery WAIT mandatory. Explicit rollback only; no retry of
 * an unresolved native mutation. VERIFY skips an untouched owned old RUNNING
 * service. No terminal success/guard completion/marker clear or network claim. */
bool setup_service_transaction_restore(SetupServiceTransaction* transaction,unsigned service,
    SetupServiceRestore action,const L4UpdateState* active,DWORD timeout_ms);
bool setup_service_transaction_restore_reconcile(SetupServiceTransaction* transaction,
    const L4UpdateState* active,DWORD timeout_ms);
/* Known actual retained SCM/epoch closes a pending forward100 for rollback.
 * Config version checks exact old/candidate bytes/SD and closes pending21 only.
 * Neither API mutates services/configs or invents a successful forward action. */
bool setup_service_transaction_abandon(SetupServiceTransaction* transaction,
    const L4UpdateState* active,DWORD timeout_ms);
bool setup_service_transaction_abandon_config(SetupServiceTransaction* transaction,
    const L4UpdateState* active,DWORD timeout_ms);
/* All4 durable old restore105 + fresh retained owned old RUNNING epochs only.
 * Caller still must verify full old applications/REQ+EVT, win guard DONE
 * arbitration, record honest old outcome103 and only then clear active marker. */
bool setup_service_transaction_restored(SetupServiceTransaction* transaction,
    const L4UpdateState* active,DWORD timeout_ms);
/* Fresh restored predicate FIRST, then copy actual105 sequences. No terminal
 * proof; opaque whole-old producer additionally verifies apps/barrier/guards. */
bool setup_service_transaction_restore_records(SetupServiceTransaction* transaction,
    const L4UpdateState* active,DWORD timeout_ms,ULONGLONG sequences[4]);
void setup_service_transaction_close(SetupServiceTransaction* transaction);
