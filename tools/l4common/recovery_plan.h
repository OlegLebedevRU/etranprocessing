#pragma once
#include "layout.h"
/* Initial supervisor-only wire contract. No catalog, download, generic installer,
 * pointers on disk or arbitrary service/path selection. Config is always the
 * fixed ProgramData/config/l4superv.json, account always LocalSystem.
 * Initial unpublished wire now includes original supervisor PID+creation. */
#define L4_RECOVERY_CONFIG_LIMIT 65536u
#define L4_RECOVERY_PLAN_LIMIT (160u*1024u)
typedef struct {
    GUID operation;ULONGLONG sequence;
    DWORD worker_pid,recovery_ms,start_type;FILETIME worker_created;
    DWORD supervisor_pid;FILETIME supervisor_created;
    ULONGLONG armed_utc,deadline_utc,old_size,new_size;
    BYTE old_sha256[32],new_sha256[32];
    wchar_t before[2048],after[2048];
    bool old_exists;const BYTE* old_config;DWORD old_config_size;
    const BYTE* new_config;DWORD new_config_size;
    const BYTE* config_sd;DWORD config_sd_size;
} L4RecoveryPlan;
/* All memory returned by decode belongs to bytes, except fixed fields/commands;
 * caller retains bytes throughout plan use. Codec validates against supplied
 * canonical roots; only supervisor versions, unchanged argument suffix allowed.
 * Hash is corruption/binding protection, NOT a metadata signature. */
bool l4_recovery_encode(const L4Layout* roots,const L4RecoveryPlan* plan,BYTE** bytes,DWORD* size);
bool l4_recovery_decode(const L4Layout* roots,const BYTE* bytes,DWORD size,L4RecoveryPlan* plan);
bool l4_recovery_hash(const void* bytes,DWORD size,BYTE hash[32]);
typedef enum {L4_RECOVERY_COMMITTED=1,L4_RECOVERY_RESTORED=2,L4_RECOVERY_FAILED=3,L4_RECOVERY_STARTED=4} L4RecoveryStatus;
typedef enum {L4_RECOVERY_WAIT=1,L4_RECOVERY_DONE=2,L4_RECOVERY_REQUIRED=3,L4_RECOVERY_BLOCKED=4} L4RecoveryAction;
#define L4_RECOVERY_RESULT_SIZE 96u
bool l4_recovery_result_encode(const L4RecoveryPlan* plan,const BYTE digest[32],L4RecoveryStatus status,DWORD error,BYTE bytes[L4_RECOVERY_RESULT_SIZE]);
bool l4_recovery_result_decode(const L4RecoveryPlan* plan,const BYTE digest[32],const BYTE* bytes,DWORD size,L4RecoveryStatus* status,DWORD* error);
/* Boot flag is supplied only by the future protected SYSTEM task. STARTED is
 * irrevocable: worker cannot commit afterward, including before deadline. */
bool l4_recovery_decide(const L4RecoveryPlan* plan,L4RecoveryStatus status,ULONGLONG now,bool boot,L4RecoveryAction* action);
