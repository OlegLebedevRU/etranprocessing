#include "../communication_monitor.h"
#include "../recovery_store.h"
#include "../switch_decode.h"
#include "../journal_internal.h"
#include "update_state_fixture.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sddl.h>
static unsigned checks,failures;static bool real_system;
#define CHECK(x) do{++checks;if(!(x)){++failures;printf("FAIL comm plan %u: %s (error=%lu)\n",__LINE__,#x,GetLastError());}}while(0)
#include "../tests/communication_signal_fixture.h"
static void command_for(const L4Layout* roots,const wchar_t* version,unsigned i,wchar_t command[2048]){
    L4Layout layout;wchar_t image[MAX_PATH];const wchar_t* components[]={L"leo4proxy",L"mosquitto"};const wchar_t* exes[]={L"leo4proxy.exe",L"mosquitto.exe"};
    CHECK(l4_layout_from_roots(&layout,roots->binaries,roots->data,version));CHECK(l4_layout_component(&layout,components[i],exes[i],image));
    swprintf_s(command,2048,L"\"%ls\" --fixture",image);
}

static unsigned events[32],count,workers;static int fault_mode;static L4ServiceSwitch pair_mock[2];
static HANDLE barrier_entered,barrier_finish;
static DWORD states[3],selected;static wchar_t commands[3][2048];static DWORD fake_pids[]={900001,900002,0};
static ULONGLONG stamp(void){FILETIME t;GetSystemTimeAsFileTime(&t);return ((ULONGLONG)t.dwHighDateTime<<32)|t.dwLowDateTime;}
static void record(unsigned code){CHECK(count<32);if(count<32)events[count++]=code;}
static SC_HANDLE mock_manager(LPCWSTR a,LPCWSTR b,DWORD access){(void)a;(void)b;(void)access;return (SC_HANDLE)1;}
static SC_HANDLE mock_service(SC_HANDLE manager,LPCWSTR name,DWORD access){(void)manager;(void)access;
    selected=!wcscmp(name,L"L4Superv")?2:!wcscmp(name,L"mosquitto");return (SC_HANDLE)(ULONG_PTR)(10+selected);}
static BOOL mock_close_service(SC_HANDLE s){(void)s;return TRUE;}
static BOOL mock_config(SC_HANDLE s,LPQUERY_SERVICE_CONFIGW c,DWORD size,LPDWORD needed){
    unsigned i=(unsigned)(ULONG_PTR)s-10;CHECK(i<3);selected=i;*needed=sizeof(*c);
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
static BOOL mock_sid(PSID sid,WELL_KNOWN_SID_TYPE type){return fault_mode==10?FALSE:real_system?IsWellKnownSid(sid,type):TRUE;} /* Model SYSTEM only in this TU. */
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
static bool mock_monitor_worker_live(const L4CommunicationPlan* p){CHECK(p && p->worker_pid);return true;}
#define l4_communication_worker_live mock_monitor_worker_live
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

static BOOL boot_image(HANDLE h,DWORD flags,LPWSTR image,PDWORD size){
    if(fake(h))return mock_image(h,flags,image,size);
    if(GetProcessId(h)==GetCurrentProcessId()){const wchar_t* q=wcschr(commands[2]+1,L'"');DWORD n=(DWORD)(q-commands[2]-1);if(*size<=n)return FALSE;wcsncpy_s(image,*size,commands[2]+1,n);*size=n;return TRUE;}
    return QueryFullProcessImageNameW(h,flags,image,size);
}
#define fail boot_fail
#define system_owner boot_system_owner
#define OpenSCManagerW mock_manager
#define OpenServiceW mock_service
#define CloseServiceHandle mock_close_service
#define QueryServiceConfigW mock_config
#define QueryServiceStatusEx mock_status
#define QueryFullProcessImageNameW boot_image
#define l4_release_pin mock_pin
#define l4_release_unpin mock_unpin
#define IsWellKnownSid mock_sid
#include "../communication_boot.c"
#undef IsWellKnownSid
#undef l4_release_unpin
#undef l4_release_pin
#undef QueryFullProcessImageNameW
#undef QueryServiceStatusEx
#undef QueryServiceConfigW
#undef CloseServiceHandle
#undef OpenServiceW
#undef OpenSCManagerW
#undef system_owner
#undef fail
static void scenario(unsigned mode){
    InterlockedExchange(&boot_attempted,0); /* Independent modeled new supervisor lifetime for each scenario. */
    count=workers=0;fault_mode=0;
    UpdateFixture fixture;bool initialized=update_fixture_init(&fixture);CHECK(initialized);if(!initialized)return;
    CHECK(update_fixture_put(&fixture,0));const wchar_t* id=L"17730000-0000-4000-8000-000000000001";
    L4Journal* j=NULL;CHECK(l4_journal_open(&fixture.layout,id,true,&j));if(!j)return;
    L4CommunicationPlan p={0};memcpy(&p.operation,j->header+8,16);p.generation=1;
    FILETIME now,exit,kernel,user;GetSystemTimeAsFileTime(&now);p.armed_utc=((ULONGLONG)now.dwHighDateTime<<32)|now.dwLowDateTime;p.deadline_utc=p.armed_utc+6000000000ull;

    wchar_t exe[MAX_PATH],cmd[MAX_PATH+40];CHECK(GetModuleFileNameW(NULL,exe,MAX_PATH));swprintf_s(cmd,_countof(cmd),L"\"%ls\" --hold-fixture",exe);
    STARTUPINFOW startup={sizeof(startup)};PROCESS_INFORMATION child={0};CHECK(CreateProcessW(exe,cmd,NULL,NULL,FALSE,CREATE_NO_WINDOW,NULL,NULL,&startup,&child));
    if(!child.hProcess){l4_journal_close(j);update_fixture_dispose(&fixture);return;}CloseHandle(child.hThread);
    p.worker_pid=child.dwProcessId;CHECK(GetProcessTimes(child.hProcess,&p.worker_created,&exit,&kernel,&user));
    PROCESS_INFORMATION old={0};CHECK(CreateProcessW(exe,cmd,NULL,NULL,FALSE,CREATE_NO_WINDOW,NULL,NULL,&startup,&old));CHECK(old.hProcess);CloseHandle(old.hThread);
    p.supervisor_pid=old.dwProcessId;CHECK(GetProcessTimes(old.hProcess,&p.supervisor_created,&exit,&kernel,&user));
    p.armed_utc-=100000000ull;p.deadline_utc=stamp()+600000000ull;
    /* Real NTFS flush/hash/lock work belongs to functional fixtures, not a 1s
     * performance assertion. Keep bounded 30s phases; mode8 still expires its
     * original 1s admission reserve. Production policy is not changed here. */
    p.budget=(L4CommunicationBudget){mode==8?1000:30000,30000,30000,30000,30000,300000,30000,600000};
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


    CHECK(l4_communication_plan_prepare(j,&p,child.hProcess));
    L4RecoveryPlan anchor={0};anchor.operation=p.operation;anchor.sequence=p.sequence;
    anchor.worker_pid=p.worker_pid;anchor.worker_created=p.worker_created;anchor.supervisor_pid=p.supervisor_pid;anchor.supervisor_created=p.supervisor_created;
    if(mode==6)anchor.supervisor_created.dwLowDateTime^=1;
    anchor.armed_utc=p.armed_utc;anchor.deadline_utc=p.deadline_utc+((ULONGLONG)p.budget.total_ms+1000)*10000;
    anchor.recovery_ms=1000;anchor.start_type=SERVICE_AUTO_START;anchor.old_size=100;anchor.new_size=101;memset(anchor.old_sha256,1,32);memset(anchor.new_sha256,2,32);
    anchor.old_exists=true;anchor.old_config=(BYTE*)"old";anchor.old_config_size=3;anchor.new_config=(BYTE*)"new";anchor.new_config_size=3;
    PSECURITY_DESCRIPTOR sd=NULL;CHECK(ConvertStringSecurityDescriptorToSecurityDescriptorW(L"O:BAG:BAD:P(A;;FA;;;SY)(A;;FA;;;BA)(A;;FR;;;BU)",SDDL_REVISION_1,&sd,NULL));anchor.config_sd=sd;anchor.config_sd_size=GetSecurityDescriptorLength(sd);
    L4Layout target;wchar_t image[MAX_PATH];CHECK(l4_layout_from_roots(&target,fixture.layout.binaries,fixture.layout.data,L"1.13.2") && l4_layout_component(&target,L"l4superv",L"l4superv.exe",image));swprintf_s(anchor.before,2048,L"\"%ls\" --fixture",image);
    CHECK(l4_layout_from_roots(&target,fixture.layout.binaries,fixture.layout.data,L"1.13.3") && l4_layout_component(&target,L"l4superv",L"l4superv.exe",image));swprintf_s(anchor.after,2048,L"\"%ls\" --fixture",image);
    CHECK(l4_recovery_prepare(j,&anchor,child.hProcess));LocalFree(sd);
    HANDLE leaked=NULL;CHECK(!OpenThreadToken(GetCurrentThread(),TOKEN_QUERY,TRUE,&leaked) && GetLastError()==ERROR_NO_TOKEN);if(leaked)CloseHandle(leaked);
    for(unsigned i=0;i<2;i++){CHECK(l4_switch_decode_bytes(&fixture.layout,p.switches[i],p.switch_size[i],&pair_mock[i]));wcscpy_s(commands[i],2048,pair_mock[i].after);states[i]=SERVICE_RUNNING;}
    wcscpy_s(commands[2],2048,anchor.before);states[2]=SERVICE_START_PENDING;fake_pids[2]=GetCurrentProcessId();
    L4UpdateState state={0};state.window=1;state.generation=1;state.plan_sequence=p.sequence;state.deadline_utc=p.deadline_utc;
    strcpy_s(state.owner,40,"17730000-0000-4000-8000-000000000001");BYTE bytes[L4_UPDATE_STATE_SIZE];CHECK(l4_update_state_encode(&state,bytes) && l4_update_state_replace(&fixture.layout,bytes,false));
    L4CommunicationBoot* permit=NULL;CHECK(!l4_communication_boot_open(&fixture.layout,&state,&permit) && !permit); /* Real original supervisor still alive. */
    CHECK(TerminateProcess(old.hProcess,0) && WaitForSingleObject(old.hProcess,5000)==WAIT_OBJECT_0);
    fault_mode=10;CHECK(!l4_communication_boot_open(&fixture.layout,&state,&permit) && !permit);fault_mode=0;
    L4UpdateState bad=state;bad.window=2;CHECK(!l4_communication_boot_open(&fixture.layout,&bad,&permit) && !permit);
    bad=state;bad.deadline_utc++;CHECK(!l4_communication_boot_open(&fixture.layout,&bad,&permit) && !permit);
    L4CommunicationDecision* previous=NULL;
    if(mode==1 || mode==2){CHECK(l4_communication_decision_open(&fixture.layout,id,1000,&previous));CHECK(l4_communication_decision_begin(previous,p.deadline_utc,false));l4_communication_decision_release(previous);if(mode==1){l4_communication_decision_close(previous);previous=NULL;}}
    if(mode==3){CHECK(l4_communication_decision_open(&fixture.layout,id,1000,&previous));CHECK(l4_communication_decision_finish(previous,L4_COMM_DEC_COMMITTED,0,stamp()));l4_communication_decision_close(previous);previous=NULL;}
    HANDLE held_runner=INVALID_HANDLE_VALUE;
    if(mode==7){wchar_t runner_path[MAX_PATH];swprintf_s(runner_path,MAX_PATH,L"%ls\\supervisor.runner.lock",j->directory);held_runner=CreateFileW(runner_path,GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,0,NULL);CHECK(held_runner!=INVALID_HANDLE_VALUE);OVERLAPPED io={0};CHECK(LockFileEx(held_runner,LOCKFILE_EXCLUSIVE_LOCK,0,1,0,&io));}
    if(mode==6 || mode==7){CHECK(!l4_communication_boot_open(&fixture.layout,&state,&permit) && !permit);if(held_runner!=INVALID_HANDLE_VALUE)CloseHandle(held_runner);}
    else{
        CHECK(l4_communication_boot_open(&fixture.layout,&state,&permit) && permit);
        if(permit){DWORD reserve=l4_communication_boot_remaining(permit);CHECK(reserve && reserve<=p.budget.verify_ms);Sleep(30);CHECK(l4_communication_boot_remaining(permit)<reserve);
            const L4RecoveryPlan* saved=l4_recovery_plan(permit->supervisor);ULONGLONG saved_deadline=saved->deadline_utc;((L4RecoveryPlan*)saved)->deadline_utc=boot_utc()-1;
            CHECK(!l4_communication_boot_check(permit,&fixture.layout,&state));((L4RecoveryPlan*)saved)->deadline_utc=saved_deadline;
            L4RecoveryGuard* reader=NULL;CHECK(l4_recovery_open(&fixture.layout,id,30,&reader));l4_recovery_close(reader); /* No short decision mutex held. */
            wchar_t runner_path[MAX_PATH];swprintf_s(runner_path,MAX_PATH,L"%ls\\supervisor.runner.lock",j->directory);HANDLE contender=CreateFileW(runner_path,GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,0,NULL);CHECK(contender!=INVALID_HANDLE_VALUE);OVERLAPPED io={0};CHECK(!LockFileEx(contender,LOCKFILE_EXCLUSIVE_LOCK|LOCKFILE_FAIL_IMMEDIATELY,0,1,0,&io));CloseHandle(contender);
            L4Layout bad_roots=fixture.layout;wcscat_s(bad_roots.operations,MAX_PATH,L"\\\\other");CHECK(!l4_communication_boot_check(permit,&bad_roots,&state));
            bad=state;bad.generation++;CHECK(!l4_communication_boot_check(permit,&fixture.layout,&bad));CHECK(l4_communication_boot_check(permit,&fixture.layout,&state));
            CHECK(l4_config_apply(j,p.config_sequence[0]) && l4_config_apply(j,p.config_sequence[1]));l4_journal_close(j);j=NULL;
            if(mode==8){Sleep(p.budget.verify_ms+20);CHECK(!l4_communication_boot_remaining(permit));} /* Real admission/claim reserve cannot be renewed. */
            if(mode==4)fault_mode=1;if(mode==5)wcscpy_s(commands[2],2048,anchor.after);
            L4CommunicationSignals signals={signal_probe,signal_channels,signal_barrier,NULL};L4CommunicationResult result;
            ULONGLONG admission_ms=GetTickCount64()-permit->started,execute_tick=GetTickCount64();
            bool ok=l4_communication_execute_boot(&fixture.layout,&state,&signals,permit,&result);
            if((mode==4 && (count!=10 || workers!=1)) || ok!=(mode==0 || mode==1))printf("boot mode=%u outcome=%u completed=%u count=%u workers=%u error=%lu admission_ms=%llu execution_ms=%llu\n",mode,result.outcome,result.completed,count,workers,result.error,admission_ms,GetTickCount64()-execute_tick);
            CHECK(ok==(mode==0 || mode==1));
            if(mode==0 || mode==1)CHECK(result.outcome==L4_COMM_VERIFIED && count==10 && workers==1);
            if(mode==4)CHECK(result.outcome==L4_COMM_CONNECTIVITY_UNCONFIRMED && count==10 && workers==1);
            if(mode==2 || mode==3 || mode==5 || mode==8)CHECK(!count && !workers);
            unsigned before=count;CHECK(!l4_communication_execute_boot(&fixture.layout,&state,&signals,permit,&result));CHECK(before==count);
            l4_communication_boot_close(permit);permit=NULL;
            L4CommunicationBoot* retry=NULL;CHECK(!l4_communication_boot_open(&fixture.layout,&state,&retry) && !retry && GetLastError()==ERROR_ALREADY_EXISTS);
            if(previous){l4_communication_decision_close(previous);previous=NULL;}
            wcscpy_s(commands[2],2048,anchor.before);
            L4CommunicationDecision* d=NULL;L4CommunicationPhase phase;DWORD error;CHECK(l4_communication_decision_open(&fixture.layout,id,1000,&d));CHECK(l4_communication_decision_read(d,&phase,&error));CHECK(phase==(mode==0 || mode==1?L4_COMM_DEC_RESTORED:mode==4?L4_COMM_DEC_UNCONFIRMED:mode==2?L4_COMM_DEC_STARTED:mode==3?L4_COMM_DEC_COMMITTED:L4_COMM_DEC_WAIT));l4_communication_decision_close(d);
        }
    }
    l4_communication_boot_close(permit);l4_communication_decision_close(previous);l4_journal_close(j);
    L4UpdateState after;CHECK(l4_update_state_read(&fixture.layout,&after) && after.window==1 && after.generation==1 && after.deadline_utc==state.deadline_utc);
    CHECK(TerminateProcess(child.hProcess,0) && WaitForSingleObject(child.hProcess,5000)==WAIT_OBJECT_0);CloseHandle(child.hProcess);CloseHandle(old.hProcess);
    for(unsigned i=0;i<2;i++){free(switches[i]);free(configs[i]);}free(con_bytes);CHECK(update_fixture_dispose(&fixture));
}
int wmain(int argc,wchar_t** argv){if(argc==2 && !wcscmp(argv[1],L"--hold-fixture")){Sleep(60000);return 0;}
    real_system=argc==2 && !wcscmp(argv[1],L"--system-fixture");
    if(real_system){HANDLE token=NULL;BYTE user[512];DWORD size=0;bool ok=OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token) && GetTokenInformation(token,TokenUser,user,sizeof(user),&size) && IsWellKnownSid(((TOKEN_USER*)user)->User.Sid,WinLocalSystemSid);
        if(token)CloseHandle(token);if(!ok){puts("SYSTEM fixture requires actual LocalSystem");return ERROR_ACCESS_DENIED;}puts("Actual LocalSystem token required; SCM/images/signals remain modeled");}
    CHECK(!l4_communication_boot_remaining(NULL));for(unsigned i=0;i<9;i++)scenario(i);
    printf("Protected boot recovery: %u checks, %u failures; real original process exit/plans/runner/decisions/configs, modeled SCM/SYSTEM/images/signals; no live boot/SCM\n",checks,failures);return failures?1:0;}
