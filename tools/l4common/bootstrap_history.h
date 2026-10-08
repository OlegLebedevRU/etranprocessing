#pragma once
#include "bootstrap.h"
/* Shared read-only recorded phase semantics. No SCM/file mutation or readiness. */
typedef struct {ULONGLONG plan;bool rollback,complete,committing,committed,local,local_done,crash_intent,crash_done;unsigned created,registered,started,ready,stopping,stopped,type_intent,type_done;DWORD pids[4];ULONGLONG births[4];} L4BootstrapHistory;
bool l4_bootstrap_history(L4Journal* journal,ULONGLONG sequence,L4BootstrapHistory* history);
