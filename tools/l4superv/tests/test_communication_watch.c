#include "../src/communication_watch.h"
#include "../src/communication_signals.h"
#include "../../l4common/communication_monitor.h"
#include "../../l4common/supervisor_crash_profile.h"
#include "../../l4common/journal_internal.h"
#include "../../l4common/tests/update_state_fixture.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static volatile LONG checks,failures,opened,started,closed,worker_ok=1;static HANDLE closing,release_close;
#define CHECK(x) do{InterlockedIncrement(&checks);if(!(x)){InterlockedIncrement(&failures);printf("FAIL watch %u: %s (%lu)\n",__LINE__,#x,GetLastError());}}while(0)
static bool signal_open(const L4Layout* roots,const L4CommunicationPin* pin,DWORD t,L4SignalProfile** profile,L4CommunicationSignals* signals){CHECK(roots && pin && t);
    L4CommunicationDecision* d=NULL;L4CommunicationPhase phase;DWORD error;
    CHECK(l4_communication_decision_open(roots,L"17730000-0000-4000-8000-000000000001",1000,&d));
    if(d){CHECK(l4_communication_decision_read(d,&phase,&error) && phase==L4_COMM_DEC_WAIT);l4_communication_decision_close(d);}
    InterlockedIncrement(&opened);*profile=(L4SignalProfile*)1;memset(signals,0,sizeof(*signals));return true;}
static void signal_close(L4SignalProfile* p){if(p)InterlockedIncrement(&closed);}
static bool worker_live(const L4CommunicationPlan* p){CHECK(p && p->worker_pid);return InterlockedCompareExchange(&worker_ok,0,0)!=0;}
static bool monitor_start(const L4Layout* r,const wchar_t* id,const L4CommunicationSignals* s,L4CommunicationMonitor** m){CHECK(r && id && s);InterlockedIncrement(&started);*m=(L4CommunicationMonitor*)1;return true;}
static bool monitor_poll(L4CommunicationMonitor* m,bool* done,L4CommunicationResult* result){(void)m;*done=false;memset(result,0,sizeof(*result));return true;}
static bool monitor_proof_ok=true;
static bool crash_profile_ok=true;
static bool crash_current(DWORD pid,DWORD budget){CHECK(pid==GetCurrentProcessId() && budget>=1000);return crash_profile_ok;}
static bool monitor_proof(L4CommunicationMonitor* m,const L4CommunicationPin* p,const L4UpdateState* s,DWORD t,HANDLE cancel){CHECK(m && p && s && t);(void)cancel;return monitor_proof_ok && worker_live(l4_communication_pinned_plan(p));}
static bool monitor_close(L4CommunicationMonitor** m,DWORD t){CHECK(*m && t);SetEvent(closing);if(WaitForSingleObject(release_close,t)!=WAIT_OBJECT_0){SetLastError(ERROR_TIMEOUT);return false;}*m=NULL;return true;}
static BOOL system_sid(PSID s,WELL_KNOWN_SID_TYPE t){(void)s;(void)t;return TRUE;}
static unsigned boot_mode;static volatile LONG boot_opens,boot_signal_opens,boot_executes,boot_closes;
static bool boot_open(const L4Layout* r,const L4UpdateState* s,L4CommunicationBoot** b){CHECK(r && s);InterlockedIncrement(&boot_opens);*b=NULL;
    if(boot_mode==3){SetLastError(ERROR_REVISION_MISMATCH);return false;}*b=(L4CommunicationBoot*)1;return true;}
static void boot_close(L4CommunicationBoot* b){if(b){CHECK(b==(L4CommunicationBoot*)1);InterlockedIncrement(&boot_closes);}}
static bool boot_signals(const L4Layout* r,const L4CommunicationPin* p,L4CommunicationBoot* b,const L4UpdateState* s,L4SignalProfile** profile,L4CommunicationSignals* signals){
    CHECK(r && p && b && s);InterlockedIncrement(&boot_signal_opens);*profile=NULL;memset(signals,0,sizeof(*signals));
    if(boot_mode==4){SetLastError(ERROR_NOT_READY);return false;}*profile=(L4SignalProfile*)1;return true;}
static bool boot_execute(const L4Layout* r,const L4UpdateState* s,const L4CommunicationSignals* signals,L4CommunicationBoot* b,L4CommunicationResult* result){
    CHECK(r && s && signals && b && result);InterlockedIncrement(&boot_executes);
    if(boot_mode==2){SetEvent(closing);CHECK(WaitForSingleObject(release_close,5000)==WAIT_OBJECT_0);}
    if(boot_mode==5){SetLastError(ERROR_TIMEOUT);return false;}return true;
}
#define l4_communication_boot_open boot_open
#define l4_communication_boot_close boot_close
#define supervisor_signals_open_boot boot_signals
#define l4_communication_execute_boot boot_execute
#define supervisor_signals_open signal_open
#define supervisor_signals_close signal_close
#define l4_communication_worker_live worker_live
#define l4_communication_monitor_start monitor_start
#define l4_communication_monitor_poll monitor_poll
#define l4_communication_monitor_proof monitor_proof
#define l4_supervisor_crash_profile_current crash_current
#define l4_communication_monitor_close monitor_close
#define IsWellKnownSid system_sid
#include "../src/communication_watch.c"
#undef IsWellKnownSid
static ULONGLONG stamp(void){FILETIME t;GetSystemTimeAsFileTime(&t);return ((ULONGLONG)t.dwHighDateTime<<32)|t.dwLowDateTime;}
#include "../../l4common/tests/communication_signal_fixture.h"
static void command_for(const L4Layout* roots,const wchar_t* version,unsigned i,wchar_t command[2048]){
    L4Layout layout;wchar_t image[MAX_PATH];const wchar_t* components[]={L"leo4proxy",L"mosquitto"};const wchar_t* exes[]={L"leo4proxy.exe",L"mosquitto.exe"};
    CHECK(l4_layout_from_roots(&layout,roots->binaries,roots->data,version));CHECK(l4_layout_component(&layout,components[i],exes[i],image));swprintf_s(command,2048,L"\"%ls\" --fixture",image);
}
static void scenario(unsigned mode){
    UpdateFixture fixture;bool initialized=update_fixture_init(&fixture);CHECK(initialized);if(!initialized)return;
    CHECK(update_fixture_put(&fixture,0));const wchar_t* id=L"17730000-0000-4000-8000-000000000001";
    L4Journal* j=NULL;CHECK(l4_journal_open(&fixture.layout,id,true,&j));if(!j)return;
    L4CommunicationPlan p={0};memcpy(&p.operation,j->header+8,16);p.generation=1;
    FILETIME now,exit,kernel,user;GetSystemTimeAsFileTime(&now);p.armed_utc=((ULONGLONG)now.dwHighDateTime<<32)|now.dwLowDateTime;p.deadline_utc=p.armed_utc+6000000000ull;

    wchar_t exe[MAX_PATH],cmd[MAX_PATH+40];CHECK(GetModuleFileNameW(NULL,exe,MAX_PATH));swprintf_s(cmd,_countof(cmd),L"\"%ls\" --hold-fixture",exe);
    STARTUPINFOW startup={sizeof(startup)};PROCESS_INFORMATION child={0};CHECK(CreateProcessW(exe,cmd,NULL,NULL,FALSE,CREATE_NO_WINDOW,NULL,NULL,&startup,&child));
    if(!child.hProcess){l4_journal_close(j);update_fixture_dispose(&fixture);return;}CloseHandle(child.hThread);
    p.worker_pid=child.dwProcessId;CHECK(GetProcessTimes(child.hProcess,&p.worker_created,&exit,&kernel,&user));
    PROCESS_INFORMATION old={0};
    if(mode>=2){CHECK(CreateProcessW(exe,cmd,NULL,NULL,FALSE,CREATE_NO_WINDOW,NULL,NULL,&startup,&old));if(old.hThread)CloseHandle(old.hThread);p.supervisor_pid=old.dwProcessId;CHECK(GetProcessTimes(old.hProcess,&p.supervisor_created,&exit,&kernel,&user));}
    else{p.supervisor_pid=GetCurrentProcessId();CHECK(GetProcessTimes(GetCurrentProcess(),&p.supervisor_created,&exit,&kernel,&user));}
    p.armed_utc-=100000000ull;p.deadline_utc=stamp()+600000000ull;
    p.budget=(L4CommunicationBudget){1000,1000,1000,1000,1000,300000,1000,310000};
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
    L4CommunicationWatch view={0};view.roots=fixture.layout;L4CommunicationPin* pin=NULL;wchar_t operation_id[40];
    if(mode<2){CHECK(discover(&view,&pin,operation_id));CHECK(pin && !wcscmp(operation_id,id));
        L4UpdateState clear={0};CHECK(original(pin,&clear));clear.generation=1;CHECK(!original(pin,&clear));clear.generation=0;
        L4CommunicationPlan* observed=(L4CommunicationPlan*)l4_communication_pinned_plan(pin);if(observed){observed->supervisor_created.dwLowDateTime^=1;CHECK(!original(pin,&clear));observed->supervisor_created.dwLowDateTime^=1;}
        l4_communication_plan_close(pin);pin=NULL;
    }else CHECK(!discover(&view,&pin,operation_id) && !pin);
    L4UpdateConsumer consumer={0};consumer.enabled=true;consumer.layout=fixture.layout;
    L4UpdateState expected={0};expected.window=1;expected.generation=1;expected.plan_sequence=p.sequence;expected.deadline_utc=p.deadline_utc;strcpy_s(expected.owner,40,"17730000-0000-4000-8000-000000000001");
    HANDLE cancel=CreateEventW(NULL,TRUE,FALSE,NULL);CHECK(cancel);CHECK(supervisor_communication_watch_query(NULL,&expected,1000,cancel)==ERROR_NOT_SUPPORTED);
    SetEvent(cancel);CHECK(supervisor_communication_watch_query(NULL,&expected,1000,cancel)==ERROR_CANCELLED);ResetEvent(cancel);
    InterlockedExchange(&opened,0);InterlockedExchange(&started,0);InterlockedExchange(&closed,0);InterlockedExchange(&worker_ok,mode==1?0:1);
    closing=CreateEventW(NULL,TRUE,FALSE,NULL);release_close=CreateEventW(NULL,TRUE,FALSE,NULL);CHECK(closing && release_close);
    boot_mode=mode;InterlockedExchange(&boot_opens,0);InterlockedExchange(&boot_signal_opens,0);InterlockedExchange(&boot_executes,0);InterlockedExchange(&boot_closes,0);
    if(mode>=2){BYTE active[L4_UPDATE_STATE_SIZE];CHECK(l4_update_state_encode(&expected,active) && l4_update_state_replace(&fixture.layout,active,false));CHECK(TerminateProcess(old.hProcess,0) && WaitForSingleObject(old.hProcess,5000)==WAIT_OBJECT_0);}
    L4CommunicationWatch* watch=NULL;CHECK(supervisor_communication_watch_start(&consumer,&watch) && watch);
    if(mode>=2){
        ULONGLONG until=GetTickCount64()+2000;while(!InterlockedCompareExchange(&boot_opens,0,0) && GetTickCount64()<until)Sleep(10);
        if(mode==2){CHECK(WaitForSingleObject(closing,2000)==WAIT_OBJECT_0 && boot_executes==1 && !closed && !boot_closes);
            CHECK(!supervisor_communication_watch_close(&watch,0) && watch);CHECK(supervisor_communication_watch_query(watch,&expected,1000,cancel)!=ERROR_SUCCESS);
            SetEvent(release_close);CHECK(supervisor_communication_watch_close(&watch,3000) && !watch && closed==1 && boot_closes==1);
        }else{Sleep(250);CHECK(boot_opens==1 && !opened && !started);CHECK(boot_signal_opens==(mode==3?0:1) && boot_executes==(mode==5?1:0));
            CHECK(supervisor_communication_watch_close(&watch,3000) && !watch && closed==(mode==5?1:0) && boot_closes==(mode==3?0:1));}
        L4CommunicationDecision* d=NULL;L4CommunicationPhase phase;DWORD error;CHECK(l4_communication_decision_open(&fixture.layout,id,1000,&d));
        CHECK(l4_communication_decision_read(d,&phase,&error) && phase==L4_COMM_DEC_WAIT);l4_communication_decision_close(d); /* Executor is modeled, no actual claim. */
        L4UpdateState retained;CHECK(l4_update_state_read(&fixture.layout,&retained) && retained.window==1 && retained.generation==1);
        CloseHandle(closing);CloseHandle(release_close);CloseHandle(cancel);l4_journal_close(j);CloseHandle(old.hProcess);
        for(unsigned i=0;i<2;i++){free(switches[i]);free(configs[i]);}free(con_bytes);CHECK(TerminateProcess(child.hProcess,0) && WaitForSingleObject(child.hProcess,5000)==WAIT_OBJECT_0);CloseHandle(child.hProcess);CHECK(update_fixture_dispose(&fixture));return;
    }
    ULONGLONG end=GetTickCount64()+2000;while(!InterlockedCompareExchange(&started,0,0) && GetTickCount64()<end && mode!=1)Sleep(10);
    if(mode==1)CHECK(supervisor_communication_watch_query(watch,&expected,1000,cancel)!=ERROR_SUCCESS);
    if(mode==0){
        ULONGLONG until=GetTickCount64()+2000;DWORD proof;
        do{proof=supervisor_communication_watch_proof(watch,&expected,1000,cancel);if(proof)Sleep(10);}while(proof && GetTickCount64()<until);
        CHECK(proof==ERROR_SUCCESS);CHECK(supervisor_communication_watch_query(watch,&expected,1000,cancel)==ERROR_SUCCESS);
        NativeBootRegistration copy=native_boot;AcquireSRWLockExclusive(&watch->state_lock);watch->boot=&copy;ReleaseSRWLockExclusive(&watch->state_lock);
        CHECK(supervisor_communication_watch_query(watch,&expected,1000,cancel)!=ERROR_SUCCESS);
        AcquireSRWLockExclusive(&watch->state_lock);watch->boot=&native_boot;watch->startup_checked=false;ReleaseSRWLockExclusive(&watch->state_lock);
        CHECK(supervisor_communication_watch_query(watch,&expected,1000,cancel)!=ERROR_SUCCESS);
        AcquireSRWLockExclusive(&watch->state_lock);watch->startup_checked=true;ReleaseSRWLockExclusive(&watch->state_lock);
        L4UpdateState foreign=expected;foreign.deadline_utc++;
        CHECK(supervisor_communication_watch_proof(watch,&foreign,1000,cancel)!=ERROR_SUCCESS);
        monitor_proof_ok=false;CHECK(supervisor_communication_watch_query(watch,&expected,1000,cancel)!=ERROR_SUCCESS);monitor_proof_ok=true;
        crash_profile_ok=false;CHECK(supervisor_communication_watch_query(watch,&expected,1000,cancel)!=ERROR_SUCCESS);crash_profile_ok=true;
        InterlockedExchange(&worker_ok,0);CHECK(supervisor_communication_watch_proof(watch,&expected,1000,cancel)!=ERROR_SUCCESS);InterlockedExchange(&worker_ok,1);
        SetEvent(cancel);CHECK(supervisor_communication_watch_proof(watch,&expected,1000,cancel)==ERROR_CANCELLED);ResetEvent(cancel);
    }
    if(mode==1){Sleep(200);CHECK(!opened && !started);SetEvent(release_close);CHECK(supervisor_communication_watch_close(&watch,3000) && !watch);}
    else {CHECK(started==1 && opened==1);CHECK(!supervisor_communication_watch_close(&watch,0) && watch);CHECK(WaitForSingleObject(closing,2000)==WAIT_OBJECT_0 && !closed);
        CHECK(supervisor_communication_watch_query(watch,&expected,1000,cancel)!=ERROR_SUCCESS);
        CHECK(supervisor_communication_watch_proof(watch,&expected,10,cancel)!=ERROR_SUCCESS);
        SetEvent(release_close);CHECK(supervisor_communication_watch_close(&watch,3000) && !watch && closed==1);}
    CHECK(supervisor_communication_watch_close(&watch,0));CloseHandle(closing);CloseHandle(release_close);CloseHandle(cancel);
    /* Two eligible original plans must refuse ambiguity; an active owner is
     * addressed directly and does not depend on arbitrary enumeration order. */
    l4_journal_close(j);j=NULL;
    const wchar_t* second=L"17730000-0000-4000-8000-000000000002";L4Journal* clone=NULL;
    CHECK(l4_journal_open(&fixture.layout,second,true,&clone));L4CommunicationPlan other=p;
    if(clone){memcpy(&other.operation,clone->header+8,16);
        for(unsigned i=0;i<2;i++)CHECK(l4_journal_append(clone,10,p.switches[i],p.switch_size[i],&other.switch_sequence[i]));
        for(unsigned i=0;i<2;i++)CHECK(l4_journal_append(clone,20,p.configs[i],p.config_size[i],&other.config_sequence[i]));
        for(unsigned i=0;i<2;i++){l4_store_u64(operation+28+i*8,other.config_sequence[i]);l4_store_u64(operation+44+i*8,other.switch_sequence[i]);}
        BYTE* clone_con=fixture_communication_con(clone,&other,operation);
        CHECK(l4_journal_append(clone,64,operation,sizeof(operation),&other.sequence));CHECK(l4_store_hash(operation,sizeof(operation),NULL,0,other.operation_sha256));
        CHECK(l4_communication_plan_prepare(clone,&other,child.hProcess));free(clone_con);l4_journal_close(clone);}
    CHECK(!discover(&view,&pin,operation_id) && !pin && GetLastError()==ERROR_DUP_NAME);
    L4UpdateState bad=expected;bad.deadline_utc++;BYTE state_bytes[L4_UPDATE_STATE_SIZE];CHECK(l4_update_state_encode(&bad,state_bytes) && l4_update_state_replace(&fixture.layout,state_bytes,false));CHECK(!discover(&view,&pin,operation_id) && !pin);
    CHECK(l4_update_state_encode(&expected,state_bytes) && l4_update_state_replace(&fixture.layout,state_bytes,false));CHECK(discover(&view,&pin,operation_id));l4_communication_plan_close(pin);pin=NULL;
    CHECK(update_fixture_put(&fixture,2));CHECK(!discover(&view,&pin,operation_id) && !pin);
    l4_journal_close(j);for(unsigned i=0;i<2;i++){free(switches[i]);free(configs[i]);}free(con_bytes);
    CHECK(TerminateProcess(child.hProcess,0) && WaitForSingleObject(child.hProcess,5000)==WAIT_OBJECT_0);CloseHandle(child.hProcess);CHECK(update_fixture_dispose(&fixture));
}
static void startup_rejection(void){
    UpdateFixture fixture;CHECK(update_fixture_init(&fixture));CHECK(update_fixture_put(&fixture,0));
    wchar_t state_path[MAX_PATH];CHECK(l4_layout_data_path(&fixture.layout,L"update\\operations\\update.state",state_path));
    HANDLE bad=CreateFileW(state_path,GENERIC_WRITE,0,NULL,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,NULL);CHECK(bad!=INVALID_HANDLE_VALUE);
    if(bad!=INVALID_HANDLE_VALUE){DWORD written=0;CHECK(WriteFile(bad,"x",1,&written,NULL) && written==1);CloseHandle(bad);}
    L4UpdateConsumer consumer={0};consumer.enabled=true;consumer.layout=fixture.layout;L4CommunicationWatch* watch=NULL;
    CHECK(supervisor_communication_watch_start(&consumer,&watch));if(watch){Sleep(250);
        AcquireSRWLockShared(&watch->state_lock);CHECK(!watch->startup_checked && !watch->monitor && !watch->boot_owned);ReleaseSRWLockShared(&watch->state_lock);
        L4UpdateState expected={0};expected.window=L4_UPDATE_COMMUNICATION;CHECK(supervisor_communication_watch_query(watch,&expected,1000,NULL)!=ERROR_SUCCESS);
        /* A later clear state must not silently re-admit failed startup. */
        CHECK(update_fixture_put(&fixture,0));Sleep(150);AcquireSRWLockShared(&watch->state_lock);CHECK(!watch->startup_checked && !watch->monitor);ReleaseSRWLockShared(&watch->state_lock);
        CHECK(supervisor_communication_watch_close(&watch,3000) && !watch);
    }CHECK(update_fixture_dispose(&fixture));
}
int wmain(int argc,wchar_t** argv){if(argc==2 && !wcscmp(argv[1],L"--hold-fixture")){Sleep(60000);return 0;}for(unsigned mode=0;mode<6;mode++)scenario(mode);startup_rejection();
    printf("Communication watch: %ld passed, %ld failed; real immutable plan/state/decision/owner thread, modeled SYSTEM/signals/executor/SCM; technical mode3 runtime registration checked\n",checks-failures,failures);return failures?1:0;}
