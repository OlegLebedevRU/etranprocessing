#pragma once
#include "recovery_task.h"
/* Narrow local transport for isolated tests. Strings returned are malloc-owned.
 * Native canonicalization uses TaskDefinition parsing, not a custom XML parser.
 * No generic delete/update/run/enumeration operation is exposed. */
typedef struct {
    void* context;
    bool (*helper)(void*,const L4Layout*,const L4RecoveryHelper*);
    bool (*folder)(void*,bool create);
    bool (*canonical)(void*,const wchar_t*,wchar_t**);
    bool (*read)(void*,const L4RecoveryTask*,bool* present,wchar_t** xml,wchar_t** sddl);
    bool (*create)(void*,const L4RecoveryTask*);
} L4RecoveryTaskOps;
bool l4_recovery_task_check_acl(const wchar_t* sddl,bool folder);
/* Account names returned by Task Scheduler may be localized. Trust the SID. */
bool l4_recovery_task_system_account(const wchar_t* user);
bool l4_recovery_task_run(L4Journal* journal,const L4RecoveryHelper* helper,DWORD overhead_ms,bool arm,const L4RecoveryTaskOps* ops);
