#include "remote_service.h"
#include "../../l4common/journal_internal.h"
#include <tlhelp32.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
struct SetupRemoteService {
    L4Journal* journal;L4ServiceSwitch plan;L4UpdateState expected;ULONGLONG reference;
    L4ReleaseFence *old_pin,*new_pin;wchar_t old_path[MAX_PATH],new_path[MAX_PATH];
    SC_HANDLE service;HANDLE process,prior_process;DWORD pid,prior_pid;FILETIME created,prior_created;
    ULONGLONG start_before,start_after;bool configured_target,active_target,start_attempted,start_issued,start_target;
};
static bool fail(DWORD e){SetLastError(e);return false;}
static bool within(ULONGLONG end){return GetTickCount64()<end?true:fail(ERROR_TIMEOUT);}
static ULONGLONG utc(void){FILETIME t;GetSystemTimeAsFileTime(&t);return ((ULONGLONG)t.dwHighDateTime<<32)|t.dwLowDateTime;}
static bool system_process(HANDLE process){HANDLE token=NULL;BYTE user[512];DWORD size=0,session=~0u;
    bool ok=OpenProcessToken(process,TOKEN_QUERY,&token) && GetTokenInformation(token,TokenUser,user,sizeof(user),&size) && IsWellKnownSid(((TOKEN_USER*)user)->User.Sid,WinLocalSystemSid) && GetTokenInformation(token,TokenSessionId,&session,sizeof(session),&size) && !session;
    DWORD error=GetLastError();if(token)CloseHandle(token);return ok?true:fail(error?error:ERROR_ACCESS_DENIED);
}
static bool system_owner(void){HANDLE thread=NULL;if(OpenThreadToken(GetCurrentThread(),TOKEN_QUERY,TRUE,&thread)){CloseHandle(thread);return fail(ERROR_ACCESS_DENIED);}if(GetLastError()!=ERROR_NO_TOKEN)return false;return system_process(GetCurrentProcess());}
static bool component(const wchar_t* service,const wchar_t** dir,const wchar_t** exe){
    const wchar_t* services[]={L"Leo4Proxy",L"mosquitto",L"L4Con",L"L4Superv"};const wchar_t* dirs[]={L"leo4proxy",L"mosquitto",L"l4con",L"l4superv"};const wchar_t* files[]={L"leo4proxy.exe",L"mosquitto.exe",L"l4con.exe",L"l4superv.exe"};
    for(unsigned i=0;i<4;i++)if(!wcscmp(service,services[i])){*dir=dirs[i];*exe=files[i];return true;}return fail(ERROR_NOT_SUPPORTED);
}
static bool command_path(const wchar_t* command,wchar_t path[MAX_PATH]){
    if(!command || command[0]!=L'"')return fail(ERROR_INVALID_DATA);const wchar_t* end=wcschr(command+1,L'"');
    if(!end || end-command-1>=MAX_PATH || (end[1] && end[1]!=L' ' && end[1]!=L'\t'))return fail(ERROR_INVALID_DATA);
    for(const wchar_t* c=end+1;*c;c++)if(*c<32 && *c!=L'\t')return fail(ERROR_INVALID_DATA);
    wcsncpy_s(path,MAX_PATH,command+1,(size_t)(end-command-1));return true;
}
static bool source_layout(const L4Layout* target,const wchar_t* path,const wchar_t* dir,const wchar_t* exe,L4Layout* source){
    wchar_t prefix[MAX_PATH],version[32],expected[MAX_PATH];if(swprintf_s(prefix,MAX_PATH,L"%ls\\releases\\",target->binaries)<0)return fail(ERROR_INVALID_NAME);size_t n=wcslen(prefix);
    if(wcsncmp(path,prefix,n))return fail(ERROR_BAD_PATHNAME);const wchar_t* slash=wcschr(path+n,L'\\');if(!slash || slash-path-n>=32)return fail(ERROR_INVALID_NAME);
    wcsncpy_s(version,32,path+n,(size_t)(slash-path-n));
    if(!l4_layout_from_roots(source,target->binaries,target->data,version) || !l4_layout_component(source,dir,exe,expected))return false;
    return !wcscmp(expected,path)?true:fail(ERROR_BAD_PATHNAME);
}
static bool same_plan(const L4ServiceSwitch* a,const L4ServiceSwitch* b){return !memcmp(&a->layout,&b->layout,sizeof(a->layout)) && !wcscmp(a->service,b->service) && a->before.installed==b->before.installed && !wcscmp(a->before.account,b->before.account) && !wcscmp(a->before.image_path,b->before.image_path) && a->before.start_type==b->before.start_type && !wcscmp(a->after,b->after) && a->size==b->size && a->before_size==b->before_size && !memcmp(a->sha256,b->sha256,32) && !memcmp(a->before_sha256,b->before_sha256,32);}
static bool marker(const SetupRemoteService* p){
    if(!p || !p->journal || !p->journal->lock || p->journal->lock==INVALID_HANDLE_VALUE || p->journal->poisoned || !system_owner())return fail(ERROR_ACCESS_DENIED);
    L4UpdateState s;if(!l4_update_state_read(&p->journal->layout,&s))return false;
    if(strcmp(s.owner,p->expected.owner) || s.window!=p->expected.window || s.generation!=p->expected.generation || s.plan_sequence!=p->expected.plan_sequence || s.deadline_utc!=p->expected.deadline_utc)return fail(ERROR_REVISION_MISMATCH);
    return utc()<s.deadline_utc?true:fail(ERROR_TIMEOUT);
}
static bool snapshot(SetupRemoteService* p,bool target,SERVICE_STATUS_PROCESS* status){
    DWORD size=0;QueryServiceConfigW(p->service,NULL,0,&size);if(GetLastError()!=ERROR_INSUFFICIENT_BUFFER || size<sizeof(QUERY_SERVICE_CONFIGW) || size>65536)return fail(ERROR_INVALID_DATA);
    QUERY_SERVICE_CONFIGW* c=malloc(size);if(!c)return fail(ERROR_NOT_ENOUGH_MEMORY);bool ok=QueryServiceConfigW(p->service,c,size,&size)!=0;
    if(ok && (c->dwServiceType!=SERVICE_WIN32_OWN_PROCESS || c->dwStartType!=p->plan.before.start_type || !c->lpServiceStartName || wcscmp(c->lpServiceStartName,L"LocalSystem") || !c->lpBinaryPathName || wcscmp(c->lpBinaryPathName,target?p->plan.after:p->plan.before.image_path)))ok=fail(ERROR_REVISION_MISMATCH);
    DWORD error=GetLastError();free(c);if(!ok)return fail(error?error:ERROR_INVALID_DATA);
    if(!QueryServiceStatusEx(p->service,SC_STATUS_PROCESS_INFO,(BYTE*)status,sizeof(*status),&size))return false;
    return status->dwServiceType==SERVICE_WIN32_OWN_PROCESS?true:fail(ERROR_REVISION_MISMATCH);
}
static bool epoch(SetupRemoteService* p,bool running){FILETIME c,e,k,u;wchar_t image[MAX_PATH];DWORD size=MAX_PATH;
    if(!p->process || GetProcessId(p->process)!=p->pid || !GetProcessTimes(p->process,&c,&e,&k,&u) || CompareFileTime(&c,&p->created) || !QueryFullProcessImageNameW(p->process,0,image,&size) || wcscmp(image,p->active_target?p->new_path:p->old_path))return fail(ERROR_REVISION_MISMATCH);
    if(running && WaitForSingleObject(p->process,0)!=WAIT_TIMEOUT)return fail(ERROR_PROCESS_ABORTED);return system_process(p->process);
}
static bool capture(SetupRemoteService* p,DWORD pid,bool target){
    if(!pid || p->process)return fail(ERROR_INVALID_STATE);HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,pid);if(!process)return false;
    p->process=process;p->pid=pid;p->active_target=target;FILETIME e,k,u;bool ok=GetProcessTimes(process,&p->created,&e,&k,&u) && epoch(p,true);
    if(ok && p->start_issued){ULONGLONG born=((ULONGLONG)p->created.dwHighDateTime<<32)|p->created.dwLowDateTime;
        if(target!=p->start_target || p->start_after<p->start_before || born<p->start_before || born>p->start_after)ok=fail(ERROR_REVISION_MISMATCH);}
    if(!ok){DWORD error=GetLastError();CloseHandle(p->process);p->process=NULL;p->pid=0;return fail(error?error:ERROR_REVISION_MISMATCH);}return true;
}
static bool configuration(SetupRemoteService* p,SERVICE_STATUS_PROCESS* s){
    if(snapshot(p,false,s)){p->configured_target=false;return true;}
    if(GetLastError()!=ERROR_REVISION_MISMATCH)return false;
    if(!snapshot(p,true,s))return false;p->configured_target=true;return true;
}
static bool no_survivor(const SetupRemoteService* p,DWORD owned){
    HANDLE search=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);if(search==INVALID_HANDLE_VALUE)return false;
    const wchar_t* basename=wcsrchr(p->old_path,L'\\');if(!basename){CloseHandle(search);return fail(ERROR_BAD_PATHNAME);}++basename;
    PROCESSENTRY32W entry={sizeof(entry)};bool ok=true;BOOL next=Process32FirstW(search,&entry);if(!next && GetLastError()!=ERROR_NO_MORE_FILES)ok=false;
    while(next && ok){if(entry.th32ProcessID!=owned && !_wcsicmp(entry.szExeFile,basename)){
        HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,entry.th32ProcessID);
        if(!process){if(GetLastError()!=ERROR_INVALID_PARAMETER)ok=false;}
        else{wchar_t image[MAX_PATH];DWORD size=MAX_PATH;
            if(WaitForSingleObject(process,0)==WAIT_TIMEOUT){ok=QueryFullProcessImageNameW(process,0,image,&size)!=0;
                if(ok && (!_wcsicmp(image,p->old_path) || !_wcsicmp(image,p->new_path)))ok=fail(ERROR_BUSY);}
            CloseHandle(process);}
    }if(ok){next=Process32NextW(search,&entry);if(!next && GetLastError()!=ERROR_NO_MORE_FILES)ok=false;}}
    DWORD error=GetLastError();CloseHandle(search);return ok?true:fail(error?error:ERROR_NOT_READY);
}
void setup_remote_service_close(SetupRemoteService* p){if(p){if(p->process)CloseHandle(p->process);if(p->prior_process)CloseHandle(p->prior_process);if(p->service)CloseServiceHandle(p->service);l4_release_unpin(p->old_pin);l4_release_unpin(p->new_pin);free(p);}}
#define REQUIRE(x) do{SetLastError(0);if(!(x) || !within(end)){error=GetLastError()?GetLastError():ERROR_NOT_READY;goto done;}}while(0)
bool setup_remote_service_open(L4Journal* j,ULONGLONG ref,const L4ServiceSwitch* plan,const L4UpdateState* expected,DWORD timeout,SetupRemoteService** result){
    if(!result)return fail(ERROR_INVALID_PARAMETER);*result=NULL;if(!j || !ref || !plan || !expected || timeout<1 || timeout>600000 || !expected->generation || !expected->plan_sequence || !expected->deadline_utc || !expected->owner[0] || expected->owner[36] || (expected->window!=1 && expected->window!=2))return fail(ERROR_INVALID_PARAMETER);
    ULONGLONG end=GetTickCount64()+timeout;DWORD error=ERROR_INVALID_DATA;SetupRemoteService* p=calloc(1,sizeof(*p));if(!p)return fail(ERROR_NOT_ENOUGH_MEMORY);p->journal=j;p->plan=*plan;p->expected=*expected;p->reference=ref;
    const wchar_t* id=wcsrchr(j->directory,L'\\');wchar_t owner[40];REQUIRE(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,expected->owner,-1,owner,40));if(!id || wcscmp(id+1,owner)){error=ERROR_REVISION_MISMATCH;goto done;}
    REQUIRE(marker(p));L4ServiceSwitch saved;REQUIRE(l4_journal_load_switch(j,ref,&saved));if(!same_plan(plan,&saved)){error=ERROR_REVISION_MISMATCH;goto done;}
    if(!plan->before.installed || wcscmp(plan->before.account,L"LocalSystem") || plan->before.start_type!=SERVICE_AUTO_START || !plan->before_size || !plan->size){error=ERROR_NOT_SUPPORTED;goto done;}
    const wchar_t* dir=NULL;const wchar_t* exe=NULL;REQUIRE(component(plan->service,&dir,&exe));
    const wchar_t* version=wcsrchr(plan->layout.release,L'\\'),*source_version=wcsrchr(j->layout.release,L'\\');L4Layout native,old,original_roots;
    if(!version || !source_version || !l4_layout_resolve(&native,version+1) || !l4_layout_resolve(&original_roots,source_version+1) || memcmp(&native,&plan->layout,sizeof(native)) || memcmp(&original_roots,&j->layout,sizeof(original_roots)) || wcscmp(j->layout.binaries,native.binaries) || wcscmp(j->layout.data,native.data)){error=ERROR_BAD_PATHNAME;goto done;}
    REQUIRE(command_path(plan->before.image_path,p->old_path));REQUIRE(command_path(plan->after,p->new_path));REQUIRE(source_layout(&plan->layout,p->old_path,dir,exe,&old));
    wchar_t target[MAX_PATH];REQUIRE(l4_layout_component(&plan->layout,dir,exe,target));if(wcscmp(target,p->new_path)){error=ERROR_BAD_PATHNAME;goto done;}
    L4ReleaseFile before={dir,exe,plan->before_size,{0}},after={dir,exe,plan->size,{0}};memcpy(before.sha256,plan->before_sha256,32);memcpy(after.sha256,plan->sha256,32);
    REQUIRE(l4_release_pin(&old,&before,&p->old_pin,target));REQUIRE(l4_release_pin(&plan->layout,&after,&p->new_pin,target));
    SC_HANDLE manager=OpenSCManagerW(NULL,NULL,SC_MANAGER_CONNECT);if(!manager){error=GetLastError();goto done;}p->service=OpenServiceW(manager,plan->service,SERVICE_QUERY_CONFIG|SERVICE_QUERY_STATUS|SERVICE_STOP|SERVICE_START);error=GetLastError();CloseServiceHandle(manager);if(!p->service)goto done;
    SERVICE_STATUS_PROCESS status={0};REQUIRE(snapshot(p,false,&status));if(status.dwCurrentState!=SERVICE_RUNNING || !status.dwProcessId){error=ERROR_SERVICE_NOT_ACTIVE;goto done;}
    REQUIRE(capture(p,status.dwProcessId,false));REQUIRE(snapshot(p,false,&status));if(status.dwCurrentState!=SERVICE_RUNNING || status.dwProcessId!=p->pid){error=ERROR_REVISION_MISMATCH;goto done;}REQUIRE(marker(p));*result=p;return true;
done:setup_remote_service_close(p);return fail(error);
}
bool setup_remote_service_rebind_state(SetupRemoteService* p,const L4UpdateState* next,ULONGLONG recovery_deadline){
    if(!p || !next || !p->journal || !p->journal->lock || p->journal->lock==INVALID_HANDLE_VALUE || p->journal->poisoned || p->expected.window!=1 || next->window!=2 || p->expected.generation==~0ull || next->generation!=p->expected.generation+1 || strcmp(next->owner,p->expected.owner) || next->plan_sequence!=p->expected.plan_sequence || !next->deadline_utc || next->deadline_utc>=recovery_deadline || !system_owner())return fail(ERROR_INVALID_PARAMETER);
    L4UpdateState s;if(!l4_update_state_read(&p->journal->layout,&s))return false;
    if(strcmp(s.owner,next->owner) || s.window!=next->window || s.generation!=next->generation || s.plan_sequence!=next->plan_sequence || s.deadline_utc!=next->deadline_utc)return fail(ERROR_REVISION_MISMATCH);
    if(utc()>=next->deadline_utc)return fail(ERROR_TIMEOUT);p->expected=*next;return true;
}
bool setup_remote_service_stop(SetupRemoteService* p,DWORD timeout){
    if(!p || timeout<1 || timeout>600000 || !p->process)return fail(ERROR_INVALID_PARAMETER);ULONGLONG end=GetTickCount64()+timeout;bool requested=false;
    for(;;){if(!within(end) || !marker(p))return false;SERVICE_STATUS_PROCESS s={0};if(!snapshot(p,p->active_target,&s) || !within(end))return false;
        if(s.dwCurrentState==SERVICE_STOPPED){if(s.dwProcessId)return fail(ERROR_REVISION_MISMATCH);
            DWORD wait=WaitForSingleObject(p->process,0);if(wait==WAIT_OBJECT_0)return GetTickCount64()<end?marker(p):fail(ERROR_TIMEOUT);if(wait!=WAIT_TIMEOUT)return fail(ERROR_INVALID_HANDLE);
        }else{
            if(s.dwProcessId!=p->pid || !epoch(p,true))return fail(ERROR_REVISION_MISMATCH);
            if(s.dwCurrentState==SERVICE_RUNNING && !requested){SERVICE_STATUS status={0};if(!within(end) || !ControlService(p->service,SERVICE_CONTROL_STOP,&status) || !within(end))return false;requested=true;}
            else if(s.dwCurrentState!=SERVICE_STOP_PENDING && s.dwCurrentState!=SERVICE_START_PENDING && s.dwCurrentState!=SERVICE_RUNNING)return fail(ERROR_INVALID_STATE);
        }
        if(GetTickCount64()>=end)return fail(ERROR_TIMEOUT);Sleep(10);
    }
}
bool setup_remote_service_switch(SetupRemoteService* p,bool target,DWORD timeout){
    if(!p || timeout<1 || timeout>600000)return fail(ERROR_INVALID_PARAMETER);ULONGLONG end=GetTickCount64()+timeout;
    HANDLE retained=p->process?p->process:p->prior_process;if(!retained || WaitForSingleObject(retained,0)!=WAIT_OBJECT_0)return fail(ERROR_BUSY);if(!marker(p))return false;
    SERVICE_STATUS_PROCESS status={0};if(!configuration(p,&status) || !within(end))return false;if(status.dwCurrentState!=SERVICE_STOPPED || status.dwProcessId)return fail(ERROR_SERVICE_ALREADY_RUNNING);
    bool ok=target?l4_service_switch_apply(&p->plan):l4_service_switch_rollback(&p->plan);if(!ok)return false;p->configured_target=target;
    if(!within(end))return false;if(!snapshot(p,target,&status) || status.dwCurrentState!=SERVICE_STOPPED || status.dwProcessId)return fail(ERROR_REVISION_MISMATCH);return marker(p) && within(end);
}
static bool observe_start(SetupRemoteService* p,ULONGLONG end){
    bool target=p->start_target;SERVICE_STATUS_PROCESS s={0};
    for(;;){if(!within(end) || !marker(p) || !snapshot(p,target,&s) || !within(end))return false;
        if(s.dwCurrentState!=SERVICE_START_PENDING && s.dwCurrentState!=SERVICE_RUNNING)return fail(ERROR_SERVICE_NOT_ACTIVE);
        if(s.dwProcessId && !p->process){if(!capture(p,s.dwProcessId,target))return false;}
        if(p->process && (s.dwProcessId!=p->pid || !epoch(p,true)))return fail(ERROR_REVISION_MISMATCH);
        if(s.dwCurrentState==SERVICE_RUNNING){if(!p->process || !no_survivor(p,p->pid) || !within(end))return fail(GetLastError()?GetLastError():ERROR_NOT_READY);
            if(!snapshot(p,target,&s) || s.dwCurrentState!=SERVICE_RUNNING || s.dwProcessId!=p->pid || !epoch(p,true))return fail(ERROR_REVISION_MISMATCH);
            return marker(p) && within(end);}
        if(!within(end))return false;Sleep(10);
    }
}
bool setup_remote_service_observe_start(SetupRemoteService* p,DWORD timeout){
    if(!p || !p->start_issued || timeout<1 || timeout>600000 || p->configured_target!=p->start_target)return fail(ERROR_INVALID_PARAMETER);
    return observe_start(p,GetTickCount64()+timeout);
}
bool setup_remote_service_start(SetupRemoteService* p,bool target,DWORD timeout){
    if(!p || timeout<1 || timeout>600000 || target!=p->configured_target)return fail(ERROR_INVALID_PARAMETER);
    ULONGLONG end=GetTickCount64()+timeout;
    if(!marker(p))return false;if(p->process && WaitForSingleObject(p->process,0)!=WAIT_OBJECT_0)return fail(ERROR_BUSY);
    SERVICE_STATUS_PROCESS s={0};if(!snapshot(p,target,&s))return false;if(s.dwCurrentState!=SERVICE_STOPPED || s.dwProcessId)return fail(ERROR_SERVICE_ALREADY_RUNNING);
    if(!no_survivor(p,0) || !marker(p) || !within(end))return false;
    if(p->process){if(p->prior_process)CloseHandle(p->prior_process);p->prior_process=p->process;p->prior_pid=p->pid;p->prior_created=p->created;p->process=NULL;p->pid=0;}p->start_attempted=true;p->start_issued=false;p->start_target=target;p->start_before=utc();
    bool issued=StartServiceW(p->service,0,NULL)!=0;DWORD error=GetLastError();p->start_after=utc();p->start_issued=issued;
    if(!issued)return fail(error?error:ERROR_NOT_READY);if(!within(end))return false;return observe_start(p,end);
}
bool setup_remote_service_running(SetupRemoteService* p,DWORD* pid,FILETIME* created){
    if(!p || !pid || !created)return fail(ERROR_INVALID_PARAMETER);if(!marker(p))return false;SERVICE_STATUS_PROCESS s={0};
    if(!snapshot(p,p->active_target,&s) || s.dwCurrentState!=SERVICE_RUNNING || s.dwProcessId!=p->pid || !epoch(p,true) || !no_survivor(p,p->pid) || !snapshot(p,p->active_target,&s) || s.dwCurrentState!=SERVICE_RUNNING || s.dwProcessId!=p->pid || !epoch(p,true) || !marker(p))return fail(GetLastError()?GetLastError():ERROR_NOT_READY);
    *pid=p->pid;*created=p->created;return true;
}
bool setup_remote_service_observe(SetupRemoteService* p,SetupRemoteServiceObservation* out){
    if(!out)return fail(ERROR_INVALID_PARAMETER);memset(out,0,sizeof(*out));
    if(!p)return fail(ERROR_INVALID_PARAMETER);HANDLE retained=p->process?p->process:p->prior_process;DWORD owned=p->process?p->pid:p->prior_pid;FILETIME retained_birth=p->process?p->created:p->prior_created;
    if(!retained)return fail(ERROR_NOT_READY);if(!marker(p))return false;
    SERVICE_STATUS_PROCESS s={0};if(!configuration(p,&s))return false;
    bool target=p->configured_target,running=s.dwCurrentState==SERVICE_RUNNING;
    if(running){if(!p->process || s.dwProcessId!=p->pid || target!=p->active_target || !epoch(p,true))return fail(ERROR_REVISION_MISMATCH);}
    else if(s.dwCurrentState!=SERVICE_STOPPED || s.dwProcessId || WaitForSingleObject(retained,0)!=WAIT_OBJECT_0)return fail(ERROR_NOT_READY);
    FILETIME c,e,k,u;if(GetProcessId(retained)!=owned || !GetProcessTimes(retained,&c,&e,&k,&u) || CompareFileTime(&c,&retained_birth))return fail(ERROR_REVISION_MISMATCH);
    if(!no_survivor(p,running?owned:0) || !snapshot(p,target,&s) || !marker(p))return false;
    if(running){if(s.dwCurrentState!=SERVICE_RUNNING || s.dwProcessId!=p->pid || !epoch(p,true))return fail(ERROR_REVISION_MISMATCH);}
    else if(s.dwCurrentState!=SERVICE_STOPPED || s.dwProcessId || WaitForSingleObject(retained,0)!=WAIT_OBJECT_0)return fail(ERROR_REVISION_MISMATCH);
    out->phase=running?(target?SETUP_SERVICE_RUNNING_TARGET:SETUP_SERVICE_RUNNING_OLD):(target?SETUP_SERVICE_STOPPED_TARGET:SETUP_SERVICE_STOPPED_OLD);
    out->state=p->expected;out->switch_reference=p->reference;out->pid=owned;out->created=retained_birth;return true;
}
