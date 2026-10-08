#include "bootstrap.h"
#include "bootstrap_history.h"
#include "journal_internal.h"
#include "update_state.h"
#include "supervisor_crash_profile.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define SERVICE_RIGHTS (SERVICE_QUERY_CONFIG|SERVICE_QUERY_STATUS|DELETE)
static bool time_left(ULONGLONG deadline,DWORD* remaining);
static bool marker(L4Journal* j,ULONGLONG sequence,const wchar_t* service,wchar_t out[128]){
    const wchar_t* id=wcsrchr(j->directory,L'\\');if(!id || wcslen(id+1)!=36)return l4_store_fail(ERROR_INVALID_DATA);
    return swprintf_s(out,128,L"%ls [L4:%ls:%llu]",service,id+1,sequence)>0;
}
static bool record(L4Journal* j,DWORD kind,ULONGLONG sequence,unsigned index){BYTE payload[12];l4_store_u64(payload,sequence);l4_store_u32(payload+8,index);return l4_journal_append(j,kind,payload,sizeof(payload),NULL);}
static bool configuration_start(SC_HANDLE service,const L4BootstrapPlan* plan,unsigned index,const wchar_t* owner,bool selected){
    DWORD needed=0;QueryServiceConfigW(service,NULL,0,&needed);
    if(GetLastError()!=ERROR_INSUFFICIENT_BUFFER || needed<sizeof(QUERY_SERVICE_CONFIGW) || needed>65536)return l4_store_fail(ERROR_INVALID_DATA);
    QUERY_SERVICE_CONFIGW* config=(QUERY_SERVICE_CONFIGW*)malloc(needed);if(!config)return l4_store_fail(ERROR_NOT_ENOUGH_MEMORY);
    bool ok=QueryServiceConfigW(service,config,needed,&needed)!=0;DWORD code=GetLastError();
    if(ok){ok=config->lpDisplayName && config->lpBinaryPathName && config->lpServiceStartName &&
        !wcscmp(config->lpDisplayName,owner) && !wcscmp(config->lpBinaryPathName,plan->commands[index]) && !_wcsicmp(config->lpServiceStartName,L"LocalSystem") &&
        config->dwServiceType==SERVICE_WIN32_OWN_PROCESS && (config->dwStartType==SERVICE_DEMAND_START || (selected && config->dwStartType==plan->start_types[index])) && config->dwErrorControl==SERVICE_ERROR_NORMAL &&
        (!config->lpDependencies || !*config->lpDependencies) && (!config->lpLoadOrderGroup || !*config->lpLoadOrderGroup);
        if(!ok)code=ERROR_REVISION_MISMATCH;}
    free(config);return ok?true:l4_store_fail(code);
}
static bool configuration(SC_HANDLE service,const L4BootstrapPlan* plan,unsigned index,const wchar_t* owner){return configuration_start(service,plan,index,owner,false);}
static bool fingerprint(SC_HANDLE service,const L4BootstrapPlan* plan,unsigned index,const wchar_t* owner){
    bool ok=configuration(service,plan,index,owner);DWORD code=GetLastError(),needed=0;SERVICE_STATUS_PROCESS status={0};
    if(ok){ok=QueryServiceStatusEx(service,SC_STATUS_PROCESS_INFO,(BYTE*)&status,sizeof(status),&needed)!=0;code=GetLastError();}
    if(ok && status.dwCurrentState!=SERVICE_STOPPED){ok=false;code=ERROR_SERVICE_ALREADY_RUNNING;}
    return ok?true:l4_store_fail(code);
}
static bool whole_record(L4Journal* j,DWORD kind,ULONGLONG sequence){BYTE bytes[8];l4_store_u64(bytes,sequence);return l4_journal_append(j,kind,bytes,sizeof(bytes),NULL);}
static bool create_one(L4Journal* j,ULONGLONG sequence,const L4BootstrapPlan* plan,unsigned index,SC_HANDLE manager){
    const wchar_t* components[]={L"leo4proxy",L"mosquitto",L"l4con",L"l4superv"};wchar_t exe[64],path[MAX_PATH],owner[128];
    swprintf_s(exe,_countof(exe),L"%ls.exe",components[index]);L4ReleaseFile file={components[index],exe,plan->sizes[index],{0}};memcpy(file.sha256,plan->sha256[index],32);
    L4ReleaseFence* pinned=NULL;if(!marker(j,sequence,plan->services[index],owner) || !l4_release_pin(&plan->layout,&file,&pinned,path))return false;
    bool ok=record(j,L4_RECORD_BOOTSTRAP_CREATE_INTENT,sequence,index);SC_HANDLE service=NULL;DWORD code=GetLastError();
    DWORD rights=SERVICE_RIGHTS|(index==3?SERVICE_CHANGE_CONFIG:0);
    if(ok){service=OpenServiceW(manager,plan->services[index],rights);code=GetLastError();
        if(!service && code==ERROR_SERVICE_DOES_NOT_EXIST){
            /* Display marker is written atomically by CreateService, not as a second registry edit. */
            service=CreateServiceW(manager,plan->services[index],owner,rights,SERVICE_WIN32_OWN_PROCESS,SERVICE_DEMAND_START,SERVICE_ERROR_NORMAL,
                plan->commands[index],NULL,NULL,NULL,L"LocalSystem",NULL);code=GetLastError();
            if(!service && code==ERROR_SERVICE_EXISTS){service=OpenServiceW(manager,plan->services[index],rights);code=GetLastError();}
        }
        ok=service!=NULL;
    }
    if(ok){ok=fingerprint(service,plan,index,owner);code=GetLastError();}
    if(ok && index==3){
        L4BootstrapHistory observed;ok=l4_bootstrap_history(j,sequence,&observed);
        if(ok && observed.crash_done)ok=l4_supervisor_crash_profile_read(service,L4_SUPERVISOR_RESTART_MS);
        else if(ok)ok=record(j,L4_RECORD_SUPERVISOR_CRASH_INTENT,sequence,index) && fingerprint(service,plan,index,owner) &&
            l4_supervisor_crash_profile_write(service) && fingerprint(service,plan,index,owner) &&
            l4_supervisor_crash_profile_read(service,L4_SUPERVISOR_RESTART_MS) && record(j,L4_RECORD_SUPERVISOR_CRASH_DONE,sequence,index);
        code=GetLastError();
    }
    if(ok){ok=record(j,L4_RECORD_BOOTSTRAP_CREATE_DONE,sequence,index);code=GetLastError();}
    if(service)CloseServiceHandle(service);l4_release_unpin(pinned);return ok?true:l4_store_fail(code);
}
bool l4_bootstrap_register(L4Journal* j,ULONGLONG sequence){
    L4BootstrapPlan plan;L4BootstrapHistory current;if(!l4_bootstrap_load(j,sequence,&plan) || !l4_bootstrap_history(j,sequence,&current))return false;
    if(current.rollback || current.committing)return l4_store_fail(ERROR_CANCELLED);
    if(!l4_update_state_provision(j))return false;
    SC_HANDLE manager=OpenSCManagerW(NULL,NULL,SC_MANAGER_CONNECT|SC_MANAGER_CREATE_SERVICE);if(!manager)return false;
    bool ok=true;for(unsigned i=0;ok && i<L4_BOOTSTRAP_SERVICES;i++)ok=create_one(j,sequence,&plan,i,manager);
    DWORD code=GetLastError();CloseServiceHandle(manager);return ok?true:l4_store_fail(code);
}
static bool wait_absent(SC_HANDLE manager,const wchar_t* name,ULONGLONG deadline){
    for(;;){SC_HANDLE service=OpenServiceW(manager,name,SERVICE_QUERY_STATUS);if(service)CloseServiceHandle(service);
        DWORD code=GetLastError();if(!service && code==ERROR_SERVICE_DOES_NOT_EXIST)return true;
        if(!service && code!=ERROR_SERVICE_MARKED_FOR_DELETE)return l4_store_fail(code);
        ULONGLONG now=GetTickCount64();if(now>=deadline)return l4_store_fail(ERROR_TIMEOUT);
        DWORD pause=(DWORD)(deadline-now);Sleep(pause<50?pause:50);
    }
}
static bool delete_one(L4Journal* j,ULONGLONG sequence,const L4BootstrapPlan* plan,unsigned index,SC_HANDLE manager,ULONGLONG deadline){
    DWORD remaining=0;if(!time_left(deadline,&remaining))return false;
    wchar_t owner[128];if(!marker(j,sequence,plan->services[index],owner))return false;
    SC_HANDLE service=OpenServiceW(manager,plan->services[index],SERVICE_RIGHTS);DWORD code=GetLastError();
    if(!service && code==ERROR_SERVICE_DOES_NOT_EXIST)return record(j,L4_RECORD_BOOTSTRAP_DELETE_DONE,sequence,index);
    if(!service && code==ERROR_SERVICE_MARKED_FOR_DELETE){
        /* No mutation of a service we cannot inspect; confirm eventual absence only. */
        return wait_absent(manager,plan->services[index],deadline) && record(j,L4_RECORD_BOOTSTRAP_DELETE_DONE,sequence,index);
    }
    if(!service)return false;
    bool ok=fingerprint(service,plan,index,owner);
    if(ok)ok=record(j,L4_RECORD_BOOTSTRAP_DELETE_INTENT,sequence,index);
    /* Recheck after durable intent; never delete an operator-modified/running object. */
    if(ok)ok=fingerprint(service,plan,index,owner);
    if(ok)ok=time_left(deadline,&remaining);
    if(ok){ok=DeleteService(service)!=0;if(!ok && GetLastError()==ERROR_SERVICE_MARKED_FOR_DELETE)ok=true;}
    code=GetLastError();CloseServiceHandle(service);
    if(ok){ok=wait_absent(manager,plan->services[index],deadline);code=GetLastError();}
    if(ok){ok=time_left(deadline,&remaining) && record(j,L4_RECORD_BOOTSTRAP_DELETE_DONE,sequence,index) && time_left(deadline,&remaining);code=GetLastError();}
    return ok?true:l4_store_fail(code);
}
static bool rollback_until(L4Journal* j,ULONGLONG sequence,ULONGLONG deadline){
    L4BootstrapPlan plan;L4BootstrapHistory current;if(!l4_bootstrap_load(j,sequence,&plan) || !l4_bootstrap_history(j,sequence,&current))return false;
    if(current.complete)return true;
    if(current.committed || current.local_done)return l4_store_fail(ERROR_CANCELLED);
    if(!current.rollback && !whole_record(j,L4_RECORD_BOOTSTRAP_ROLLBACK_BEGIN,sequence))return false;
    SC_HANDLE manager=OpenSCManagerW(NULL,NULL,SC_MANAGER_CONNECT);if(!manager)return false;
    DWORD first=ERROR_SUCCESS;
    for(unsigned i=L4_BOOTSTRAP_SERVICES;i>0;i--){
        if((current.started&(1u<<(i-1))) && !(current.stopped&(1u<<(i-1)))){if(!first)first=ERROR_NOT_READY;continue;}
        if(!delete_one(j,sequence,&plan,i-1,manager,deadline) && !first)first=GetLastError()?GetLastError():ERROR_INVALID_DATA;
    }
    CloseServiceHandle(manager);if(first)return l4_store_fail(first);DWORD remaining=0;
    return time_left(deadline,&remaining) && whole_record(j,L4_RECORD_BOOTSTRAP_ROLLBACK_DONE,sequence) && time_left(deadline,&remaining);
}
bool l4_bootstrap_rollback(L4Journal* j,ULONGLONG sequence,DWORD timeout_ms){
    if(!timeout_ms || timeout_ms>300000)return l4_store_fail(ERROR_INVALID_PARAMETER);
    return rollback_until(j,sequence,GetTickCount64()+timeout_ms);
}


#define ACTIVATE_RIGHTS (SERVICE_QUERY_CONFIG|SERVICE_QUERY_STATUS|SERVICE_START)
static bool status_of(SC_HANDLE service,SERVICE_STATUS_PROCESS* status){
    DWORD needed=0;memset(status,0,sizeof(*status));
    return QueryServiceStatusEx(service,SC_STATUS_PROCESS_INFO,(BYTE*)status,sizeof(*status),&needed)!=0;
}
static bool time_left(ULONGLONG deadline,DWORD* remaining){
    ULONGLONG now=GetTickCount64();if(now>=deadline)return l4_store_fail(ERROR_TIMEOUT);
    *remaining=(DWORD)(deadline-now);return true;
}
/* Keep the exact process handle, not just a reusable PID, until all barriers end. */
static bool process_of(SC_HANDLE service,const wchar_t* expected,HANDLE* process,DWORD* pid){
    SERVICE_STATUS_PROCESS before={0},after={0};if(!status_of(service,&before))return false;
    if(before.dwCurrentState!=SERVICE_RUNNING || !before.dwProcessId || before.dwServiceType!=SERVICE_WIN32_OWN_PROCESS)return l4_store_fail(ERROR_SERVICE_NOT_ACTIVE);
    HANDLE h=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,before.dwProcessId);
    if(!h)return false;
    wchar_t image[MAX_PATH];DWORD length=MAX_PATH;HANDLE token=NULL;union {TOKEN_USER alignment;BYTE bytes[256];} user={0};DWORD needed=0;
    bool ok=QueryFullProcessImageNameW(h,0,image,&length)!=0;DWORD code=GetLastError();
    if(ok && _wcsicmp(image,expected)){ok=false;code=ERROR_REVISION_MISMATCH;}
    if(ok){ok=OpenProcessToken(h,TOKEN_QUERY,&token)!=0;code=GetLastError();}
    if(ok){ok=GetTokenInformation(token,TokenUser,user.bytes,sizeof(user.bytes),&needed)!=0;code=GetLastError();}
    if(ok && !IsWellKnownSid(((TOKEN_USER*)user.bytes)->User.Sid,WinLocalSystemSid)){ok=false;code=ERROR_ACCESS_DENIED;}
    if(token)CloseHandle(token);
    if(ok){DWORD wait=WaitForSingleObject(h,0);if(wait!=WAIT_TIMEOUT){ok=false;code=wait==WAIT_FAILED?GetLastError():ERROR_PROCESS_ABORTED;}}
    if(ok){ok=status_of(service,&after);code=GetLastError();}
    if(ok && (after.dwCurrentState!=SERVICE_RUNNING || after.dwProcessId!=before.dwProcessId)){ok=false;code=ERROR_RETRY;}
    if(!ok){CloseHandle(h);return l4_store_fail(code);}*process=h;*pid=before.dwProcessId;return true;
}
static bool process_birth(HANDLE process,ULONGLONG* birth){
    FILETIME created,exited,kernel,user;if(!GetProcessTimes(process,&created,&exited,&kernel,&user))return false;
    *birth=((ULONGLONG)created.dwHighDateTime<<32)|created.dwLowDateTime;return *birth?true:l4_store_fail(ERROR_INVALID_DATA);
}
static bool observe_process(L4Journal* j,ULONGLONG sequence,unsigned index,HANDLE process,DWORD pid,ULONGLONG* birth){
    if(!process_birth(process,birth))return false;BYTE bytes[24];l4_store_u64(bytes,sequence);l4_store_u32(bytes+8,index);
    l4_store_u32(bytes+12,pid);l4_store_u64(bytes+16,*birth);return l4_journal_append(j,L4_RECORD_BOOTSTRAP_PROCESS,bytes,sizeof(bytes),NULL);
}
static bool live_bundle(SC_HANDLE* services,HANDLE* processes,const DWORD* pids,const L4BootstrapPlan* plan,wchar_t owners[4][128],unsigned count){
    for(unsigned i=0;i<count;i++){
        if(!configuration(services[i],plan,i,owners[i]))return false;
        DWORD wait=WaitForSingleObject(processes[i],0);if(wait!=WAIT_TIMEOUT)return l4_store_fail(wait==WAIT_FAILED?GetLastError():ERROR_PROCESS_ABORTED);
        SERVICE_STATUS_PROCESS status={0};if(!status_of(services[i],&status))return false;
        if(status.dwCurrentState!=SERVICE_RUNNING || status.dwProcessId!=pids[i] || status.dwServiceType!=SERVICE_WIN32_OWN_PROCESS)return l4_store_fail(ERROR_RETRY);
    }return true;
}
static bool activate_one(L4Journal* j,ULONGLONG sequence,const L4BootstrapPlan* plan,unsigned index,SC_HANDLE service,
                         const wchar_t* owner,const wchar_t* image,ULONGLONG deadline,const L4BootstrapChecks* gates,HANDLE* process,DWORD* pid){
    DWORD remaining=0;SERVICE_STATUS_PROCESS status={0};
    if(!time_left(deadline,&remaining) || !configuration(service,plan,index,owner) || !status_of(service,&status))return false;
    if(index==3 && !l4_supervisor_crash_profile_read(service,L4_SUPERVISOR_RESTART_MS))return false;
    if(!record(j,L4_RECORD_BOOTSTRAP_START_INTENT,sequence,index) || !time_left(deadline,&remaining))return false;
    if(status.dwCurrentState==SERVICE_STOPPED){
        /* Recheck after durable intent; arbitrary admin SCM edits are not serialized by our journal lock. */
        if(!configuration(service,plan,index,owner) || !time_left(deadline,&remaining))return false;
        if(index==3 && !l4_supervisor_crash_profile_read(service,L4_SUPERVISOR_RESTART_MS))return false;
        if(!StartServiceW(service,0,NULL) && GetLastError()!=ERROR_SERVICE_ALREADY_RUNNING)return false;
    }else if(status.dwCurrentState!=SERVICE_RUNNING && status.dwCurrentState!=SERVICE_START_PENDING)return l4_store_fail(ERROR_SERVICE_CANNOT_ACCEPT_CTRL);
    for(;;){
        if(!time_left(deadline,&remaining) || !configuration(service,plan,index,owner) || !status_of(service,&status))return false;
        if(status.dwCurrentState==SERVICE_RUNNING)break;
        if(status.dwCurrentState!=SERVICE_START_PENDING)return l4_store_fail(status.dwWin32ExitCode?status.dwWin32ExitCode:ERROR_SERVICE_NOT_ACTIVE);
        Sleep(remaining<50?remaining:50);
    }
    if(!process_of(service,image,process,pid) || !time_left(deadline,&remaining))return false;
    ULONGLONG birth=0;if(!observe_process(j,sequence,index,*process,*pid,&birth) || !time_left(deadline,&remaining))return false;
    SetLastError(ERROR_SUCCESS);
    if(!gates->probe(plan,index,remaining,gates->context))return l4_store_fail(GetLastError()?GetLastError():ERROR_NOT_READY);
    if(!time_left(deadline,&remaining))return false;
    return true;
}
bool l4_bootstrap_activate(L4Journal* j,ULONGLONG sequence,const L4BootstrapChecks* gates){
    if(!gates || !gates->probe || !gates->barrier || !gates->barrier_ms || gates->barrier_ms>300000 || gates->service_ms[1]!=300000)return l4_store_fail(ERROR_INVALID_PARAMETER);
    for(unsigned i=0;i<4;i++)if(!gates->service_ms[i] || gates->service_ms[i]>300000)return l4_store_fail(ERROR_INVALID_PARAMETER);
    L4BootstrapPlan plan;L4BootstrapHistory current;if(!l4_bootstrap_load(j,sequence,&plan) || !l4_bootstrap_history(j,sequence,&current))return false;
    if(current.rollback || current.committing)return l4_store_fail(ERROR_CANCELLED);
    if(current.registered!=15u)return l4_store_fail(ERROR_NOT_READY);
    SC_HANDLE manager=OpenSCManagerW(NULL,NULL,SC_MANAGER_CONNECT);if(!manager)return false;
    SC_HANDLE services[4]={0};HANDLE processes[4]={0};DWORD pids[4]={0};L4ReleaseFence* pins[4]={0};
    wchar_t owners[4][128],images[4][MAX_PATH];const wchar_t* components[]={L"leo4proxy",L"mosquitto",L"l4con",L"l4superv"};
    bool ok=true;DWORD code=ERROR_SUCCESS;
    /* Validate the entire bundle before starting the first process. */
    for(unsigned i=0;ok && i<4;i++){
        wchar_t exe[64];swprintf_s(exe,64,L"%ls.exe",components[i]);L4ReleaseFile file={components[i],exe,plan.sizes[i],{0}};memcpy(file.sha256,plan.sha256[i],32);
        ok=marker(j,sequence,plan.services[i],owners[i]) && l4_release_pin(&plan.layout,&file,&pins[i],images[i]);
        if(ok){services[i]=OpenServiceW(manager,plan.services[i],ACTIVATE_RIGHTS);ok=services[i]!=NULL;}
        SERVICE_STATUS_PROCESS status={0};
        if(ok)ok=configuration(services[i],&plan,i,owners[i]) && status_of(services[i],&status);
        if(ok && status.dwCurrentState!=SERVICE_STOPPED){
            ok=(current.started&(1u<<i)) && (status.dwCurrentState==SERVICE_RUNNING || status.dwCurrentState==SERVICE_START_PENDING);
            if(!ok)SetLastError(ERROR_SERVICE_ALREADY_RUNNING);
        }
        if(!ok)code=GetLastError();
    }
    for(unsigned i=0;ok && i<4;i++){
        if(i==3){
            ULONGLONG deadline=GetTickCount64()+gates->barrier_ms;DWORD remaining=0;
            ok=live_bundle(services,processes,pids,&plan,owners,3) && time_left(deadline,&remaining);
            if(ok){SetLastError(ERROR_SUCCESS);ok=gates->barrier(&plan,remaining,gates->context);if(!ok&&!GetLastError())SetLastError(ERROR_NOT_READY);}
            if(ok)ok=time_left(deadline,&remaining) && live_bundle(services,processes,pids,&plan,owners,3) && time_left(deadline,&remaining);
            if(!ok){code=GetLastError();break;}
        }
        if(i && !live_bundle(services,processes,pids,&plan,owners,i)){ok=false;code=GetLastError();break;}
        ULONGLONG deadline=GetTickCount64()+gates->service_ms[i];DWORD remaining=0;
        ok=activate_one(j,sequence,&plan,i,services[i],owners[i],images[i],deadline,gates,&processes[i],&pids[i]);
        if(ok)ok=live_bundle(services,processes,pids,&plan,owners,i+1) && time_left(deadline,&remaining);
        if(ok)ok=record(j,L4_RECORD_BOOTSTRAP_READY,sequence,i) && time_left(deadline,&remaining);
        if(!ok)code=GetLastError();
    }
    if(ok){
        ULONGLONG deadline=GetTickCount64()+gates->barrier_ms;DWORD remaining=0;
        ok=live_bundle(services,processes,pids,&plan,owners,4) && time_left(deadline,&remaining);
        if(ok){SetLastError(ERROR_SUCCESS);ok=gates->barrier(&plan,remaining,gates->context);if(!ok&&!GetLastError())SetLastError(ERROR_NOT_READY);}
        if(ok)ok=time_left(deadline,&remaining) && live_bundle(services,processes,pids,&plan,owners,4) && time_left(deadline,&remaining);
        if(!ok)code=GetLastError();
    }
    for(unsigned i=0;i<4;i++){if(processes[i])CloseHandle(processes[i]);if(services[i])CloseServiceHandle(services[i]);l4_release_unpin(pins[i]);}
    CloseServiceHandle(manager);return ok?true:l4_store_fail(code?code:ERROR_INVALID_DATA);
}


#define COMMIT_RIGHTS (SERVICE_QUERY_CONFIG|SERVICE_QUERY_STATUS|SERVICE_CHANGE_CONFIG)
static bool selected_start(SC_HANDLE service,DWORD expected){
    DWORD needed=0;QueryServiceConfigW(service,NULL,0,&needed);
    if(GetLastError()!=ERROR_INSUFFICIENT_BUFFER || needed<sizeof(QUERY_SERVICE_CONFIGW) || needed>65536)return l4_store_fail(ERROR_INVALID_DATA);
    QUERY_SERVICE_CONFIGW* c=malloc(needed);if(!c)return l4_store_fail(ERROR_NOT_ENOUGH_MEMORY);
    bool ok=QueryServiceConfigW(service,c,needed,&needed) && c->dwStartType==expected;free(c);
    return ok?true:l4_store_fail(ERROR_REVISION_MISMATCH);
}
static bool commit_bundle(SC_HANDLE services[4],HANDLE processes[4],const L4BootstrapPlan* plan,wchar_t owners[4][128],const L4BootstrapHistory* p){
    for(unsigned i=0;i<4;i++){
        SERVICE_STATUS_PROCESS status={0};ULONGLONG birth=0;
        if(!configuration_start(services[i],plan,i,owners[i],(p->type_intent&(1u<<i))!=0) || !status_of(services[i],&status) ||
            status.dwCurrentState!=SERVICE_RUNNING || status.dwProcessId!=p->pids[i] || !process_birth(processes[i],&birth) ||
            birth!=p->births[i] || WaitForSingleObject(processes[i],0)!=WAIT_TIMEOUT ||
            ((p->type_done&(1u<<i)) && !selected_start(services[i],plan->start_types[i])))return l4_store_fail(ERROR_REVISION_MISMATCH);
    }return l4_supervisor_crash_profile_read(services[3],L4_SUPERVISOR_RESTART_MS);
}
static bool commit_health(SC_HANDLE services[4],HANDLE processes[4],const L4BootstrapPlan* plan,wchar_t owners[4][128],const L4BootstrapHistory* p,const L4BootstrapChecks* gates,ULONGLONG end){
    for(unsigned i=0;i<4;i++){
        DWORD ms=0;if(!time_left(end,&ms) || !commit_bundle(services,processes,plan,owners,p) || !time_left(end,&ms))return false;
        if(ms>gates->service_ms[i])ms=gates->service_ms[i];ULONGLONG part=GetTickCount64()+ms;SetLastError(ERROR_SUCCESS);
        if(!gates->probe(plan,i,ms,gates->context))return l4_store_fail(GetLastError()?GetLastError():ERROR_NOT_READY);
        if(!time_left(part,&ms) || !time_left(end,&ms) || !commit_bundle(services,processes,plan,owners,p))return false;
    }
    DWORD ms=0;if(!time_left(end,&ms))return false;if(ms>gates->barrier_ms)ms=gates->barrier_ms;
    ULONGLONG part=GetTickCount64()+ms;SetLastError(ERROR_SUCCESS);
    if(!gates->barrier(plan,ms,gates->context))return l4_store_fail(GetLastError()?GetLastError():ERROR_NOT_READY);
    return time_left(part,&ms) && time_left(end,&ms) && commit_bundle(services,processes,plan,owners,p) && time_left(part,&ms) && time_left(end,&ms);
}
bool l4_bootstrap_commit(L4Journal* j,ULONGLONG sequence,const L4BootstrapChecks* gates,DWORD timeout){
    if(!timeout || timeout>600000 || !gates || !gates->probe || !gates->barrier || !gates->barrier_ms || gates->barrier_ms>300000 || gates->service_ms[1]!=300000)return l4_store_fail(ERROR_INVALID_PARAMETER);
    for(unsigned i=0;i<4;i++)if(!gates->service_ms[i] || gates->service_ms[i]>300000)return l4_store_fail(ERROR_INVALID_PARAMETER);
    ULONGLONG end=GetTickCount64()+timeout;L4BootstrapPlan plan;L4BootstrapHistory current;
    if(!l4_bootstrap_load(j,sequence,&plan) || !l4_bootstrap_history(j,sequence,&current))return false;
    if(current.rollback || current.complete)return l4_store_fail(ERROR_CANCELLED);
    if(current.local || current.ready!=15u || current.registered!=15u)return l4_store_fail(ERROR_NOT_READY);
    SC_HANDLE manager=OpenSCManagerW(NULL,NULL,SC_MANAGER_CONNECT);if(!manager)return false;
    SC_HANDLE services[4]={0};HANDLE processes[4]={0};L4ReleaseFence* pins[4]={0};wchar_t owners[4][128],images[4][MAX_PATH];bool ok=true;DWORD code=0,ms=0;
    const wchar_t* components[]={L"leo4proxy",L"mosquitto",L"l4con",L"l4superv"};
    for(unsigned i=0;ok && i<4;i++){
        wchar_t exe[64];swprintf_s(exe,64,L"%ls.exe",components[i]);L4ReleaseFile file={components[i],exe,plan.sizes[i],{0}};memcpy(file.sha256,plan.sha256[i],32);
        ok=time_left(end,&ms) && marker(j,sequence,plan.services[i],owners[i]) && l4_release_pin(&plan.layout,&file,&pins[i],images[i]);
        if(ok){services[i]=OpenServiceW(manager,plan.services[i],COMMIT_RIGHTS);ok=services[i]!=NULL;}
        DWORD pid=0;ULONGLONG birth=0;
        if(ok)ok=configuration_start(services[i],&plan,i,owners[i],(current.type_intent&(1u<<i))!=0) && process_of(services[i],images[i],&processes[i],&pid) &&
            process_birth(processes[i],&birth) && pid==current.pids[i] && birth==current.births[i];
        if(!ok)code=GetLastError();
    }
    if(ok)ok=commit_health(services,processes,&plan,owners,&current,gates,end);
    if(ok && !current.committed){
        if(!current.committing){ok=whole_record(j,L4_RECORD_BOOTSTRAP_COMMIT_BEGIN,sequence);if(ok)current.committing=true;}
        for(unsigned i=0;ok && i<4;i++){
            ok=time_left(end,&ms) && commit_bundle(services,processes,&plan,owners,&current) && time_left(end,&ms) && record(j,L4_RECORD_BOOTSTRAP_START_TYPE_INTENT,sequence,i);
            if(ok)current.type_intent|=1u<<i;
            if(ok)ok=time_left(end,&ms) && commit_bundle(services,processes,&plan,owners,&current) && time_left(end,&ms) &&
                ChangeServiceConfigW(services[i],SERVICE_NO_CHANGE,plan.start_types[i],SERVICE_NO_CHANGE,NULL,NULL,NULL,NULL,NULL,NULL,NULL) &&
                selected_start(services[i],plan.start_types[i]) && time_left(end,&ms) && commit_bundle(services,processes,&plan,owners,&current) &&
                time_left(end,&ms) && record(j,L4_RECORD_BOOTSTRAP_START_TYPE_DONE,sequence,i);
            if(ok)current.type_done|=1u<<i;
        }
        if(ok)ok=commit_health(services,processes,&plan,owners,&current,gates,end) && whole_record(j,L4_RECORD_BOOTSTRAP_COMMIT_DONE,sequence) && time_left(end,&ms);
    }
    if(!ok)code=GetLastError()?GetLastError():(code?code:ERROR_NOT_READY);
    for(unsigned i=0;i<4;i++){if(processes[i])CloseHandle(processes[i]);if(services[i])CloseServiceHandle(services[i]);l4_release_unpin(pins[i]);}
    CloseServiceHandle(manager);return ok?true:l4_store_fail(code);
}
static bool local_bundle(SC_HANDLE services[4],const L4BootstrapPlan* plan,wchar_t owners[4][128],const L4BootstrapHistory* p){
    for(unsigned i=0;i<4;i++){SERVICE_STATUS_PROCESS status={0};
        if(!configuration_start(services[i],plan,i,owners[i],(p->type_intent&(1u<<i))!=0) || !status_of(services[i],&status) || status.dwCurrentState!=SERVICE_STOPPED ||
            ((p->type_done&(1u<<i)) && !selected_start(services[i],plan->start_types[i])))return l4_store_fail(ERROR_REVISION_MISMATCH);
    }return l4_supervisor_crash_profile_read(services[3],L4_SUPERVISOR_RESTART_MS);
}
bool l4_bootstrap_local_commit(L4Journal* j,ULONGLONG sequence,DWORD timeout){
    if(!timeout || timeout>600000)return l4_store_fail(ERROR_INVALID_PARAMETER);
    L4BootstrapPlan plan;L4BootstrapHistory p;if(!l4_bootstrap_load(j,sequence,&plan) || !l4_bootstrap_history(j,sequence,&p))return false;
    if(p.rollback || p.complete || p.committed || p.local_done || (p.committing && !p.local) || p.started || p.ready || p.registered!=15u)return l4_store_fail(ERROR_NOT_READY);
    ULONGLONG end=GetTickCount64()+timeout;SC_HANDLE manager=OpenSCManagerW(NULL,NULL,SC_MANAGER_CONNECT);if(!manager)return false;
    SC_HANDLE services[4]={0};L4ReleaseFence* pins[4]={0};wchar_t owners[4][128],images[4][MAX_PATH];bool ok=true;DWORD ms=0;
    const wchar_t* components[]={L"leo4proxy",L"mosquitto",L"l4con",L"l4superv"};
    for(unsigned i=0;ok && i<4;i++){
        wchar_t exe[64];swprintf_s(exe,64,L"%ls.exe",components[i]);L4ReleaseFile file={components[i],exe,plan.sizes[i],{0}};memcpy(file.sha256,plan.sha256[i],32);
        ok=time_left(end,&ms) && marker(j,sequence,plan.services[i],owners[i]) && l4_release_pin(&plan.layout,&file,&pins[i],images[i]);
        if(ok){services[i]=OpenServiceW(manager,plan.services[i],COMMIT_RIGHTS);ok=services[i]!=NULL;}
    }
    if(ok)ok=local_bundle(services,&plan,owners,&p);
    if(ok && !p.local){ok=whole_record(j,L4_RECORD_LOCAL_DEPLOY_BEGIN,sequence);if(ok)p.local=p.committing=true;}
    for(unsigned i=0;ok && i<4;i++){
        ok=time_left(end,&ms) && local_bundle(services,&plan,owners,&p) && record(j,L4_RECORD_BOOTSTRAP_START_TYPE_INTENT,sequence,i);
        if(ok)p.type_intent|=1u<<i;
        if(ok)ok=local_bundle(services,&plan,owners,&p) && time_left(end,&ms) && ChangeServiceConfigW(services[i],SERVICE_NO_CHANGE,plan.start_types[i],SERVICE_NO_CHANGE,NULL,NULL,NULL,NULL,NULL,NULL,NULL) &&
            selected_start(services[i],plan.start_types[i]) && local_bundle(services,&plan,owners,&p) && time_left(end,&ms) && record(j,L4_RECORD_BOOTSTRAP_START_TYPE_DONE,sequence,i);
        if(ok)p.type_done|=1u<<i;
    }
    if(ok)ok=local_bundle(services,&plan,owners,&p) && time_left(end,&ms) && whole_record(j,L4_RECORD_LOCAL_DEPLOY_DONE,sequence);
    DWORD code=GetLastError();for(unsigned i=0;i<4;i++){if(services[i])CloseServiceHandle(services[i]);l4_release_unpin(pins[i]);}CloseServiceHandle(manager);
    return ok?true:l4_store_fail(code?code:ERROR_NOT_READY);
}
bool l4_bootstrap_local_start(L4Journal* j,ULONGLONG sequence,DWORD timeout){
    if(!timeout || timeout>300000)return l4_store_fail(ERROR_INVALID_PARAMETER);
    L4BootstrapPlan plan;L4BootstrapHistory p;if(!l4_bootstrap_load(j,sequence,&plan) || !l4_bootstrap_history(j,sequence,&p))return false;
    if(!p.local_done)return l4_store_fail(ERROR_NOT_READY);
    SC_HANDLE manager=OpenSCManagerW(NULL,NULL,SC_MANAGER_CONNECT);if(!manager)return false;
    ULONGLONG end=GetTickCount64()+timeout;DWORD first=0;
    const wchar_t* components[]={L"leo4proxy",L"mosquitto",L"l4con",L"l4superv"};
    for(unsigned i=0;i<4;i++){DWORD ms=0;wchar_t owner[128],exe[64],image[MAX_PATH];SC_HANDLE service=NULL;L4ReleaseFence* pin=NULL;
        swprintf_s(exe,64,L"%ls.exe",components[i]);L4ReleaseFile file={components[i],exe,plan.sizes[i],{0}};memcpy(file.sha256,plan.sha256[i],32);
        bool ok=time_left(end,&ms) && marker(j,sequence,plan.services[i],owner) && l4_release_pin(&plan.layout,&file,&pin,image);
        if(ok){service=OpenServiceW(manager,plan.services[i],ACTIVATE_RIGHTS);ok=service!=NULL;}
        SERVICE_STATUS_PROCESS status={0};if(ok)ok=configuration_start(service,&plan,i,owner,true) && selected_start(service,plan.start_types[i]) && status_of(service,&status);
        if(ok && i==3)ok=l4_supervisor_crash_profile_read(service,L4_SUPERVISOR_RESTART_MS);
        if(ok && status.dwCurrentState==SERVICE_STOPPED)ok=StartServiceW(service,0,NULL)!=0;
        else if(ok && status.dwCurrentState!=SERVICE_RUNNING && status.dwCurrentState!=SERVICE_START_PENDING)ok=l4_store_fail(ERROR_NOT_READY);
        if(!ok && !first)first=GetLastError()?GetLastError():ERROR_NOT_READY;if(service)CloseServiceHandle(service);l4_release_unpin(pin);
    }
    CloseServiceHandle(manager);return first?l4_store_fail(first):true;
}
/* Rollback intent makes commit permanently unavailable before reverting partial
 * start types. Exact fingerprint and recorded live epoch guard every mutation. */
static bool reset_start_types(SC_HANDLE manager,const L4BootstrapPlan* plan,const L4BootstrapHistory* p,L4Journal* j,ULONGLONG sequence,ULONGLONG end){
    for(unsigned i=0;i<4;i++)if(p->type_intent&(1u<<i)){
        DWORD ms=0;if(!time_left(end,&ms))return false;wchar_t owner[128];if(!marker(j,sequence,plan->services[i],owner))return false;
        SC_HANDLE s=OpenServiceW(manager,plan->services[i],COMMIT_RIGHTS);if(!s)return false;
        bool ok=configuration_start(s,plan,i,owner,true);
        if(ok && p->local){SERVICE_STATUS_PROCESS status={0};ok=status_of(s,&status) && status.dwCurrentState==SERVICE_STOPPED;}
        if(ok && !selected_start(s,SERVICE_DEMAND_START)){
            wchar_t image[MAX_PATH];const wchar_t* quote=wcschr(plan->commands[i]+1,L'"');HANDLE h=NULL;DWORD pid=0;ULONGLONG birth=0;
            ok=quote && quote-plan->commands[i]-1<MAX_PATH;
            if(ok && !p->local){wcsncpy_s(image,MAX_PATH,plan->commands[i]+1,(size_t)(quote-plan->commands[i]-1));ok=process_of(s,image,&h,&pid) && process_birth(h,&birth) && pid==p->pids[i] && birth==p->births[i];}
            if(ok)ok=time_left(end,&ms) && configuration_start(s,plan,i,owner,true) && time_left(end,&ms) && ChangeServiceConfigW(s,SERVICE_NO_CHANGE,SERVICE_DEMAND_START,SERVICE_NO_CHANGE,NULL,NULL,NULL,NULL,NULL,NULL,NULL) && selected_start(s,SERVICE_DEMAND_START) && time_left(end,&ms);
            if(h)CloseHandle(h);
        }
        DWORD code=GetLastError();CloseServiceHandle(s);if(!ok)return l4_store_fail(code?code:ERROR_REVISION_MISMATCH);
    }return true;
}
#define STOP_RIGHTS (SERVICE_QUERY_CONFIG|SERVICE_QUERY_STATUS|SERVICE_STOP)
/* Opening a reused PID is harmless: creation time is compared before any action.
 * No service identity is inferred from STOPPED/pending state's undefined SCM PID. */
static bool recorded_process(DWORD pid,ULONGLONG birth,HANDLE* process){
    *process=NULL;HANDLE h=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,pid);
    if(!h)return GetLastError()==ERROR_INVALID_PARAMETER?true:false;
    ULONGLONG actual=0;if(!process_birth(h,&actual)){DWORD code=GetLastError();CloseHandle(h);return l4_store_fail(code);}
    if(actual!=birth){CloseHandle(h);return true;}*process=h;return true;
}
static bool stop_one(L4Journal* j,ULONGLONG sequence,const L4BootstrapPlan* plan,unsigned index,SC_HANDLE manager,ULONGLONG deadline){
    L4BootstrapHistory current;if(!l4_bootstrap_history(j,sequence,&current))return false;
    wchar_t owner[128],image[MAX_PATH],exe[64];const wchar_t* components[]={L"leo4proxy",L"mosquitto",L"l4con",L"l4superv"};
    swprintf_s(exe,64,L"%ls.exe",components[index]);
    if(!marker(j,sequence,plan->services[index],owner) || !l4_layout_component(&plan->layout,components[index],exe,image))return false;
    SC_HANDLE service=OpenServiceW(manager,plan->services[index],STOP_RIGHTS);DWORD code=GetLastError();
    if(!service && code!=ERROR_SERVICE_DOES_NOT_EXIST)return false;
    HANDLE process=NULL;bool ok=true;SERVICE_STATUS_PROCESS status={0};DWORD remaining=0;
    if(service)ok=configuration(service,plan,index,owner) && status_of(service,&status);
    if(ok && service && status.dwCurrentState!=SERVICE_STOPPED && !(current.started&(1u<<index))){ok=false;SetLastError(ERROR_SERVICE_ALREADY_RUNNING);}
    /* Wait START_PENDING to a state with a valid PID, or rely on prior recorded identity. */
    while(ok && service && status.dwCurrentState==SERVICE_START_PENDING){
        if(!time_left(deadline,&remaining)){ok=false;break;}Sleep(remaining<50?remaining:50);
        ok=configuration(service,plan,index,owner) && status_of(service,&status);
    }
    if(ok && service && status.dwCurrentState==SERVICE_RUNNING){
        DWORD pid=0;ULONGLONG birth=0;ok=process_of(service,image,&process,&pid) && process_birth(process,&birth);
        if(ok && current.pids[index] && (current.pids[index]!=pid || current.births[index]!=birth)){ok=false;SetLastError(ERROR_RETRY);}
        if(ok && !current.pids[index])ok=observe_process(j,sequence,index,process,pid,&birth);
        if(ok){current.pids[index]=pid;current.births[index]=birth;}
    }else if(ok){
        if(service && status.dwCurrentState==SERVICE_STOP_PENDING && !(current.stopping&(1u<<index))){ok=false;SetLastError(ERROR_NOT_READY);}
        if(service && status.dwCurrentState!=SERVICE_STOPPED && status.dwCurrentState!=SERVICE_STOP_PENDING){ok=false;SetLastError(ERROR_SERVICE_CANNOT_ACCEPT_CTRL);}
        if(ok && (current.started&(1u<<index)) && !current.pids[index]){ok=false;SetLastError(ERROR_NOT_READY);}
        if(ok && current.pids[index])ok=recorded_process(current.pids[index],current.births[index],&process);
    }
    if(ok)ok=time_left(deadline,&remaining) && record(j,L4_RECORD_BOOTSTRAP_STOP_INTENT,sequence,index);
    if(ok && service && status.dwCurrentState==SERVICE_RUNNING){
        /* Durable identity+intent precede STOP; recheck configuration and PID. */
        ok=configuration(service,plan,index,owner) && status_of(service,&status) && time_left(deadline,&remaining);
        if(ok && (status.dwCurrentState!=SERVICE_RUNNING || status.dwProcessId!=current.pids[index])){ok=false;SetLastError(ERROR_RETRY);}
        if(ok){DWORD wait=WaitForSingleObject(process,0);if(wait!=WAIT_TIMEOUT){ok=false;SetLastError(wait==WAIT_FAILED?GetLastError():ERROR_PROCESS_ABORTED);}}
        if(ok){SERVICE_STATUS reply={0};if(!ControlService(service,SERVICE_CONTROL_STOP,&reply)){
            code=GetLastError();if(code!=ERROR_SERVICE_NOT_ACTIVE && code!=ERROR_SERVICE_CANNOT_ACCEPT_CTRL)ok=false;
        }}
    }
    while(ok){
        if(!time_left(deadline,&remaining)){ok=false;break;}
        if(service && (!configuration(service,plan,index,owner) || !status_of(service,&status))){ok=false;break;}
        bool stopped=!service || status.dwCurrentState==SERVICE_STOPPED;
        if(service && !stopped && status.dwCurrentState!=SERVICE_STOP_PENDING){ok=false;SetLastError(ERROR_RETRY);break;}
        DWORD wait=process?WaitForSingleObject(process,0):WAIT_OBJECT_0;
        if(wait!=WAIT_OBJECT_0 && wait!=WAIT_TIMEOUT){ok=false;if(wait!=WAIT_FAILED)SetLastError(ERROR_INVALID_DATA);break;}
        if(stopped && wait==WAIT_OBJECT_0){ok=record(j,L4_RECORD_BOOTSTRAP_STOP_DONE,sequence,index) && time_left(deadline,&remaining);break;}
        Sleep(remaining<50?remaining:50);
    }
    code=GetLastError();if(process)CloseHandle(process);if(service)CloseServiceHandle(service);
    return ok?true:l4_store_fail(code?code:ERROR_INVALID_DATA);
}
bool l4_bootstrap_abort(L4Journal* j,ULONGLONG sequence,DWORD timeout_ms){
    if(!timeout_ms || timeout_ms>300000)return l4_store_fail(ERROR_INVALID_PARAMETER);
    L4BootstrapPlan plan;L4BootstrapHistory current;if(!l4_bootstrap_load(j,sequence,&plan) || !l4_bootstrap_history(j,sequence,&current))return false;
    if(current.complete)return true;
    if(current.committed || current.local_done)return l4_store_fail(ERROR_CANCELLED);
    ULONGLONG deadline=GetTickCount64()+timeout_ms;
    if(!current.rollback && !whole_record(j,L4_RECORD_BOOTSTRAP_ROLLBACK_BEGIN,sequence))return false;
    SC_HANDLE manager=OpenSCManagerW(NULL,NULL,SC_MANAGER_CONNECT);if(!manager)return false;
    bool ok=reset_start_types(manager,&plan,&current,j,sequence,deadline);for(unsigned i=4;ok && i>0;i--)ok=stop_one(j,sequence,&plan,i-1,manager,deadline);
    DWORD code=GetLastError();CloseServiceHandle(manager);if(!ok)return l4_store_fail(code);
    DWORD remaining=0;if(!time_left(deadline,&remaining))return false;
    return rollback_until(j,sequence,deadline);
}
