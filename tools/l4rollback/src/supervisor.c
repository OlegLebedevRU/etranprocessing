#include "rollback.h"
#include "../../l4common/probe_ipc.h"
#include <objbase.h>
#include <stdlib.h>
#include <string.h>
static bool fail(DWORD error){SetLastError(error);return false;}
static bool query(SC_HANDLE service,const L4RecoveryPlan* plan,SERVICE_STATUS_PROCESS* status,bool old_only){
    DWORD needed=0;QueryServiceConfigW(service,NULL,0,&needed);
    if(GetLastError()!=ERROR_INSUFFICIENT_BUFFER || needed<sizeof(QUERY_SERVICE_CONFIGW) || needed>65536)return fail(ERROR_INVALID_DATA);
    QUERY_SERVICE_CONFIGW* config=malloc(needed);if(!config)return fail(ERROR_NOT_ENOUGH_MEMORY);
    bool ok=QueryServiceConfigW(service,config,needed,&needed) && config->dwServiceType==SERVICE_WIN32_OWN_PROCESS &&
        config->dwStartType==plan->start_type && !wcscmp(config->lpServiceStartName,L"LocalSystem") &&
        (!wcscmp(config->lpBinaryPathName,plan->before) || (!old_only && !wcscmp(config->lpBinaryPathName,plan->after)));
    free(config);if(!ok)return fail(ERROR_REVISION_MISMATCH);
    return QueryServiceStatusEx(service,SC_STATUS_PROCESS_INFO,(BYTE*)status,sizeof(*status),&needed)!=0;
}
static bool process_image(HANDLE process,const wchar_t* command){
    wchar_t image[MAX_PATH];DWORD count=MAX_PATH;const wchar_t* end=wcschr(command+1,L'"');
    if(!end || !QueryFullProcessImageNameW(process,0,image,&count) || count!=(DWORD)(end-command-1) || _wcsnicmp(image,command+1,count))return fail(ERROR_REVISION_MISMATCH);
    HANDLE token=NULL;DWORD needed=0;BYTE user[512];
    bool ok=OpenProcessToken(process,TOKEN_QUERY,&token) && GetTokenInformation(token,TokenUser,user,sizeof(user),&needed) && IsWellKnownSid(((TOKEN_USER*)user)->User.Sid,WinLocalSystemSid);
    if(token)CloseHandle(token);return ok?true:fail(ERROR_ACCESS_DENIED);
}
static bool observe_original(const L4RecoveryPlan* plan,HANDLE* result){
    *result=NULL;HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,plan->supervisor_pid);
    if(!process)return GetLastError()==ERROR_INVALID_PARAMETER;
    FILETIME created,e,k,u;bool ok=GetProcessTimes(process,&created,&e,&k,&u)!=0;
    if(ok && CompareFileTime(&created,&plan->supervisor_created)){CloseHandle(process);return true;} /* Reused PID is never our old supervisor. */
    if(ok && WaitForSingleObject(process,0)==WAIT_OBJECT_0){CloseHandle(process);return true;}
    if(ok)ok=process_image(process,plan->before);
    if(!ok){DWORD error=GetLastError();CloseHandle(process);return fail(error);}*result=process;return true;
}
static bool pause(ULONGLONG deadline){DWORD left=rollback_remaining(deadline);if(!left)return fail(ERROR_TIMEOUT);Sleep(left<10?left:10);return true;}
static bool exited(HANDLE process,ULONGLONG deadline){if(!process)return true;DWORD left=rollback_remaining(deadline);return left && WaitForSingleObject(process,left)==WAIT_OBJECT_0?true:fail(ERROR_TIMEOUT);}
static bool marker(const L4Layout* roots,const L4RecoveryPlan* plan,L4UpdateState* state){
    wchar_t text[40];bool ok=l4_update_state_read(roots,state) && StringFromGUID2(&plan->operation,text,40)==39 &&
        state->window==L4_UPDATE_OTHER_TOOLS && state->plan_sequence==plan->sequence;
    for(unsigned i=0;ok && i<36;i++){wchar_t c=text[i+1];if(c>=L'A' && c<=L'F')c+=32;ok=state->owner[i]==(char)c;}
    return ok?true:fail(ERROR_REVISION_MISMATCH);
}
bool rollback_supervisor(const L4Layout* roots,const L4RecoveryPlan* plan,ULONGLONG deadline){
    HANDLE image=INVALID_HANDLE_VALUE,original=NULL,current=NULL;L4FileFence parents={0};wchar_t old_image[MAX_PATH];
    L4UpdateState state;SC_HANDLE manager=NULL,service=NULL;SERVICE_STATUS_PROCESS status={0};
    bool ok=rollback_remaining(deadline) && marker(roots,plan,&state) && rollback_image(roots,plan,&image,&parents,old_image);
    if(ok)ok=observe_original(plan,&original);
    if(ok){manager=OpenSCManagerW(NULL,NULL,SC_MANAGER_CONNECT);ok=manager!=NULL;}
    if(ok){service=OpenServiceW(manager,L"L4Superv",SERVICE_QUERY_CONFIG|SERVICE_QUERY_STATUS|SERVICE_STOP|SERVICE_CHANGE_CONFIG|SERVICE_START);ok=service!=NULL;}
    if(ok)ok=query(service,plan,&status,false);
    /* Wait for a start transition to yield a PID or STOPPED; never pretend a
     * zero PID in a pending state proves that its process has exited. */
    while(ok && status.dwCurrentState!=SERVICE_STOPPED && !status.dwProcessId){ok=pause(deadline) && query(service,plan,&status,false);}
    FILETIME epoch={0};DWORD pid=status.dwProcessId;
    if(ok && status.dwCurrentState!=SERVICE_STOPPED){
        current=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,pid);ok=current!=NULL;
        FILETIME e,k,u;if(ok)ok=GetProcessTimes(current,&epoch,&e,&k,&u)!=0;
        if(ok)ok=process_image(current,plan->before) || process_image(current,plan->after);
        if(ok)ok=query(service,plan,&status,false) && status.dwProcessId==pid && rollback_remaining(deadline);
        if(ok && status.dwCurrentState!=SERVICE_STOP_PENDING){SERVICE_STATUS stopped;ok=ControlService(service,SERVICE_CONTROL_STOP,&stopped)!=0;}
    }
    while(ok){ok=query(service,plan,&status,false);if(!ok || status.dwCurrentState==SERVICE_STOPPED)break;
        if(status.dwProcessId && status.dwProcessId!=pid){ok=fail(ERROR_REVISION_MISMATCH);break;}ok=pause(deadline);}
    if(ok)ok=exited(current,deadline) && exited(original,deadline) && rollback_remaining(deadline);
    if(current){CloseHandle(current);current=NULL;}
    if(ok)ok=query(service,plan,&status,false) && status.dwCurrentState==SERVICE_STOPPED && rollback_exclusive(plan,0,deadline) && rollback_config(roots,plan) && rollback_remaining(deadline);
    if(ok)ok=query(service,plan,&status,false) && status.dwCurrentState==SERVICE_STOPPED &&
        ChangeServiceConfigW(service,SERVICE_NO_CHANGE,SERVICE_NO_CHANGE,SERVICE_NO_CHANGE,plan->before,NULL,NULL,NULL,NULL,NULL,NULL);
    if(ok)ok=query(service,plan,&status,true) && status.dwCurrentState==SERVICE_STOPPED && rollback_remaining(deadline) && StartServiceW(service,0,NULL);
    while(ok){ok=query(service,plan,&status,true);if(!ok || status.dwCurrentState==SERVICE_RUNNING)break;
        if(status.dwCurrentState!=SERVICE_START_PENDING){ok=fail(ERROR_SERVICE_NOT_ACTIVE);break;}ok=pause(deadline);}
    pid=status.dwProcessId;
    if(ok){current=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,pid);ok=pid && current && process_image(current,plan->before);
        FILETIME e,k,u;if(ok)ok=GetProcessTimes(current,&epoch,&e,&k,&u)!=0;}
    /* Rollback is allowed after the original window deadline. Ordinary fresh
     * supervisor health proves a new read-only cycle; expired drain admission
     * cannot be used here and the helper never extends/clears the marker. */
    if(ok)ok=rollback_remaining(deadline) && l4_probe_call(L"superv",pid,0,rollback_remaining(deadline));
    if(ok){SERVICE_STATUS_PROCESS final={0};FILETIME created,e,k,u;L4UpdateState after;
        ok=rollback_remaining(deadline) && rollback_exclusive(plan,pid,deadline) && query(service,plan,&final,true) && final.dwCurrentState==SERVICE_RUNNING && final.dwProcessId==pid &&
            GetProcessTimes(current,&created,&e,&k,&u) && !CompareFileTime(&created,&epoch) && WaitForSingleObject(current,0)==WAIT_TIMEOUT &&
            l4_update_state_read(roots,&after) && !memcmp(state.owner,after.owner,40) && state.window==after.window && state.generation==after.generation && state.plan_sequence==after.plan_sequence && state.deadline_utc==after.deadline_utc;}
    DWORD error=GetLastError();if(current)CloseHandle(current);if(original)CloseHandle(original);if(service)CloseServiceHandle(service);if(manager)CloseServiceHandle(manager);
    if(image!=INVALID_HANDLE_VALUE)CloseHandle(image);l4_store_unpin(&parents);return ok?true:fail(error?error:ERROR_GEN_FAILURE);
}
