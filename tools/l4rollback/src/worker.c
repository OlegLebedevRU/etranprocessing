#include "rollback.h"
#include <objbase.h>
#include <stdio.h>
#include <stdlib.h>
#include <tlhelp32.h>
static bool fail(DWORD error){SetLastError(error);return false;}
/* SCM can publish STOPPED before its process exits. A crash can lose a newly
 * started candidate PID. Observe ONLY the two approved exact image paths, never
 * terminate a process found by enumeration; an unowned survivor blocks restore. */
bool rollback_exclusive(const L4RecoveryPlan* plan,DWORD allowed_pid,ULONGLONG deadline){
    HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);if(snapshot==INVALID_HANDLE_VALUE)return false;
    PROCESSENTRY32W entry={0};entry.dwSize=sizeof(entry);bool ok=true;BOOL found=Process32FirstW(snapshot,&entry);
    if(!found && GetLastError()!=ERROR_NO_MORE_FILES)ok=false;
    while(ok && found){
        if(entry.th32ProcessID!=allowed_pid && !_wcsicmp(entry.szExeFile,L"l4superv.exe")){
            HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,entry.th32ProcessID);
            if(!process){ok=GetLastError()==ERROR_INVALID_PARAMETER;}
            else{wchar_t image[MAX_PATH];DWORD count=MAX_PATH;bool read=QueryFullProcessImageNameW(process,0,image,&count)!=0;
                if(!read)ok=GetLastError()==ERROR_INSUFFICIENT_BUFFER;
                else{const wchar_t* a=wcschr(plan->before+1,L'"');const wchar_t* b=wcschr(plan->after+1,L'"');
                    bool approved=(a && count==(DWORD)(a-plan->before-1) && !_wcsnicmp(image,plan->before+1,count)) ||
                        (b && count==(DWORD)(b-plan->after-1) && !_wcsnicmp(image,plan->after+1,count));
                    if(approved){DWORD left=rollback_remaining(deadline);ok=left && WaitForSingleObject(process,left)==WAIT_OBJECT_0;if(!ok)SetLastError(ERROR_TIMEOUT);}
                }DWORD error=GetLastError();CloseHandle(process);if(!ok)SetLastError(error);
            }
        }
        if(ok && !rollback_remaining(deadline))ok=fail(ERROR_TIMEOUT);
        if(ok){found=Process32NextW(snapshot,&entry);if(!found && GetLastError()!=ERROR_NO_MORE_FILES)ok=false;}
    }
    DWORD error=GetLastError();CloseHandle(snapshot);return ok?true:fail(error?error:ERROR_GEN_FAILURE);
}
bool rollback_worker(const L4RecoveryPlan* plan,ULONGLONG deadline){
    if(!plan || !rollback_remaining(deadline))return fail(ERROR_INVALID_PARAMETER);
    wchar_t uuid[40],name[96];if(StringFromGUID2(&plan->operation,uuid,40)!=39 || swprintf_s(name,96,L"Global\\L4UpdateWorker.%ls",uuid)<0)return fail(ERROR_INVALID_NAME);
    HANDLE worker=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,plan->worker_pid);
    bool alive=false,ok=true;
    if(worker){FILETIME created,e,k,u;ok=GetProcessTimes(worker,&created,&e,&k,&u)!=0;
        alive=ok && !CompareFileTime(&created,&plan->worker_created) && WaitForSingleObject(worker,0)==WAIT_TIMEOUT;
    }else ok=GetLastError()==ERROR_INVALID_PARAMETER; /* Access denied is not an absent worker. */
    if(alive && plan->worker_pid==GetCurrentProcessId()){ok=false;SetLastError(ERROR_INVALID_PARAMETER);}
    HANDLE job=NULL;if(ok){job=OpenJobObjectW(JOB_OBJECT_QUERY|JOB_OBJECT_TERMINATE|READ_CONTROL,FALSE,name);if(!job && GetLastError()!=ERROR_FILE_NOT_FOUND)ok=false;}
    BYTE* sd=NULL;DWORD size=0;
    if(ok && job){BOOL self=FALSE,member=FALSE;ok=l4_store_security(job,true,&sd,&size) && IsProcessInJob(GetCurrentProcess(),job,&self) && !self;
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits={0};
        if(ok)ok=QueryInformationJobObject(job,JobObjectExtendedLimitInformation,&limits,sizeof(limits),NULL)!=0;
        if(ok && (!(limits.BasicLimitInformation.LimitFlags&JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE) ||
            (limits.BasicLimitInformation.LimitFlags&(JOB_OBJECT_LIMIT_BREAKAWAY_OK|JOB_OBJECT_LIMIT_SILENT_BREAKAWAY_OK))))ok=fail(ERROR_ACCESS_DENIED);
        if(ok && alive){ok=IsProcessInJob(worker,job,&member)!=0;if(ok && !member)ok=fail(ERROR_ACCESS_DENIED);}
        free(sd);sd=NULL;
        if(ok)ok=rollback_remaining(deadline) && TerminateJobObject(job,ERROR_PROCESS_ABORTED);
        while(ok){JOBOBJECT_BASIC_ACCOUNTING_INFORMATION info={0};ok=QueryInformationJobObject(job,JobObjectBasicAccountingInformation,&info,sizeof(info),NULL)!=0;
            if(!ok || !info.ActiveProcesses)break;DWORD left=rollback_remaining(deadline);if(!left){ok=false;SetLastError(ERROR_TIMEOUT);break;}Sleep(left<10?left:10);}
    }else if(ok && alive){ok=false;SetLastError(ERROR_NOT_READY);} /* Never terminate a bare PID. */
    if(ok && alive){DWORD left=rollback_remaining(deadline);ok=left && WaitForSingleObject(worker,left)==WAIT_OBJECT_0;if(!ok)SetLastError(ERROR_TIMEOUT);}
    DWORD error=GetLastError();if(job)CloseHandle(job);if(worker)CloseHandle(worker);return ok?true:fail(error?error:ERROR_ACCESS_DENIED);
}
