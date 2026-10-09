#include <winsock2.h>
#include "readiness.h"
#include "broker_environment.h"
#include "proxy_probe.h"
#include "../../l4common/probe_ipc.h"
#include "../../l4common/journal_internal.h"
#include "../../l4common/proxy_certificate.h"
#include "../../l4common/child_probe.h"
#include <stdlib.h>
#include <stdio.h>
#include <wchar.h>
#include <string.h>
static DWORD left(ULONGLONG deadline){ULONGLONG now=GetTickCount64();return now<deadline?(DWORD)(deadline-now):0;}
static bool service_pid(const L4BootstrapPlan* plan,unsigned index,DWORD* pid){
    SC_HANDLE manager=OpenSCManagerW(NULL,NULL,SC_MANAGER_CONNECT);if(!manager)return false;
    SC_HANDLE service=OpenServiceW(manager,plan->services[index],SERVICE_QUERY_CONFIG|SERVICE_QUERY_STATUS);DWORD code=GetLastError();bool ok=service!=NULL;
    DWORD needed=0;if(ok){QueryServiceConfigW(service,NULL,0,&needed);ok=GetLastError()==ERROR_INSUFFICIENT_BUFFER && needed>=sizeof(QUERY_SERVICE_CONFIGW) && needed<=65536;if(!ok)code=ERROR_INVALID_DATA;}
    QUERY_SERVICE_CONFIGW* config=ok?(QUERY_SERVICE_CONFIGW*)malloc(needed):NULL;if(ok&&!config){ok=false;code=ERROR_NOT_ENOUGH_MEMORY;}
    if(ok){ok=QueryServiceConfigW(service,config,needed,&needed)!=0;code=GetLastError();}
    if(ok && (!config->lpBinaryPathName || wcscmp(config->lpBinaryPathName,plan->commands[index]) || !config->lpServiceStartName || _wcsicmp(config->lpServiceStartName,L"LocalSystem") || config->dwServiceType!=SERVICE_WIN32_OWN_PROCESS)){ok=false;code=ERROR_REVISION_MISMATCH;}
    SERVICE_STATUS_PROCESS status={0};if(ok){ok=QueryServiceStatusEx(service,SC_STATUS_PROCESS_INFO,(BYTE*)&status,sizeof(status),&needed)!=0;code=GetLastError();}
    if(ok && (status.dwCurrentState!=SERVICE_RUNNING || !status.dwProcessId)){ok=false;code=ERROR_SERVICE_NOT_ACTIVE;}
    if(ok)*pid=status.dwProcessId;free(config);if(service)CloseServiceHandle(service);CloseServiceHandle(manager);if(!ok)SetLastError(code);return ok;
}
static bool broker_listener(int port,DWORD timeout){
    WSADATA wsa;if(WSAStartup(MAKEWORD(2,2),&wsa))return false;SOCKET sock=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);bool ok=false;
    if(sock!=INVALID_SOCKET){u_long mode=1;struct sockaddr_in address={0};address.sin_family=AF_INET;address.sin_port=htons((u_short)port);address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
        if(!ioctlsocket(sock,FIONBIO,&mode)){
            int result=connect(sock,(struct sockaddr*)&address,sizeof(address));if(!result)ok=true;
            else if(WSAGetLastError()==WSAEWOULDBLOCK){fd_set writes,errors;FD_ZERO(&writes);FD_ZERO(&errors);FD_SET(sock,&writes);FD_SET(sock,&errors);struct timeval wait={(long)(timeout/1000),(long)((timeout%1000)*1000)};
                if(select(0,NULL,&writes,&errors,&wait)>0 && FD_ISSET(sock,&writes) && !FD_ISSET(sock,&errors)){int error=0,size=sizeof(error);ok=!getsockopt(sock,SOL_SOCKET,SO_ERROR,(char*)&error,&size)&&!error;}
            }
        }closesocket(sock);
    }WSACleanup();return ok;
}
static bool probe(const L4BootstrapPlan* plan,unsigned index,DWORD timeout,void* context){
    L4Readiness* options=(L4Readiness*)context;ULONGLONG deadline=GetTickCount64()+timeout;DWORD code=ERROR_NOT_READY;
    while(left(deadline)){
        DWORD remaining=left(deadline),slice=remaining<1000?remaining:1000;bool ok=false;
        if(index==0){DWORD pid=0,after=0;WORD http=0,mqtt=0;char expected[64];
            ok=l4_proxy_signal_source(plan->commands[0],&http,&mqtt,expected) && http==options->proxy_port &&
                service_pid(plan,0,&pid);
            HANDLE process=ok?OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,pid):NULL;
            FILETIME created,ended,kernel,user,final_created;
            ok=ok && process && GetProcessTimes(process,&created,&ended,&kernel,&user) &&
                l4_child_listeners(process,pid,http,mqtt) && left(deadline) &&
                setup_proxy_probe_policy(http,(int)slice,expected) && left(deadline) &&
                l4_child_listeners(process,pid,http,mqtt) && service_pid(plan,0,&after) && pid==after &&
                GetProcessTimes(process,&final_created,&ended,&kernel,&user) && !CompareFileTime(&created,&final_created);
            if(process)CloseHandle(process);
        }
        else if(index==1)ok=setup_broker_environment_verify(plan) && broker_listener(options->broker_port,slice); /* Full MQTT gate follows through L4Con. */
        else if(index==2 || index==3){DWORD pid=0,after=0;
            ok=service_pid(plan,index,&pid) && l4_probe_call(index==2?L"con":L"superv",pid,0,remaining) && service_pid(plan,index,&after) && pid==after;
        }else {SetLastError(ERROR_INVALID_PARAMETER);return false;}
        if(ok && left(deadline))return true;code=GetLastError();remaining=left(deadline);if(remaining)Sleep(remaining<50?remaining:50);
    }SetLastError(code?code:ERROR_TIMEOUT);return false;
}
static bool barrier(const L4BootstrapPlan* plan,DWORD timeout,void* context){
    (void)context;DWORD pid=0,after=0;ULONGLONG deadline=GetTickCount64()+timeout;
    bool ok=setup_broker_environment_verify(plan) && service_pid(plan,2,&pid) && left(deadline) && l4_probe_barrier_call(pid,left(deadline)) && service_pid(plan,2,&after) && pid==after && left(deadline) && setup_broker_environment_verify(plan) && left(deadline);
    if(!ok && !GetLastError())SetLastError(ERROR_NOT_READY);return ok;
}
bool setup_readiness_checks(L4Readiness* context,const DWORD service_ms[4],DWORD barrier_ms,L4BootstrapChecks* checks){
    if(!context || !checks || !service_ms || context->proxy_port<1 || context->proxy_port>65535 || context->broker_port<1 || context->broker_port>65535 || !barrier_ms || barrier_ms>300000 || service_ms[1]!=300000){SetLastError(ERROR_INVALID_PARAMETER);return false;}
    for(unsigned i=0;i<4;i++)if(!service_ms[i] || service_ms[i]>300000){SetLastError(ERROR_INVALID_PARAMETER);return false;}
    memset(checks,0,sizeof(*checks));checks->probe=probe;checks->barrier=barrier;checks->context=context;memcpy(checks->service_ms,service_ms,sizeof(checks->service_ms));checks->barrier_ms=barrier_ms;return true;
}
typedef L4ReadinessEpoch ServiceEpoch;
static bool epoch(const L4BootstrapPlan* source,unsigned index,ServiceEpoch* result){
    L4ServiceInventory current;DWORD after=0;
    if(!l4_service_inventory(source->services[index],&current) || !current.installed ||
        current.start_type!=source->start_types[index] || wcscmp(current.image_path,source->commands[index]) ||
        _wcsicmp(current.account,L"LocalSystem")){SetLastError(ERROR_REVISION_MISMATCH);return false;}
    if(!service_pid(source,index,&result->pid))return false;
    HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,result->pid);if(!process)return false;
    FILETIME exit,kernel,user;DWORD code=0;bool ok=GetProcessTimes(process,&result->created,&exit,&kernel,&user)!=0;
    if(!ok)code=GetLastError();CloseHandle(process);
    if(ok)ok=service_pid(source,index,&after) && after==result->pid;
    if(!ok)SetLastError(code?code:ERROR_RETRY);return ok;
}
static bool config_check(L4Journal* journal,const ULONGLONG* sequences,unsigned count){
    for(unsigned i=0;i<count;i++){if(!sequences[i]){SetLastError(ERROR_INVALID_PARAMETER);return false;}
        for(unsigned k=0;k<i;k++)if(sequences[k]==sequences[i]){SetLastError(ERROR_DUP_NAME);return false;}
        if(!l4_config_verify(journal,sequences[i],false))return false;
    }return true;
}
static bool source_valid(L4Journal* journal,const L4BootstrapPlan* source){
    /* Fixed current suite profile; do not probe arbitrary SCM services/commands
     * taken from an initiator. Independently signed full source remains mandatory. */
    const wchar_t* names[]={L"Leo4Proxy",L"mosquitto",L"L4Con",L"L4Superv"};
    const wchar_t* components[]={L"leo4proxy",L"mosquitto",L"l4con",L"l4superv"};
    const wchar_t* executables[]={L"leo4proxy.exe",L"mosquitto.exe",L"l4con.exe",L"l4superv.exe"};
    if(!wmemchr(source->layout.release,0,MAX_PATH)){SetLastError(ERROR_INVALID_DATA);return false;}
    const wchar_t* version=wcsrchr(source->layout.release,L'\\');L4Layout canonical;
    if(!version || !l4_layout_from_roots(&canonical,journal->layout.binaries,journal->layout.data,version+1) || memcmp(&canonical,&source->layout,sizeof(canonical))){SetLastError(ERROR_INVALID_DATA);return false;}
    for(unsigned i=0;i<4;i++){wchar_t image[MAX_PATH],prefix[MAX_PATH+3];
        if(!wmemchr(source->services[i],0,32) || !wmemchr(source->commands[i],0,2048)){SetLastError(ERROR_INVALID_DATA);return false;}
        if(wcscmp(source->services[i],names[i]) || !l4_layout_component(&canonical,components[i],executables[i],image)){SetLastError(ERROR_INVALID_DATA);return false;}
        swprintf_s(prefix,_countof(prefix),L"\"%ls\"",image);size_t n=wcslen(prefix);
        if(wcsncmp(prefix,source->commands[i],n) || (source->commands[i][n] && source->commands[i][n]!=L' ' && source->commands[i][n]!=L'\t')){SetLastError(ERROR_INVALID_DATA);return false;}
    }
    return true;
}
bool setup_readiness_capture(L4Journal* journal,const L4BootstrapPlan* source,const L4AccessActors* actors,
    const ULONGLONG* configs,unsigned count,const L4BootstrapChecks* checks,DWORD timeout,L4ReadinessSnapshot* snapshot){
    if(snapshot)memset(snapshot,0,sizeof(*snapshot));
    if(!journal || !source || !actors || !checks || !checks->probe || !checks->barrier || !timeout || timeout>300000 ||
        count>L4_OPERATION_CONFIG_LIMIT || (count && !configs) || !checks->barrier_ms || checks->barrier_ms>300000 || checks->service_ms[1]!=300000){SetLastError(ERROR_INVALID_PARAMETER);return false;}
    for(unsigned i=0;i<4;i++)if(!checks->service_ms[i] || checks->service_ms[i]>300000){SetLastError(ERROR_INVALID_PARAMETER);return false;}
    if(!source_valid(journal,source))return false;
    ULONGLONG deadline=GetTickCount64()+timeout;ServiceEpoch before[4],after;
    bool ok=config_check(journal,configs,count) && l4_access_verify(&source->layout,actors);
    for(unsigned i=0;ok && i<4;i++)ok=left(deadline) && epoch(source,i,&before[i]);
    for(unsigned i=0;ok && i<4;i++){DWORD budget=left(deadline);if(budget>checks->service_ms[i])budget=checks->service_ms[i];ok=budget && checks->probe(source,i,budget,checks->context) && left(deadline);}
    if(ok){DWORD budget=left(deadline);if(budget>checks->barrier_ms)budget=checks->barrier_ms;ok=budget && checks->barrier(source,budget,checks->context) && left(deadline);}
    /* No success survives config/account/process changes during the remote gate. */
    if(ok)ok=config_check(journal,configs,count) && l4_access_verify(&source->layout,actors);
    for(unsigned i=0;ok && i<4;i++)ok=left(deadline) && epoch(source,i,&after) && after.pid==before[i].pid &&
        CompareFileTime(&after.created,&before[i].created)==0;
    if(ok && !left(deadline))ok=false;
    if(!ok){if(!left(deadline))SetLastError(ERROR_TIMEOUT);else if(!GetLastError())SetLastError(ERROR_RETRY);}
    if(ok && snapshot)memcpy(snapshot->services,before,sizeof(before));return ok;
}

bool setup_readiness_preflight(L4Journal* journal,const L4BootstrapPlan* source,const L4AccessActors* actors,
    const ULONGLONG* configs,unsigned count,const L4BootstrapChecks* checks,DWORD timeout){
    return setup_readiness_capture(journal,source,actors,configs,count,checks,timeout,NULL);
}
static bool state_equal(const L4UpdateState* a,const L4UpdateState* b){
    return !memcmp(a->owner,b->owner,40) && a->window==b->window && a->generation==b->generation &&
        a->plan_sequence==b->plan_sequence && a->deadline_utc==b->deadline_utc;
}
static DWORD gate_left(ULONGLONG deadline,const L4UpdateState* expected){
    FILETIME time;GetSystemTimeAsFileTime(&time);ULONGLONG now=((ULONGLONG)time.dwHighDateTime<<32)|time.dwLowDateTime;
    DWORD budget=left(deadline);if(now>=expected->deadline_utc)return 0;
    ULONGLONG utc=(expected->deadline_utc-now)/10000;if(utc<budget)budget=(DWORD)utc;return budget;
}
static bool gate_local(L4Journal* j,const L4BootstrapPlan* source,const L4AccessActors* actors,
    const ULONGLONG* configs,unsigned count,const L4ReadinessSnapshot* original,const L4UpdateState* expected,ULONGLONG deadline){
    L4UpdateState current;
    if(!gate_left(deadline,expected)){SetLastError(ERROR_TIMEOUT);return false;}
    if(!l4_update_state_read(&j->layout,&current))return false;
    if(!state_equal(&current,expected)){SetLastError(ERROR_REVISION_MISMATCH);return false;}
    if(!config_check(j,configs,count) || !l4_access_verify(&source->layout,actors))return false;
    for(unsigned i=0;i<4;i++){
        ServiceEpoch observed;if(!original->services[i].pid || !epoch(source,i,&observed))return false;
        if(observed.pid!=original->services[i].pid || CompareFileTime(&observed.created,&original->services[i].created)){
            SetLastError(ERROR_REVISION_MISMATCH);return false;
        }
    }
    if(!l4_update_state_read(&j->layout,&current))return false;
    if(!state_equal(&current,expected)){SetLastError(ERROR_REVISION_MISMATCH);return false;}
    if(!gate_left(deadline,expected)){SetLastError(ERROR_TIMEOUT);return false;}
    return true;
}
bool setup_readiness_quiescence(L4Journal* j,const L4BootstrapPlan* source,const L4AccessActors* actors,
    const ULONGLONG* configs,unsigned count,const L4BootstrapChecks* checks,const L4ReadinessSnapshot* original,
    const L4UpdateState* expected,DWORD timeout){
    if(!j || !source || !actors || !checks || !checks->probe || !checks->barrier || !original || !expected ||
       expected->window!=L4_UPDATE_COMMUNICATION || !expected->generation || !expected->plan_sequence ||
       !timeout || timeout>300000 || count>L4_OPERATION_CONFIG_LIMIT || (count && !configs) || !checks->barrier_ms || checks->barrier_ms>300000 ||
       checks->service_ms[1]!=300000){SetLastError(ERROR_INVALID_PARAMETER);return false;}
    for(unsigned i=0;i<4;i++)if(!checks->service_ms[i] || checks->service_ms[i]>300000){SetLastError(ERROR_INVALID_PARAMETER);return false;}
    if(!source_valid(j,source))return false;ULONGLONG deadline=GetTickCount64()+timeout;
    if(!gate_local(j,source,actors,configs,count,original,expected,deadline))return false;
    const unsigned order[]={3,2};
    for(unsigned k=0;k<2;k++){
        unsigned index=order[k];DWORD budget=gate_left(deadline,expected);if(budget>checks->service_ms[index])budget=checks->service_ms[index];
        if(!budget){SetLastError(ERROR_TIMEOUT);return false;}
        if(!l4_probe_drain_call(index==2?L"con":L"superv",original->services[index].pid,expected,budget) ||
           !gate_local(j,source,actors,configs,count,original,expected,deadline))return false;
    }
    for(unsigned i=0;i<2;i++){
        DWORD budget=gate_left(deadline,expected);if(budget>checks->service_ms[i])budget=checks->service_ms[i];
        if(!budget){SetLastError(ERROR_TIMEOUT);return false;}
        if(!checks->probe(source,i,budget,checks->context))return false;
    }
    DWORD budget=gate_left(deadline,expected);if(budget>checks->barrier_ms)budget=checks->barrier_ms;
    if(!budget){SetLastError(ERROR_TIMEOUT);return false;}
    return checks->barrier(source,budget,checks->context) &&
        gate_local(j,source,actors,configs,count,original,expected,deadline);
}

bool setup_readiness_proxy_identity(const L4BootstrapPlan* source,const L4ReadinessSnapshot* original,DWORD timeout,char thumbprint[64]){
    if(!source || !original || !thumbprint || !timeout || timeout>300000){SetLastError(ERROR_INVALID_PARAMETER);return false;}thumbprint[0]=0;
    ULONGLONG end=GetTickCount64()+timeout;WORD http=0,mqtt=0;char expected[64];ServiceEpoch before={0},after={0};
    bool ok=l4_proxy_signal_source(source->commands[0],&http,&mqtt,expected) && epoch(source,0,&before) &&
        before.pid==original->services[0].pid && !CompareFileTime(&before.created,&original->services[0].created);
    HANDLE process=ok?OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,before.pid):NULL;
    if(ok)ok=process!=NULL && l4_child_listeners(process,before.pid,http,mqtt) && left(end) && setup_proxy_probe_thumbprint(http,(int)left(end),thumbprint) &&
        (!*expected || !_stricmp(expected,thumbprint)) && left(end) && setup_proxy_probe_policy(http,(int)left(end),thumbprint) &&
        l4_child_listeners(process,before.pid,http,mqtt) && epoch(source,0,&after) &&
        after.pid==before.pid && !CompareFileTime(&before.created,&after.created) && left(end);
    DWORD error=GetLastError();if(process)CloseHandle(process);if(!ok){thumbprint[0]=0;SetLastError(error?error:ERROR_REVISION_MISMATCH);}return ok;
}
