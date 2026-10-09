#include <winsock2.h>
#include <windows.h>
#include <assert.h>
#include <stdio.h>
#include "readiness.h"
#include "broker_environment.h"
#include "../../l4common/probe_ipc.h"
static L4BootstrapPlan fixture_plan;
static bool acl_ok=true,config_ok=true,mutate_config,mutate_service,late_barrier,deny_barrier,drift;
static unsigned config_calls,acl_calls,probe_calls,barrier_calls;
static bool mutate_creation,created_drift;
static BOOL WINAPI fixture_times(HANDLE process,LPFILETIME created,LPFILETIME ended,LPFILETIME kernel,LPFILETIME user){
    BOOL ok=GetProcessTimes(process,created,ended,kernel,user);if(ok && created_drift)++created->dwLowDateTime;return ok;}
static bool identity_listeners=true,identity_http=true,identity_creation_drift;static unsigned identity_requests;
static bool identity_source(const wchar_t* command,WORD* http,WORD* mqtt,char expected[64]){assert(command);*http=18443;*mqtt=18883;strcpy_s(expected,64,"1111111111111111111111111111111111111111");return true;}
static bool identity_probe(int port,int timeout,char thumb[64]){assert(port==18443 && timeout);++identity_requests;strcpy_s(thumb,64,identity_http?"1111111111111111111111111111111111111111":"2222222222222222222222222222222222222222");if(identity_creation_drift)created_drift=true;return true;}
static bool identity_policy(int port,int timeout,const char* thumb){assert(port==18443 && timeout && thumb);return identity_http;}
static bool identity_owned(HANDLE process,DWORD pid,WORD first,WORD second){assert(process && pid==GetCurrentProcessId() && first==18443 && second==18883);return identity_listeners;}
#define l4_proxy_signal_source identity_source
#define setup_proxy_probe_thumbprint identity_probe
#define setup_proxy_probe_policy identity_policy
#define l4_child_listeners identity_owned
#define GetProcessTimes fixture_times
static SC_HANDLE WINAPI fake_scm_manager(LPCWSTR a,LPCWSTR b,DWORD rights){assert(!a&&!b&&rights==SC_MANAGER_CONNECT);return (SC_HANDLE)1;}
static SC_HANDLE WINAPI fake_scm_service(SC_HANDLE h,LPCWSTR name,DWORD rights){assert(h==(SC_HANDLE)1&&rights==(SERVICE_QUERY_CONFIG|SERVICE_QUERY_STATUS));
    const wchar_t* names[]={L"Leo4Proxy",L"mosquitto",L"L4Con",L"L4Superv"};for(unsigned i=0;i<4;i++)if(!wcscmp(name,names[i]))return (SC_HANDLE)(ULONG_PTR)(i+2);return NULL;}
static BOOL WINAPI close_service(SC_HANDLE h){(void)h;return TRUE;}
static BOOL WINAPI query_config(SC_HANDLE h,LPQUERY_SERVICE_CONFIGW cfg,DWORD size,LPDWORD needed){*needed=sizeof(*cfg);if(!cfg||size<sizeof(*cfg)){SetLastError(ERROR_INSUFFICIENT_BUFFER);return FALSE;}
    memset(cfg,0,sizeof(*cfg));cfg->lpBinaryPathName=fixture_plan.commands[(ULONG_PTR)h-2];cfg->lpServiceStartName=L"LocalSystem";cfg->dwServiceType=SERVICE_WIN32_OWN_PROCESS;return TRUE;}
static BOOL WINAPI query_status(SC_HANDLE h,SC_STATUS_TYPE type,LPBYTE bytes,DWORD size,LPDWORD needed){(void)h;assert(type==SC_STATUS_PROCESS_INFO&&size>=sizeof(SERVICE_STATUS_PROCESS));SERVICE_STATUS_PROCESS* s=(SERVICE_STATUS_PROCESS*)bytes;memset(s,0,sizeof(*s));s->dwCurrentState=drift?SERVICE_STOPPED:SERVICE_RUNNING;s->dwProcessId=GetCurrentProcessId();*needed=sizeof(*s);return TRUE;}
static bool inventory(const wchar_t* name,L4ServiceInventory* result){for(unsigned i=0;i<4;i++)if(!wcscmp(name,fixture_plan.services[i])){
    memset(result,0,sizeof(*result));result->installed=true;result->start_type=fixture_plan.start_types[i];wcscpy_s(result->account,256,L"LocalSystem");wcscpy_s(result->image_path,2048,fixture_plan.commands[i]);return true;}return false;}
static bool fixture_acl(const L4Layout* layout,const L4AccessActors* actors){assert(layout && actors);++acl_calls;return acl_ok;}
static bool fixture_config(L4Journal* journal,ULONGLONG sequence,bool candidate){assert(journal && sequence==42 && !candidate);++config_calls;return config_ok;}
static L4UpdateState marker;static bool state_ok=true,drain_ok=true,mutate_marker,late_drain;static unsigned drains;
static bool fixture_state(const L4Layout* layout,L4UpdateState* state){(void)layout;if(!state_ok){SetLastError(ERROR_INVALID_DATA);return false;}*state=marker;return true;}
#define l4_update_state_read fixture_state
#define l4_service_inventory inventory
#define l4_access_verify fixture_acl
#define l4_config_verify fixture_config
#define OpenSCManagerW fake_scm_manager
#define OpenServiceW fake_scm_service
#define CloseServiceHandle close_service
#define QueryServiceConfigW query_config
#define QueryServiceStatusEx query_status
static bool broker_environment(const L4BootstrapPlan* plan){assert(plan);return true;}
#define setup_broker_environment_verify broker_environment
#include "../src/readiness.c"
#undef OpenSCManagerW
#undef OpenServiceW
#undef CloseServiceHandle
#undef QueryServiceConfigW
#undef QueryServiceStatusEx
static unsigned health_wait,fresh_exchanges;
static DWORD fixture(DWORD mode,DWORD timeout,HANDLE cancel,void* context){(void)timeout;(void)cancel;(void)context;assert(mode<=1);
    if(!mode && health_wait){--health_wait;return ERROR_NOT_READY;}if(mode){assert(!health_wait);++fresh_exchanges;}return ERROR_SUCCESS;}
static bool preflight_probe(const L4BootstrapPlan* p,unsigned index,DWORD timeout,void* context){(void)context;assert(p==&fixture_plan && index<4 && timeout && timeout<=1000);++probe_calls;return true;}
static bool preflight_barrier(const L4BootstrapPlan* p,DWORD timeout,void* context){(void)context;assert(p==&fixture_plan && timeout && timeout<=1000);++barrier_calls;
    if(mutate_config)config_ok=false;if(mutate_service)drift=true;if(mutate_creation)created_drift=true;if(late_barrier)Sleep(30);return !deny_barrier;}
static DWORD drain_fixture(const L4UpdateState* expected,DWORD timeout,HANDLE cancel,void* context){
    (void)timeout;(void)cancel;(void)context;++drains;assert(expected->generation==marker.generation);
    if(mutate_marker)marker.generation++;if(late_drain)Sleep(50);return drain_ok?ERROR_SUCCESS:ERROR_NOT_READY;
}
static void test_stop_gate(L4Journal* journal,const L4AccessActors* actors,const ULONGLONG* refs,L4BootstrapChecks* checks){
    L4ReadinessSnapshot original;
    assert(setup_readiness_capture(journal,&fixture_plan,actors,refs,1,checks,1000,&original));
    FILETIME time;GetSystemTimeAsFileTime(&time);strcpy_s(marker.owner,40,"17730000-0000-4000-8000-000000000001");
    marker.window=1;marker.generation=1;marker.plan_sequence=64;marker.deadline_utc=(((ULONGLONG)time.dwHighDateTime<<32)|time.dwLowDateTime)+600000000ull;
    L4ProbeServer *con=NULL,*superv=NULL;assert(l4_probe_server_start_ex(L"con",fixture,drain_fixture,NULL,&con));assert(l4_probe_server_start_ex(L"superv",fixture,drain_fixture,NULL,&superv));
    L4UpdateState expected=marker;unsigned before=probe_calls,barriers=barrier_calls;
    assert(setup_readiness_quiescence(journal,&fixture_plan,actors,refs,1,checks,&original,&expected,1000));
    assert(drains==2 && probe_calls==before+2 && barrier_calls==barriers+1); /* No ordinary superv health in window1. */
    unsigned d=drains;state_ok=false;assert(!setup_readiness_quiescence(journal,&fixture_plan,actors,refs,1,checks,&original,&expected,1000));assert(drains==d);state_ok=true;
    created_drift=true;assert(!setup_readiness_quiescence(journal,&fixture_plan,actors,refs,1,checks,&original,&expected,1000));assert(drains==d);created_drift=false;
    original.services[2].pid++;assert(!setup_readiness_quiescence(journal,&fixture_plan,actors,refs,1,checks,&original,&expected,1000));assert(drains==d);original.services[2].pid--;
    drain_ok=false;assert(!setup_readiness_quiescence(journal,&fixture_plan,actors,refs,1,checks,&original,&expected,1000));assert(drains==d+1 && barrier_calls==barriers+1);drain_ok=true;
    mutate_marker=true;assert(!setup_readiness_quiescence(journal,&fixture_plan,actors,refs,1,checks,&original,&expected,1000));mutate_marker=false;marker=expected;
    mutate_config=true;assert(!setup_readiness_quiescence(journal,&fixture_plan,actors,refs,1,checks,&original,&expected,1000));mutate_config=false;config_ok=true;
    mutate_creation=true;assert(!setup_readiness_quiescence(journal,&fixture_plan,actors,refs,1,checks,&original,&expected,1000));mutate_creation=false;created_drift=false;
    deny_barrier=true;assert(!setup_readiness_quiescence(journal,&fixture_plan,actors,refs,1,checks,&original,&expected,1000));deny_barrier=false;
    late_drain=true;assert(!setup_readiness_quiescence(journal,&fixture_plan,actors,refs,1,checks,&original,&expected,20));late_drain=false;
    expected.deadline_utc=1;d=drains;assert(!setup_readiness_quiescence(journal,&fixture_plan,actors,refs,1,checks,&original,&expected,1000));assert(GetLastError()==ERROR_TIMEOUT && drains==d);
    l4_probe_server_stop(con);l4_probe_server_stop(superv);
    puts("Pre-stop gate: original PID/creation, operation marker, actual drain IPC, no superv health, repeated config/epoch, fresh barrier/errors/late reply PASS");
}
int main(void){
    WSADATA wsa;assert(!WSAStartup(MAKEWORD(2,2),&wsa));SOCKET listener=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);assert(listener!=INVALID_SOCKET);
    struct sockaddr_in address={0};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);assert(!bind(listener,(struct sockaddr*)&address,sizeof(address))&&!listen(listener,2));int size=sizeof(address);assert(!getsockname(listener,(struct sockaddr*)&address,&size));
    L4Readiness context={18443,ntohs(address.sin_port)};DWORD budgets[4]={1000,300000,1000,1000};L4BootstrapChecks checks;
    assert(setup_readiness_checks(&context,budgets,1000,&checks));assert(checks.probe(&fixture_plan,1,500,checks.context));
    closesocket(listener);assert(!checks.probe(&fixture_plan,1,100,checks.context));
    wcscpy_s(fixture_plan.services[2],32,L"L4Con");wcscpy_s(fixture_plan.services[3],32,L"L4Superv");wcscpy_s(fixture_plan.commands[2],2048,L"fixture-con");wcscpy_s(fixture_plan.commands[3],2048,L"fixture-superv");
    L4ProbeServer *con=NULL,*superv=NULL;assert(l4_probe_server_start(L"con",fixture,NULL,&con));assert(l4_probe_server_start(L"superv",fixture,NULL,&superv));
    assert(checks.probe(&fixture_plan,2,1000,checks.context));assert(checks.probe(&fixture_plan,3,1000,checks.context));assert(checks.barrier(&fixture_plan,1000,checks.context));
    unsigned previous_fresh=fresh_exchanges;health_wait=3;assert(checks.barrier(&fixture_plan,1000,checks.context));assert(!health_wait && fresh_exchanges==previous_fresh+1);
    l4_probe_server_stop(con);l4_probe_server_stop(superv);
    budgets[1]=299999;assert(!setup_readiness_checks(&context,budgets,1000,&checks));budgets[1]=300000;context.proxy_port=0;assert(!setup_readiness_checks(&context,budgets,1000,&checks));
    /* Pre-stop orchestration verdicts modeled; current process creation FILETIME
     * and layout validation are real. Existing actual TCP/pipe checks above stay. */
    L4Journal journal={0};assert(l4_layout_from_roots(&journal.layout,L"C:\\L4PreflightTest\\Programs",L"C:\\L4PreflightTest\\Data",L"1.13.3"));fixture_plan.layout=journal.layout;
    const wchar_t* names[]={L"Leo4Proxy",L"mosquitto",L"L4Con",L"L4Superv"};const wchar_t* components[]={L"leo4proxy",L"mosquitto",L"l4con",L"l4superv"};const wchar_t* exes[]={L"leo4proxy.exe",L"mosquitto.exe",L"l4con.exe",L"l4superv.exe"};
    for(unsigned i=0;i<4;i++){wchar_t path[MAX_PATH];wcscpy_s(fixture_plan.services[i],32,names[i]);assert(l4_layout_component(&journal.layout,components[i],exes[i],path));swprintf_s(fixture_plan.commands[i],2048,L"\"%ls\"",path);}
    checks.probe=preflight_probe;checks.barrier=preflight_barrier;checks.context=NULL;checks.barrier_ms=1000;memcpy(checks.service_ms,budgets,sizeof(budgets));L4AccessActors actors={0};ULONGLONG refs[]={42};
    assert(setup_readiness_preflight(&journal,&fixture_plan,&actors,refs,1,&checks,1000));assert(config_calls==2 && acl_calls==2 && probe_calls==4 && barrier_calls==1);
    unsigned previous=barrier_calls;config_ok=false;assert(!setup_readiness_preflight(&journal,&fixture_plan,&actors,refs,1,&checks,1000));assert(barrier_calls==previous);config_ok=true;
    acl_ok=false;assert(!setup_readiness_preflight(&journal,&fixture_plan,&actors,refs,1,&checks,1000));assert(barrier_calls==previous);acl_ok=true;
    mutate_config=true;assert(!setup_readiness_preflight(&journal,&fixture_plan,&actors,refs,1,&checks,1000));mutate_config=false;config_ok=true;
    mutate_service=true;assert(!setup_readiness_preflight(&journal,&fixture_plan,&actors,refs,1,&checks,1000));mutate_service=false;drift=false;
    mutate_creation=true;assert(!setup_readiness_preflight(&journal,&fixture_plan,&actors,refs,1,&checks,1000));mutate_creation=false;created_drift=false;
    deny_barrier=true;assert(!setup_readiness_preflight(&journal,&fixture_plan,&actors,refs,1,&checks,1000));deny_barrier=false;
    late_barrier=true;assert(!setup_readiness_preflight(&journal,&fixture_plan,&actors,refs,1,&checks,20));assert(GetLastError()==ERROR_TIMEOUT);late_barrier=false;
    test_stop_gate(&journal,&actors,refs,&checks);
    L4ReadinessSnapshot captured={0};captured.services[0].pid=GetCurrentProcessId();FILETIME e,k,u;assert(GetProcessTimes(GetCurrentProcess(),&captured.services[0].created,&e,&k,&u));char thumb[64];
    assert(setup_readiness_proxy_identity(&fixture_plan,&captured,1000,thumb) && strlen(thumb)==40 && identity_requests==1);
    identity_http=false;assert(!setup_readiness_proxy_identity(&fixture_plan,&captured,1000,thumb) && !*thumb);identity_http=true;
    identity_listeners=false;unsigned previous_requests=identity_requests;assert(!setup_readiness_proxy_identity(&fixture_plan,&captured,1000,thumb) && identity_requests==previous_requests);identity_listeners=true;
    captured.services[0].created.dwLowDateTime^=1;assert(!setup_readiness_proxy_identity(&fixture_plan,&captured,1000,thumb) && identity_requests==previous_requests);captured.services[0].created.dwLowDateTime^=1;
    identity_creation_drift=true;assert(!setup_readiness_proxy_identity(&fixture_plan,&captured,1000,thumb) && !*thumb);identity_creation_drift=false;created_drift=false;
    assert(!setup_readiness_proxy_identity(&fixture_plan,&captured,0,thumb));
    puts("Selected certificate capture: exact original epoch before/after, listener/HTTP/explicit certificate refusal PASS (signal responses modeled)");
    ULONGLONG duplicate[]={42,42};assert(!setup_readiness_preflight(&journal,&fixture_plan,&actors,duplicate,2,&checks,1000));
    fixture_plan.layout.cache[0]=L'Z';assert(!setup_readiness_preflight(&journal,&fixture_plan,&actors,refs,1,&checks,1000));fixture_plan.layout=journal.layout;
    wcscpy_s(fixture_plan.commands[0],2048,L"C:\\l4tools\\leo4proxy.exe");assert(!setup_readiness_preflight(&journal,&fixture_plan,&actors,refs,1,&checks,1000));
    WSACleanup();puts("Readiness adapter: actual TCP/pipe callbacks, modeled SCM, explicit budgets and missing listener PASS");return 0;
}
