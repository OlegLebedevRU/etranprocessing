#pragma once
#include "recovery_store.h"
/* Authenticated immutable bootstrap inventory, supplied by its owner. No path
 * override, publisher selection or installation in this adapter. */
typedef struct {ULONGLONG size;BYTE sha256[32];} L4RecoveryHelper;
#define L4_RECOVERY_TASK_XML_LIMIT 8192u
/* Existing helper reserves 1s initial decision open + 1s final failure save.
 * Caller adds measured startup/scheduler reserve; no implicit overhead default. */
#define L4_RECOVERY_TASK_MIN_OVERHEAD_MS 2000u
#define L4_RECOVERY_TASK_FOLDER L"\\L4ToolsRecovery"
#define L4_RECOVERY_TASK_SDDL L"O:SYG:SYD:P(A;;FA;;;SY)(A;;FA;;;BA)"
#define L4_RECOVERY_FOLDER_SDDL L"O:SYG:SYD:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)"
typedef struct {
    wchar_t operation[40],name[64],path[MAX_PATH];
    wchar_t xml[L4_RECOVERY_TASK_XML_LIMIT];
    DWORD execution_seconds;
    ULONGLONG deadline_utc;
} L4RecoveryTask;
bool l4_recovery_task_spec(const L4Layout* roots,const L4RecoveryPlan* plan,const L4RecoveryHelper* helper,
                           DWORD overhead_ms,L4RecoveryTask* task);
/* SYSTEM local COM only. Arm creates an absent exact task; never updates an
 * existing task. Audit creates nothing. Both require original journal lock,
 * private plan and WAIT state before/after, helper hash/ACL and explicit overhead.
 * Successful readback is a fresh observation, NOT stop permission/durable READY. */
bool l4_recovery_task_arm(L4Journal* journal,const L4RecoveryHelper* helper,DWORD overhead_ms);
bool l4_recovery_task_audit(L4Journal* journal,const L4RecoveryHelper* helper,DWORD overhead_ms);
