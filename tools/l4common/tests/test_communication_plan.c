#include "../communication_plan.h"
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
typedef struct {L4Layout roots;const wchar_t* operation;HANDLE go;ULONGLONG now;bool recovery,ok;} DecisionRace;
static DWORD WINAPI decision_race(void* context){
    DecisionRace* race=context;WaitForSingleObject(race->go,5000);L4CommunicationDecision* d=NULL;
    if(l4_communication_decision_open(&race->roots,race->operation,1000,&d)){
        race->ok=race->recovery?l4_communication_decision_begin(d,race->now,true):
            l4_communication_decision_finish(d,L4_COMM_DEC_COMMITTED,0,race->now);
        l4_communication_decision_close(d);
    }return 0;
}
int wmain(int argc,wchar_t** argv){
    if(argc==2 && !wcscmp(argv[1],L"--hold-fixture")){Sleep(60000);return 0;}
    UpdateFixture fixture;CHECK(update_fixture_init(&fixture));if(failures)return 1;
    CHECK(update_fixture_put(&fixture,0));const wchar_t* id=L"17730000-0000-4000-8000-000000000001";
    L4Journal* j=NULL;CHECK(l4_journal_open(&fixture.layout,id,true,&j));if(!j)return 1;
    L4CommunicationPlan p={0};memcpy(&p.operation,j->header+8,16);p.generation=1;
    FILETIME now,exit,kernel,user;GetSystemTimeAsFileTime(&now);p.armed_utc=((ULONGLONG)now.dwHighDateTime<<32)|now.dwLowDateTime;p.deadline_utc=p.armed_utc+6000000000ull;
    p.worker_pid=GetCurrentProcessId();CHECK(GetProcessTimes(GetCurrentProcess(),&p.worker_created,&exit,&kernel,&user));
    wchar_t exe[MAX_PATH],cmd[MAX_PATH+40];CHECK(GetModuleFileNameW(NULL,exe,MAX_PATH));swprintf_s(cmd,_countof(cmd),L"\"%ls\" --hold-fixture",exe);
    STARTUPINFOW startup={sizeof(startup)};PROCESS_INFORMATION child={0};CHECK(CreateProcessW(exe,cmd,NULL,NULL,FALSE,CREATE_NO_WINDOW,NULL,NULL,&startup,&child));
    if(!child.hProcess){l4_journal_close(j);update_fixture_dispose(&fixture);return 1;}CloseHandle(child.hThread);
    p.supervisor_pid=child.dwProcessId;CHECK(GetProcessTimes(child.hProcess,&p.supervisor_created,&exit,&kernel,&user));
    p.budget=(L4CommunicationBudget){100,100,100,100,100,300000,100,300800};
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
    BYTE* bytes=NULL;DWORD size=0;CHECK(l4_communication_plan_encode(&fixture.layout,&p,&bytes,&size));L4CommunicationPlan decoded;
    CHECK(l4_communication_plan_decode(&fixture.layout,bytes,size,&decoded));CHECK(decoded.sequence==p.sequence && decoded.budget.mosquitto_ms==300000);
    CHECK(!l4_communication_plan_decode(&fixture.layout,bytes,size-1,&decoded));
    for(unsigned offset=0;offset<size;offset++){BYTE old=bytes[offset];bytes[offset]^=1;CHECK(!l4_communication_plan_decode(&fixture.layout,bytes,size,&decoded));bytes[offset]=old;}
    /* Attacker recomputes corruption hash: structural restrictions still apply. */
    const unsigned invalid_offsets[]={12,288,136,140,184,192,208,212,224};
    for(unsigned i=0;i<_countof(invalid_offsets);i++){DWORD offset=invalid_offsets[i],old=l4_store_get32(bytes+offset);
        l4_store_u32(bytes+offset,offset==212?0:0xffffffff);CHECK(l4_store_hash(bytes,size-32,NULL,0,bytes+size-32));
        CHECK(!l4_communication_plan_decode(&fixture.layout,bytes,size,&decoded));l4_store_u32(bytes+offset,old);
        CHECK(l4_store_hash(bytes,size-32,NULL,0,bytes+size-32));}
    DWORD config_at=320+p.switch_size[0];BYTE old_path=bytes[config_at+24];bytes[config_at+24]='x';
    CHECK(l4_store_hash(bytes,size-32,NULL,0,bytes+size-32));CHECK(!l4_communication_plan_decode(&fixture.layout,bytes,size,&decoded));
    bytes[config_at+24]=old_path;CHECK(l4_store_hash(bytes,size-32,NULL,0,bytes+size-32));
    DWORD sd_at=config_at+24+l4_store_get32(bytes+config_at+8)+l4_store_get32(bytes+config_at+12)+l4_store_get32(bytes+config_at+16);
    DWORD old_owner=l4_store_get32(bytes+sd_at+4);l4_store_u32(bytes+sd_at+4,0xfffffffc);
    CHECK(l4_store_hash(bytes,size-32,NULL,0,bytes+size-32));CHECK(!l4_communication_plan_decode(&fixture.layout,bytes,size,&decoded));
    l4_store_u32(bytes+sd_at+4,old_owner);CHECK(l4_store_hash(bytes,size-32,NULL,0,bytes+size-32));
    L4CommunicationPlan bad=p;bad.worker_pid=p.supervisor_pid;BYTE* unused=NULL;DWORD unused_size=0;CHECK(!l4_communication_plan_encode(&fixture.layout,&bad,&unused,&unused_size));
    bad=p;bad.config_sequence[1]=p.config_sequence[0];CHECK(!l4_communication_plan_encode(&fixture.layout,&bad,&unused,&unused_size));
    L4CommunicationPin* pin=NULL;CHECK(!l4_communication_plan_open(&fixture.layout,id,&pin));CHECK(!pin);
    bad=p;bad.supervisor_created.dwLowDateTime++;CHECK(!l4_communication_plan_prepare(j,&bad,GetCurrentProcess()));
    bad=p;bad.operation_sha256[0]^=1;CHECK(!l4_communication_plan_prepare(j,&bad,GetCurrentProcess()));
    bad=p;bad.worker_created.dwLowDateTime++;CHECK(!l4_communication_plan_prepare(j,&bad,GetCurrentProcess()));
    CHECK(l4_config_apply(j,p.config_sequence[0]));CHECK(!l4_communication_plan_prepare(j,&p,GetCurrentProcess()));
    CHECK(l4_config_rollback(j,p.config_sequence[0]));
    CHECK(l4_communication_plan_prepare(j,&p,GetCurrentProcess()));ULONGLONG sequence=j->sequence;
    CHECK(l4_communication_plan_prepare(j,&p,GetCurrentProcess()));CHECK(j->sequence==sequence);
    bad=p;bad.budget.total_ms++;CHECK(!l4_communication_plan_prepare(j,&bad,GetCurrentProcess()));CHECK(j->sequence==sequence);
    /* Reader works WHILE producer holds deployment.lock, keeps immutable pins. */
    CHECK(l4_communication_plan_open(&fixture.layout,id,&pin));CHECK(l4_communication_pinned_plan(pin)->sequence==p.sequence);
    CHECK(l4_communication_plan_verify_journal(j,pin));CHECK(j->sequence==sequence);
    CHECK(!l4_communication_plan_verify_journal(j,NULL));
    wchar_t drift_path[MAX_PATH];swprintf_s(drift_path,MAX_PATH,L"%ls\\mosquitto\\mosquitto.conf",fixture.layout.config);
    HANDLE drift=CreateFileW(drift_path,GENERIC_WRITE,0,NULL,OPEN_EXISTING,0,NULL);DWORD drift_written=0;CHECK(drift!=INVALID_HANDLE_VALUE);
    CHECK(WriteFile(drift,"bad",3,&drift_written,NULL) && drift_written==3);CHECK(CloseHandle(drift));CHECK(!l4_communication_plan_verify_journal(j,pin));
    drift=CreateFileW(drift_path,GENERIC_WRITE,0,NULL,OPEN_EXISTING,0,NULL);CHECK(drift!=INVALID_HANDLE_VALUE);
    CHECK(WriteFile(drift,"old",3,&drift_written,NULL) && drift_written==3);CHECK(CloseHandle(drift));CHECK(l4_communication_plan_verify_journal(j,pin));
    L4UpdateState state={0};state.window=1;state.generation=1;state.plan_sequence=p.sequence;state.deadline_utc=p.deadline_utc;
    strcpy_s(state.owner,40,"17730000-0000-4000-8000-000000000001");CHECK(l4_communication_plan_matches(pin,&state));state.deadline_utc++;CHECK(!l4_communication_plan_matches(pin,&state));state.deadline_utc--;
    wchar_t plan[MAX_PATH],alias[MAX_PATH];swprintf_s(plan,MAX_PATH,L"%ls\\communication.recovery",j->directory);
    HANDLE raw=CreateFileW(plan,GENERIC_WRITE,0,NULL,OPEN_EXISTING,0,NULL);CHECK(raw==INVALID_HANDLE_VALUE);CHECK(!DeleteFileW(plan));
    l4_communication_plan_close(pin);pin=NULL;
    L4CommunicationDecision *decision=NULL,*other=NULL;L4CommunicationPhase phase;DWORD error;
    CHECK(l4_communication_decision_open(&fixture.layout,id,1000,&decision));
    ULONGLONG waited=GetTickCount64();CHECK(!l4_communication_decision_open(&fixture.layout,id,30,&other));CHECK(!other && GetTickCount64()-waited<1000);
    CHECK(l4_communication_decision_read(decision,&phase,&error) && phase==L4_COMM_DEC_WAIT && !error);
    CHECK(!l4_communication_decision_begin(decision,p.deadline_utc,false)); /* Plan alone/clear marker never admits rollback. */
    BYTE state_bytes[L4_UPDATE_STATE_SIZE];CHECK(l4_update_state_encode(&state,state_bytes));CHECK(l4_update_state_replace(&fixture.layout,state_bytes,false));
    CHECK(!l4_communication_decision_begin(decision,p.armed_utc,false));
    CHECK(!l4_communication_decision_finish(decision,L4_COMM_DEC_COMMITTED,0,p.deadline_utc));
    CHECK(l4_communication_decision_begin(decision,p.deadline_utc,false));
    CHECK(!l4_communication_decision_begin(decision,p.deadline_utc,true));
    CHECK(!l4_communication_decision_finish(decision,L4_COMM_DEC_COMMITTED,0,p.armed_utc));
    l4_communication_decision_release(decision);
    CHECK(!l4_communication_decision_read(decision,&phase,&error));
    CHECK(l4_communication_decision_open(&fixture.layout,id,1000,&other));
    CHECK(!l4_communication_decision_begin(other,0,true)); /* Runner still held after decision release. */
    CHECK(!l4_communication_decision_finish(other,L4_COMM_DEC_RESTORED,0,p.deadline_utc));
    l4_communication_decision_close(other);other=NULL;
    CHECK(l4_communication_decision_relock(decision,1000));
    CHECK(l4_communication_decision_finish(decision,L4_COMM_DEC_RESTORED,0,p.deadline_utc));
    CHECK(l4_communication_decision_finish(decision,L4_COMM_DEC_RESTORED,0,p.deadline_utc));
    l4_communication_decision_close(decision);decision=NULL;
    wchar_t result_path[MAX_PATH];swprintf_s(result_path,MAX_PATH,L"%ls\\communication.result",j->directory);
    CHECK(DeleteFileW(result_path)); /* Reset only our isolated fixture, no production reset API. */
    CHECK(l4_communication_decision_open(&fixture.layout,id,1000,&decision));
    CHECK(l4_communication_decision_begin(decision,p.deadline_utc,false));
    l4_communication_decision_close(decision);decision=NULL; /* Model crashed runner's released kernel locks. */
    CHECK(l4_communication_decision_open(&fixture.layout,id,1000,&decision));
    CHECK(!l4_communication_decision_begin(decision,p.deadline_utc,false));
    CHECK(l4_communication_decision_begin(decision,0,true));
    CHECK(l4_communication_decision_finish(decision,L4_COMM_DEC_UNCONFIRMED,ERROR_TIMEOUT,0));
    CHECK(!l4_communication_decision_begin(decision,0,true));
    l4_communication_decision_close(decision);decision=NULL;CHECK(DeleteFileW(result_path));
    HANDLE go=CreateEventW(NULL,TRUE,FALSE,NULL);CHECK(go);
    DecisionRace races[2]={{fixture.layout,id,go,p.armed_utc,false,false},{fixture.layout,id,go,p.armed_utc,true,false}};
    HANDLE racers[2]={CreateThread(NULL,0,decision_race,&races[0],0,NULL),CreateThread(NULL,0,decision_race,&races[1],0,NULL)};
    CHECK(racers[0] && racers[1]);SetEvent(go);CHECK(WaitForMultipleObjects(2,racers,TRUE,5000)==WAIT_OBJECT_0);
    CHECK(races[0].ok!=races[1].ok);CloseHandle(racers[0]);CloseHandle(racers[1]);CloseHandle(go);
    CHECK(l4_communication_decision_open(&fixture.layout,id,1000,&decision));
    CHECK(l4_communication_decision_read(decision,&phase,&error));
    CHECK(phase==(races[0].ok?L4_COMM_DEC_COMMITTED:L4_COMM_DEC_STARTED));
    l4_communication_decision_close(decision);decision=NULL;
    CHECK(DeleteFileW(result_path));CHECK(l4_communication_decision_open(&fixture.layout,id,1000,&decision));
    CHECK(l4_communication_decision_begin(decision,0,true));
    CHECK(l4_communication_decision_finish(decision,L4_COMM_DEC_FAILED,ERROR_ACCESS_DENIED,0));
    CHECK(!l4_communication_decision_begin(decision,0,true));l4_communication_decision_close(decision);decision=NULL;
    raw=CreateFileW(result_path,GENERIC_WRITE,0,NULL,OPEN_EXISTING,0,NULL);CHECK(raw!=INVALID_HANDLE_VALUE);
    BYTE corrupt=0;DWORD corrupt_written=0;CHECK(WriteFile(raw,&corrupt,1,&corrupt_written,NULL));CloseHandle(raw);
    CHECK(l4_communication_decision_open(&fixture.layout,id,1000,&decision));
    CHECK(!l4_communication_decision_read(decision,&phase,&error));CHECK(!l4_communication_decision_begin(decision,0,true));
    CHECK(!l4_communication_decision_finish(decision,L4_COMM_DEC_COMMITTED,0,p.armed_utc));l4_communication_decision_close(decision);decision=NULL;
    swprintf_s(alias,MAX_PATH,L"%ls\\alias",j->directory);CHECK(CreateHardLinkW(alias,plan,NULL));CHECK(!l4_communication_plan_open(&fixture.layout,id,&pin));CHECK(!pin);CHECK(DeleteFileW(alias));
    swprintf_s(alias,MAX_PATH,L"%ls:extra",plan);raw=CreateFileW(alias,GENERIC_WRITE,0,NULL,CREATE_NEW,0,NULL);CHECK(raw!=INVALID_HANDLE_VALUE);CloseHandle(raw);
    CHECK(!l4_communication_plan_open(&fixture.layout,id,&pin));CHECK(DeleteFileW(alias));
    PSECURITY_DESCRIPTOR sd=NULL;CHECK(ConvertStringSecurityDescriptorToSecurityDescriptorW(L"O:BAG:BAD:P(A;;FA;;;SY)(A;;FA;;;BA)(A;;FA;;;WD)",SDDL_REVISION_1,&sd,NULL));
    CHECK(SetFileSecurityW(plan,DACL_SECURITY_INFORMATION|PROTECTED_DACL_SECURITY_INFORMATION,sd));LocalFree(sd);sd=NULL;
    CHECK(!l4_communication_plan_open(&fixture.layout,id,&pin));CHECK(!pin);
    CHECK(ConvertStringSecurityDescriptorToSecurityDescriptorW(L"O:BAG:BAD:P(A;;FA;;;SY)(A;;FA;;;BA)",SDDL_REVISION_1,&sd,NULL));
    CHECK(SetFileSecurityW(plan,DACL_SECURITY_INFORMATION|PROTECTED_DACL_SECURITY_INFORMATION,sd));LocalFree(sd);
    CHECK(l4_communication_plan_open(&fixture.layout,id,&pin));CHECK(l4_communication_plan_verify_journal(j,pin));
    CHECK(l4_journal_append(j,70,bytes,size,NULL));CHECK(!l4_communication_plan_verify_journal(j,pin));l4_communication_plan_close(pin);pin=NULL;
    CHECK(TerminateProcess(child.hProcess,0));CHECK(WaitForSingleObject(child.hProcess,5000)==WAIT_OBJECT_0);CloseHandle(child.hProcess);
    free(bytes);for(unsigned i=0;i<2;i++){free(switches[i]);free(configs[i]);}free(con_bytes);l4_journal_close(j);CHECK(update_fixture_dispose(&fixture));
    printf("Communication immutable plan: %u checks, %u failures; actual files/ACL/pins/epochs, source signatures modeled, no SCM/tasks/network\n",checks,failures);
    return failures?1:0;
}
