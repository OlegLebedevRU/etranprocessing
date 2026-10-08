#pragma once
#include "journal.h"
#include "update_guard.h"

typedef struct {
    char owner[40];
    DWORD window; /* 0 is an explicit verified completion, never a missing file. */
    ULONGLONG generation,plan_sequence,deadline_utc; /* UTC FILETIME, 100ns ticks. */
} L4UpdateState;
typedef struct { bool enabled; L4Layout layout; SRWLOCK admission; } L4UpdateConsumer;
/* Fixed protected operations/update.state; readers never take deployment.lock.
 * Missing/corrupt/unsafe is failure, even on first boot. Deadline is evidence
 * for the external watchdog, NOT an automatic release of ordinary admission. */
bool l4_update_state_read(const L4Layout* layout,L4UpdateState* state);
bool l4_update_consumer_init(const wchar_t* component,L4UpdateConsumer* consumer);
bool l4_update_consumer_read(const L4UpdateConsumer* consumer,L4UpdateState* state);
/* Ordinary admission holds admission shared THROUGH dispatch/publication. Drain
 * takes it exclusive, so no earlier clear-state decision can start after ACK.
 * Busy covers accepted async work through its final result/cleanup. This is a
 * read-only acknowledgement, not a lease or permission to stop a service. */
typedef bool (*L4UpdateBusy)(void* context);
DWORD l4_update_consumer_drain(L4UpdateConsumer* consumer,const L4UpdateState* expected,
                               DWORD timeout,HANDLE cancel,L4UpdateBusy busy,void* context);
/* Fresh provisioning only, under the journal lock and with all four fixed SCM
 * services absent. Existing valid clear state is retained; active/corrupt refuses,
 * never resets or repairs. Bootstrap register calls BEFORE service creation. */
bool l4_update_state_provision(L4Journal* journal);
/* Internal storage API, not permission to stop/apply. Caller must verify saved
 * operation64/preflight/commit-or-rollback evidence externally. CAS generation;
 * active owner/plan cannot change, window1->2 only, fixed per-window deadline.
 * 0 requires current active owner; exact retry is idempotent. Intent flushed
 * before atomic publication. Never deletes the durable terminal marker. */
bool l4_update_state_publish(L4Journal* journal,ULONGLONG plan_sequence,
                             ULONGLONG expected_generation,DWORD window,ULONGLONG deadline_utc);
