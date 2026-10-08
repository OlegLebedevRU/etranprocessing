#pragma once
#include "../../l4common/recovery_store.h"
#include "../../l4common/journal_internal.h"
/* Fixed operations only. No task registration, network, marker clear or journal. */
DWORD rollback_remaining(ULONGLONG deadline);
bool rollback_worker(const L4RecoveryPlan* plan,ULONGLONG deadline);
HANDLE rollback_lock(const L4Layout* roots,ULONGLONG deadline);
HANDLE rollback_runner(const L4Layout* roots,const wchar_t* operation,ULONGLONG deadline);
bool rollback_image(const L4Layout* roots,const L4RecoveryPlan* plan,HANDLE* file,L4FileFence* fence,wchar_t image[MAX_PATH]);
bool rollback_supervisor(const L4Layout* roots,const L4RecoveryPlan* plan,ULONGLONG deadline);
bool rollback_config(const L4Layout* roots,const L4RecoveryPlan* plan);
bool rollback_execute(const L4Layout* roots,const wchar_t* operation,bool boot);
bool rollback_installed(const L4Layout* roots,const wchar_t* image);
bool rollback_exclusive(const L4RecoveryPlan* plan,DWORD allowed_pid,ULONGLONG deadline);
