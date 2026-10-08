#include "communication_boot.h"
#include "recovery_store.h"
#include "release.h"
#include "journal_internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
struct L4CommunicationBoot {
    L4Layout roots;L4UpdateState expected;L4CommunicationPin* communication;
    L4RecoveryGuard* supervisor;L4ReleaseFence* image;
    HANDLE process,runner;FILETIME created;wchar_t executable[MAX_PATH];
    ULONGLONG started;volatile LONG spent;
};
/* Exactly one recovery invocation in this restarted supervisor lifetime. A
 * missing terminal publication must not permit a second STARTED retry merely
 * by closing/reopening a permit in the same still-running process. */
static volatile LONG boot_attempted;
static bool fail(DWORD code){SetLastError(code);return false;}
static bool system_owner(void){HANDLE thread=NULL,token=NULL;BYTE user[512];DWORD needed=0;
    if(OpenThreadToken(GetCurrentThread(),TOKEN_QUERY,TRUE,&thread)){CloseHandle(thread);return fail(ERROR_ACCESS_DENIED);}
    if(GetLastError()!=ERROR_NO_TOKEN)return false;
    bool ok=OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token) && GetTokenInformation(token,TokenUser,user,sizeof(user),&needed) && IsWellKnownSid(((TOKEN_USER*)user)->User.Sid,WinLocalSystemSid);
    if(token)CloseHandle(token);return ok?true:fail(ERROR_ACCESS_DENIED);
}
static ULONGLONG boot_utc(void){FILETIME t;GetSystemTimeAsFileTime(&t);return ((ULONGLONG)t.dwHighDateTime<<32)|t.dwLowDateTime;}
static bool original_gone(const L4CommunicationPlan* p){
    HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,p->supervisor_pid);
    if(!process)return GetLastError()==ERROR_INVALID_PARAMETER;
    FILETIME c,e,k,u;bool ok=GetProcessTimes(process,&c,&e,&k,&u) &&
        (CompareFileTime(&c,&p->supervisor_created) || WaitForSingleObject(process,0)==WAIT_OBJECT_0);
    DWORD code=GetLastError();CloseHandle(process);return ok?true:fail(code?code:ERROR_BUSY);
}
static bool same_plans(const L4CommunicationPlan* c,const L4RecoveryPlan* s){
    return c && s && !memcmp(&c->operation,&s->operation,16) && c->sequence==s->sequence &&
        c->worker_pid==s->worker_pid && !CompareFileTime(&c->worker_created,&s->worker_created) &&
        c->supervisor_pid==s->supervisor_pid && !CompareFileTime(&c->supervisor_created,&s->supervisor_created) &&
        s->armed_utc<=c->armed_utc && c->deadline_utc<s->deadline_utc &&
        s->deadline_utc-c->deadline_utc>(ULONGLONG)c->budget.total_ms*10000 && l4_communication_native_budget_valid(&c->budget)?true:fail(ERROR_REVISION_MISMATCH);
}
static bool installed(L4CommunicationBoot* b){
    const L4RecoveryPlan* p=l4_recovery_plan(b->supervisor);if(!p)return fail(ERROR_INVALID_DATA);
    FILETIME c,e,k,u;wchar_t image[MAX_PATH];DWORD size=MAX_PATH;
    bool ok=b->process && GetProcessId(b->process)==GetCurrentProcessId() && GetProcessTimes(b->process,&c,&e,&k,&u) &&
        !CompareFileTime(&c,&b->created) && WaitForSingleObject(b->process,0)==WAIT_TIMEOUT &&
        QueryFullProcessImageNameW(b->process,0,image,&size) && !wcscmp(image,b->executable);
    if(!ok)return fail(ERROR_REVISION_MISMATCH);
    SC_HANDLE manager=OpenSCManagerW(NULL,NULL,SC_MANAGER_CONNECT);if(!manager)return false;
    SC_HANDLE service=OpenServiceW(manager,L"L4Superv",SERVICE_QUERY_CONFIG|SERVICE_QUERY_STATUS);DWORD code=GetLastError();CloseServiceHandle(manager);if(!service)return fail(code);
    DWORD needed=0;QueryServiceConfigW(service,NULL,0,&needed);ok=GetLastError()==ERROR_INSUFFICIENT_BUFFER && needed>=sizeof(QUERY_SERVICE_CONFIGW) && needed<=65536;
    QUERY_SERVICE_CONFIGW* config=ok?malloc(needed):NULL;if(ok && !config)ok=fail(ERROR_NOT_ENOUGH_MEMORY);
    if(ok)ok=QueryServiceConfigW(service,config,needed,&needed) && config->dwServiceType==SERVICE_WIN32_OWN_PROCESS && config->dwStartType==p->start_type &&
        config->lpServiceStartName && !wcscmp(config->lpServiceStartName,L"LocalSystem") && config->lpBinaryPathName && !wcscmp(config->lpBinaryPathName,p->before);
    SERVICE_STATUS_PROCESS status={0};if(ok)ok=QueryServiceStatusEx(service,SC_STATUS_PROCESS_INFO,(BYTE*)&status,sizeof(status),&needed) &&
        (status.dwCurrentState==SERVICE_RUNNING || status.dwCurrentState==SERVICE_START_PENDING) && status.dwProcessId==GetCurrentProcessId();
    free(config);code=GetLastError();CloseServiceHandle(service);return ok?true:fail(code?code:ERROR_REVISION_MISMATCH);
}
static bool streams(const wchar_t* name){WIN32_FIND_STREAM_DATA data;HANDLE h=FindFirstStreamW(name,FindStreamInfoStandard,&data,0);
    if(h==INVALID_HANDLE_VALUE)return GetLastError()==ERROR_HANDLE_EOF;bool ok=true;
    do{if(wcscmp(data.cStreamName,L"::$DATA")){ok=false;break;}}while(FindNextStreamW(h,&data));
    DWORD code=GetLastError();FindClose(h);return ok && code==ERROR_HANDLE_EOF;
}
static bool runner_open(L4CommunicationBoot* b,const wchar_t* operation){wchar_t name[MAX_PATH];
    if(swprintf_s(name,MAX_PATH,L"%ls\\%ls\\supervisor.runner.lock",b->roots.operations,operation)<0)return fail(ERROR_FILENAME_EXCED_RANGE);
    b->runner=CreateFileW(name,GENERIC_READ|GENERIC_WRITE|READ_CONTROL,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,NULL);
    if(b->runner==INVALID_HANDLE_VALUE)return false;BY_HANDLE_FILE_INFORMATION info;LARGE_INTEGER size;BYTE* sd=NULL;DWORD sd_size=0;
    bool ok=GetFileInformationByHandle(b->runner,&info) && !(info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)) && info.nNumberOfLinks==1 &&
        GetFileSizeEx(b->runner,&size) && !size.QuadPart && l4_store_security(b->runner,true,&sd,&sd_size) && streams(name);free(sd);
    if(!ok)return fail(ERROR_INVALID_DATA);OVERLAPPED io={0};return LockFileEx(b->runner,LOCKFILE_EXCLUSIVE_LOCK|LOCKFILE_FAIL_IMMEDIATELY,0,1,0,&io)!=0;
}
bool l4_communication_boot_check(L4CommunicationBoot* b,const L4Layout* roots,const L4UpdateState* expected){
    if(!b || !roots || !expected)return fail(ERROR_INVALID_PARAMETER);if(!system_owner())return false;
    const L4CommunicationPlan* c=l4_communication_pinned_plan(b->communication);L4UpdateState actual;
    bool ok=!memcmp(roots,&b->roots,sizeof(*roots)) &&
        l4_communication_plan_matches(b->communication,expected) && l4_update_state_read(&b->roots,&actual) && l4_communication_plan_matches(b->communication,&actual) &&
        same_plans(c,l4_recovery_plan(b->supervisor)) && boot_utc()>=c->armed_utc && boot_utc()<l4_recovery_plan(b->supervisor)->deadline_utc && original_gone(c) && installed(b);
    return ok?true:fail(GetLastError()?GetLastError():ERROR_REVISION_MISMATCH);
}
void l4_communication_boot_close(L4CommunicationBoot* b){if(!b)return;
    if(b->runner!=INVALID_HANDLE_VALUE)CloseHandle(b->runner);if(b->process)CloseHandle(b->process);
    l4_release_unpin(b->image);l4_recovery_close(b->supervisor);l4_communication_plan_close(b->communication);free(b);
}
bool l4_communication_boot_open(const L4Layout* roots,const L4UpdateState* expected,L4CommunicationBoot** output){
    ULONGLONG started=GetTickCount64();
    if(!output)return fail(ERROR_INVALID_PARAMETER);*output=NULL;
    if(InterlockedCompareExchange(&boot_attempted,0,0))return fail(ERROR_ALREADY_EXISTS);
    if(!roots || !expected || expected->window!=L4_UPDATE_COMMUNICATION || expected->owner[36] || expected->owner[37] || expected->owner[38] || expected->owner[39])return fail(ERROR_INVALID_PARAMETER);
    if(!system_owner())return false;L4CommunicationBoot* b=calloc(1,sizeof(*b));if(!b)return fail(ERROR_NOT_ENOUGH_MEMORY);
    b->runner=INVALID_HANDLE_VALUE;b->roots=*roots;b->expected=*expected;b->started=started;wchar_t operation[40];
    bool ok=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,expected->owner,-1,operation,40) && l4_communication_plan_open(roots,operation,&b->communication) && l4_communication_plan_matches(b->communication,expected);
    const L4CommunicationPlan* c=l4_communication_pinned_plan(b->communication);ULONGLONG end=ok?b->started+c->budget.verify_ms:0;
    if(ok)ok=l4_communication_native_budget_valid(&c->budget) && end>b->started && GetTickCount64()<end;
    if(ok){ULONGLONG now=GetTickCount64();ok=now<end?l4_recovery_open(roots,operation,(DWORD)(end-now),&b->supervisor):fail(ERROR_TIMEOUT);}
    /* Do not hold the helper decision mutex while acquiring runner/image/SCM.
     * Sup runner exclusion prevents a concurrent frozen helper changing this
     * process; it does not claim/finish the helper's independent decision. */
    if(b->supervisor)l4_recovery_release(b->supervisor);
    const L4RecoveryPlan* s=l4_recovery_plan(b->supervisor);if(ok){ULONGLONG now=boot_utc();ok=same_plans(c,s) && now>=c->armed_utc && now<s->deadline_utc &&
        s->deadline_utc-now>(ULONGLONG)c->budget.total_ms*10000 && original_gone(c) && runner_open(b,operation);}
    if(ok){const wchar_t* q=wcschr(s->before+1,L'"');wchar_t prefix[MAX_PATH],version[32];L4Layout old;
        size_t n=0;ok=q && q-s->before-1<MAX_PATH && swprintf_s(prefix,MAX_PATH,L"%ls\\releases\\",roots->binaries)>0;
        if(ok){n=wcslen(prefix);ok=(size_t)(q-s->before-1)>n && !wcsncmp(s->before+1,prefix,n);}
        const wchar_t* slash=ok?wcschr(s->before+1+n,L'\\'):NULL;ok=ok && slash && slash<q && slash-s->before-1-n<32;
        if(ok){wcsncpy_s(version,32,s->before+1+n,(size_t)(slash-s->before-1-n));L4ReleaseFile f={L"l4superv",L"l4superv.exe",s->old_size,{0}};memcpy(f.sha256,s->old_sha256,32);
            ok=l4_layout_from_roots(&old,roots->binaries,roots->data,version) && l4_release_pin(&old,&f,&b->image,b->executable) &&
                wcslen(b->executable)==(size_t)(q-s->before-1) && !wcsncmp(b->executable,s->before+1,wcslen(b->executable));}}
    if(ok){b->process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,GetCurrentProcessId());FILETIME e,k,u;ok=b->process && GetProcessTimes(b->process,&b->created,&e,&k,&u) && l4_communication_boot_check(b,roots,expected);}
    if(ok && GetTickCount64()>=end)ok=fail(ERROR_TIMEOUT);if(!ok){DWORD code=GetLastError();l4_communication_boot_close(b);return fail(code?code:ERROR_REVISION_MISMATCH);}*output=b;return true;
}
bool l4_communication_boot_take(L4CommunicationBoot* b,ULONGLONG* started){
    if(!b || !started)return fail(ERROR_INVALID_PARAMETER);if(InterlockedCompareExchange(&b->spent,1,0))return fail(ERROR_ALREADY_EXISTS);
    if(InterlockedCompareExchange(&boot_attempted,1,0))return fail(ERROR_ALREADY_EXISTS);
    const L4CommunicationPlan* p=l4_communication_pinned_plan(b->communication);
    if(!p || GetTickCount64()-b->started>=p->budget.verify_ms)return fail(ERROR_TIMEOUT);
    *started=b->started;return true;
}

DWORD l4_communication_boot_remaining(const L4CommunicationBoot* b){
    const L4CommunicationPlan* p=b?l4_communication_pinned_plan(b->communication):NULL;ULONGLONG now=GetTickCount64();
    if(!p || now<b->started || now-b->started>=p->budget.verify_ms){SetLastError(ERROR_TIMEOUT);return 0;}
    return p->budget.verify_ms-(DWORD)(now-b->started);
}
