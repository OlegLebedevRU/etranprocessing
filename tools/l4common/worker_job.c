#include "worker_job_internal.h"
#include "journal_internal.h"
#include <objbase.h>
#include <sddl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
struct L4WorkerJob {GUID operation;HANDLE job;PROCESS_INFORMATION child;FILETIME created;bool assigned,poisoned;volatile LONG resume_state;};
static bool fail(DWORD error){SetLastError(error);return false;}
static bool system_user(void){HANDLE token=NULL;BYTE user[512];DWORD count=0;
    bool ok=OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token) && GetTokenInformation(token,TokenUser,user,sizeof(user),&count) && IsWellKnownSid(((TOKEN_USER*)user)->User.Sid,WinLocalSystemSid);
    if(token)CloseHandle(token);return ok?true:fail(ERROR_ACCESS_DENIED);
}
static bool private_job(HANDLE job){
    BYTE* sd=NULL;DWORD size=0;bool ok=l4_store_security(job,true,&sd,&size);PACL acl=NULL;BOOL present,defaulted;
    if(ok)ok=GetSecurityDescriptorDacl(sd,&present,&acl,&defaulted) && present && acl && acl->AceCount==2;
    bool system=false,admin=false;
    for(WORD i=0;ok && i<acl->AceCount;i++){ACCESS_ALLOWED_ACE* ace=NULL;ok=GetAce(acl,i,(void**)&ace) && ace->Header.AceFlags==0 && ace->Mask==JOB_OBJECT_ALL_ACCESS;
        if(ok){PSID sid=&ace->SidStart;if(IsWellKnownSid(sid,WinLocalSystemSid) && !system)system=true;else if(IsWellKnownSid(sid,WinBuiltinAdministratorsSid) && !admin)admin=true;else ok=false;}}
    free(sd);return ok && system && admin?true:fail(ERROR_ACCESS_DENIED);
}
static bool profile(L4WorkerJob* owner){
    if(!owner || !owner->job || owner->poisoned)return fail(ERROR_INVALID_STATE);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits={0};BOOL parent=FALSE;
    bool ok=private_job(owner->job) && QueryInformationJobObject(owner->job,JobObjectExtendedLimitInformation,&limits,sizeof(limits),NULL) &&
        limits.BasicLimitInformation.LimitFlags==JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE && IsProcessInJob(GetCurrentProcess(),owner->job,&parent) && !parent;
    return ok?true:fail(ERROR_ACCESS_DENIED);
}
bool l4_worker_job_create_id(const GUID* operation,L4WorkerJob** result){
    if(!result)return fail(ERROR_INVALID_PARAMETER);*result=NULL;GUID zero={0};
    if(!operation || !memcmp(operation,&zero,16))return fail(ERROR_INVALID_PARAMETER);
    wchar_t uuid[40],name[96],policy[160];if(StringFromGUID2(operation,uuid,40)!=39 || swprintf_s(name,96,L"Global\\L4UpdateWorker.%ls",uuid)<0 ||
        swprintf_s(policy,160,L"O:BAG:BAD:P(A;;0x%lx;;;SY)(A;;0x%lx;;;BA)",(DWORD)JOB_OBJECT_ALL_ACCESS,(DWORD)JOB_OBJECT_ALL_ACCESS)<0)return fail(ERROR_INVALID_NAME);
    PSECURITY_DESCRIPTOR sd=NULL;if(!ConvertStringSecurityDescriptorToSecurityDescriptorW(policy,SDDL_REVISION_1,&sd,NULL))return false;
    SECURITY_ATTRIBUTES attributes={sizeof(attributes),sd,FALSE};SetLastError(0);HANDLE job=CreateJobObjectW(&attributes,name);DWORD error=GetLastError();LocalFree(sd);
    if(!job)return fail(error);if(error==ERROR_ALREADY_EXISTS){CloseHandle(job);return fail(ERROR_ALREADY_EXISTS);} /* NEVER configure an existing object. */
    L4WorkerJob* owner=calloc(1,sizeof(*owner));if(!owner){CloseHandle(job);return fail(ERROR_NOT_ENOUGH_MEMORY);}owner->operation=*operation;owner->job=job;
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits={0};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    bool ok=SetInformationJobObject(job,JobObjectExtendedLimitInformation,&limits,sizeof(limits)) && profile(owner);
    if(!ok){error=GetLastError();CloseHandle(job);free(owner);return fail(error);}*result=owner;return true;
}
bool l4_worker_job_create(L4Journal* j,L4WorkerJob** result){
    if(result)*result=NULL;if(!j || !result || !j->lock || j->lock==INVALID_HANDLE_VALUE || j->poisoned)return fail(ERROR_INVALID_PARAMETER);
    if(!system_user())return false;GUID operation;memcpy(&operation,j->header+8,16);return l4_worker_job_create_id(&operation,result);
}
HANDLE l4_worker_job_process(const L4WorkerJob* owner){return owner?owner->child.hProcess:NULL;}
bool l4_worker_job_spawn(L4WorkerJob* owner,const wchar_t* executable,const wchar_t* directory,wchar_t* command,const wchar_t* environment){
    if(!owner || owner->child.hProcess || !executable || !*executable || !directory || !*directory || !command || !*command || !environment)return fail(ERROR_INVALID_PARAMETER);
    if(!profile(owner))return false;STARTUPINFOW startup={0};startup.cb=sizeof(startup);
    if(!CreateProcessW(executable,command,NULL,NULL,FALSE,CREATE_SUSPENDED|CREATE_NO_WINDOW|CREATE_UNICODE_ENVIRONMENT,(void*)environment,directory,&startup,&owner->child))return false;
    FILETIME exit,kernel,user;bool ok=GetProcessTimes(owner->child.hProcess,&owner->created,&exit,&kernel,&user) && AssignProcessToJobObject(owner->job,owner->child.hProcess);
    if(ok){owner->assigned=true;BOOL member=FALSE;ok=IsProcessInJob(owner->child.hProcess,owner->job,&member) && member && profile(owner);}
    if(!ok){DWORD error=GetLastError();owner->poisoned=true; /* only our locally created, never-resumed process */
        BOOL killed=owner->assigned?TerminateJobObject(owner->job,ERROR_CANCELLED):TerminateProcess(owner->child.hProcess,ERROR_CANCELLED);
        if(!killed || WaitForSingleObject(owner->child.hProcess,5000)!=WAIT_OBJECT_0)error=ERROR_TIMEOUT;return fail(error?error:ERROR_ACCESS_DENIED);}
    return true;
}
bool l4_worker_job_verify(L4WorkerJob* owner,const L4RecoveryPlan* plan){
    if(!owner || !plan || !owner->assigned || !owner->child.hProcess || memcmp(&owner->operation,&plan->operation,16) ||
       plan->worker_pid!=owner->child.dwProcessId || CompareFileTime(&plan->worker_created,&owner->created))return fail(ERROR_INVALID_DATA);
    FILETIME created,exit,kernel,user;BOOL member=FALSE;
    bool ok=profile(owner) && GetProcessId(owner->child.hProcess)==plan->worker_pid && GetProcessTimes(owner->child.hProcess,&created,&exit,&kernel,&user) &&
        !CompareFileTime(&created,&plan->worker_created) && IsProcessInJob(owner->child.hProcess,owner->job,&member) && member && WaitForSingleObject(owner->child.hProcess,0)==WAIT_TIMEOUT;
    return ok?true:fail(ERROR_INVALID_STATE);
}
bool l4_worker_job_resume_checked(L4WorkerJob* owner,const L4RecoveryPlan* plan,L4WorkerArmCheck audit,void* context){
    if(!owner || !audit || InterlockedCompareExchange(&owner->resume_state,1,0)!=0)return fail(ERROR_INVALID_STATE);
    if(!l4_worker_job_verify(owner,plan) || !audit(context) || !l4_worker_job_verify(owner,plan)){InterlockedExchange(&owner->resume_state,0);return false;}
    /* Resume consumes the attempt; unexpected suspension counts never execute
     * twice or get silently repaired. Cancellation remains mandatory on failure. */
    InterlockedExchange(&owner->resume_state,2);DWORD count=ResumeThread(owner->child.hThread);
    if(count!=1){owner->poisoned=true;TerminateJobObject(owner->job,ERROR_CANCELLED);return fail(ERROR_INVALID_STATE);}return true;
}
typedef struct {L4Journal* journal;const L4RecoveryHelper* helper;DWORD overhead;} Arm;
static bool audit_task(void* context){Arm* arm=context;return l4_recovery_task_audit(arm->journal,arm->helper,arm->overhead);}
bool l4_worker_job_resume(L4WorkerJob* owner,L4Journal* j,const L4RecoveryHelper* helper,DWORD overhead){
    if(!owner || !j || !j->lock || j->lock==INVALID_HANDLE_VALUE || j->poisoned || memcmp(&owner->operation,j->header+8,16))return fail(ERROR_INVALID_PARAMETER);
    if(!system_user())return false;wchar_t uuid[40],operation[40];if(StringFromGUID2(&owner->operation,uuid,40)!=39)return fail(ERROR_INVALID_DATA);wcsncpy_s(operation,40,uuid+1,36);
    L4RecoveryGuard* guard=NULL;if(!l4_recovery_open(&j->layout,operation,1000,&guard))return false;
    L4RecoveryPlan plan=*l4_recovery_plan(guard);l4_recovery_release(guard);Arm arm={j,helper,overhead};
    bool ok=l4_worker_job_resume_checked(owner,&plan,audit_task,&arm);DWORD error=GetLastError();l4_recovery_close(guard);return ok?true:fail(error);
}
bool l4_worker_job_close(L4WorkerJob** pointer,DWORD timeout){
    if(!pointer || timeout<1 || timeout>300000)return fail(ERROR_INVALID_PARAMETER);L4WorkerJob* owner=*pointer;if(!owner)return true;*pointer=NULL;
    ULONGLONG deadline=GetTickCount64()+timeout;bool ok=TerminateJobObject(owner->job,ERROR_CANCELLED)!=0;DWORD error=ok?0:GetLastError();
    if(owner->child.hProcess && !owner->assigned && !TerminateProcess(owner->child.hProcess,ERROR_CANCELLED)){ok=false;error=GetLastError();}
    if(owner->child.hProcess){ULONGLONG now=GetTickCount64();DWORD waited=now<deadline?WaitForSingleObject(owner->child.hProcess,(DWORD)(deadline-now)):WAIT_TIMEOUT;
        if(waited!=WAIT_OBJECT_0){ok=false;error=waited==WAIT_FAILED?GetLastError():ERROR_TIMEOUT;}}
    while(ok){JOBOBJECT_BASIC_ACCOUNTING_INFORMATION info={0};ok=QueryInformationJobObject(owner->job,JobObjectBasicAccountingInformation,&info,sizeof(info),NULL)!=0;
        if(!ok){error=GetLastError();break;}if(!info.ActiveProcesses)break;ULONGLONG now=GetTickCount64();if(now>=deadline){ok=false;error=ERROR_TIMEOUT;break;}Sleep(deadline-now<10?(DWORD)(deadline-now):10);}
    if(GetTickCount64()>=deadline){ok=false;error=ERROR_TIMEOUT;}if(owner->child.hThread)CloseHandle(owner->child.hThread);if(owner->child.hProcess)CloseHandle(owner->child.hProcess);CloseHandle(owner->job);free(owner);
    return ok?true:fail(error?error:ERROR_TIMEOUT);
}
