#pragma once
#include "../../l4common/journal.h"
#include "../../l4common/remote_launch_failure.h"
#include "../../l4common/worker_start.h"
/* Original controller93 or exact technically admitted worker69 only. Records an
 * unsuccessful pre-window launch; recovery plans/tasks are retained, including
 * unknown scheduler outcomes. Explicit clear marker mandatory. Any window/apply
 * record refuses: this API cannot turn partial mutation into a preparation error.
 * Exact retry is read-only; error/stage/cleanup drift refuses. No task deletion,
 * marker release, rollback completion, delivery or success assertion. */
bool setup_remote_launch_failure_finish(L4Journal* journal,DWORD error,DWORD stage,DWORD cleanup_error,L4RemoteLaunchFailure* result);
