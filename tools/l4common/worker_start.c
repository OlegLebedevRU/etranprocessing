#include "worker_start_internal.h"
#include "journal_internal.h"
#include "update_state.h"
#include <objbase.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static bool fail(DWORD error){SetLastError(error);return false;}
static ULONGLONG utc(void){FILETIME t;GetSystemTimeAsFileTime(&t);return ((ULONGLONG)t.dwHighDateTime<<32)|t.dwLowDateTime;}
static bool clear(L4Journal* j){L4UpdateState state;if(!l4_update_state_read(&j->layout,&state))return false;return !state.window?true:fail(ERROR_INVALID_STATE);}
static bool waiting(L4Journal* j){
    GUID id;memcpy(&id,j->header+8,16);wchar_t guid[40],operation[40];if(StringFromGUID2(&id,guid,40)!=39)return fail(ERROR_INVALID_DATA);wcsncpy_s(operation,40,guid+1,36);
    L4RecoveryGuard* guard=NULL;if(!l4_recovery_open(&j->layout,operation,1000,&guard))return false;
    L4RecoveryAction action;bool ok=l4_recovery_action(guard,utc(),false,&action);if(ok && action!=L4_RECOVERY_WAIT)ok=fail(ERROR_INVALID_STATE);
    DWORD error=GetLastError();l4_recovery_close(guard);return ok?true:fail(error?error:ERROR_INVALID_STATE);
}
static bool validate(L4Journal* j,const L4WorkerStart* r,L4RecoveryPlan* plan){
    if(!j || !r || !j->lock || j->lock==INVALID_HANDLE_VALUE || j->poisoned || !r->executable || !*r->executable || !r->directory || !*r->directory ||
       !r->command || !*r->command || !r->environment || r->cleanup_ms<1 || r->cleanup_ms>300000 || r->recovery.worker_pid ||
       r->recovery.worker_created.dwLowDateTime || r->recovery.worker_created.dwHighDateTime || memcmp(&r->recovery.operation,j->header+8,16))return fail(ERROR_INVALID_PARAMETER);
    *plan=r->recovery;FILETIME e,k,u;plan->worker_pid=GetCurrentProcessId();if(!GetProcessTimes(GetCurrentProcess(),&plan->worker_created,&e,&k,&u))return false;
    L4RecoveryTask task;if(!l4_recovery_task_spec(&j->layout,plan,&r->helper,r->overhead_ms,&task))return false;
    ULONGLONG now=utc();if(now<plan->armed_utc || now>=plan->deadline_utc)return fail(ERROR_TIME_SKEW);
    BYTE* operation=NULL;DWORD size=0;bool ok=l4_store_find_record(j,64,plan->sequence,&operation,&size);free(operation);if(!ok)return false;if(!size)return fail(ERROR_INVALID_DATA);
    wchar_t path[MAX_PATH];if(swprintf_s(path,MAX_PATH,L"%ls\\supervisor.recovery",j->directory)<0)return fail(ERROR_INVALID_NAME);
    DWORD attributes=GetFileAttributesW(path);if(attributes!=INVALID_FILE_ATTRIBUTES)return fail(ERROR_ALREADY_EXISTS);
    if(GetLastError()!=ERROR_FILE_NOT_FOUND)return false;return clear(j);
}
bool l4_worker_start_run(L4Journal* j,const L4WorkerStart* r,L4WorkerJob** output,L4WorkerStartReport* report,const L4WorkerStartOps* ops){
    if(!output || !report)return fail(ERROR_INVALID_PARAMETER);*output=NULL;memset(report,0,sizeof(*report));report->stage=L4_START_VALIDATE;
    if(!ops || !ops->create || !ops->arm || !ops->resume)return fail(report->error=ERROR_INVALID_PARAMETER);
    L4RecoveryPlan plan;if(!validate(j,r,&plan)){report->error=GetLastError();return false;}
    L4WorkerJob* owner=NULL;report->stage=L4_START_JOB;bool ok=ops->create(ops->context,j,&owner);
    if(ok){report->stage=L4_START_SPAWN;ok=l4_worker_job_spawn(owner,r->executable,r->directory,r->command,r->environment);}
    if(ok){HANDLE process=l4_worker_job_process(owner);FILETIME e,k,u;plan.worker_pid=GetProcessId(process);ok=GetProcessTimes(process,&plan.worker_created,&e,&k,&u)!=0;}
    if(ok){report->stage=L4_START_PLAN;ok=l4_worker_job_verify(owner,&plan) && clear(j) && l4_recovery_prepare(j,&plan,l4_worker_job_process(owner));report->plan_published=ok;}
    if(ok){report->stage=L4_START_ARM;report->task_attempted=true;ok=ops->arm(ops->context,j,&r->helper,r->overhead_ms);}
    if(ok){report->stage=L4_START_RESUME;ok=clear(j) && ops->resume(ops->context,owner,j,&r->helper,r->overhead_ms);}
    if(ok){report->stage=L4_START_VERIFY;ok=clear(j) && l4_worker_job_verify(owner,&plan) && waiting(j);}
    if(!ok){DWORD error=GetLastError();report->error=error?error:ERROR_INVALID_STATE;
        if(owner && !l4_worker_job_close(&owner,r->cleanup_ms))report->cleanup_error=GetLastError();return fail(report->error);}
    report->stage=L4_START_RUNNING;*output=owner;return true;
}
static bool create(void* context,L4Journal* j,L4WorkerJob** owner){(void)context;return l4_worker_job_create(j,owner);}
static bool arm(void* context,L4Journal* j,const L4RecoveryHelper* helper,DWORD overhead){(void)context;return l4_recovery_task_arm(j,helper,overhead);}
static bool resume(void* context,L4WorkerJob* owner,L4Journal* j,const L4RecoveryHelper* helper,DWORD overhead){(void)context;return l4_worker_job_resume(owner,j,helper,overhead);}
bool l4_worker_start(L4Journal* j,const L4WorkerStart* r,L4WorkerJob** owner,L4WorkerStartReport* report){L4WorkerStartOps ops={NULL,create,arm,resume};return l4_worker_start_run(j,r,owner,report,&ops);}
