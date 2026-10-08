#pragma once
#include "update_metadata.h"
#include "../../l4common/worker_handoff.h"
#include "recovery_receipt.h"
/* Compile-time implementation only, never CLI/test/environment override.
 * preflight is read-only after technical69 admission; execute owns real apply,
 * recovery decisions and durable terminal result. Entry retains signed pins and
 * strict immutable helper receipt; helper borrows that receipt through execute.
 * NULL/incomplete engine refuses BEFORE69. No success/no-op worker fallback. */
typedef struct {
    bool (*preflight)(L4Journal*,const SetupOperationPlan*,const L4WorkerAdmission*,const L4RecoveryHelper*);
    DWORD (*execute)(L4Journal**,const SetupOperationPlan*,const L4WorkerAdmission*,const L4RecoveryHelper*);
} SetupWorkerEngine;
/* One compile-time capability shared by child dispatch and controller launch.
 * NULL until the real forward executor and recovery acceptance are complete. */
const SetupWorkerEngine* setup_worker_compiled_engine(void);
bool setup_worker_entry(int argc,wchar_t** argv,const SetupWorkerEngine* engine,DWORD* result);
