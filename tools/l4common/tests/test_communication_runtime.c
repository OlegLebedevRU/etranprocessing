#include "../communication_monitor.h"
#include "../switch_decode.h"
#include "../journal_internal.h"
#include "update_state_fixture.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sddl.h>
static unsigned checks,failures;
#define CHECK(x) do{++checks;if(!(x)){++failures;printf("FAIL comm plan %u: %s (error=%lu)\n",__LINE__,#x,GetLastError());}}while(0)
#include "../tests/communication_signal_fixture.h"
static void command_for(const L4Layout* roots,const wchar_t* version,unsigned i,wchar_t command[2048]){
    L4Layout layout;wchar_t image[MAX_PATH];const wchar_t* components[]={L"leo4proxy",L"mosquitto"};const wchar_t* exes[]={L"leo4proxy.exe",L"mosquitto.exe"};
    CHECK(l4_layout_from_roots(&layout,roots->binaries,roots->data,version));CHECK(l4_layout_component(&layout,components[i],exes[i],image));
    swprintf_s(command,2048,L"\"%ls\" --fixture",image);
}

static unsigned events[32],count,workers;static int fault_mode;static L4ServiceSwitch pair_mock[2];
static HANDLE barrier_entered,barrier_finish;
static DWORD states[2],selected;static wchar_t commands[2][2048];static DWORD fake_pids[]={900001,900002};
static ULONGLONG stamp(void){FILETIME t;GetSystemTimeAsFileTime(&t);return ((ULONGLONG)t.dwHighDateTime<<32)|t.dwLowDateTime;}
static void record(unsigned code){CHECK(count<32);if(count<32)events[count++]=code;}
static SC_HANDLE mock_manager(LPCWSTR a,LPCWSTR b,DWORD access){(void)a;(void)b;(void)access;return (SC_HANDLE)1;}
static SC_HANDLE mock_service(SC_HANDLE manager,LPCWSTR name,DWORD access){(void)manager;(void)access;
    selected=!wcscmp(name,L"mosquitto");return (SC_HANDLE)(ULONG_PTR)(10+selected);}
static BOOL mock_close_service(SC_HANDLE s){(void)s;return TRUE;}
static BOOL mock_config(SC_HANDLE s,LPQUERY_SERVICE_CONFIGW c,DWORD size,LPDWORD needed){
    unsigned i=(unsigned)(ULONG_PTR)s-10;CHECK(i<2);selected=i;*needed=sizeof(*c);
    if(!c || size<*needed){SetLastError(ERROR_INSUFFICIENT_BUFFER);return FALSE;}
    memset(c,0,sizeof(*c));c->dwServiceType=SERVICE_WIN32_OWN_PROCESS;c->dwStartType=SERVICE_AUTO_START;
    c->lpServiceStartName=L"LocalSystem";c->lpBinaryPathName=commands[i];return TRUE;
}
static BOOL mock_status(SC_HANDLE svc,SC_STATUS_TYPE type,LPBYTE bytes,DWORD size,LPDWORD needed){
    (void)type;CHECK(size==sizeof(SERVICE_STATUS_PROCESS));unsigned i=(unsigned)(ULONG_PTR)svc-10;selected=i;
    SERVICE_STATUS_PROCESS s={0};s.dwCurrentState=states[i];s.dwProcessId=states[i]==SERVICE_STOPPED?0:fake_pids[i];memcpy(bytes,&s,sizeof(s));*needed=sizeof(s);return TRUE;
}
static BOOL mock_control(SC_HANDLE svc,DWORD control,LPSERVICE_STATUS status){
    (void)status;CHECK(control==SERVICE_CONTROL_STOP);unsigned i=(unsigned)(ULONG_PTR)svc-10;states[i]=SERVICE_STOPPED;record(i?6:1);return TRUE;
}
static BOOL mock_start(SC_HANDLE svc,DWORD args,LPCWSTR* argv){(void)argv;CHECK(!args);unsigned i=(unsigned)(ULONG_PTR)svc-10;states[i]=SERVICE_RUNNING;record(i?8:3);return TRUE;}
static HANDLE mock_process(DWORD access,BOOL inherit,DWORD pid){(void)access;CHECK(!inherit);
    for(unsigned i=0;i<2;i++)if(pid==fake_pids[i])return (HANDLE)(ULONG_PTR)(1000+i);
    return OpenProcess(access,inherit,pid);
}
static bool fake(HANDLE h){return (ULONG_PTR)h==1000 || (ULONG_PTR)h==1001;}
static BOOL mock_close(HANDLE h){return fake(h)?TRUE:CloseHandle(h);}
static BOOL mock_times(HANDLE h,LPFILETIME c,LPFILETIME e,LPFILETIME k,LPFILETIME u){
    if(!fake(h))return GetProcessTimes(h,c,e,k,u);memset(c,0,8);c->dwLowDateTime=1;memset(e,0,8);memset(k,0,8);memset(u,0,8);return TRUE;
}
static DWORD mock_wait(HANDLE h,DWORD ms){if(!fake(h))return WaitForSingleObject(h,ms);return states[(ULONG_PTR)h-1000]==SERVICE_STOPPED?WAIT_OBJECT_0:WAIT_TIMEOUT;}
static BOOL mock_image(HANDLE h,DWORD flags,LPWSTR image,PDWORD size){if(!fake(h))return QueryFullProcessImageNameW(h,flags,image,size);
    const wchar_t* q=wcschr(commands[selected]+1,L'"');DWORD n=(DWORD)(q-commands[selected]-1);CHECK(*size>n);wcsncpy_s(image,*size,commands[selected]+1,n);*size=n;return TRUE;
}
static BOOL mock_token(HANDLE process,DWORD access,PHANDLE token){return OpenProcessToken(fake(process)?GetCurrentProcess():process,access,token);}
static BOOL mock_sid(PSID sid,WELL_KNOWN_SID_TYPE type){(void)sid;(void)type;return fault_mode==10?FALSE:TRUE;} /* Model SYSTEM only in this TU. */
static bool mock_pin(const L4Layout* layout,const L4ReleaseFile* file,L4ReleaseFence** fence,wchar_t path[MAX_PATH]){
    CHECK(file->size==100);*fence=(L4ReleaseFence*)1;return l4_layout_component(layout,file->component,file->file,path);
}
static void mock_unpin(L4ReleaseFence* fence){(void)fence;}
static bool mock_switch(const L4ServiceSwitch* p){unsigned i=!wcscmp(p->service,L"mosquitto");CHECK(states[i]==SERVICE_STOPPED);
    wcscpy_s(commands[i],2048,p->before.image_path);record(i?7:2);return true;
}
static bool mock_worker(const L4CommunicationPlan* p,DWORD ms){CHECK(p->worker_pid && ms);workers++;if(fault_mode==3){SetLastError(ERROR_ACCESS_DENIED);return false;}return true;}
#define OpenSCManagerW mock_manager
#define OpenServiceW mock_service
#define CloseServiceHandle mock_close_service
#define QueryServiceConfigW mock_config
#define QueryServiceStatusEx mock_status
#define ControlService mock_control
#define StartServiceW mock_start
#define OpenProcess mock_process
#define CloseHandle mock_close
#define GetProcessTimes mock_times
#define WaitForSingleObject mock_wait
#define QueryFullProcessImageNameW mock_image
#define OpenProcessToken mock_token
#define IsWellKnownSid mock_sid
#define l4_release_pin mock_pin
#define l4_release_unpin mock_unpin
#define l4_service_switch_rollback mock_switch
#define l4_communication_worker_exit mock_worker
#include "../communication_runtime.c"
#undef l4_communication_worker_exit
#undef l4_service_switch_rollback
#undef l4_release_unpin
#undef l4_release_pin
#undef QueryFullProcessImageNameW
#undef WaitForSingleObject
#undef GetProcessTimes
#undef CloseHandle
#undef OpenProcess
#undef StartServiceW
#undef ControlService
#undef QueryServiceStatusEx
#undef QueryServiceConfigW
#undef CloseServiceHandle
#undef OpenServiceW
#undef OpenSCManagerW
/* Same process-only SYSTEM seam; real monitor thread/event/locks/clock/state. */
static bool proof_worker_ok=true;
static bool proof_worker(const L4CommunicationPlan* p){CHECK(p && p->worker_pid);return proof_worker_ok;}
#define l4_communication_worker_live proof_worker
#define fail monitor_fail
#define system_owner monitor_system
#define utc monitor_utc
#include "../communication_monitor.c"
#undef l4_communication_worker_live
#undef utc
#undef system_owner
#undef fail
#undef IsWellKnownSid
#undef OpenProcessToken
static bool signal_probe(unsigned i,DWORD pid,DWORD ms,void* context){(void)context;CHECK(ms && pid==fake_pids[i]);record(i?9:4);return true;}
static bool signal_channels(DWORD pid,DWORD ms,void* context){(void)context;CHECK(pid==fake_pids[0] && ms);record(5);
    if(fault_mode==2){SetLastError(ERROR_NOT_READY);return false;}return true;}
static bool signal_barrier(DWORD ms,void* context){(void)context;CHECK(ms);record(10);if(fault_mode==14){SetEvent(barrier_entered);CHECK(WaitForSingleObject(barrier_finish,ms)==WAIT_OBJECT_0);}if(fault_mode==1){SetLastError(ERROR_TIMEOUT);return false;}return true;}
static void scenario(unsigned mode){
    count=workers=0;fault_mode=(int)mode;
    UpdateFixture fixture;bool initialized=update_fixture_init(&fixture);CHECK(initialized);if(!initialized)return;
    CHECK(update_fixture_put(&fixture,0));const wchar_t* id=L"17730000-0000-4000-8000-000000000001";
    L4Journal* j=NULL;CHECK(l4_journal_open(&fixture.layout,id,true,&j));if(!j)return;
    L4CommunicationPlan p={0};memcpy(&p.operation,j->header+8,16);p.generation=1;
    FILETIME now,exit,kernel,user;GetSystemTimeAsFileTime(&now);p.armed_utc=((ULONGLONG)now.dwHighDateTime<<32)|now.dwLowDateTime;p.deadline_utc=p.armed_utc+6000000000ull;

    wchar_t exe[MAX_PATH],cmd[MAX_PATH+40];CHECK(GetModuleFileNameW(NULL,exe,MAX_PATH));swprintf_s(cmd,_countof(cmd),L"\"%ls\" --hold-fixture",exe);
    STARTUPINFOW startup={sizeof(startup)};PROCESS_INFORMATION child={0};CHECK(CreateProcessW(exe,cmd,NULL,NULL,FALSE,CREATE_NO_WINDOW,NULL,NULL,&startup,&child));
    if(!child.hProcess){l4_journal_close(j);update_fixture_dispose(&fixture);return;}CloseHandle(child.hThread);
    p.worker_pid=child.dwProcessId;CHECK(GetProcessTimes(child.hProcess,&p.worker_created,&exit,&kernel,&user));
    p.supervisor_pid=GetCurrentProcessId();CHECK(GetProcessTimes(GetCurrentProcess(),&p.supervisor_created,&exit,&kernel,&user));
    if(mode==11){DWORD pid=p.worker_pid;FILETIME epoch=p.worker_created;p.worker_pid=p.supervisor_pid;p.worker_created=p.supervisor_created;p.supervisor_pid=pid;p.supervisor_created=epoch;}
    /* Functional cases exercise real NTFS flush/hash/lock work, not a 1s
     * performance limit. Give each phase 30s and monitor-proof mode8 time to
     * inspect a live window. Missing-marker expiry still uses the original 1s.
     * Production budgets remain explicit caller input. */
    p.armed_utc-=100000000ull;p.deadline_utc=stamp()+(mode==8?300000000ull:10000000ull);
    p.budget=(L4CommunicationBudget){30000,30000,30000,30000,30000,300000,30000,600000};
    BYTE* switches[2]={0},*configs[2]={0};const wchar_t* services[]={L"Leo4Proxy",L"mosquitto"};
    for(unsigned i=0;i<2;i++){L4ServiceSwitch s={0};CHECK(l4_layout_from_roots(&s.layout,fixture.layout.binaries,fixture.layout.data,L"1.13.3"));
        wcscpy_s(s.service,32,services[i]);s.before.installed=true;wcscpy_s(s.before.account,256,L"LocalSystem");s.before.start_type=SERVICE_AUTO_START;
        s.size=101;s.before_size=100;memset(s.sha256,2,32);memset(s.before_sha256,1,32);
        command_for(&fixture.layout,L"1.13.2",i,s.before.image_path);command_for(&fixture.layout,L"1.13.3",i,s.after);
        CHECK(l4_journal_save_switch(j,&s,&p.switch_sequence[i]));CHECK(l4_store_find_record(j,10,p.switch_sequence[i],&switches[i],&p.switch_size[i]));p.switches[i]=switches[i];
    }
    wchar_t directory[MAX_PATH];swprintf_s(directory,MAX_PATH,L"%ls\\mosquitto",fixture.layout.config);CHECK(CreateDirectoryW(directory,NULL));
    const wchar_t* names[]={L"mosquitto\\mosquitto.conf",L"mosquitto\\acl.conf"};
    for(unsigned i=0;i<2;i++){wchar_t path[MAX_PATH];swprintf_s(path,MAX_PATH,L"%ls\\%ls",fixture.layout.config,names[i]);
        HANDLE file=CreateFileW(path,GENERIC_WRITE,0,NULL,CREATE_NEW,0,NULL);CHECK(file!=INVALID_HANDLE_VALUE);DWORD written=0;CHECK(WriteFile(file,"old",3,&written,NULL));CloseHandle(file);
        CHECK(l4_config_prepare(j,names[i],"new",3,&p.config_sequence[i]));CHECK(l4_store_find_record(j,20,p.config_sequence[i],&configs[i],&p.config_size[i]));p.configs[i]=configs[i];
    }
    BYTE operation[76]={0};l4_store_u32(operation,1);l4_store_u32(operation+20,1);l4_store_u32(operation+24,2);
    for(unsigned i=0;i<2;i++){l4_store_u64(operation+28+i*8,p.config_sequence[i]);l4_store_u64(operation+44+i*8,p.switch_sequence[i]);}
    BYTE* con_bytes=fixture_communication_con(j,&p,operation);
    CHECK(l4_journal_append(j,64,operation,sizeof(operation),&p.sequence));CHECK(l4_store_hash(operation,sizeof(operation),NULL,0,p.operation_sha256));

    for(unsigned i=0;i<2;i++){CHECK(l4_switch_decode_bytes(&fixture.layout,p.switches[i],p.switch_size[i],&pair_mock[i]));
        wcscpy_s(commands[i],2048,pair_mock[i].after);states[i]=SERVICE_RUNNING;}
    CHECK(l4_communication_plan_prepare(j,&p,mode==11?GetCurrentProcess():child.hProcess));
    HANDLE leaked=NULL;CHECK(!OpenThreadToken(GetCurrentThread(),TOKEN_QUERY,TRUE,&leaked) && GetLastError()==ERROR_NO_TOKEN);if(leaked)CloseHandle(leaked);
    CHECK(l4_config_apply(j,p.config_sequence[0]) && l4_config_apply(j,p.config_sequence[1]));
    L4UpdateState state={0};state.window=1;state.generation=1;state.plan_sequence=p.sequence;state.deadline_utc=p.deadline_utc;
    strcpy_s(state.owner,40,"17730000-0000-4000-8000-000000000001");BYTE state_bytes[L4_UPDATE_STATE_SIZE];
    if(mode!=7 && mode!=12){CHECK(l4_update_state_encode(&state,state_bytes));CHECK(l4_update_state_replace(&fixture.layout,state_bytes,false));}
    l4_journal_close(j);j=NULL;
    L4CommunicationSignals signals={signal_probe,signal_channels,signal_barrier,NULL};L4CommunicationResult result;
    if(mode==4)wcscpy_s(commands[1],2048,L"foreign");
    if(mode==5){wchar_t path[MAX_PATH];swprintf_s(path,MAX_PATH,L"%ls\\mosquitto\\acl.conf",fixture.layout.config);
        HANDLE h=CreateFileW(path,GENERIC_WRITE,0,NULL,OPEN_EXISTING,0,NULL);CHECK(h!=INVALID_HANDLE_VALUE);DWORD written;CHECK(WriteFile(h,"bad",3,&written,NULL));CloseHandle(h);}
    if(mode==9)signals.barrier=NULL;
    if(mode==14){barrier_entered=CreateEventW(NULL,TRUE,FALSE,NULL);barrier_finish=CreateEventW(NULL,TRUE,FALSE,NULL);CHECK(barrier_entered && barrier_finish);}
    if((mode>=6 && mode<=8) || mode>=12){L4CommunicationMonitor* monitor=NULL;CHECK(l4_communication_monitor_start(&fixture.layout,id,&signals,&monitor));
        CHECK(monitor);if(monitor){
            if(mode==8){
                CHECK(WaitForSingleObject(monitor->ready,1000)==WAIT_OBJECT_0);
                CHECK(l4_communication_monitor_proof(monitor,monitor->pin,&state,1000,NULL));
                L4UpdateState wrong=state;wrong.deadline_utc++;
                CHECK(!l4_communication_monitor_proof(monitor,monitor->pin,&wrong,1000,NULL));
                proof_worker_ok=false;CHECK(!l4_communication_monitor_proof(monitor,monitor->pin,&state,1000,NULL));proof_worker_ok=true;
                HANDLE cancel=CreateEventW(NULL,TRUE,TRUE,NULL);CHECK(cancel);
                CHECK(!l4_communication_monitor_proof(monitor,monitor->pin,&state,1000,cancel) && GetLastError()==ERROR_CANCELLED);CloseHandle(cancel);
                L4CommunicationMonitor view=*monitor;InitializeSRWLock(&view.result_lock);view.monotonic_end=0;
                CHECK(!l4_communication_monitor_proof(&view,monitor->pin,&state,1000,NULL));
                view=*monitor;InitializeSRWLock(&view.result_lock);view.finished=true;
                CHECK(!l4_communication_monitor_proof(&view,monitor->pin,&state,1000,NULL));
            }
            if(mode==12){Sleep(30);CHECK(l4_update_state_encode(&state,state_bytes));CHECK(l4_update_state_replace(&fixture.layout,state_bytes,false));}
            if(mode==13){L4UpdateState foreign=state;foreign.generation++;CHECK(l4_update_state_encode(&foreign,state_bytes));CHECK(l4_update_state_replace(&fixture.layout,state_bytes,false));}
            if(mode==14){
                CHECK(WaitForSingleObject(barrier_entered,30000)==WAIT_OBJECT_0);CHECK(!l4_communication_monitor_close(&monitor,0) && monitor);SetEvent(barrier_finish);}
            if(mode==15){L4CommunicationDecision* d=NULL;CHECK(l4_communication_decision_open(&fixture.layout,id,1000,&d));
                CHECK(l4_communication_decision_finish(d,L4_COMM_DEC_COMMITTED,0,stamp()));l4_communication_decision_close(d);}
            if(mode==8)CHECK(l4_communication_monitor_close(&monitor,30000));
            else{bool done=false;ULONGLONG end=GetTickCount64()+30000;
                do{CHECK(l4_communication_monitor_poll(monitor,&done,&result));if(!done)Sleep(20);}while(!done && GetTickCount64()<end);
                DWORD expected_error=(DWORD)(mode==7?ERROR_TIMEOUT:mode==13?ERROR_REVISION_MISMATCH:mode==15?ERROR_INVALID_STATE:0);
                if(result.error!=expected_error)printf("monitor scenario%u observed error%lu expected%lu\n",mode,result.error,expected_error);
                CHECK(done);CHECK(result.error==expected_error);CHECK(l4_communication_monitor_close(&monitor,30000));}
        }
        CHECK(workers==(mode==6 || mode==12 || mode==14?1u:0u));
        if(mode==14){CloseHandle(barrier_entered);CloseHandle(barrier_finish);barrier_entered=barrier_finish=NULL;}
    }else{
        while(stamp()<p.deadline_utc)Sleep(10);
        bool success=l4_communication_execute(&fixture.layout,&state,&signals,&result);
        printf("scenario%u success%d error%lu completed%u workers%u steps%u\n",mode,success,result.error,result.completed,workers,count);CHECK(success==(mode==0));CHECK(result.outcome==(mode==0?L4_COMM_VERIFIED:mode==1?L4_COMM_CONNECTIVITY_UNCONFIRMED:L4_COMM_FAILED));
        L4CommunicationDecision* d=NULL;L4CommunicationPhase phase;DWORD error;
        CHECK(l4_communication_decision_open(&fixture.layout,id,1000,&d));CHECK(l4_communication_decision_read(d,&phase,&error));
        CHECK(phase==(mode==0?L4_COMM_DEC_RESTORED:mode==1?L4_COMM_DEC_UNCONFIRMED:mode>=9 && mode<=11?L4_COMM_DEC_WAIT:L4_COMM_DEC_FAILED));
        l4_communication_decision_close(d);unsigned was=count;
        CHECK(!l4_communication_execute(&fixture.layout,&state,&signals,&result));CHECK(was==count);
        if(mode==0 || mode==1){CHECK(count==10);for(unsigned i=0;i<count;i++)CHECK(events[i]==i+1);}
        if(mode==2)CHECK(count==5 && workers==1);if(mode==3)CHECK(!count && workers==1);if(mode==4)CHECK(!count && !workers);
        if(mode==5)CHECK(count==6 && workers==1);if(mode>=9 && mode<=11)CHECK(!count && !workers);
    }
    L4UpdateState after;CHECK(l4_update_state_read(&fixture.layout,&after));CHECK(after.window==(mode==7?0u:1u));
    if(mode==0 || mode==1 || mode==6 || mode==12 || mode==14){CHECK(l4_journal_open(&fixture.layout,id,false,&j));CHECK(l4_config_verify(j,p.config_sequence[0],false));CHECK(l4_config_verify(j,p.config_sequence[1],false));l4_journal_close(j);}
    CHECK(TerminateProcess(child.hProcess,0));CHECK(WaitForSingleObject(child.hProcess,5000)==WAIT_OBJECT_0);CloseHandle(child.hProcess);
    for(unsigned i=0;i<2;i++){free(switches[i]);free(configs[i]);}free(con_bytes);CHECK(update_fixture_dispose(&fixture));
}
int wmain(int argc,wchar_t** argv){
    if(argc==2 && !wcscmp(argv[1],L"--hold-fixture")){Sleep(60000);return 0;}
    for(unsigned i=0;i<16;i++)scenario(i);
    printf("Communication runtime/monitor: %u checks, %u failures; real journals/configs/decisions/thread, modeled SCM/images/SYSTEM/signals; no live services\n",checks,failures);return failures?1:0;
}
