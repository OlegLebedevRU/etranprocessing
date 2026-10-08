#pragma once
#include "communication_recovery.h"
/* Immutable window1 recovery binding. Fixed first-hop old pair only; producer
 * must authenticate source/all-hop inventory/operation64 before saving. No
 * generic paths/commands/URLs, downloaded metadata keys or inherited handles.
 * Original raw record10/20 copies retain exact commands/config bytes/ACLs. */
#define L4_COMMUNICATION_PLAN_LIMIT L4_JOURNAL_MAX_RECORD
typedef struct {
    GUID operation;ULONGLONG sequence,generation,armed_utc,deadline_utc;
    DWORD worker_pid,supervisor_pid;FILETIME worker_created,supervisor_created;
    BYTE operation_sha256[32];L4CommunicationBudget budget;
    ULONGLONG switch_sequence[2],config_sequence[2];
    const BYTE* switches[2];DWORD switch_size[2];
    const BYTE* configs[2];DWORD config_size[2];
    /* Signed operation first-hop Con switch, original process epoch and selected
     * old certificate captured before stop. No new console adoption at restart. */
    ULONGLONG con_sequence;const BYTE* con_switch;DWORD con_size,con_pid;
    FILETIME con_created;char thumbprint[64];
} L4CommunicationPlan;
bool l4_communication_plan_encode(const L4Layout* roots,const L4CommunicationPlan* plan,BYTE** bytes,DWORD* size);
/* Borrowed byte pointers remain valid only while caller retains original bytes.
 * Hash protects corruption/binding, NOT producer authentication/signature. */
bool l4_communication_plan_decode(const L4Layout* roots,const BYTE* bytes,DWORD size,L4CommunicationPlan* plan);
/* Independent protected reader; no deployment.lock, no missing-file creation.
 * Holds immutable file and parents against replacement/write across its use.
 * Reading/validating a plan is NOT evidence of an armed watchdog. */
typedef struct L4CommunicationPin L4CommunicationPin;
/* Trusted producer under original deployment.lock, explicit clear generation.
 * Caller verifies signed source/all-hop metadata, original supervisor SCM and
 * original worker Job confinement BEFORE this call. Storage checks live epochs,
 * operation64 digest/first two switch refs/config membership, exact record bytes
 * and current old config snapshots; it does NOT authenticate source signatures.
 * Both broker files must already exist. Intent70 then immutable exact retry;
 * conflicts/corrupt partial publication never repaired or overwritten. */
bool l4_communication_plan_prepare(L4Journal* journal,const L4CommunicationPlan* plan,HANDLE worker);
bool l4_communication_plan_open(const L4Layout* roots,const wchar_t* operation,L4CommunicationPin** pin);
const L4CommunicationPlan* l4_communication_pinned_plan(const L4CommunicationPin* pin);
/* Original locked journal only; read-only exact70/file and signed64 references/
 * record10/20/config binding. Caller still authenticates64's owner metadata.
 * No creation, publication, arm/stop or replacement of a partial plan. */
bool l4_communication_plan_verify_journal(L4Journal* journal,const L4CommunicationPin* pin);
/* Exact state identity only; no fresh transport/SCM, timer/Job or arm proof. */
bool l4_communication_plan_matches(const L4CommunicationPin* pin,const L4UpdateState* state);
void l4_communication_plan_close(L4CommunicationPin* pin);
typedef enum {
    L4_COMM_DEC_WAIT=0,L4_COMM_DEC_STARTED=1,L4_COMM_DEC_COMMITTED=2,
    L4_COMM_DEC_RESTORED=3,L4_COMM_DEC_UNCONFIRMED=4,L4_COMM_DEC_FAILED=5
} L4CommunicationPhase;
typedef struct L4CommunicationDecision L4CommunicationDecision;
/* Existing protected plan/locks only; independent of deployment.lock. Decision
 * mutex is short-lived. Release it BEFORE worker exit or journal acquisition;
 * original immutable pins and exclusive runner lock remain held across work. */
bool l4_communication_decision_open(const L4Layout* roots,const wchar_t* operation,DWORD timeout,L4CommunicationDecision** decision);
void l4_communication_decision_close(L4CommunicationDecision* decision);
void l4_communication_decision_release(L4CommunicationDecision* decision);
bool l4_communication_decision_relock(L4CommunicationDecision* decision,DWORD timeout);
bool l4_communication_decision_read(L4CommunicationDecision* decision,L4CommunicationPhase* phase,DWORD* error);
/* Exact active window1 marker plus deadline or verified SYSTEM boot only.
 * Plan alone/clear/mismatched marker always refuses. One runner. STARTED boot continuation
 * requires old runner gone (OS lock available); normal repeat refuses. Worker
 * can never commit after STARTED. Boot evidence comes from native startup,
 * never RPC input. This layer does not validate SCM or execute recovery. */
bool l4_communication_decision_begin(L4CommunicationDecision* decision,ULONGLONG now,bool boot_reconciled);
/* COMMITTED is storage DONE after externally verified new link, or a fully
 * verified whole old suite plus fresh barrier, before the communication deadline.
 * It is NOT new-suite/success authority: only authenticated102/108 distinguish
 * the verified new/old state. Existing DONE is read-only arbitration on window2,
 * never permission for late mutation. Other results require this guard's exclusive runner after
 * STARTED. UNCONFIRMED/FAILED block automatic retries; never clear marker here. */
bool l4_communication_decision_finish(L4CommunicationDecision* decision,L4CommunicationPhase phase,DWORD error,ULONGLONG now);
