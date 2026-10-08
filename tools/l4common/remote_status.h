#pragma once
#include "remote_host.h"
#include "remote_result.h"
#include "remote_launch_failure.h"
#include "remote_outcome.h"
typedef enum {
    L4_REMOTE_RECORDED_REQUEST=1,L4_REMOTE_RECORDED_CONTROLLER,
    L4_REMOTE_RECORDED_ROUTE,L4_REMOTE_RECORDED_PACKAGES_PROGRESS,
    L4_REMOTE_RECORDED_PACKAGES,L4_REMOTE_RECORDED_PLANNING,
    L4_REMOTE_RECORDED_PLAN,L4_REMOTE_RECORDED_FINISHED,
    L4_REMOTE_RECORDED_WORKER_STARTING,L4_REMOTE_RECORDED_HANDOFF,
    L4_REMOTE_RECORDED_WORKER,L4_REMOTE_RECORDED_RECOVERY_REQUIRED
} L4RemoteRecordedPhase;
typedef struct {
    char operation_id[37],resolved_version[32];L4RemoteRecordedPhase recorded_phase;
    ULONGLONG last_sequence,route_sequence,packages_sequence,plan_sequence;
    L4RemoteRequest request;L4RemoteHostReceipt host;L4RemoteResult result;
    bool has_host,has_result;
    bool has_launch_failure;
    L4RemoteLaunchFailure launch_failure;
    bool has_outcome,outcome_clear_recorded,outcome_cleared;
    ULONGLONG outcome_clear_generation;
    L4RemoteOutcome outcome;
} L4RemoteStatus;
/* Existing full-chain private snapshot only. Original92/93 and trusted route60
 * are decoded without a mutable/live journal codec view. Original source layout
 * derives from93; current caller may be a later installed version. Owned output.
 * Recorded progress is NOT live ownership, readiness, stop or source authority.
 * Recovery66/task67/communication70/ticket68/receipt69 are observed in strict
 * order and bound to the original operation/plan/controller. Launch failure95
 * reports recovery_required, never verified rollback or success. Applied history
 * requires typed64/12config refs and matched100/101/104/105/106/107 epochs.
 * Outcome103 binds102 SUCCESS or108 RESTORED; REQUIRED cannot clear. Clear65 is
 * publication intent only: snapshot never sets outcome_cleared or FINISHED for
 * applied outcomes. Live observe confirms protected actual marker at exact clear
 * tuple or later strict-CAS generation. Unknown kinds, malformed records,
 * duplicate selection/completion and terminal-followed records refuse (except
 * one matching clear intent after SUCCESS/RESTORED). Package/config/operation progress is
 * recorded history only; it does not re-admit their payload/cache/SCM. */
bool l4_remote_status_snapshot(const L4JournalReader* reader,const L4Layout* roots,L4RemoteStatus* result);
/* Primary SYSTEM + canonical KnownFolders through existing live-reader gate.
 * No deployment.lock, journal append/repair, missing-file creation or retry loop.
 * Changing/incomplete snapshot returns ERROR_IO_PENDING for caller polling. */
bool l4_remote_status_observe(const L4Layout* layout,const wchar_t* operation,L4RemoteStatus* result);
