#pragma once
#include "worker_start.h"
typedef struct {
    void* context;
    bool (*create)(void*,L4Journal*,L4WorkerJob**);
    bool (*arm)(void*,L4Journal*,const L4RecoveryHelper*,DWORD);
    bool (*resume)(void*,L4WorkerJob*,L4Journal*,const L4RecoveryHelper*,DWORD);
} L4WorkerStartOps;
/* Native fixtures use actual Job/plan/store; only SYSTEM create/task arm/audit
 * adapters substituted. No public CLI or environment override. */
bool l4_worker_start_run(L4Journal*,const L4WorkerStart*,L4WorkerJob**,L4WorkerStartReport*,const L4WorkerStartOps*);
