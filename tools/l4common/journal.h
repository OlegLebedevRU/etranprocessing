#pragma once
#include "service_switch.h"
typedef struct L4Journal L4Journal;
#define L4_JOURNAL_MAX_RECORD (256u*1024u)
#define L4_RECORD_SWITCH_PLAN 10u
#define L4_RECORD_CONFIG_PLAN 20u
#define L4_OPERATION_CONFIG_LIMIT 16u
#define L4_RECORD_CONFIG_INTENT 21u
#define L4_RECORD_CONFIG_DONE 22u
#define L4_RECORD_LAUNCHER_INTENT 30u
#define L4_RECORD_LAUNCHER_DONE 31u
#define L4_RECORD_BOOTSTRAP_PLAN 40u
#define L4_RECORD_BOOTSTRAP_CREATE_INTENT 41u
#define L4_RECORD_BOOTSTRAP_CREATE_DONE 42u
#define L4_RECORD_BOOTSTRAP_DELETE_INTENT 43u
#define L4_RECORD_BOOTSTRAP_DELETE_DONE 44u
#define L4_RECORD_BOOTSTRAP_ROLLBACK_BEGIN 45u
#define L4_RECORD_BOOTSTRAP_ROLLBACK_DONE 46u
#define L4_RECORD_BOOTSTRAP_START_INTENT 47u
#define L4_RECORD_BOOTSTRAP_READY 48u
#define L4_RECORD_BOOTSTRAP_PROCESS 49u
#define L4_RECORD_BOOTSTRAP_STOP_INTENT 50u
#define L4_RECORD_BOOTSTRAP_STOP_DONE 51u
#define L4_RECORD_BOOTSTRAP_COMMIT_BEGIN 52u
#define L4_RECORD_BOOTSTRAP_START_TYPE_INTENT 53u
#define L4_RECORD_BOOTSTRAP_START_TYPE_DONE 54u
#define L4_RECORD_BOOTSTRAP_COMMIT_DONE 55u
#define L4_RECORD_LOCAL_DEPLOY_BEGIN 90u
#define L4_RECORD_LOCAL_DEPLOY_DONE 91u
#define L4_RECORD_SUPERVISOR_CRASH_INTENT 96u
#define L4_RECORD_SUPERVISOR_CRASH_DONE 97u
/* One exclusive deployment lock per layout, held until close. UUID must be the
 * original task ID; create=false never invents a missing operation OR lock. */
bool l4_journal_open(const L4Layout* layout,const wchar_t* operation_id,bool create,L4Journal** journal);
void l4_journal_close(L4Journal* journal);
bool l4_journal_append(L4Journal* journal,DWORD kind,const void* bytes,DWORD size,ULONGLONG* sequence);
typedef bool (*L4JournalVisitor)(DWORD kind,ULONGLONG sequence,const void* bytes,DWORD size,void* context);
/* All committed records are verified before the first callback. */
bool l4_journal_replay(L4Journal* journal,L4JournalVisitor visit,void* context);
bool l4_journal_save_switch(L4Journal* journal,const L4ServiceSwitch* plan,ULONGLONG* sequence);
bool l4_journal_load_switch(L4Journal* journal,ULONGLONG sequence,L4ServiceSwitch* plan);
/* Prepare stores old/new bytes + security descriptor before any target change.
 * Caller must quiesce config writers before apply/rollback. Not a service stop
 * mechanism and not an atomic transaction spanning multiple files or SCM. */
bool l4_config_prepare(L4Journal* journal,const wchar_t* relative,const void* bytes,DWORD size,ULONGLONG* sequence);
/* Fresh built-in broker renderer only: fixed path, no existing file, Users read. */
bool l4_config_prepare_public_broker(L4Journal* journal,const void* bytes,DWORD size,ULONGLONG* sequence);
bool l4_config_apply(L4Journal* journal,ULONGLONG sequence);
bool l4_config_rollback(L4Journal* journal,ULONGLONG sequence);
/* Read-only exact bytes/existence/SD recheck of the saved old or candidate state.
 * Never applies/rolls back/repairs or appends intent. Missing-original verifies
 * inherited file policy as well, using only an owned delete-on-close probe. */
bool l4_config_verify(L4Journal* journal,ULONGLONG sequence,bool candidate);

/* Standalone read-only decoder for an immutable copy of record10; no journal or
 * deployment lock. Roots must be canonical; signed source admission is external. */
bool l4_switch_decode(const L4Layout* roots,const BYTE* bytes,DWORD size,L4ServiceSwitch* plan);
