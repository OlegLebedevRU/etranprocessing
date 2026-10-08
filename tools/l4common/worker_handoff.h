#pragma once
#include "worker_job.h"
#include "update_state.h"
/* SYSTEM controller retains Job; original journal consumed ONLY on success.
 * Flush record68 then close original journal/deployment lock. Failure retains
 * journal for owner cancellation/recovery; immutable recovery/task never removed. */
bool l4_worker_transfer(L4Journal** journal,L4WorkerJob* worker,const L4RecoveryHelper* helper,DWORD overhead_ms);
/* SYSTEM child, canonical known-folder roots, original UUID only. Bounded polling
 * existing journal lock; no missing lock/operation creation. Exact self epoch/Job,
 * live original parent, plan digest, clear generation, WAIT and fresh task audit.
 * Flush one receipt69, then return held journal. No marker/SCM/apply permission;
 * reload signed operation64/pre-stop admission before any actual update. Parent
 * must retain Job until terminal result; receipt is not an external RPC event. */
bool l4_worker_accept(const wchar_t* operation,DWORD timeout_ms,L4Journal** journal);
typedef struct {
    ULONGLONG sequence,generation,deadline_utc,old_size,new_size;
    DWORD supervisor_pid,start_type;FILETIME supervisor_created;
    DWORD parent_pid;FILETIME parent_created; /* Verified original ticket68 epoch. */
    L4RecoveryHelper helper; /* Exact task-audited helper identity from ticket68. */
    wchar_t before[2048],after[2048];BYTE old_sha256[32],new_sha256[32];
} L4WorkerAdmission;
/* Read-only fresh startup recheck while receipt69 is still last; exact SYSTEM
 * child, live original parent/Job, receipt/ticket hash, clear/WAIT/task audit.
 * Fixed values copied from immutable recovery plan; no borrowed config pointers.
 * Not source admission or permission to publish an update window. */
bool l4_worker_recheck(L4Journal* journal,L4WorkerAdmission* admission);
/* Continuing original worker only, after an active window publication. Exact
 * ticket68/receipt69, worker/parent epochs, private Job and fresh task audit;
 * helper must still WAIT. Does not accept a terminal outcome or authenticate
 * action/config/metadata semantics: their owning executor must recheck them.
 * Startup recheck remains clear-state/last69 only. No mutation/stop permission. */
bool l4_worker_recheck_active(L4Journal* journal,const L4UpdateState* active,L4WorkerAdmission* admission);
