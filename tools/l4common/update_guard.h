#pragma once
#include <windows.h>
#include <stdbool.h>

typedef enum { L4_UPDATE_COMMUNICATION = 1, L4_UPDATE_OTHER_TOOLS = 2 } L4UpdateWindow;
typedef enum { L4_WORK_COMMUNICATION = 0, L4_WORK_OTHER_TOOLS = 1 } L4UpdateWork;
typedef struct {
    SRWLOCK lock;
    char owner[37];
    ULONGLONG deadline;
    unsigned active[2];
    L4UpdateWindow window;
    bool expired;
} L4UpdateGuard;
typedef struct { L4UpdateGuard* guard; L4UpdateWork work; } L4UpdateTicket;

/* Local synchronization only: not an authorization, durable marker or watchdog.
 * Zero-initialize once, before exposing to threads. Never reset a live guard.
 * Tickets are zero-initialized, single-owner, noncopyable; retain until actual
 * work (including async children/writers) has finished, then leave exactly once.
 * An authenticated persistent controller must reconcile startup before using
 * this policy and verify commit/rollback before finish. No live controller
 * activates this guard yet; persistent service admission is in update_state. */
bool l4_update_guard_enter(L4UpdateGuard* guard,L4UpdateWork work,ULONGLONG now,L4UpdateTicket* ticket);
bool l4_update_guard_leave(L4UpdateTicket* ticket);
/* Same owner/window/deadline retry is idempotent; never extends a deadline. */
bool l4_update_guard_begin(L4UpdateGuard* guard,const char* owner,L4UpdateWindow window,
                           ULONGLONG now,ULONGLONG deadline);
/* Only blocked work must drain; communication oversight may continue in window2.
 * now is GetTickCount64(), never wall-clock. Expiry is sticky;
 * the controller must perform bounded rollback, not resume ordinary work. */
bool l4_update_guard_ready(L4UpdateGuard* guard,const char* owner,ULONGLONG now);
/* Owner-only release after externally verified commit/rollback, including expired
 * windows. Refuses while any accepted work is still running. No timer release. */
bool l4_update_guard_finish(L4UpdateGuard* guard,const char* owner);
