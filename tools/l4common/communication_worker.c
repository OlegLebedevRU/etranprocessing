#include "communication_runtime.h"
#include "journal_internal.h"
#include <objbase.h>
#include <stdio.h>
#include <stdlib.h>
static bool fail(DWORD error){SetLastError(error);return false;}
static DWORD remaining(ULONGLONG end){ULONGLONG now=GetTickCount64();return now<end?(DWORD)(end-now):0;}
bool l4_communication_worker_live(const L4CommunicationPlan* p){
    if(!p || !p->worker_pid || p->worker_pid==GetCurrentProcessId())return fail(ERROR_INVALID_PARAMETER);
    wchar_t uuid[40],name[96];if(StringFromGUID2(&p->operation,uuid,40)!=39 || swprintf_s(name,96,L"Global\\L4UpdateWorker.%ls",uuid)<0)return fail(ERROR_INVALID_NAME);
    HANDLE worker=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,p->worker_pid);if(!worker)return false;
    FILETIME c,e,k,u;bool ok=GetProcessTimes(worker,&c,&e,&k,&u) && !CompareFileTime(&c,&p->worker_created) && WaitForSingleObject(worker,0)==WAIT_TIMEOUT;
    HANDLE job=ok?OpenJobObjectW(JOB_OBJECT_QUERY|READ_CONTROL,FALSE,name):NULL;if(ok && !job)ok=false;
    BYTE* sd=NULL;DWORD size=0;BOOL self=TRUE,member=FALSE;JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits={0};
    if(ok)ok=l4_store_security(job,true,&sd,&size) && IsProcessInJob(GetCurrentProcess(),job,&self) && !self &&
        IsProcessInJob(worker,job,&member) && member && QueryInformationJobObject(job,JobObjectExtendedLimitInformation,&limits,sizeof(limits),NULL) &&
        (limits.BasicLimitInformation.LimitFlags&JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE) &&
        !(limits.BasicLimitInformation.LimitFlags&(JOB_OBJECT_LIMIT_BREAKAWAY_OK|JOB_OBJECT_LIMIT_SILENT_BREAKAWAY_OK)) && WaitForSingleObject(worker,0)==WAIT_TIMEOUT;
    DWORD error=GetLastError();free(sd);if(job)CloseHandle(job);CloseHandle(worker);return ok?true:fail(error?error:ERROR_REVISION_MISMATCH);
}
bool l4_communication_worker_exit(const L4CommunicationPlan* p,DWORD timeout){
    if(!p || !timeout || timeout>300000 || !p->worker_pid || p->worker_pid==GetCurrentProcessId())return fail(ERROR_INVALID_PARAMETER);
    ULONGLONG end=GetTickCount64()+timeout;wchar_t uuid[40],name[96];
    if(StringFromGUID2(&p->operation,uuid,40)!=39 || swprintf_s(name,96,L"Global\\L4UpdateWorker.%ls",uuid)<0)return fail(ERROR_INVALID_NAME);
    HANDLE worker=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,p->worker_pid);
    bool alive=false,ok=true;
    if(worker){FILETIME created,e,k,u;ok=GetProcessTimes(worker,&created,&e,&k,&u)!=0;
        alive=ok && !CompareFileTime(&created,&p->worker_created) && WaitForSingleObject(worker,0)==WAIT_TIMEOUT;
    }else ok=GetLastError()==ERROR_INVALID_PARAMETER;
    HANDLE job=NULL;if(ok){job=OpenJobObjectW(JOB_OBJECT_QUERY|JOB_OBJECT_TERMINATE|READ_CONTROL,FALSE,name);if(!job && GetLastError()!=ERROR_FILE_NOT_FOUND)ok=false;}
    if(ok && job){BYTE* sd=NULL;DWORD size=0;BOOL self=FALSE,member=FALSE;
        ok=l4_store_security(job,true,&sd,&size) && IsProcessInJob(GetCurrentProcess(),job,&self) && !self;free(sd);
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits={0};
        if(ok)ok=QueryInformationJobObject(job,JobObjectExtendedLimitInformation,&limits,sizeof(limits),NULL)!=0;
        if(ok && (!(limits.BasicLimitInformation.LimitFlags&JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE) ||
            (limits.BasicLimitInformation.LimitFlags&(JOB_OBJECT_LIMIT_BREAKAWAY_OK|JOB_OBJECT_LIMIT_SILENT_BREAKAWAY_OK))))ok=fail(ERROR_ACCESS_DENIED);
        if(ok && alive){ok=IsProcessInJob(worker,job,&member)!=0;if(ok && !member)ok=fail(ERROR_ACCESS_DENIED);}
        if(ok)ok=remaining(end) && TerminateJobObject(job,ERROR_PROCESS_ABORTED);
        while(ok){JOBOBJECT_BASIC_ACCOUNTING_INFORMATION info={0};ok=QueryInformationJobObject(job,JobObjectBasicAccountingInformation,&info,sizeof(info),NULL)!=0;
            if(!ok || !info.ActiveProcesses)break;DWORD left=remaining(end);if(!left){ok=fail(ERROR_TIMEOUT);break;}Sleep(left<10?left:10);}
    }else if(ok && alive)ok=fail(ERROR_NOT_READY);
    if(ok && alive){DWORD left=remaining(end);ok=left && WaitForSingleObject(worker,left)==WAIT_OBJECT_0;if(!ok)SetLastError(ERROR_TIMEOUT);}
    if(ok && !remaining(end))ok=fail(ERROR_TIMEOUT);
    DWORD error=GetLastError();if(job)CloseHandle(job);if(worker)CloseHandle(worker);return ok?true:fail(error?error:ERROR_ACCESS_DENIED);
}
