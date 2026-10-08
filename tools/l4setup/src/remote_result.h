#pragma once
#include "../../l4common/remote_result.h"
#include "../../l4common/journal.h"
/* Current admitted controller only, original92/93 and pre-worker history.
 * Derives versions/time/UUID from the original journal and owner-signed route;
 * caller supplies only nonzero error. ERROR_CANCELLED selects cancelled.
 * Flush once, exact retry keeps original finish time; changed error conflicts.
 * Prepared config/source/switch/operation proposals are allowed; any worker,
 * apply or window record refuses. No SCM/config/marker mutation, delivery
 * attempt or success assertion. An exact retry is read-only after host exit. */
bool setup_remote_preparation_finish(L4Journal* journal,DWORD error,L4RemoteResult* result);
/* Read-only original locked journal; full binding checks, not live host proof.
 * Failure clears output. Terminal94 closes preparation/worker-plan admission. */
bool setup_remote_preparation_result(L4Journal* journal,L4RemoteResult* result);
