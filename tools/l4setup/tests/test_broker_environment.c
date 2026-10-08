/* Real isolated SCM/registry and SYSTEM Environment inheritance. Bootstrap journal
 * admission is modeled; no production service, PATH, broker or MQTT is touched. */
#include "../src/broker_environment.h"
#include "../../l4common/journal_internal.h"
#include <stdio.h>
#include <string.h>
#define L4_BROKER_SERVICE L"L4EnvFixture_1773_a71f42"
#define L4_BROKER_VARIABLE L"L4ENVFIXTURE_DIR"
static L4BootstrapPlan fixture_plan;
void l4_store_u64(BYTE* out,ULONGLONG value){for(unsigned i=0;i<8;i++)out[i]=(BYTE)(value>>(8*i));}
bool l4_bootstrap_load(L4Journal* j,ULONGLONG seq,L4BootstrapPlan* p){(void)j;if(seq!=1)return false;*p=fixture_plan;return true;}
bool l4_journal_append(L4Journal* j,DWORD kind,const void* data,DWORD size,ULONGLONG* seq){(void)j;(void)data;(void)seq;return (kind==86 || kind==87) && size==8;}
#include "../src/broker_environment.c"
static unsigned checks,failures;
#define CHECK(x) do{++checks;if(!(x)){++failures;printf("FAIL %u %s (%lu)\n",__LINE__,#x,GetLastError());}}while(0)
static SERVICE_STATUS_HANDLE status_handle;
static DWORD WINAPI handler(DWORD code,DWORD type,void* data,void* context){(void)code;(void)type;(void)data;(void)context;return NO_ERROR;}
static void WINAPI service_main(DWORD argc,wchar_t** argv){(void)argc;(void)argv;
    status_handle=RegisterServiceCtrlHandlerExW(L4_BROKER_SERVICE,handler,NULL);if(!status_handle)return;
    SERVICE_STATUS status={0};status.dwServiceType=SERVICE_WIN32_OWN_PROCESS;status.dwCurrentState=SERVICE_RUNNING;SetServiceStatus(status_handle,&status);
    wchar_t value[MAX_PATH];DWORD n=GetEnvironmentVariableW(L4_BROKER_VARIABLE,value,MAX_PATH);
    bool ok=n && n<MAX_PATH && !wcscmp(value,L"C:\\L4EnvFixture\\config\\mosquitto");
    status.dwCurrentState=SERVICE_STOPPED;status.dwWin32ExitCode=ok?0:ERROR_SERVICE_SPECIFIC_ERROR;status.dwServiceSpecificExitCode=ok?0:ERROR_INVALID_DATA;SetServiceStatus(status_handle,&status);
}
int wmain(int argc,wchar_t** argv){
    if(argc==2 && !wcscmp(argv[1],L"--fixture-service")){SERVICE_TABLE_ENTRYW table[]={{L4_BROKER_SERVICE,service_main},{NULL,NULL}};return StartServiceCtrlDispatcherW(table)?0:(int)GetLastError();}
    wchar_t self[MAX_PATH],command_line[1024];CHECK(GetModuleFileNameW(NULL,self,MAX_PATH));swprintf_s(command_line,1024,L"\"%ls\" --fixture-service",self);
    CHECK(l4_layout_from_roots(&fixture_plan.layout,L"C:\\L4EnvFixturePrograms",L"C:\\L4EnvFixture",L"1.13.2"));
    wcscpy_s(fixture_plan.services[1],_countof(fixture_plan.services[1]),L4_BROKER_SERVICE);wcscpy_s(fixture_plan.commands[1],_countof(fixture_plan.commands[1]),command_line);fixture_plan.start_types[1]=SERVICE_AUTO_START;
    SC_HANDLE scm=OpenSCManagerW(NULL,NULL,SC_MANAGER_CONNECT|SC_MANAGER_CREATE_SERVICE);CHECK(scm!=NULL);if(!scm)return 1;
    wchar_t owner[128];swprintf_s(owner,128,L"%ls [L4:17730000-0000-4000-8000-000000000001:1]",L4_BROKER_SERVICE);
    SC_HANDLE service=CreateServiceW(scm,L4_BROKER_SERVICE,owner,SERVICE_ALL_ACCESS,SERVICE_WIN32_OWN_PROCESS,SERVICE_DEMAND_START,SERVICE_ERROR_NORMAL,command_line,NULL,NULL,NULL,L"LocalSystem",NULL);
    CHECK(service!=NULL);if(!service){CloseServiceHandle(scm);return 1;} /* existing foreign fixture is never adopted */
    L4Journal journal={0};journal.lock=(HANDLE)(ULONG_PTR)1;wcscpy_s(journal.directory,MAX_PATH,L"C:\\L4EnvFixture\\17730000-0000-4000-8000-000000000001");
    CHECK(!setup_broker_environment_verify(&fixture_plan));CHECK(setup_broker_environment_prepare(&journal,1));CHECK(setup_broker_environment_verify(&fixture_plan));CHECK(setup_broker_environment_prepare(&journal,1));
    CHECK(StartServiceW(service,0,NULL));SERVICE_STATUS_PROCESS status={0};DWORD n=0;ULONGLONG end=GetTickCount64()+30000;
    do{CHECK(QueryServiceStatusEx(service,SC_STATUS_PROCESS_INFO,(BYTE*)&status,sizeof(status),&n));if(status.dwCurrentState==SERVICE_STOPPED)break;Sleep(50);}while(GetTickCount64()<end);
    CHECK(status.dwCurrentState==SERVICE_STOPPED);CHECK(status.dwWin32ExitCode==0);
    if(status.dwCurrentState==SERVICE_STOPPED){CHECK(DeleteService(service));CloseServiceHandle(service);service=NULL;
        end=GetTickCount64()+5000;for(;;){SC_HANDLE old=OpenServiceW(scm,L4_BROKER_SERVICE,SERVICE_QUERY_STATUS);DWORD code=GetLastError();if(old)CloseServiceHandle(old);if(!old && code==ERROR_SERVICE_DOES_NOT_EXIST)break;if(GetTickCount64()>=end){CHECK(false);break;}Sleep(50);}}
    if(service)CloseServiceHandle(service);CloseServiceHandle(scm);printf("Broker Environment: %u checks, %u failures; real isolated SCM/REG_MULTI_SZ/SYSTEM inheritance; journal modeled; no live services/MQTT\n",checks,failures);return failures?1:0;
}
