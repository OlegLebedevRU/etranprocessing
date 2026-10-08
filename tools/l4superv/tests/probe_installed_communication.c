/* Explicit installed-stand prerequisite probe; never part of release admission.
 * Query-only SCM/process handles and existing Con IPC, no MQTT connection,
 * service mutation, marker, recovery decision or test failure injection. */
#include "../../l4common/probe_ipc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    SC_HANDLE service;HANDLE process;DWORD pid,start;FILETIME created;
    wchar_t command[2048];
} Installed;
static bool fail(DWORD error){SetLastError(error);return false;}
static DWORD left(ULONGLONG end){ULONGLONG now=GetTickCount64();return now<end?(DWORD)(end-now):0;}
static bool config(Installed* p,bool capture){
    DWORD size=0;QueryServiceConfigW(p->service,NULL,0,&size);
    if(GetLastError()!=ERROR_INSUFFICIENT_BUFFER || size<sizeof(QUERY_SERVICE_CONFIGW) || size>65536)return fail(ERROR_INVALID_DATA);
    QUERY_SERVICE_CONFIGW* c=malloc(size);if(!c)return fail(ERROR_NOT_ENOUGH_MEMORY);
    bool ok=QueryServiceConfigW(p->service,c,size,&size)!=0;
    if(ok)ok=c->dwServiceType==SERVICE_WIN32_OWN_PROCESS && c->lpServiceStartName && !wcscmp(c->lpServiceStartName,L"LocalSystem") &&
        c->lpBinaryPathName && wcslen(c->lpBinaryPathName)<2048;
    if(ok && capture){wcscpy_s(p->command,2048,c->lpBinaryPathName);p->start=c->dwStartType;}
    else if(ok)ok=p->start==c->dwStartType && !wcscmp(p->command,c->lpBinaryPathName);
    free(c);return ok?true:fail(ERROR_REVISION_MISMATCH);
}
static bool running(Installed* p,bool capture){
    SERVICE_STATUS_PROCESS s={0};DWORD size=0;
    if(!QueryServiceStatusEx(p->service,SC_STATUS_PROCESS_INFO,(BYTE*)&s,sizeof(s),&size))return false;
    if(s.dwCurrentState!=SERVICE_RUNNING || !s.dwProcessId)return fail(ERROR_NOT_READY);
    if(capture)p->pid=s.dwProcessId;return p->pid==s.dwProcessId?true:fail(ERROR_REVISION_MISMATCH);
}
static bool unchanged(Installed* p){
    FILETIME c,e,k,u;
    return config(p,false) && running(p,false) && GetProcessTimes(p->process,&c,&e,&k,&u) &&
        !CompareFileTime(&c,&p->created) && WaitForSingleObject(p->process,0)==WAIT_TIMEOUT?true:fail(ERROR_REVISION_MISMATCH);
}
static bool capture(SC_HANDLE manager,const wchar_t* name,Installed* p){
    p->service=OpenServiceW(manager,name,SERVICE_QUERY_CONFIG|SERVICE_QUERY_STATUS);if(!p->service)return false;
    if(!config(p,true) || !running(p,true))return false;
    p->process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,p->pid);if(!p->process)return false;
    FILETIME e,k,u;wchar_t image[MAX_PATH];DWORD size=MAX_PATH;
    const wchar_t* end=p->command[0]==L'"'?wcschr(p->command+1,L'"'):NULL;
    bool ok=end && end-p->command-1<MAX_PATH && GetProcessTimes(p->process,&p->created,&e,&k,&u) &&
        QueryFullProcessImageNameW(p->process,0,image,&size) && size==(DWORD)(end-p->command-1) && !_wcsnicmp(image,p->command+1,size);
    HANDLE token=NULL;BYTE user[512];DWORD needed=0;
    if(ok)ok=OpenProcessToken(p->process,TOKEN_QUERY,&token) && GetTokenInformation(token,TokenUser,user,sizeof(user),&needed) &&
        IsWellKnownSid(((TOKEN_USER*)user)->User.Sid,WinLocalSystemSid);
    if(token)CloseHandle(token);return ok && unchanged(p)?true:fail(ERROR_REVISION_MISMATCH);
}
static void close_pin(Installed* p){if(p->process)CloseHandle(p->process);if(p->service)CloseServiceHandle(p->service);}
static DWORD probe(Installed* p,const wchar_t* component,DWORD mode,ULONGLONG end){
    DWORD ms=left(end);if(!ms)return ERROR_TIMEOUT;if(!unchanged(p))return GetLastError();
    /* Diagnostic mode0 must not consume the other channel's observation. The
     * fresh barrier gets the remaining common window, never a renewed timer. */
    if(!mode && ms>5000)ms=5000;
    bool ok=l4_probe_call(component,p->pid,mode,ms);DWORD code=ok?ERROR_SUCCESS:GetLastError();
    if(!left(end))return ERROR_TIMEOUT;if(!unchanged(p))return GetLastError();return code?code:(ok?ERROR_SUCCESS:ERROR_NOT_READY);
}
int wmain(int argc,wchar_t** argv){
    if(argc==2 && !wcscmp(argv[1],L"--help")){
        puts("--live-preflight: explicit installed L4Con/L4Superv IPC health and fresh existing Con REQ/RSP+EVT/EVA; no service changes or live update admission.");return 0;
    }
    if(argc!=2 || wcscmp(argv[1],L"--live-preflight"))return ERROR_INVALID_PARAMETER;
    ULONGLONG end=GetTickCount64()+60000;Installed con={0},supervisor={0};
    DWORD con_error=ERROR_NOT_READY,sup_error=ERROR_NOT_READY,barrier_error=ERROR_NOT_SUPPORTED;bool attempted=false,epochs=false;
    SC_HANDLE manager=OpenSCManagerW(NULL,NULL,SC_MANAGER_CONNECT);
    if(manager){
        con_error=capture(manager,L"L4Con",&con)?ERROR_SUCCESS:GetLastError();
        sup_error=capture(manager,L"L4Superv",&supervisor)?ERROR_SUCCESS:GetLastError();CloseServiceHandle(manager);
        if(!con_error)con_error=probe(&con,L"con",0,end);
        if(!sup_error)sup_error=probe(&supervisor,L"superv",0,end);
        if(!con_error && !sup_error){attempted=true;barrier_error=probe(&con,L"con",1,end);}
        epochs=con.process && supervisor.process && unchanged(&con) && unchanged(&supervisor);
    }else con_error=sup_error=GetLastError();
    printf("{\"v\":1,\"scope\":\"installed-ipc-prerequisites\",\"con_pid\":%lu,\"supervisor_pid\":%lu,\"con_health_error\":%lu,\"supervisor_health_error\":%lu,\"barrier_attempted\":%s,\"barrier_error\":%lu,\"epochs_unchanged\":%s,\"live_update_enabled\":false}\n",
        con.pid,supervisor.pid,con_error,sup_error,attempted?"true":"false",barrier_error,epochs?"true":"false");
    close_pin(&con);close_pin(&supervisor);
    return !con_error && !sup_error && attempted && !barrier_error && epochs?0:1;
}
