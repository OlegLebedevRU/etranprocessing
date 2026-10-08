#include "update_state_fixture.h"
#include <stdlib.h>
static bool scm_present;
static bool fixture_inventory(const wchar_t* name,L4ServiceInventory* inventory){
    (void)name;memset(inventory,0,sizeof(*inventory));inventory->installed=scm_present;return true;
}
#define l4_service_inventory fixture_inventory
#include "../update_state_store.c"
#undef l4_service_inventory
static unsigned checks,failures;
#define CHECK(x) do{++checks;if(!(x)){++failures;printf("FAIL update-state %u %s (%lu)\n",__LINE__,#x,GetLastError());}}while(0)
typedef struct {
    L4UpdateConsumer consumer;L4UpdateState expected;
    HANDLE decided,resume,done,cancel;volatile LONG busy;DWORD result;
} DrainRace;
static DWORD WINAPI admitted_before_marker(void* context){
    DrainRace* race=context;AcquireSRWLockShared(&race->consumer.admission);
    L4UpdateState state;if(!l4_update_consumer_read(&race->consumer,&state) || state.window){SetEvent(race->cancel);SetEvent(race->decided);ReleaseSRWLockShared(&race->consumer.admission);return 1;}
    SetEvent(race->decided);WaitForSingleObject(race->resume,5000);
    InterlockedExchange(&race->busy,1); /* Earlier accepted async worker now starts. */
    ReleaseSRWLockShared(&race->consumer.admission);return 0;
}
static bool race_busy(void* context){return InterlockedCompareExchange(&((DrainRace*)context)->busy,0,0)!=0;}
static DWORD WINAPI draining(void* context){
    DrainRace* race=context;race->result=l4_update_consumer_drain(&race->consumer,&race->expected,3000,race->cancel,race_busy,race);
    SetEvent(race->done);return 0;
}
static void test_drain_race(UpdateFixture* fixture){
    CHECK(update_fixture_put(fixture,0));DrainRace race={0};race.consumer.enabled=true;race.consumer.layout=fixture->layout;
    race.decided=CreateEventW(NULL,TRUE,FALSE,NULL);race.resume=CreateEventW(NULL,TRUE,FALSE,NULL);
    race.done=CreateEventW(NULL,TRUE,FALSE,NULL);race.cancel=CreateEventW(NULL,TRUE,FALSE,NULL);
    CHECK(race.decided && race.resume && race.done && race.cancel);
    HANDLE admit=CreateThread(NULL,0,admitted_before_marker,&race,0,NULL);CHECK(admit);
    CHECK(WaitForSingleObject(race.decided,3000)==WAIT_OBJECT_0);
    CHECK(update_fixture_live(fixture,&race.expected));
    HANDLE drain=CreateThread(NULL,0,draining,&race,0,NULL);CHECK(drain);
    CHECK(WaitForSingleObject(race.done,40)==WAIT_TIMEOUT); /* Old admission not finished. */
    SetEvent(race.resume);CHECK(WaitForSingleObject(admit,3000)==WAIT_OBJECT_0);
    CHECK(WaitForSingleObject(race.done,40)==WAIT_TIMEOUT); /* Async cleanup still live. */
    InterlockedExchange(&race.busy,0);CHECK(WaitForSingleObject(race.done,3000)==WAIT_OBJECT_0);CHECK(race.result==ERROR_SUCCESS);
    CHECK(WaitForSingleObject(drain,3000)==WAIT_OBJECT_0);
    L4UpdateState wrong=race.expected;wrong.owner[0]='2';
    CHECK(l4_update_consumer_drain(&race.consumer,&wrong,1000,race.cancel,race_busy,&race)==ERROR_REVISION_MISMATCH);
    wrong=race.expected;wrong.deadline_utc++;
    CHECK(l4_update_consumer_drain(&race.consumer,&wrong,1000,race.cancel,race_busy,&race)==ERROR_REVISION_MISMATCH);
    wrong=race.expected;wrong.generation++;
    CHECK(l4_update_consumer_drain(&race.consumer,&wrong,1000,race.cancel,race_busy,&race)==ERROR_REVISION_MISMATCH);
    SetEvent(race.cancel);CHECK(l4_update_consumer_drain(&race.consumer,&race.expected,1000,race.cancel,race_busy,&race)==ERROR_CANCELLED);
    CloseHandle(admit);CloseHandle(drain);CloseHandle(race.decided);CloseHandle(race.resume);CloseHandle(race.done);CloseHandle(race.cancel);
    CHECK(update_fixture_put(fixture,0));
    puts("Drain race: accepted clear decision -> active marker -> late dispatch -> async cleanup, exact owner/deadline/generation and cancel PASS");
}
static bool raw(const wchar_t* path,const void* bytes,DWORD size){
    HANDLE file=CreateFileW(path,GENERIC_WRITE,0,NULL,OPEN_EXISTING,0,NULL);if(file==INVALID_HANDLE_VALUE)return false;
    DWORD written=0;bool ok=WriteFile(file,bytes,size,&written,NULL) && written==size && SetEndOfFile(file) && FlushFileBuffers(file);CloseHandle(file);return ok;
}
static bool fresh_process(const UpdateFixture* fixture,DWORD window){
    wchar_t exe[MAX_PATH],command[2048];if(!GetModuleFileNameW(NULL,exe,MAX_PATH))return false;
    swprintf_s(command,2048,L"\"%ls\" --state-reader \"%ls\" \"%ls\" %lu",exe,fixture->layout.binaries,fixture->layout.data,window);
    STARTUPINFOW startup={0};startup.cb=sizeof(startup);PROCESS_INFORMATION process={0};
    if(!CreateProcessW(exe,command,NULL,NULL,FALSE,CREATE_NO_WINDOW,NULL,NULL,&startup,&process))return false;
    CloseHandle(process.hThread);DWORD wait=WaitForSingleObject(process.hProcess,10000),code=1;
    bool ok=wait==WAIT_OBJECT_0 && GetExitCodeProcess(process.hProcess,&code) && !code;
    if(wait!=WAIT_OBJECT_0){TerminateProcess(process.hProcess,1);WaitForSingleObject(process.hProcess,5000);}CloseHandle(process.hProcess);return ok;
}
int wmain(int argc,wchar_t** argv){
    if(argc==5 && !wcscmp(argv[1],L"--state-reader")){
        L4Layout layout;L4UpdateState state;return l4_layout_from_roots(&layout,argv[2],argv[3],L"1.13.2") &&
            l4_update_state_read(&layout,&state) && state.window==(DWORD)_wtoi(argv[4])?0:1;
    }
    if(argc!=1)return 1;
    UpdateFixture fixture;CHECK(update_fixture_init(&fixture));if(failures)return 1;
    L4UpdateState state;CHECK(!l4_update_state_read(&fixture.layout,&state));CHECK(GetLastError()==ERROR_FILE_NOT_FOUND);
    const wchar_t* id=L"17730000-0000-4000-8000-000000000001";L4Journal* journal=NULL;
    CHECK(l4_journal_open(&fixture.layout,id,true,&journal));if(!journal){update_fixture_dispose(&fixture);return 1;}
    scm_present=true;CHECK(!l4_update_state_provision(journal));CHECK(!l4_update_state_read(&fixture.layout,&state));
    scm_present=false;CHECK(l4_update_state_provision(journal));CHECK(l4_update_state_read(&fixture.layout,&state));CHECK(!state.window && !state.generation);
    CHECK(fresh_process(&fixture,0));test_drain_race(&fixture);CHECK(update_fixture_put(&fixture,0));
    ULONGLONG plan=0;CHECK(l4_journal_append(journal,64,"fixture-not-admission",21,&plan));
    FILETIME time;GetSystemTimeAsFileTime(&time);ULONGLONG deadline=(((ULONGLONG)time.dwHighDateTime<<32)|time.dwLowDateTime)+600000000ull;
    CHECK(!l4_update_state_publish(journal,plan+1,0,1,1));
    CHECK(!l4_update_state_publish(journal,plan,0,1,1));
    CHECK(l4_update_state_publish(journal,plan,0,1,deadline));CHECK(l4_update_state_read(&fixture.layout,&state));
    CHECK(state.window==1 && state.generation==1 && state.plan_sequence==plan && state.deadline_utc==deadline);
    /* Expired state still blocks; reader works while writer holds deployment.lock. */
    CHECK(fresh_process(&fixture,1));ULONGLONG journal_sequence=journal->sequence;
    CHECK(l4_update_state_publish(journal,plan,0,1,deadline));CHECK(journal_sequence==journal->sequence);
    CHECK(!l4_update_state_publish(journal,plan,1,1,deadline+1));CHECK(!l4_update_state_publish(journal,plan,99,0,0));
    BYTE expired[L4_UPDATE_STATE_SIZE];state.deadline_utc=1;CHECK(l4_update_state_encode(&state,expired));CHECK(l4_update_state_replace(&fixture.layout,expired,false));
    CHECK(fresh_process(&fixture,1));CHECK(!l4_update_state_publish(journal,plan,1,2,deadline));
    state.deadline_utc=deadline;CHECK(l4_update_state_encode(&state,expired));CHECK(l4_update_state_replace(&fixture.layout,expired,false));
    CHECK(!l4_update_state_provision(journal));CHECK(l4_update_state_read(&fixture.layout,&state) && state.window==1);
    l4_journal_close(journal);journal=NULL;CHECK(l4_journal_open(&fixture.layout,L"17730000-0000-4000-8000-000000000002",true,&journal));
    if(!journal){update_fixture_dispose(&fixture);return 1;}
    ULONGLONG foreign=0;CHECK(l4_journal_append(journal,64,"fixture",7,&foreign));
    CHECK(!l4_update_state_publish(journal,foreign,1,0,0));CHECK(!l4_update_state_publish(journal,foreign,1,2,deadline));
    l4_journal_close(journal);journal=NULL;CHECK(l4_journal_open(&fixture.layout,id,false,&journal));
    if(!journal){update_fixture_dispose(&fixture);return 1;}
    wchar_t path[MAX_PATH],alias[MAX_PATH];CHECK(l4_layout_data_path(&fixture.layout,L"update\\operations\\update.state",path));
    HANDLE blocked=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,0,NULL);
    CHECK(blocked!=INVALID_HANDLE_VALUE);ULONGLONG before_intent=journal->sequence;
    CHECK(!l4_update_state_publish(journal,plan,1,2,deadline));
    CHECK(journal->sequence==before_intent+1);CHECK(l4_update_state_read(&fixture.layout,&state) && state.window==1);
    CHECK(!l4_update_state_publish(journal,plan,1,2,deadline+1));CHECK(GetLastError()==ERROR_REVISION_MISMATCH);
    CHECK(journal->sequence==before_intent+1);CHECK(CloseHandle(blocked));
    CHECK(l4_update_state_publish(journal,plan,1,2,deadline));CHECK(journal->sequence==before_intent+1);CHECK(fresh_process(&fixture,2));
    CHECK(l4_update_state_publish(journal,plan,2,0,0));CHECK(fresh_process(&fixture,0));
    CHECK(l4_update_state_read(&fixture.layout,&state) && state.generation==3 && !state.window && state.plan_sequence==plan);
    CHECK(l4_update_state_publish(journal,plan,2,0,0));CHECK(!l4_update_state_publish(journal,plan,3,0,0));
    BYTE good[L4_UPDATE_STATE_SIZE],bad[L4_UPDATE_STATE_SIZE+1];CHECK(l4_update_state_encode(&state,good));
    memcpy(bad,good,sizeof(good));bad[10]^=1;CHECK(raw(path,bad,sizeof(good)));CHECK(!l4_update_state_read(&fixture.layout,&state));
    CHECK(!l4_update_state_provision(journal));CHECK(raw(path,good,sizeof(good)));
    for(unsigned i=0;i<3;i++){
        memcpy(bad,good,sizeof(good));
        if(!i)l4_store_u32(bad+48,3);else if(i==1)l4_store_u32(bad+52,1);else bad[44]='x';
        CHECK(l4_store_hash(bad,80,NULL,0,bad+80));CHECK(raw(path,bad,sizeof(good)));CHECK(!l4_update_state_read(&fixture.layout,&state));
    }
    CHECK(raw(path,good,111));CHECK(!l4_update_state_read(&fixture.layout,&state));
    memcpy(bad,good,sizeof(good));bad[112]=0;CHECK(raw(path,bad,113));CHECK(!l4_update_state_read(&fixture.layout,&state));CHECK(raw(path,good,sizeof(good)));
    swprintf_s(alias,MAX_PATH,L"%ls:extra",path);HANDLE stream=CreateFileW(alias,GENERIC_WRITE,0,NULL,CREATE_NEW,0,NULL);CHECK(stream!=INVALID_HANDLE_VALUE);
    if(stream!=INVALID_HANDLE_VALUE)CloseHandle(stream);CHECK(!l4_update_state_read(&fixture.layout,&state));CHECK(DeleteFileW(alias));
    swprintf_s(alias,MAX_PATH,L"%ls\\alias.state",fixture.layout.operations);CHECK(CreateHardLinkW(alias,path,NULL));CHECK(!l4_update_state_read(&fixture.layout,&state));CHECK(DeleteFileW(alias));
    PSECURITY_DESCRIPTOR insecure=NULL;
    CHECK(ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(A;;FA;;;SY)(A;;FA;;;BA)(A;;GW;;;BU)",SDDL_REVISION_1,&insecure,NULL));
    if(insecure){CHECK(SetFileSecurityW(path,DACL_SECURITY_INFORMATION|PROTECTED_DACL_SECURITY_INFORMATION,insecure));LocalFree(insecure);}
    CHECK(!l4_update_state_read(&fixture.layout,&state));CHECK(!l4_update_state_publish(journal,plan,3,1,deadline));
    CHECK(l4_update_state_replace(&fixture.layout,good,false)); /* Explicit fixture repair, never producer auto-repair. */
    HANDLE parent=CreateFileW(fixture.layout.operations,READ_CONTROL,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS,NULL);
    CHECK(parent!=INVALID_HANDLE_VALUE);BYTE* parent_sd=NULL;DWORD parent_sd_size=0;
    CHECK(l4_store_security(parent,false,&parent_sd,&parent_sd_size));if(parent!=INVALID_HANDLE_VALUE)CloseHandle(parent);
    insecure=NULL;CHECK(ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;;0x00000040;;;SU)",SDDL_REVISION_1,&insecure,NULL));
    if(insecure){CHECK(SetFileSecurityW(fixture.layout.operations,DACL_SECURITY_INFORMATION|PROTECTED_DACL_SECURITY_INFORMATION,insecure));LocalFree(insecure);}
    CHECK(!l4_update_state_read(&fixture.layout,&state));CHECK(!l4_update_state_publish(journal,plan,3,1,deadline));
    if(parent_sd){CHECK(SetFileSecurityW(fixture.layout.operations,DACL_SECURITY_INFORMATION|PROTECTED_DACL_SECURITY_INFORMATION,parent_sd));free(parent_sd);}
    CHECK(l4_update_state_read(&fixture.layout,&state));CHECK(DeleteFileW(path));CHECK(!l4_update_state_read(&fixture.layout,&state));
    scm_present=true;CHECK(!l4_update_state_provision(journal));l4_journal_close(journal);CHECK(update_fixture_dispose(&fixture));
    printf("Update state: %u passed, %u failed, %u total\n",checks-failures,failures,checks);return failures?1:0;
}
