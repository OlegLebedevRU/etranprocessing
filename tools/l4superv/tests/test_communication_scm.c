/* Actual SCM adapter acceptance. Unique test service, actual LocalSystem,
 * process handles and unchanged production stop/start/query code. No MQTT,
 * application probes, signed metadata or whole-update readiness claim. */
#include "../../l4common/communication_runtime.h"
#include "../../l4common/journal_internal.h"
#include "../../l4common/supervisor_crash_profile.h"
#include "../../l4common/tests/update_state_fixture.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sddl.h>
#include "../../l4common/communication_runtime.c"

static unsigned scm_checks,scm_failures;
#define CHECK(x) do { ++scm_checks; if(!(x)){ ++scm_failures; printf("FAIL SCM %u: %s (%lu)\n",__LINE__,#x,GetLastError()); fflush(stdout); } } while(0)
typedef struct { DWORD start_delay,stop_delay,exit_delay,fail_start,crash_delay; } FixtureMode;
static SERVICE_STATUS_HANDLE fixture_status_handle;
static HANDLE fixture_stop_event;
static wchar_t fixture_service_name[32],fixture_control[MAX_PATH];
static FixtureMode fixture_mode;static volatile LONG fixture_exit_hold;

static void fixture_report(DWORD state,DWORD error){
    SERVICE_STATUS s={0};s.dwServiceType=SERVICE_WIN32_OWN_PROCESS;s.dwCurrentState=state;
    s.dwWin32ExitCode=error;s.dwControlsAccepted=state==SERVICE_RUNNING?SERVICE_ACCEPT_STOP:0;
    s.dwCheckPoint=(state==SERVICE_START_PENDING || state==SERVICE_STOP_PENDING)?1:0;
    s.dwWaitHint=10000;SetServiceStatus(fixture_status_handle,&s);
}
static DWORD WINAPI fixture_control_handler(DWORD control,DWORD event,void* data,void* context){
    (void)event;(void)data;(void)context;
    if(control==SERVICE_CONTROL_STOP){fixture_report(SERVICE_STOP_PENDING,0);SetEvent(fixture_stop_event);return NO_ERROR;}
    return control==SERVICE_CONTROL_INTERROGATE?NO_ERROR:ERROR_CALL_NOT_IMPLEMENTED;
}
static void WINAPI fixture_service_main(DWORD argc,LPWSTR* argv){
    (void)argc;(void)argv;fixture_stop_event=CreateEventW(NULL,TRUE,FALSE,NULL);
    fixture_status_handle=RegisterServiceCtrlHandlerExW(fixture_service_name,fixture_control_handler,NULL);
    if(!fixture_status_handle || !fixture_stop_event)return;
    fixture_report(SERVICE_START_PENDING,0);HANDLE file=CreateFileW(fixture_control,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);DWORD read=0;
    bool ok=file!=INVALID_HANDLE_VALUE && ReadFile(file,&fixture_mode,sizeof(fixture_mode),&read,NULL) && read==sizeof(fixture_mode);
    if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);
    if(!ok || fixture_mode.fail_start){fixture_report(SERVICE_STOPPED,ERROR_SERVICE_SPECIFIC_ERROR);CloseHandle(fixture_stop_event);return;}
    InterlockedExchange(&fixture_exit_hold,(LONG)fixture_mode.exit_delay);
    if(fixture_mode.start_delay)Sleep(fixture_mode.start_delay);
    fixture_report(SERVICE_RUNNING,0);
    if(fixture_mode.crash_delay && WaitForSingleObject(fixture_stop_event,fixture_mode.crash_delay)==WAIT_TIMEOUT)
        ExitProcess(ERROR_PROCESS_ABORTED); /* This isolated fixture crashes itself only. */
    else if(!fixture_mode.crash_delay)WaitForSingleObject(fixture_stop_event,60000);
    if(fixture_mode.stop_delay)Sleep(fixture_mode.stop_delay);
    fixture_report(SERVICE_STOPPED,0);
    CloseHandle(fixture_stop_event);
}
static bool fixture_write(const wchar_t* path,FixtureMode mode){
    HANDLE h=CreateFileW(path,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,0,NULL);if(h==INVALID_HANDLE_VALUE)return false;
    DWORD n=0;bool ok=WriteFile(h,&mode,sizeof(mode),&n,NULL) && n==sizeof(mode) && FlushFileBuffers(h);CloseHandle(h);return ok;
}
static bool fixture_image(const L4Layout* layout,const wchar_t* source,wchar_t output[MAX_PATH]){
    if(!CreateDirectoryW(layout->release,NULL) && GetLastError()!=ERROR_ALREADY_EXISTS)return false;
    wchar_t directory[MAX_PATH];if(swprintf_s(directory,MAX_PATH,L"%ls\\mosquitto",layout->release)<0 || !CreateDirectoryW(directory,NULL) ||
        !l4_layout_component(layout,L"mosquitto",L"mosquitto.exe",output) || !CopyFileW(source,output,TRUE))return false;
    PSECURITY_DESCRIPTOR sd=NULL;
    bool ok=ConvertStringSecurityDescriptorToSecurityDescriptorW(L"O:BAG:BAD:P(A;;FA;;;SY)(A;;FA;;;BA)(A;;GRGX;;;BU)",SDDL_REVISION_1,&sd,NULL) &&
        SetFileSecurityW(output,OWNER_SECURITY_INFORMATION|DACL_SECURITY_INFORMATION|PROTECTED_DACL_SECURITY_INFORMATION,sd);
    if(sd)LocalFree(sd);return ok;
}
static void fixture_release_process(Native* n){
    if(n->processes[1])CloseHandle(n->processes[1]);n->processes[1]=NULL;n->pids[1]=0;
}
static bool fixture_wait_state(SC_HANDLE service,DWORD expected,DWORD timeout){
    ULONGLONG end=GetTickCount64()+timeout;SERVICE_STATUS_PROCESS s={0};DWORD size=0;
    do{if(!QueryServiceStatusEx(service,SC_STATUS_PROCESS_INFO,(BYTE*)&s,sizeof(s),&size))return false;
        if(s.dwCurrentState==expected)return true;Sleep(10);
    }while(GetTickCount64()<end);SetLastError(ERROR_TIMEOUT);return false;
}
/* Cleanup only the handle returned by CREATE_NEW service registration. Verify
 * its exact private command before stop/delete; never look up a production name. */
static bool fixture_service_remove(SC_HANDLE service,const L4ServiceSwitch* p){
    SERVICE_STATUS_PROCESS s={0};if(!query(service,p,false,&s))return false;
    if(s.dwCurrentState==SERVICE_START_PENDING && !fixture_wait_state(service,SERVICE_RUNNING,5000))return false;
    if(!query(service,p,false,&s))return false;
    HANDLE process=s.dwProcessId?OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,s.dwProcessId):NULL;
    bool ok=!s.dwProcessId || (process && process_matches(process,p,false));
    if(ok && s.dwCurrentState==SERVICE_RUNNING){SERVICE_STATUS status;ok=ControlService(service,SERVICE_CONTROL_STOP,&status)!=0;}
    if(ok)ok=fixture_wait_state(service,SERVICE_STOPPED,5000);
    if(ok && process)ok=WaitForSingleObject(process,5000)==WAIT_OBJECT_0;
    if(process)CloseHandle(process);if(ok)ok=DeleteService(service)!=0;return ok;
}
static bool fixture_absent(SC_HANDLE manager,const wchar_t* name){
    ULONGLONG end=GetTickCount64()+5000;
    do{SC_HANDLE s=OpenServiceW(manager,name,SERVICE_QUERY_STATUS);if(s){CloseServiceHandle(s);}
        else if(GetLastError()==ERROR_SERVICE_DOES_NOT_EXIST)return true;
        else if(GetLastError()!=ERROR_SERVICE_MARKED_FOR_DELETE)return false;Sleep(10);
    }while(GetTickCount64()<end);SetLastError(ERROR_TIMEOUT);return false;
}
static void actual_scm_cases(void){
    CHECK(system_owner());if(scm_failures)return;
    UpdateFixture f;CHECK(update_fixture_init(&f));if(scm_failures)return;
    L4Layout target;CHECK(l4_layout_from_roots(&target,f.layout.binaries,f.layout.data,L"1.13.3") && l4_layout_prepare(&target));
    wchar_t exe[MAX_PATH],old_image[MAX_PATH],new_image[MAX_PATH],control[MAX_PATH];CHECK(GetModuleFileNameW(NULL,exe,MAX_PATH));
    CHECK(fixture_image(&f.layout,exe,old_image));CHECK(fixture_image(&target,exe,new_image));
    swprintf_s(control,MAX_PATH,L"%ls\\scm-mode.bin",f.layout.state);
    Native n={0};n.roots=&f.layout;L4ServiceSwitch* p=&n.pair[1];p->layout=target;p->before.installed=true;
    p->before.start_type=SERVICE_DEMAND_START;wcscpy_s(p->before.account,256,L"LocalSystem");
    swprintf_s(p->service,32,L"L4ScmFixture-%lu",GetCurrentProcessId());
    swprintf_s(p->before.image_path,2048,L"\"%ls\" --fixture-service %ls \"%ls\"",old_image,p->service,control);
    swprintf_s(p->after,2048,L"\"%ls\" --fixture-service %ls \"%ls\"",new_image,p->service,control);
    CHECK(fixture_write(control,(FixtureMode){0}));CHECK(system_owner());if(scm_failures){CHECK(update_fixture_dispose(&f));return;}SC_HANDLE manager=OpenSCManagerW(NULL,NULL,SC_MANAGER_CREATE_SERVICE|SC_MANAGER_CONNECT);
    CHECK(manager);SC_HANDLE service=manager?CreateServiceW(manager,p->service,p->service,SERVICE_ALL_ACCESS,SERVICE_WIN32_OWN_PROCESS,SERVICE_DEMAND_START,
        SERVICE_ERROR_NORMAL,p->before.image_path,NULL,NULL,NULL,L"LocalSystem",NULL):NULL;
    CHECK(service);if(service){printf("Created exact owned SCM service %ls under %ls\n",p->service,f.root);fflush(stdout);}
    if(!service){if(manager)CloseServiceHandle(manager);CHECK(update_fixture_dispose(&f));return;}

    CHECK(start(&n,1,5000));CHECK(live(&n,1));CHECK(stop(&n,1,5000));CHECK(WaitForSingleObject(n.processes[1],0)==WAIT_OBJECT_0);fixture_release_process(&n);
    puts("SCM healthy: actual RUNNING PID/creation/System -> STOPPED and process exit PASS");

    wchar_t orphan_command[MAX_PATH+32];swprintf_s(orphan_command,_countof(orphan_command),L"\"%ls\" --fixture-hold",old_image);
    STARTUPINFOW startup={sizeof(startup)};PROCESS_INFORMATION orphan={0};
    CHECK(CreateProcessW(old_image,orphan_command,NULL,NULL,FALSE,CREATE_NO_WINDOW,NULL,NULL,&startup,&orphan));
    if(orphan.hThread)CloseHandle(orphan.hThread);
    ULONGLONG orphan_start=GetTickCount64();CHECK(start(&n,1,5000));CHECK(GetTickCount64()-orphan_start>=500);
    DWORD orphan_exit=STILL_ACTIVE;CHECK(orphan.hProcess && WaitForSingleObject(orphan.hProcess,5000)==WAIT_OBJECT_0 && GetExitCodeProcess(orphan.hProcess,&orphan_exit) && orphan_exit==0);
    if(orphan.hProcess)CloseHandle(orphan.hProcess);CHECK(stop(&n,1,5000));fixture_release_process(&n);
    puts("SCM approved orphan: actual source process waited without killing or adopting PID PASS");

    CHECK(fixture_write(control,(FixtureMode){250,0,0,0}));ULONGLONG begun=GetTickCount64();CHECK(start(&n,1,5000));CHECK(GetTickCount64()-begun>=200);CHECK(stop(&n,1,5000));fixture_release_process(&n);
    puts("SCM delayed startup: actual START_PENDING then RUNNING PASS");

    CHECK(fixture_write(control,(FixtureMode){0,0,600,0}));CHECK(start(&n,1,5000));begun=GetTickCount64();CHECK(stop(&n,1,5000));CHECK(GetTickCount64()-begun>=500);CHECK(WaitForSingleObject(n.processes[1],0)==WAIT_OBJECT_0);fixture_release_process(&n);
    puts("SCM STOPPED is insufficient: held actual process exit waited PASS");

    CHECK(fixture_write(control,(FixtureMode){0,600,0,0}));CHECK(start(&n,1,5000));begun=GetTickCount64();CHECK(!stop(&n,1,100));CHECK(GetTickCount64()-begun<2000);
    CHECK(fixture_wait_state(service,SERVICE_STOPPED,5000));CHECK(WaitForSingleObject(n.processes[1],5000)==WAIT_OBJECT_0);fixture_release_process(&n);
    puts("SCM slow stop: actual STOP_PENDING times out, ownership retained PASS");

    CHECK(fixture_write(control,(FixtureMode){600,0,0,0}));begun=GetTickCount64();CHECK(!start(&n,1,100));CHECK(GetTickCount64()-begun<2000);
    CHECK(fixture_wait_state(service,SERVICE_RUNNING,5000));CHECK(stop(&n,1,5000));fixture_release_process(&n);
    puts("SCM slow start: bounded refusal, owned pending service cleaned PASS");

    CHECK(fixture_write(control,(FixtureMode){0,0,0,1}));CHECK(!start(&n,1,5000));CHECK(fixture_wait_state(service,SERVICE_STOPPED,5000));fixture_release_process(&n);
    puts("SCM failed startup: no RUNNING/readiness success PASS");

    CHECK(fixture_write(control,(FixtureMode){0}));CHECK(ChangeServiceConfigW(service,SERVICE_NO_CHANGE,SERVICE_AUTO_START,SERVICE_NO_CHANGE,NULL,NULL,NULL,NULL,NULL,NULL,NULL));
    CHECK(!start(&n,1,1000) && GetLastError()==ERROR_REVISION_MISMATCH);CHECK(fixture_wait_state(service,SERVICE_STOPPED,1000));
    CHECK(ChangeServiceConfigW(service,SERVICE_NO_CHANGE,SERVICE_DEMAND_START,SERVICE_NO_CHANGE,NULL,NULL,NULL,NULL,NULL,NULL,NULL));
    CHECK(ChangeServiceConfigW(service,SERVICE_NO_CHANGE,SERVICE_NO_CHANGE,SERVICE_NO_CHANGE,NULL,NULL,NULL,NULL,L"NT AUTHORITY\\LocalService",NULL,NULL));
    CHECK(!start(&n,1,1000) && GetLastError()==ERROR_REVISION_MISMATCH);CHECK(fixture_wait_state(service,SERVICE_STOPPED,1000));
    CHECK(ChangeServiceConfigW(service,SERVICE_NO_CHANGE,SERVICE_NO_CHANGE,SERVICE_NO_CHANGE,NULL,NULL,NULL,NULL,L"LocalSystem",NULL,NULL));
    wchar_t drift[2048];swprintf_s(drift,2048,L"%ls --unexpected",p->before.image_path);
    CHECK(ChangeServiceConfigW(service,SERVICE_NO_CHANGE,SERVICE_NO_CHANGE,SERVICE_NO_CHANGE,drift,NULL,NULL,NULL,NULL,NULL,NULL));
    CHECK(!start(&n,1,1000) && GetLastError()==ERROR_REVISION_MISMATCH);CHECK(fixture_wait_state(service,SERVICE_STOPPED,1000));
    CHECK(ChangeServiceConfigW(service,SERVICE_NO_CHANGE,SERVICE_NO_CHANGE,SERVICE_NO_CHANGE,p->before.image_path,NULL,NULL,NULL,NULL,NULL,NULL));
    puts("SCM actual account/start mode/ImagePath drift: refused before start PASS");

    CHECK(start(&n,1,5000));n.created[1].dwLowDateTime^=1;CHECK(!live(&n,1));n.created[1].dwLowDateTime^=1;CHECK(live(&n,1));CHECK(stop(&n,1,5000));fixture_release_process(&n);

    /* Exercise the real fixed profile only on the CREATE_NEW private service.
     * Do not call the production-name current() gate or alter L4Superv. */
    CHECK(l4_supervisor_crash_profile_write(service));CHECK(l4_supervisor_crash_profile_read(service,5000));
    CHECK(!l4_supervisor_crash_profile_read(service,L4_SUPERVISOR_RESTART_MS-1));
    SERVICE_FAILURE_ACTIONS_FLAG noncrash={TRUE};
    CHECK(ChangeServiceConfig2W(service,SERVICE_CONFIG_FAILURE_ACTIONS_FLAG,&noncrash));
    CHECK(!l4_supervisor_crash_profile_read(service,5000));
    CHECK(l4_supervisor_crash_profile_write(service));CHECK(l4_supervisor_crash_profile_read(service,5000));
    CHECK(fixture_write(control,(FixtureMode){0,0,0,0,1500}));CHECK(start(&n,1,5000));
    DWORD crashed_pid=n.pids[1];FILETIME crashed_created=n.created[1];
    /* The first child has read its mode by RUNNING; the restart reads healthy mode. */
    CHECK(fixture_write(control,(FixtureMode){0}));
    CHECK(WaitForSingleObject(n.processes[1],5000)==WAIT_OBJECT_0);fixture_release_process(&n);
    ULONGLONG restart_end=GetTickCount64()+10000;SERVICE_STATUS_PROCESS restarted={0};DWORD needed=0;
    do{CHECK(QueryServiceStatusEx(service,SC_STATUS_PROCESS_INFO,(BYTE*)&restarted,sizeof(restarted),&needed));
        if(restarted.dwCurrentState==SERVICE_RUNNING && restarted.dwProcessId && restarted.dwProcessId!=crashed_pid)break;
        Sleep(50);
    }while(GetTickCount64()<restart_end);
    CHECK(restarted.dwCurrentState==SERVICE_RUNNING && restarted.dwProcessId && restarted.dwProcessId!=crashed_pid);
    CHECK(observe(&n,1,service,&restarted,true));CHECK(live(&n,1));CHECK(CompareFileTime(&n.created[1],&crashed_created)!=0);
    CHECK(stop(&n,1,5000));fixture_release_process(&n);
    Sleep(1500);CHECK(fixture_wait_state(service,SERVICE_STOPPED,1000));
    puts("SCM crash profile: exact readback/drift refusal, self-crash recovered with new process epoch; graceful stop stays stopped PASS");
    bool removed=fixture_service_remove(service,p);CHECK(removed);CloseServiceHandle(service);
    bool absent=removed && fixture_absent(manager,p->service);CHECK(absent);CloseServiceHandle(manager);
    if(absent){CHECK(update_fixture_dispose(&f));printf("Removed exact owned SCM service %ls and private fixture tree\n",p->service);}
    else printf("Retained owned SCM fixture %ls and recovery files under %ls; cleanup refused\n",p->service,f.root);
}
int wmain(int argc,wchar_t** argv){
    if(argc==2 && !wcscmp(argv[1],L"--fixture-hold")){Sleep(600);return 0;}
    if(argc==4 && !wcscmp(argv[1],L"--fixture-service")){
        if(wcsncmp(argv[2],L"L4ScmFixture-",13) || wcslen(argv[2])>=32 || wcslen(argv[3])>=MAX_PATH)return ERROR_INVALID_PARAMETER;
        wcscpy_s(fixture_service_name,32,argv[2]);wcscpy_s(fixture_control,MAX_PATH,argv[3]);
        SERVICE_TABLE_ENTRYW table[]={{fixture_service_name,fixture_service_main},{NULL,NULL}};
        bool connected=StartServiceCtrlDispatcherW(table)!=0;DWORD error=GetLastError();
        /* Dispatcher can return as soon as STOPPED is reported. Keep the actual
         * host process alive here, rather than sleeping in the service thread. */
        LONG hold=InterlockedCompareExchange(&fixture_exit_hold,0,0);if(hold>0)Sleep((DWORD)hold);
        return connected?0:(int)error;
    }
    if(argc!=2 || wcscmp(argv[1],L"--system-scm")){
        puts("Actual isolated SCM fixture: --system-scm requires LocalSystem; unique test service only; no MQTT/production services; normal build does not run it.");return argc==2 && !wcscmp(argv[1],L"--help")?0:ERROR_INVALID_PARAMETER;
    }
    actual_scm_cases();printf("Actual SCM adapter: %u checks, %u failures; actual LocalSystem/SCM/primary tokens/PID/exit; no application barrier/full update acceptance\n",scm_checks,scm_failures);return scm_failures?1:0;
}
