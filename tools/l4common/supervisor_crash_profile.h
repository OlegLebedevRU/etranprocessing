#pragma once
#include <windows.h>
#include <stdbool.h>
#include <stdlib.h>
#include <wchar.h>
#include <string.h>
#define L4_SUPERVISOR_RESTART_MS 1000u
#define L4_SUPERVISOR_RESTART_RESET 86400u
/* Fixed crash-only Windows SCM profile. No command, reboot or graceful-STOP
 * restart. The last (single) action repeats per SCM. Delay is a configured
 * minimum, never a promise of OS scheduling latency. */
static inline bool l4_supervisor_crash_profile_valid(const SERVICE_FAILURE_ACTIONSW* p,const SERVICE_FAILURE_ACTIONS_FLAG* flag,DWORD budget){
    return p && flag && budget>=L4_SUPERVISOR_RESTART_MS && !flag->fFailureActionsOnNonCrashFailures &&
        p->dwResetPeriod==L4_SUPERVISOR_RESTART_RESET && p->cActions==1 && p->lpsaActions &&
        p->lpsaActions[0].Type==SC_ACTION_RESTART && p->lpsaActions[0].Delay==L4_SUPERVISOR_RESTART_MS &&
        (!p->lpRebootMsg || !*p->lpRebootMsg) && (!p->lpCommand || !*p->lpCommand);
}
static inline bool l4_crash_empty_string(const BYTE* base,DWORD size,const wchar_t* text){
    if(!text)return true;ULONG_PTR address=(ULONG_PTR)text,start=(ULONG_PTR)base;
    return address>=start && address-start<=size-sizeof(wchar_t) && !(address%sizeof(wchar_t)) && !*text;
}
static inline bool l4_crash_system_account(SC_HANDLE service){
    DWORD size=0;QueryServiceConfigW(service,NULL,0,&size);
    if(GetLastError()!=ERROR_INSUFFICIENT_BUFFER || size<sizeof(QUERY_SERVICE_CONFIGW) || size>65536){SetLastError(ERROR_INVALID_DATA);return false;}
    BYTE* bytes=calloc(1,size);if(!bytes){SetLastError(ERROR_NOT_ENOUGH_MEMORY);return false;}
    DWORD n=0;bool ok=QueryServiceConfigW(service,(QUERY_SERVICE_CONFIGW*)bytes,size,&n)!=0;
    QUERY_SERVICE_CONFIGW* config=(QUERY_SERVICE_CONFIGW*)bytes;ULONG_PTR base=(ULONG_PTR)bytes,address=(ULONG_PTR)config->lpServiceStartName;
    if(ok){ok=config->dwServiceType==SERVICE_WIN32_OWN_PROCESS && config->dwStartType==SERVICE_AUTO_START &&
        address>=base && address-base<=size-sizeof(L"LocalSystem") && !(address%sizeof(wchar_t)) &&
        !memcmp(config->lpServiceStartName,L"LocalSystem",sizeof(L"LocalSystem"));
        if(!ok)SetLastError(ERROR_REVISION_MISMATCH);}
    DWORD error=GetLastError();free(bytes);if(!ok)SetLastError(error?error:ERROR_INVALID_DATA);return ok;
}
static inline bool l4_supervisor_crash_profile_read(SC_HANDLE service,DWORD budget){
    DWORD n=0;QueryServiceConfig2W(service,SERVICE_CONFIG_FAILURE_ACTIONS,NULL,0,&n);
    if(GetLastError()!=ERROR_INSUFFICIENT_BUFFER || n<sizeof(SERVICE_FAILURE_ACTIONSW) || n>65536){SetLastError(ERROR_INVALID_DATA);return false;}
    BYTE* bytes=calloc(1,n);if(!bytes){SetLastError(ERROR_NOT_ENOUGH_MEMORY);return false;}DWORD size=n;
    bool ok=QueryServiceConfig2W(service,SERVICE_CONFIG_FAILURE_ACTIONS,bytes,size,&n)!=0;
    SERVICE_FAILURE_ACTIONSW* actions=(SERVICE_FAILURE_ACTIONSW*)bytes;ULONG_PTR start=(ULONG_PTR)bytes,address=(ULONG_PTR)actions->lpsaActions;
    if(ok)ok=actions->cActions==1 && address>=start && address-start<=size-sizeof(SC_ACTION) && !(address%sizeof(DWORD)) &&
        l4_crash_empty_string(bytes,size,actions->lpRebootMsg) && l4_crash_empty_string(bytes,size,actions->lpCommand);
    if(!ok && GetLastError()==ERROR_INSUFFICIENT_BUFFER)SetLastError(ERROR_INVALID_DATA);
    SERVICE_FAILURE_ACTIONS_FLAG flag={0};if(ok)ok=QueryServiceConfig2W(service,SERVICE_CONFIG_FAILURE_ACTIONS_FLAG,(BYTE*)&flag,sizeof(flag),&n)!=0;
    if(ok && !l4_supervisor_crash_profile_valid(actions,&flag,budget)){ok=false;SetLastError(ERROR_REVISION_MISMATCH);}DWORD error=GetLastError();free(bytes);
    if(!ok)SetLastError(error?error:ERROR_REVISION_MISMATCH);return ok;
}
/* Caller must already own/pin a STOPPED provisional service and flush intent. */
static inline bool l4_supervisor_crash_profile_write(SC_HANDLE service){
    SERVICE_FAILURE_ACTIONS_FLAG flag={FALSE};SC_ACTION restart={SC_ACTION_RESTART,L4_SUPERVISOR_RESTART_MS};
    SERVICE_FAILURE_ACTIONSW actions={L4_SUPERVISOR_RESTART_RESET,L"",L"",1,&restart};
    return ChangeServiceConfig2W(service,SERVICE_CONFIG_FAILURE_ACTIONS_FLAG,&flag) &&
        ChangeServiceConfig2W(service,SERVICE_CONFIG_FAILURE_ACTIONS,&actions);
}
/* Read-only original installed supervisor check; no repair/arming. */
static inline bool l4_supervisor_crash_profile_current(DWORD pid,DWORD budget){
    if(pid!=GetCurrentProcessId()){SetLastError(ERROR_REVISION_MISMATCH);return false;}
    SC_HANDLE scm=OpenSCManagerW(NULL,NULL,SC_MANAGER_CONNECT);if(!scm)return false;
    SC_HANDLE service=OpenServiceW(scm,L"L4Superv",SERVICE_QUERY_CONFIG|SERVICE_QUERY_STATUS);CloseServiceHandle(scm);
    if(!service)return false;SERVICE_STATUS_PROCESS status={0};DWORD n=0;
    bool ok=QueryServiceStatusEx(service,SC_STATUS_PROCESS_INFO,(BYTE*)&status,sizeof(status),&n) &&
        status.dwServiceType==SERVICE_WIN32_OWN_PROCESS && status.dwCurrentState==SERVICE_RUNNING && status.dwProcessId==pid;
    if(ok)ok=l4_crash_system_account(service) && l4_supervisor_crash_profile_read(service,budget);DWORD error=GetLastError();CloseServiceHandle(service);
    if(!ok)SetLastError(error?error:ERROR_REVISION_MISMATCH);return ok;
}
