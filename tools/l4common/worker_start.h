#pragma once
#include "worker_job.h"
typedef struct {
    const wchar_t *executable,*directory,*environment;
    wchar_t* command;
    L4RecoveryPlan recovery; /* authenticated supervisor template, worker epoch ZERO */
    L4RecoveryHelper helper; /* authenticated immutable bootstrap receipt */
    DWORD overhead_ms,cleanup_ms;
} L4WorkerStart;
typedef enum {L4_START_VALIDATE=1,L4_START_JOB,L4_START_SPAWN,L4_START_PLAN,L4_START_ARM,L4_START_RESUME,L4_START_VERIFY,L4_START_RUNNING} L4WorkerStartStage;
/* plan_published=true is observed publication; false is NOT proof of absence
 * after failed file I/O. task_attempted includes unknown registration outcome. */
typedef struct {L4WorkerStartStage stage;DWORD error,cleanup_error;bool plan_published,task_attempted;} L4WorkerStartReport;
/* Trusted SYSTEM controller only; authenticate/pin worker + exact before/after
 * supervisor snapshots BEFORE calling. Original journal held throughout and on
 * return. Explicit valid clear marker required; no service stop/state publication.
 * Success returns live Job owner, not stop permission. Controller retains Job and
 * must implement journal handoff/worker admission before any actual apply.
 * Failure cancels owned child, preserving immutable plan/task and original error;
 * report distinguishes cleanup failure/unknown task creation. Never writes COMMITTED
 * for an aborted start, deletes tasks, clears state or retries with a new PID. */
bool l4_worker_start(L4Journal* journal,const L4WorkerStart* request,L4WorkerJob** owner,L4WorkerStartReport* report);
