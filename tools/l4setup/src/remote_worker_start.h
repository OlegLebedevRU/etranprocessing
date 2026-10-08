#pragma once
#include "supervisor_template.h"
#include "recovery_receipt.h"
#include "worker_entry.h"
#include "../../l4common/worker_start.h"
typedef struct SetupRemoteWorkerLaunch SetupRemoteWorkerLaunch;
typedef struct {
    ULONGLONG armed_utc,supervisor_deadline_utc,communication_deadline_utc;
    DWORD recovery_ms,overhead_ms,cleanup_ms,timeout_ms;
    L4CommunicationBudget communication;
} SetupRemoteWorkerPolicy;
typedef struct {L4WorkerStartReport startup;DWORD error,cleanup_error,result_error;bool transferred,recovery_reported;} SetupRemoteWorkerLaunchReport;
/* Read-only owned launch descriptor: authenticated current source/self/ACK93,
 * strict immutable helper receipt, signed supervisor template. No borrowed source,
 * journal or worker-plan pointer retained. Pins outlive launch/handoff. */
bool setup_remote_worker_launch_prepare(L4Journal* journal,SetupInstalledSource* source,
    const SetupRemoteWorkerPlan* plan,const SetupRemoteWorkerPolicy* policy,
    const volatile LONG* cancelled,SetupRemoteWorkerLaunch** result);
/* Complete compiled child engine mandatory BEFORE any Job/task/receipt. Current
 * getter is NULL, so production start is closed. With future compiled executor:
 * freshly repeat source/template, own startup/communication plan, repeat exact
 * startup proof, then transfer original journal ONLY on success. No window/stop.
 * Caller retains live owner/pins until terminal decision. Source/j invalid after
 * transfer; this object never calls source_verify after transfer. */
bool setup_remote_worker_launch_start(L4Journal** journal,SetupInstalledSource* source,
    SetupRemoteWorkerLaunch* launch,const L4BootstrapChecks* checks,
    const volatile LONG* cancelled,SetupRemoteWorkerLaunchReport* report);
/* Retains original private Job and all admission pins until its exact child
 * exits. Exit code is diagnostic only, never a durable terminal outcome. */
bool setup_remote_worker_launch_wait(SetupRemoteWorkerLaunch* launch,DWORD timeout_ms,DWORD* exit_code);
/* Explicit cancel only the owned Job; always releases descriptor pins. Never
 * removes task/immutable recovery or clears marker/records terminal success. */
bool setup_remote_worker_launch_close(SetupRemoteWorkerLaunch** launch,DWORD cleanup_ms);
