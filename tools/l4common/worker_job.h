#pragma once
#include "recovery_task.h"
typedef struct L4WorkerJob L4WorkerJob;
/* Single controller owns lifecycle: serialize spawn/close and never close while
 * resume is in flight. Resume itself elects only one caller/attempt atomically. */
/* SYSTEM producer, original journal lock. Creates only a NEW private UUID Job;
 * refuses collisions, including apparently compatible existing Jobs. */
bool l4_worker_job_create(L4Journal* journal,L4WorkerJob** owner);
/* Trusted controller supplies authenticated pinned executable/directory, exact
 * authorized command and a non-inherited double-NUL Unicode environment. No CLI,
 * catalog resolution or token fallback. One locally created suspended worker;
 * job assignment/profile check precedes any execution. No handle inheritance. */
bool l4_worker_job_spawn(L4WorkerJob* owner,const wchar_t* executable,const wchar_t* directory,
                         wchar_t* command,const wchar_t* environment);
/* Borrowed process handle (never close/duplicate/inherit for worker); bind immutable
 * recovery plan to it while still suspended. No raw job/thread handle exposed. */
HANDLE l4_worker_job_process(const L4WorkerJob* owner);
bool l4_worker_job_verify(L4WorkerJob* owner,const L4RecoveryPlan* plan);
/* One-shot resume after matching immutable plan and fresh SYSTEM task audit.
 * Must retain owner until operation is terminal; cannot transfer handle to worker.
 * Does not publish marker, stop services or authorize actual update. */
bool l4_worker_job_resume(L4WorkerJob* owner,L4Journal* journal,const L4RecoveryHelper* helper,DWORD overhead_ms);
/* Explicit cancellation kills only owned job (or own never-assigned suspended
 * child after assignment failure), observes child exit and zero active processes.
 * Always releases handles; timeout is a failed cleanup, never reported as success. */
bool l4_worker_job_close(L4WorkerJob** owner,DWORD timeout_ms);
