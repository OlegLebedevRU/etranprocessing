#pragma once
#include "remote_commit.h"
#include "remote_service_transaction.h"
#include "../../l4common/remote_outcome.h"

typedef struct SetupRemoteCompletion SetupRemoteCompletion;
/* Exact admitted worker69, clear state, authenticated installed source and held
 * signed one-hop operation. Copies source/updater identities and duplicates ACL
 * tokens BEFORE stopping anything; no source borrow survives later mutation.
 * This is a context, not a successful-update proof or stop authorization. */
bool setup_remote_completion_prepare(L4Journal* original,SetupInstalledSource* source,
    const SetupOperationPlan* operation,SetupRemoteCompletion** context);
/* Actual native application checks and fresh orphan barrier, repeated local
 * epoch/config/inventory/marker checks. Appends102 only after the whole suite is
 * verified. Never accepts a caller success boolean, clears a marker, changes
 * services, deletes recovery tasks, or claims restored state on failure.
 * Exact retry of a flushed receipt repeats all live checks before returning. */
bool setup_remote_completion_commit(SetupRemoteCompletion* context,L4Journal* original,
    const SetupOperationPlan* operation,SetupServiceTransaction* transaction,
    SetupRemoteService* services[4],DWORD timeout_ms,SetupRemoteCommit* receipt);
/* Verified success only: repeats real commit gates, derives/flushed103 via the
 * private bound writer, repeats local authority and DONE, then publishes clear.
 * A failed atomic clear leaves103 observable as finishing, never cleared.
 * No caller result/success flag or raw102 is admitted. */
bool setup_remote_completion_finish(SetupRemoteCompletion* context,L4Journal* original,
    const SetupOperationPlan* operation,SetupServiceTransaction* transaction,
    SetupRemoteService* services[4],DWORD timeout_ms,L4RemoteOutcome* outcome);
/* Read-only ambiguity fence: once final DONE publication has been attempted,
 * caller may reconcile exact completion but must never begin rollback104. */
bool setup_remote_completion_settling(const SetupRemoteCompletion* context);
void setup_remote_completion_free(SetupRemoteCompletion* context);
