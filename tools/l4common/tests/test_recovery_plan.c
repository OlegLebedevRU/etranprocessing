#include "../recovery_store.h"
#include "../journal_internal.h"
#include "update_state_fixture.h"
#include <sddl.h>
#include <objbase.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks,failures;
#define CHECK(x) do{++checks;if(!(x)){++failures;printf("FAIL recovery %u: %s (%lu)\n",__LINE__,#x,GetLastError());}}while(0)
static ULONGLONG now_utc(void){FILETIME t;GetSystemTimeAsFileTime(&t);return ((ULONGLONG)t.dwHighDateTime<<32)|t.dwLowDateTime;}
static void command_for(const L4Layout* roots,const wchar_t* version,wchar_t command[2048]){
    L4Layout layout;wchar_t image[MAX_PATH];CHECK(l4_layout_from_roots(&layout,roots->binaries,roots->data,version));
    CHECK(l4_layout_component(&layout,L"l4superv",L"l4superv.exe",image));swprintf_s(command,2048,L"\"%ls\" --service",image);
}
typedef struct{L4Layout roots;const wchar_t* id;HANDLE go;bool helper,ok;ULONGLONG now;} Race;
static DWORD WINAPI decide_race(void* context){
    Race* race=context;WaitForSingleObject(race->go,5000);L4RecoveryGuard* guard=NULL;
    if(l4_recovery_open(&race->roots,race->id,1000,&guard)){
        race->ok=race->helper?l4_recovery_begin(guard,race->now,true):l4_recovery_finish(guard,L4_RECOVERY_COMMITTED,0,race->now);
        l4_recovery_close(guard);
    }return 0;
}
static bool result_path(const L4Journal* j,wchar_t name[MAX_PATH]){return swprintf_s(name,MAX_PATH,L"%ls\\supervisor.result",j->directory)>0;}
int main(void){
    UpdateFixture fixture;CHECK(update_fixture_init(&fixture));if(failures)return 1;
    const wchar_t* id=L"17730000-0000-4000-8000-000000000001";L4Journal* journal=NULL;CHECK(l4_journal_open(&fixture.layout,id,true,&journal));if(!journal)return 1;
    L4RecoveryPlan plan={0};memcpy(&plan.operation,journal->header+8,16);plan.worker_pid=GetCurrentProcessId();FILETIME e,k,u;
    CHECK(GetProcessTimes(GetCurrentProcess(),&plan.worker_created,&e,&k,&u));plan.supervisor_pid=plan.worker_pid;plan.supervisor_created=plan.worker_created;plan.armed_utc=now_utc();plan.deadline_utc=plan.armed_utc+600000000ull;
    plan.recovery_ms=1000;plan.start_type=SERVICE_AUTO_START;plan.old_size=100;plan.new_size=101;memset(plan.old_sha256,1,32);memset(plan.new_sha256,2,32);
    plan.old_exists=true;plan.old_config=(BYTE*)"old";plan.old_config_size=3;plan.new_config=(BYTE*)"newer";plan.new_config_size=5;
    PSECURITY_DESCRIPTOR sd=NULL;CHECK(ConvertStringSecurityDescriptorToSecurityDescriptorW(L"O:BAG:BAD:P(A;;FA;;;SY)(A;;FA;;;BA)(A;;FR;;;BU)",SDDL_REVISION_1,&sd,NULL));
    plan.config_sd=sd;plan.config_sd_size=GetSecurityDescriptorLength(sd);
    command_for(&fixture.layout,L"1.13.2",plan.before);command_for(&fixture.layout,L"1.13.3",plan.after);
    CHECK(l4_journal_append(journal,64,"fixture-not-stop-permission",27,&plan.sequence));
    BYTE* bytes=NULL;DWORD size=0;CHECK(l4_recovery_encode(&fixture.layout,&plan,&bytes,&size));L4RecoveryPlan decoded;
    CHECK(l4_recovery_decode(&fixture.layout,bytes,size,&decoded));CHECK(decoded.old_config_size==3 && !memcmp(decoded.old_config,"old",3));
    CHECK(!wcscmp(plan.before,decoded.before) && !wcscmp(plan.after,decoded.after) && decoded.deadline_utc==plan.deadline_utc);
    CHECK(!l4_recovery_decode(&fixture.layout,bytes,size-1,&decoded));BYTE saved=bytes[40];bytes[40]^=1;CHECK(!l4_recovery_decode(&fixture.layout,bytes,size,&decoded));bytes[40]=saved;
    DWORD offset=192+(DWORD)wcslen(plan.before)*2+(DWORD)wcslen(plan.after)*2+8;
    DWORD invalid=0xffffffff,original;memcpy(&original,bytes+offset+4,4);memcpy(bytes+offset+4,&invalid,4);
    CHECK(l4_recovery_hash(bytes,size-32,bytes+size-32));CHECK(!l4_recovery_decode(&fixture.layout,bytes,size,&decoded));
    memcpy(bytes+offset+4,&original,4);CHECK(l4_recovery_hash(bytes,size-32,bytes+size-32));CHECK(l4_recovery_decode(&fixture.layout,bytes,size,&decoded));
    L4RecoveryPlan invalid_plan=plan;wcscpy_s(invalid_plan.after,2048,L"\"C:\\Windows\\system32\\cmd.exe\" /c anything");BYTE* unused=NULL;DWORD count=0;
    CHECK(!l4_recovery_encode(&fixture.layout,&invalid_plan,&unused,&count));invalid_plan=plan;wcscpy_s(invalid_plan.after,2048,plan.before);CHECK(!l4_recovery_encode(&fixture.layout,&invalid_plan,&unused,&count));
    invalid_plan=plan;wcscat_s(invalid_plan.after,2048,L" --different");CHECK(!l4_recovery_encode(&fixture.layout,&invalid_plan,&unused,&count));
    invalid_plan=plan;invalid_plan.worker_pid=0;CHECK(!l4_recovery_encode(&fixture.layout,&invalid_plan,&unused,&count));
    invalid_plan=plan;invalid_plan.recovery_ms=0;CHECK(!l4_recovery_encode(&fixture.layout,&invalid_plan,&unused,&count));
    L4RecoveryAction action;CHECK(l4_recovery_decide(&plan,0,plan.armed_utc,false,&action) && action==L4_RECOVERY_WAIT);
    CHECK(l4_recovery_decide(&plan,0,plan.deadline_utc,false,&action) && action==L4_RECOVERY_REQUIRED);
    CHECK(l4_recovery_decide(&plan,0,plan.armed_utc,true,&action) && action==L4_RECOVERY_REQUIRED);
    CHECK(!l4_recovery_decide(&plan,0,plan.armed_utc-1,false,&action));
    CHECK(l4_recovery_decide(&plan,0,0,true,&action) && action==L4_RECOVERY_REQUIRED);
    CHECK(l4_recovery_decide(&plan,L4_RECOVERY_COMMITTED,0,true,&action) && action==L4_RECOVERY_DONE);
    BYTE result[96];L4RecoveryStatus phase;DWORD error;
    CHECK(l4_recovery_result_encode(&plan,bytes+size-32,L4_RECOVERY_COMMITTED,0,result));CHECK(l4_recovery_result_decode(&plan,bytes+size-32,result,96,&phase,&error));
    L4RecoveryPlan foreign=plan;foreign.operation.Data1++;CHECK(!l4_recovery_result_decode(&foreign,bytes+size-32,result,96,&phase,&error));
    result[24]^=1;CHECK(l4_recovery_hash(result,64,result+64));CHECK(!l4_recovery_result_decode(&plan,bytes+size-32,result,96,&phase,&error));
    L4RecoveryGuard* guard=NULL;CHECK(!l4_recovery_open(&fixture.layout,id,20,&guard));CHECK(!guard);
    invalid_plan=plan;invalid_plan.worker_created.dwLowDateTime++;CHECK(!l4_recovery_prepare(journal,&invalid_plan,GetCurrentProcess()));
    CHECK(l4_recovery_prepare(journal,&plan,GetCurrentProcess()));ULONGLONG sequence=journal->sequence;
    CHECK(l4_recovery_prepare(journal,&plan,GetCurrentProcess()));CHECK(journal->sequence==sequence);
    invalid_plan=plan;invalid_plan.recovery_ms++;CHECK(!l4_recovery_prepare(journal,&invalid_plan,GetCurrentProcess()));CHECK(journal->sequence==sequence);
    CHECK(l4_recovery_open(&fixture.layout,id,1000,&guard));CHECK(l4_recovery_plan(guard)->worker_pid==GetCurrentProcessId()); /* deployment.lock still held by producer */
    L4RecoveryGuard* blocked=NULL;ULONGLONG started=GetTickCount64();CHECK(!l4_recovery_open(&fixture.layout,id,30,&blocked));CHECK(GetTickCount64()-started<500 && !blocked);
    CHECK(l4_recovery_action(guard,plan.armed_utc,false,&action) && action==L4_RECOVERY_WAIT);
    CHECK(!l4_recovery_finish(guard,L4_RECOVERY_RESTORED,0,plan.armed_utc));CHECK(!l4_recovery_begin(guard,plan.armed_utc,false));
    CHECK(!l4_recovery_finish(guard,L4_RECOVERY_COMMITTED,0,plan.deadline_utc));
    CHECK(l4_recovery_begin(guard,plan.armed_utc,true));CHECK(l4_recovery_begin(guard,plan.armed_utc,true));
    CHECK(!l4_recovery_finish(guard,L4_RECOVERY_COMMITTED,0,plan.armed_utc));
    l4_recovery_release(guard);CHECK(l4_recovery_plan(guard)->sequence==plan.sequence);CHECK(!l4_recovery_action(guard,plan.armed_utc,false,&action));
    CHECK(l4_recovery_open(&fixture.layout,id,1000,&blocked));CHECK(l4_recovery_action(blocked,plan.armed_utc,false,&action) && action==L4_RECOVERY_REQUIRED);
    CHECK(!l4_recovery_finish(blocked,L4_RECOVERY_COMMITTED,0,plan.armed_utc));l4_recovery_close(blocked);blocked=NULL;
    CHECK(l4_recovery_relock(guard,1000));CHECK(l4_recovery_finish(guard,L4_RECOVERY_RESTORED,0,plan.deadline_utc+1));
    CHECK(l4_recovery_finish(guard,L4_RECOVERY_RESTORED,0,plan.deadline_utc+1));CHECK(l4_recovery_action(guard,0,true,&action) && action==L4_RECOVERY_DONE);
    l4_recovery_close(guard);guard=NULL;
    wchar_t name[MAX_PATH];CHECK(result_path(journal,name));CHECK(DeleteFileW(name)); /* Only owned fixture reset, no production reset API. */
    HANDLE go=CreateEventW(NULL,TRUE,FALSE,NULL);CHECK(go);Race race[2]={{fixture.layout,id,go,false,false,plan.armed_utc},{fixture.layout,id,go,true,false,plan.armed_utc}};
    HANDLE workers[2]={CreateThread(NULL,0,decide_race,&race[0],0,NULL),CreateThread(NULL,0,decide_race,&race[1],0,NULL)};CHECK(workers[0] && workers[1]);
    SetEvent(go);DWORD joined=WaitForMultipleObjects(2,workers,TRUE,5000);CHECK(joined==WAIT_OBJECT_0);CHECK(race[0].ok!=race[1].ok);CloseHandle(workers[0]);CloseHandle(workers[1]);CloseHandle(go);
    CHECK(l4_recovery_open(&fixture.layout,id,1000,&guard));CHECK(l4_recovery_action(guard,plan.deadline_utc,true,&action));CHECK(action==(race[0].ok?L4_RECOVERY_DONE:L4_RECOVERY_REQUIRED));
    l4_recovery_close(guard);guard=NULL;CHECK(DeleteFileW(name));
    CHECK(l4_recovery_open(&fixture.layout,id,1000,&guard));CHECK(l4_recovery_begin(guard,plan.deadline_utc,false));
    CHECK(l4_recovery_finish(guard,L4_RECOVERY_FAILED,ERROR_ACCESS_DENIED,plan.deadline_utc));
    CHECK(l4_recovery_action(guard,plan.deadline_utc,true,&action) && action==L4_RECOVERY_BLOCKED);CHECK(!l4_recovery_begin(guard,plan.deadline_utc,true));
    l4_recovery_close(guard);guard=NULL;
    HANDLE raw=CreateFileW(name,GENERIC_WRITE,0,NULL,OPEN_EXISTING,0,NULL);CHECK(raw!=INVALID_HANDLE_VALUE);BYTE corrupt=0;DWORD written;CHECK(WriteFile(raw,&corrupt,1,&written,NULL));CloseHandle(raw);
    CHECK(l4_recovery_open(&fixture.layout,id,1000,&guard));CHECK(!l4_recovery_action(guard,plan.deadline_utc,true,&action));l4_recovery_close(guard);guard=NULL;
    wchar_t plan_name[MAX_PATH],alias[MAX_PATH];swprintf_s(plan_name,MAX_PATH,L"%ls\\supervisor.recovery",journal->directory);swprintf_s(alias,MAX_PATH,L"%ls\\alias",journal->directory);
    CHECK(CreateHardLinkW(alias,plan_name,NULL));CHECK(!l4_recovery_open(&fixture.layout,id,1000,&guard));CHECK(DeleteFileW(alias));
    swprintf_s(alias,MAX_PATH,L"%ls:extra",plan_name);raw=CreateFileW(alias,GENERIC_WRITE,0,NULL,CREATE_NEW,0,NULL);CHECK(raw!=INVALID_HANDLE_VALUE);CloseHandle(raw);
    CHECK(!l4_recovery_open(&fixture.layout,id,1000,&guard));CHECK(DeleteFileW(alias));
    l4_journal_close(journal);LocalFree(sd);free(bytes);CHECK(update_fixture_dispose(&fixture));
    printf("Supervisor recovery contract: %u passed, %u failed; actual private files/decision locks/worker epoch, no SCM/tasks/network\n",checks-failures,failures);
    return failures?1:0;
}
