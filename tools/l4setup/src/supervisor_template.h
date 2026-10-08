#pragma once
#include "remote_worker_plan.h"
#include "../../l4common/recovery_plan.h"
#include "../../l4common/worker_job.h"
typedef struct SetupSupervisorTemplate SetupSupervisorTemplate;
/* Actual missing l4_worker_start.recovery prerequisite: signed operation64,
 * fixed operation-member config20, original live SCM/process/token epoch.
 * Explicit policy times/recovery_ms come from trusted controller only, not RPC.
 * Returned recovery template has worker PID/birth ZERO (no worker exists).
 * Owns config bytes/SD and source/target signature pins + supervisor process.
 * No journal append, helper receipt/installation, task/Job, barrier, watchdog
 * arm, stop/apply or READY. Caller must supply authenticated helper and actual
 * implemented worker, then freshly recheck immediately BEFORE worker_start and
 * after startup BEFORE transfer (cancel only owned Job if that recheck fails).
 * Recheck requires original locked journal; no installed-source pointer retained.
 * Do not use this object after handoff/window publication; copy needed typed
 * recovery into worker_start while pins remain held before transfer. Returned
 * plan/config pointers borrow template storage: a shallow struct copy does NOT
 * own config bytes; keep template alive across worker_start/publication. */
bool setup_supervisor_template_prepare(L4Journal* journal,const SetupRemoteWorkerPlan* worker_plan,
    ULONGLONG armed_utc,ULONGLONG deadline_utc,DWORD recovery_ms,DWORD timeout,
    const volatile LONG* cancelled,SetupSupervisorTemplate** result);
bool setup_supervisor_template_verify(L4Journal* journal,const SetupSupervisorTemplate* template);
/* Post-start, BEFORE68: exact66/template+owned worker,67/task spec+fresh audit,
 * optional70 validated file/journal/source refs. Strict order, no duplicates,
 * active window/apply/history continuation. communication selects exact phase.
 * Still read-only, no watchdog arm/stop permission. */
bool setup_supervisor_template_verify_started(L4Journal* journal,const SetupSupervisorTemplate* template,
    L4WorkerJob* worker,const L4RecoveryHelper* helper,DWORD overhead_ms,bool communication);
const L4RecoveryPlan* setup_supervisor_template_plan(const SetupSupervisorTemplate* template);
void setup_supervisor_template_free(SetupSupervisorTemplate* template);
