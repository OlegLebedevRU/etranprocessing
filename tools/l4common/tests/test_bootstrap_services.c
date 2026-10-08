#include "../bootstrap.h"
#include "../journal_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "deployment_fixture.h"
#include "bootstrap_codec_fixture.h"
static unsigned checks,failures,creates,deletes;
#define CHECK(x) do{++checks;if(!(x)){++failures;printf("FAIL line %u: %s (win32=%lu)\n",__LINE__,#x,GetLastError());}}while(0)
typedef struct {bool present,marked,hold,crash_profile,noncrash;unsigned handles;wchar_t image[2048],account[256],display[128];DWORD type,start,error,state,pid;ULONGLONG ready_at,birth,stopped_at,exit_at;bool dead;} Slot;
static Slot slots[4];static const wchar_t* names[]={L"Leo4Proxy",L"mosquitto",L"L4Con",L"L4Superv"};
static int failed_create=-1,race=-1,failed_query=-1;static bool poison_create,poison_delete;
static bool unknown_create,unknown_delete,failed_delete;
static unsigned starts,probes,barriers,process_refs[4],token_refs,events[64],event_count;static int failed_start=-1,failed_probe=-1,failed_barrier=-1;
static DWORD start_delay,broker_delay,probe_delay,barrier_delay;static bool poison_start,poison_probe,start_exits,unknown_start,wrong_image,wrong_user,kill_previous,swap_pid;
static int dead_barrier=-1,failed_stop=-1;static unsigned stops,stop_order[4];
static DWORD stop_delay,exit_delay;static bool poison_stop,unknown_stop,deny_process;
static unsigned type_changes;static int failed_type=-1;static bool unknown_type,poison_type;static DWORD type_delay;
static L4Journal* active;static ULONGLONG tick,release_at;static unsigned deleted_order[4],deleted_count;
static int service_number(const wchar_t* name){for(unsigned i=0;i<4;i++)if(!_wcsicmp(name,names[i]))return (int)i;return -1;}
static void settle(Slot* s){if(s->stopped_at && tick>=s->stopped_at)s->state=SERVICE_STOPPED;if(s->exit_at && tick>=s->exit_at)s->dead=true;if(release_at && tick>=release_at)s->hold=false;if(s->marked && !s->handles && !s->hold)s->present=false;}
static SC_HANDLE WINAPI mock_manager(LPCWSTR a,LPCWSTR b,DWORD rights){CHECK(!a&&!b);CHECK(rights==SC_MANAGER_CONNECT || rights==(SC_MANAGER_CONNECT|SC_MANAGER_CREATE_SERVICE));return (SC_HANDLE)(ULONG_PTR)1;}
static SC_HANDLE WINAPI mock_open_service(SC_HANDLE h,LPCWSTR name,DWORD rights){CHECK(h==(SC_HANDLE)(ULONG_PTR)1);CHECK(rights==(SERVICE_QUERY_CONFIG|SERVICE_QUERY_STATUS|DELETE) || rights==(SERVICE_QUERY_CONFIG|SERVICE_QUERY_STATUS|DELETE|SERVICE_CHANGE_CONFIG|SERVICE_START) || rights==SERVICE_QUERY_STATUS || rights==(SERVICE_QUERY_CONFIG|SERVICE_QUERY_STATUS|SERVICE_START) || rights==(SERVICE_QUERY_CONFIG|SERVICE_QUERY_STATUS|SERVICE_STOP) || rights==(SERVICE_QUERY_CONFIG|SERVICE_QUERY_STATUS|SERVICE_CHANGE_CONFIG));
    int i=service_number(name);if(i<0){SetLastError(ERROR_SERVICE_DOES_NOT_EXIST);return NULL;}Slot* s=&slots[i];settle(s);
    if(!s->present){SetLastError(ERROR_SERVICE_DOES_NOT_EXIST);return NULL;}if(s->marked){SetLastError(ERROR_SERVICE_MARKED_FOR_DELETE);return NULL;}
    ++s->handles;return (SC_HANDLE)s;}
static BOOL WINAPI mock_close_service(SC_HANDLE h){if(h==(SC_HANDLE)(ULONG_PTR)1)return TRUE;Slot* s=(Slot*)h;CHECK(s->handles>0);--s->handles;settle(s);return TRUE;}
static bool poison(void){HANDLE read_only=NULL;bool ok=DuplicateHandle(GetCurrentProcess(),active->file,GetCurrentProcess(),&read_only,GENERIC_READ,FALSE,0)!=0;
    CHECK(ok);if(ok){CloseHandle(active->file);active->file=read_only;}return ok;}
static SC_HANDLE WINAPI mock_create_service(SC_HANDLE h,LPCWSTR name,LPCWSTR display,DWORD rights,DWORD type,DWORD start,DWORD error,LPCWSTR image,LPCWSTR group,LPDWORD tag,LPCWSTR dependencies,LPCWSTR account,LPCWSTR password){
    CHECK(h==(SC_HANDLE)(ULONG_PTR)1);CHECK(rights==(DWORD)((SERVICE_QUERY_CONFIG|SERVICE_QUERY_STATUS|DELETE)|(!wcscmp(name,L"L4Superv")?(SERVICE_CHANGE_CONFIG|SERVICE_START):0)));CHECK(type==SERVICE_WIN32_OWN_PROCESS && start==SERVICE_DEMAND_START && error==SERVICE_ERROR_NORMAL);
    CHECK(!group&&!tag&&!dependencies&&!password);CHECK(account && !wcscmp(account,L"LocalSystem"));
    int i=service_number(name);CHECK(i>=0);if(i<0)return NULL;
    if(i==race){slots[i].present=true;wcscpy_s(slots[i].display,128,L"Foreign operator service");race=-1;SetLastError(ERROR_SERVICE_EXISTS);return NULL;}
    if(i==failed_create){SetLastError(ERROR_ACCESS_DENIED);return NULL;}
    if(slots[i].present){SetLastError(ERROR_SERVICE_EXISTS);return NULL;}
    Slot* s=&slots[i];memset(s,0,sizeof(*s));s->present=true;s->handles=1;s->type=type;s->start=start;s->error=error;s->state=SERVICE_STOPPED;
    wcscpy_s(s->image,2048,image);wcscpy_s(s->account,256,account);wcscpy_s(s->display,128,display);++creates;
    CHECK(wcsstr(display,L"[L4:17730000-0000-4000-8000-000000000004:")!=NULL);
    if(poison_create){poison_create=false;poison();}
    if(unknown_create){unknown_create=false;s->handles=0;SetLastError(RPC_S_CALL_FAILED);return NULL;}return (SC_HANDLE)s;
}
static BOOL WINAPI mock_query_config(SC_HANDLE h,LPQUERY_SERVICE_CONFIGW cfg,DWORD size,LPDWORD needed){
    Slot* s=(Slot*)h;*needed=sizeof(*cfg);if(!cfg || size<sizeof(*cfg)){SetLastError(ERROR_INSUFFICIENT_BUFFER);return FALSE;}
    int i=(int)(s-slots);if(i==failed_query){failed_query=-1;SetLastError(ERROR_ACCESS_DENIED);return FALSE;}
    memset(cfg,0,sizeof(*cfg));cfg->dwServiceType=s->type;cfg->dwStartType=s->start;cfg->dwErrorControl=s->error;cfg->lpBinaryPathName=s->image;cfg->lpServiceStartName=s->account;cfg->lpDisplayName=s->display;
    cfg->lpDependencies=L"";cfg->lpLoadOrderGroup=L"";return TRUE;
}
static BOOL WINAPI mock_query_status(SC_HANDLE h,SC_STATUS_TYPE type,LPBYTE bytes,DWORD size,LPDWORD needed){CHECK(type==SC_STATUS_PROCESS_INFO && size>=sizeof(SERVICE_STATUS_PROCESS));
    SERVICE_STATUS_PROCESS* status=(SERVICE_STATUS_PROCESS*)bytes;memset(status,0,sizeof(*status));Slot* s=(Slot*)h;settle(s);if(s->state==SERVICE_START_PENDING && s->ready_at && tick>=s->ready_at)s->state=SERVICE_RUNNING;
    status->dwCurrentState=s->state;status->dwServiceType=s->type;status->dwProcessId=(s->state==SERVICE_STOPPED || s->state==SERVICE_STOP_PENDING || s->state==SERVICE_START_PENDING)?0:s->pid;*needed=sizeof(*status);return TRUE;}
static BOOL WINAPI mock_change2(SC_HANDLE h,DWORD level,LPVOID bytes){Slot* s=(Slot*)h;CHECK(s==slots+3 && s->state==SERVICE_STOPPED);
    if(level==SERVICE_CONFIG_FAILURE_ACTIONS_FLAG){SERVICE_FAILURE_ACTIONS_FLAG* p=bytes;s->noncrash=p->fFailureActionsOnNonCrashFailures!=FALSE;return TRUE;}
    CHECK(level==SERVICE_CONFIG_FAILURE_ACTIONS);SERVICE_FAILURE_ACTIONSW* p=bytes;
    CHECK(p->dwResetPeriod==86400 && p->cActions==1 && p->lpsaActions[0].Type==SC_ACTION_RESTART && p->lpsaActions[0].Delay==1000 && !*p->lpCommand && !*p->lpRebootMsg);
    s->crash_profile=true;return TRUE;
}
static BOOL WINAPI mock_query2(SC_HANDLE h,DWORD level,LPBYTE bytes,DWORD size,LPDWORD needed){Slot* s=(Slot*)h;CHECK(s==slots+3);
    *needed=level==SERVICE_CONFIG_FAILURE_ACTIONS?sizeof(SERVICE_FAILURE_ACTIONSW)+sizeof(SC_ACTION):sizeof(SERVICE_FAILURE_ACTIONS_FLAG);
    if(!bytes || size<*needed){SetLastError(ERROR_INSUFFICIENT_BUFFER);return FALSE;}
    memset(bytes,0,size);if(level==SERVICE_CONFIG_FAILURE_ACTIONS_FLAG){((SERVICE_FAILURE_ACTIONS_FLAG*)bytes)->fFailureActionsOnNonCrashFailures=s->noncrash;return TRUE;}
    CHECK(level==SERVICE_CONFIG_FAILURE_ACTIONS);SERVICE_FAILURE_ACTIONSW* p=(SERVICE_FAILURE_ACTIONSW*)bytes;
    if(s->crash_profile){p->dwResetPeriod=86400;p->cActions=1;p->lpsaActions=(SC_ACTION*)(bytes+sizeof(*p));p->lpsaActions[0].Type=SC_ACTION_RESTART;p->lpsaActions[0].Delay=1000;}return TRUE;
}
static BOOL WINAPI mock_delete_service(SC_HANDLE h){Slot* s=(Slot*)h;CHECK(s->state==SERVICE_STOPPED);CHECK(s->present&&!s->marked);if(failed_delete){failed_delete=false;SetLastError(ERROR_ACCESS_DENIED);return FALSE;}s->marked=true;++deletes;if(deleted_count<4)deleted_order[deleted_count++]=(unsigned)(s-slots);
    if(poison_delete){poison_delete=false;poison();}
    if(unknown_delete){unknown_delete=false;SetLastError(RPC_S_CALL_FAILED);return FALSE;}return TRUE;}
static BOOL WINAPI mock_start_service(SC_HANDLE h,DWORD count,LPCWSTR* args){
    Slot* s=(Slot*)h;int i=(int)(s-slots);CHECK(!count&&!args);CHECK(s->state==SERVICE_STOPPED);++starts;
    if(i==failed_start){SetLastError(ERROR_SERVICE_LOGON_FAILED);return FALSE;}
    s->pid=1000u+(unsigned)i;s->birth=0x1122334400000000ull+100*starts+(unsigned)i;s->dead=false;DWORD delay=i==1 && broker_delay?broker_delay:start_delay;s->state=delay?SERVICE_START_PENDING:SERVICE_RUNNING;s->ready_at=tick+delay;if(start_exits)s->state=SERVICE_STOPPED;
    if(poison_start){poison_start=false;poison();}
    if(unknown_start){unknown_start=false;SetLastError(RPC_S_CALL_FAILED);return FALSE;}return TRUE;
}
static BOOL WINAPI mock_change(SC_HANDLE h,DWORD type,DWORD start,DWORD error,LPCWSTR image,LPCWSTR group,LPDWORD tag,LPCWSTR deps,LPCWSTR account,LPCWSTR password,LPCWSTR display){
    Slot* s=(Slot*)h;int i=(int)(s-slots);CHECK(type==SERVICE_NO_CHANGE && error==SERVICE_NO_CHANGE);
    CHECK(!image&&!group&&!tag&&!deps&&!account&&!password&&!display);CHECK(start==SERVICE_AUTO_START || start==SERVICE_DEMAND_START);
    if(i==failed_type){SetLastError(ERROR_ACCESS_DENIED);return FALSE;}s->start=start;++type_changes;tick+=type_delay;
    if(poison_type){poison_type=false;poison();}if(unknown_type){unknown_type=false;SetLastError(RPC_S_CALL_FAILED);return FALSE;}return TRUE;
}
static HANDLE WINAPI mock_process(DWORD rights,BOOL inherit,DWORD pid){CHECK(rights==(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE) && !inherit);
    for(unsigned i=0;i<4;i++)if(slots[i].pid==pid){if(deny_process){SetLastError(ERROR_ACCESS_DENIED);return NULL;}++process_refs[i];return (HANDLE)(ULONG_PTR)(0x10000+i);}
    SetLastError(ERROR_INVALID_PARAMETER);return NULL;
}
static unsigned process_index(HANDLE h){unsigned i=(unsigned)((ULONG_PTR)h-0x10000);CHECK(i<4);return i<4?i:0;}
static BOOL WINAPI mock_times(HANDLE h,LPFILETIME created,LPFILETIME exited,LPFILETIME kernel,LPFILETIME user){unsigned i=process_index(h);
    created->dwLowDateTime=(DWORD)slots[i].birth;created->dwHighDateTime=(DWORD)(slots[i].birth>>32);memset(exited,0,sizeof(*exited));memset(kernel,0,sizeof(*kernel));memset(user,0,sizeof(*user));return TRUE;
}
static BOOL WINAPI mock_stop(SC_HANDLE h,DWORD control,LPSERVICE_STATUS reply){Slot* s=(Slot*)h;unsigned i=(unsigned)(s-slots);
    CHECK(control==SERVICE_CONTROL_STOP && s->state==SERVICE_RUNNING);if((int)i==failed_stop){SetLastError(ERROR_ACCESS_DENIED);return FALSE;}
    if(stops<4)stop_order[stops]=i;++stops;memset(reply,0,sizeof(*reply));s->state=SERVICE_STOP_PENDING;
    s->stopped_at=tick+stop_delay;s->exit_at=tick+exit_delay;settle(s);reply->dwCurrentState=s->state;
    if(poison_stop){poison_stop=false;poison();}if(unknown_stop){unknown_stop=false;SetLastError(RPC_S_CALL_FAILED);return FALSE;}return TRUE;
}
static BOOL WINAPI mock_image(HANDLE h,DWORD flags,LPWSTR image,PDWORD length){unsigned i=process_index(h);CHECK(!flags);
    const wchar_t* from=slots[i].image+1;const wchar_t* end=wcschr(from,L'"');CHECK(end!=NULL);if(!end)return FALSE;
    size_t size=(size_t)(end-from);CHECK(*length>size);wmemcpy(image,from,size);image[size]=0;*length=(DWORD)size;
    if(wrong_image)image[0]=L'x';return TRUE;
}
static BOOL WINAPI mock_token(HANDLE h,DWORD rights,PHANDLE token){process_index(h);CHECK(rights==TOKEN_QUERY);++token_refs;*token=(HANDLE)(ULONG_PTR)0x20000;return TRUE;}
static BOOL WINAPI mock_token_info(HANDLE token,TOKEN_INFORMATION_CLASS type,LPVOID bytes,DWORD size,PDWORD needed){
    CHECK(token==(HANDLE)(ULONG_PTR)0x20000 && type==TokenUser);CHECK(size>=sizeof(TOKEN_USER)+SECURITY_MAX_SID_SIZE);
    TOKEN_USER* user=(TOKEN_USER*)bytes;DWORD sid_size=SECURITY_MAX_SID_SIZE;user->User.Sid=(BYTE*)bytes+sizeof(TOKEN_USER);user->User.Attributes=0;
    *needed=sizeof(TOKEN_USER)+sid_size;return CreateWellKnownSid(wrong_user?WinBuiltinUsersSid:WinLocalSystemSid,NULL,user->User.Sid,&sid_size);
}
static DWORD WINAPI mock_process_wait(HANDLE h,DWORD timeout){CHECK(timeout==0);unsigned i=process_index(h);settle(&slots[i]);return slots[i].dead?WAIT_OBJECT_0:WAIT_TIMEOUT;}
static BOOL WINAPI mock_process_close(HANDLE h){if(h==(HANDLE)(ULONG_PTR)0x20000){CHECK(token_refs>0);--token_refs;return TRUE;}unsigned i=process_index(h);CHECK(process_refs[i]>0);--process_refs[i];return TRUE;}
static bool application_probe(const L4BootstrapPlan* p,unsigned index,DWORD remaining,void* context){CHECK(p && context==slots);CHECK(remaining>0);CHECK(slots[index].state==SERVICE_RUNNING);++probes;if(event_count<64)events[event_count++]=index;
    if(poison_probe){poison_probe=false;poison();}tick+=probe_delay;if(kill_previous && index==1)slots[0].dead=true;if(swap_pid && index==1)++slots[0].pid;
    return (int)index!=failed_probe;
}
static bool communication_barrier(const L4BootstrapPlan* p,DWORD remaining,void* context){CHECK(p && remaining>0 && context==slots);++barriers;if(event_count<64)events[event_count++]=4;
    if((int)barriers==dead_barrier)slots[0].dead=true;
    for(unsigned i=0;i<3;i++)CHECK(slots[i].state==SERVICE_RUNNING);
    if(barriers==1)CHECK(slots[3].state==SERVICE_STOPPED || slots[3].state==SERVICE_RUNNING);tick+=barrier_delay;return (int)barriers!=failed_barrier;
}
static ULONGLONG WINAPI mock_now(void){return tick;}
static void WINAPI mock_delay(DWORD ms){tick+=ms;}
static bool inventory(const wchar_t* name,L4ServiceInventory* result){int i=service_number(name);memset(result,0,sizeof(*result));if(i<0)return false;result->installed=slots[i].present;return true;}
#define OpenSCManagerW mock_manager
#define OpenServiceW mock_open_service
#define CloseServiceHandle mock_close_service
#define CreateServiceW mock_create_service
#define QueryServiceConfigW mock_query_config
#define QueryServiceStatusEx mock_query_status
#define QueryServiceConfig2W mock_query2
#define ChangeServiceConfig2W mock_change2
#define DeleteService mock_delete_service
#define GetTickCount64 mock_now
#define Sleep mock_delay
#define ControlService mock_stop
#define ChangeServiceConfigW mock_change
#define GetProcessTimes mock_times
#define StartServiceW mock_start_service
#define OpenProcess mock_process
#define QueryFullProcessImageNameW mock_image
#define OpenProcessToken mock_token
#define GetTokenInformation mock_token_info
#define WaitForSingleObject mock_process_wait
#define CloseHandle mock_process_close
#include "../bootstrap_services.c"
#undef ControlService
#undef ChangeServiceConfigW
#undef GetProcessTimes
#undef StartServiceW
#undef OpenProcess
#undef QueryFullProcessImageNameW
#undef OpenProcessToken
#undef GetTokenInformation
#undef WaitForSingleObject
#undef CloseHandle
#undef OpenSCManagerW
#undef OpenServiceW
#undef CloseServiceHandle
#undef CreateServiceW
#undef QueryServiceConfigW
#undef QueryServiceStatusEx
#undef DeleteService
#undef GetTickCount64
#undef Sleep
#define l4_service_inventory inventory
#include "../bootstrap.c"
#include "../journal_codec.c"
#include "../update_state_store.c"
#undef l4_service_inventory
static void cleanup(const wchar_t* root){wchar_t pattern[MAX_PATH],child[MAX_PATH];swprintf_s(pattern,MAX_PATH,L"%ls\\*",root);WIN32_FIND_DATAW d;HANDLE h=FindFirstFileW(pattern,&d);
    if(h!=INVALID_HANDLE_VALUE){do{if(!wcscmp(d.cFileName,L".")||!wcscmp(d.cFileName,L".."))continue;swprintf_s(child,MAX_PATH,L"%ls\\%ls",root,d.cFileName);
        if(d.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY){if(!(d.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT))cleanup(child);else RemoveDirectoryW(child);}else DeleteFileW(child);
    }while(FindNextFileW(h,&d));FindClose(h);}RemoveDirectoryW(root);}
static bool write_file(const wchar_t* path,const void* bytes,DWORD size){HANDLE h=CreateFileW(path,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,0,NULL);if(h==INVALID_HANDLE_VALUE)return false;bool ok=l4_store_write(h,bytes,size)&&FlushFileBuffers(h);CloseHandle(h);return ok;}
static void reset(void){for(unsigned i=0;i<4;i++){CHECK(slots[i].handles==0);CHECK(process_refs[i]==0);}CHECK(token_refs==0);memset(slots,0,sizeof(slots));creates=deletes=deleted_count=0;failed_create=race=failed_query=-1;poison_create=poison_delete=unknown_create=unknown_delete=failed_delete=false;tick=100;release_at=0;starts=probes=barriers=event_count=0;dead_barrier=-1;failed_start=failed_probe=failed_barrier=-1;stops=0;failed_stop=-1;stop_delay=exit_delay=0;poison_stop=unknown_stop=deny_process=false;
    type_changes=0;failed_type=-1;unknown_type=poison_type=false;type_delay=0;
    start_delay=broker_delay=probe_delay=barrier_delay=0;poison_start=poison_probe=start_exits=unknown_start=wrong_image=wrong_user=kill_previous=swap_pid=false;}
static bool reopen(const L4Layout* layout){l4_journal_close(active);active=NULL;return l4_journal_open(layout,L"17730000-0000-4000-8000-000000000004",false,&active);}
int wmain(void){
    SC_ACTION restart={SC_ACTION_RESTART,1000};SERVICE_FAILURE_ACTIONS_FLAG crash_only={FALSE};
    SERVICE_FAILURE_ACTIONSW profile={86400,L"",L"",1,&restart};
    CHECK(l4_supervisor_crash_profile_valid(&profile,&crash_only,1000));
    CHECK(!l4_supervisor_crash_profile_valid(&profile,&crash_only,999));
    crash_only.fFailureActionsOnNonCrashFailures=TRUE;CHECK(!l4_supervisor_crash_profile_valid(&profile,&crash_only,1000));crash_only.fFailureActionsOnNonCrashFailures=FALSE;
    restart.Type=SC_ACTION_RUN_COMMAND;CHECK(!l4_supervisor_crash_profile_valid(&profile,&crash_only,1000));restart.Type=SC_ACTION_RESTART;
    restart.Delay=0;CHECK(!l4_supervisor_crash_profile_valid(&profile,&crash_only,1000));restart.Delay=1000;
    profile.cActions=0;CHECK(!l4_supervisor_crash_profile_valid(&profile,&crash_only,1000));profile.cActions=1;
    profile.dwResetPeriod=0;CHECK(!l4_supervisor_crash_profile_valid(&profile,&crash_only,1000));profile.dwResetPeriod=86400;
    profile.lpCommand=L"foreign";CHECK(!l4_supervisor_crash_profile_valid(&profile,&crash_only,1000));profile.lpCommand=L"";
    profile.lpRebootMsg=L"foreign";CHECK(!l4_supervisor_crash_profile_valid(&profile,&crash_only,1000));profile.lpRebootMsg=L"";
    wchar_t temp[MAX_PATH],root[MAX_PATH],programs[MAX_PATH],data[MAX_PATH],archive[MAX_PATH],expanded[MAX_PATH],source[MAX_PATH];
    CHECK(GetTempPathW(MAX_PATH,temp));swprintf_s(root,MAX_PATH,L"%lsl4registration-%lu-%llu",temp,GetCurrentProcessId(),GetTickCount64());CHECK(CreateDirectoryW(root,NULL));
    swprintf_s(programs,MAX_PATH,L"%ls\\Programs",root);swprintf_s(data,MAX_PATH,L"%ls\\Data",root);L4Layout layout;
    CHECK(l4_layout_from_roots(&layout,programs,data,L"1.13.2"));CHECK(l4_layout_prepare(&layout));
    const wchar_t* components[]={L"leo4proxy",L"mosquitto",L"l4con",L"l4superv"};const wchar_t* exes[]={L"leo4proxy.exe",L"mosquitto.exe",L"l4con.exe",L"l4superv.exe"};L4ReleaseFile files[4]={0};
    for(unsigned i=0;i<4;i++){wchar_t leaf[MAX_PATH];swprintf_s(leaf,MAX_PATH,L"update\\staging\\fixture\\%ls",components[i]);CHECK(l4_layout_prepare_leaf(&layout,leaf,NULL,NULL,false));
        swprintf_s(source,MAX_PATH,L"%ls\\update\\staging\\fixture\\%ls\\%ls",data,components[i],exes[i]);CHECK(write_file(source,"exe",3));files[i].component=components[i];files[i].file=exes[i];files[i].size=3;CHECK(l4_store_hash("exe",3,NULL,0,files[i].sha256));}
    CHECK(l4_layout_data_path(&layout,L"update\\cache\\fixture.zip",archive));CHECK(write_file(archive,new_archive,sizeof(new_archive)));CHECK(l4_layout_data_path(&layout,L"update\\staging\\fixture",expanded));
    BYTE hash[32];CHECK(l4_store_hash(new_archive,sizeof(new_archive),NULL,0,hash));CHECK(l4_release_publish(&layout,archive,hash,expanded,files,4));
    L4BootstrapProfile profiles[4];for(unsigned i=0;i<4;i++){profiles[i].service=names[i];profiles[i].account=L"LocalSystem";profiles[i].arguments=L"--service";profiles[i].start_type=SERVICE_AUTO_START;}
    L4BootstrapPlan plan,decoded;CHECK(l4_bootstrap_plan(&layout,profiles,4,files,4,&plan));CHECK(l4_journal_open(&layout,L"17730000-0000-4000-8000-000000000004",true,&active));if(!active){cleanup(root);return 1;}
    /* Both native architectures emit and read the same independent wire fixture. */
    L4BootstrapPlan golden=plan;CHECK(l4_layout_from_roots(&golden.layout,L"C:\\Fixture\\Programs",L"C:\\Fixture\\Data",L"1.13.2"));
    memset(golden.commands,0,sizeof(golden.commands));
    for(unsigned i=0;i<4;i++){wchar_t image[MAX_PATH];CHECK(l4_layout_component(&golden.layout,components[i],exes[i],image));swprintf_s(golden.commands[i],2048,L"\"%ls\" --service",image);}
    L4Layout actual=active->layout;active->layout=golden.layout;ULONGLONG golden_sequence=0;CHECK(l4_bootstrap_save(active,&golden,&golden_sequence));
    BYTE* golden_bytes=NULL;DWORD golden_size=0;CHECK(l4_store_find_record(active,L4_RECORD_BOOTSTRAP_PLAN,golden_sequence,&golden_bytes,&golden_size));
    CHECK(golden_size==sizeof(bootstrap_codec_fixture) && !memcmp(golden_bytes,bootstrap_codec_fixture,sizeof(bootstrap_codec_fixture)));free(golden_bytes);
    CHECK(l4_bootstrap_load(active,golden_sequence,&decoded));CHECK(!memcmp(&golden,&decoded,sizeof(golden)));
    active->layout=actual;CHECK(!l4_bootstrap_load(active,golden_sequence,&decoded));
    ULONGLONG sequence=0;CHECK(l4_bootstrap_save(active,&plan,&sequence));CHECK(l4_bootstrap_load(active,sequence,&decoded));CHECK(!memcmp(&plan,&decoded,sizeof(plan)));
    CHECK(reopen(&layout));CHECK(l4_bootstrap_register(active,sequence));CHECK(creates==4);CHECK(l4_bootstrap_register(active,sequence));CHECK(creates==4);
    L4UpdateState control;CHECK(l4_update_state_read(&layout,&control));CHECK(!control.window && !control.generation);
    CHECK(!l4_bootstrap_save(active,&plan,NULL));CHECK(GetLastError()==ERROR_SERVICE_EXISTS);
    CHECK(l4_bootstrap_rollback(active,sequence,1000));CHECK(deletes==4);for(unsigned i=0;i<4;i++){CHECK(!slots[i].present);CHECK(deleted_order[i]==3-i);}
    CHECK(l4_bootstrap_rollback(active,sequence,1000));CHECK(deletes==4);
    CHECK(!l4_bootstrap_register(active,sequence));CHECK(GetLastError()==ERROR_CANCELLED);CHECK(creates==4);
    /* Partial creation leaves only stopped/manual services; resume creates the rest. */
    reset();CHECK(l4_bootstrap_save(active,&plan,&sequence));failed_create=2;CHECK(!l4_bootstrap_register(active,sequence));CHECK(creates==2);CHECK(slots[0].start==SERVICE_DEMAND_START && slots[1].state==SERVICE_STOPPED);
    CHECK(reopen(&layout));failed_create=-1;CHECK(l4_bootstrap_register(active,sequence));CHECK(creates==4);CHECK(l4_bootstrap_rollback(active,sequence,1000));
    /* Race after absence: a foreign service is never adopted or removed. */
    reset();CHECK(l4_bootstrap_save(active,&plan,&sequence));race=2;CHECK(!l4_bootstrap_register(active,sequence));CHECK(creates==2);CHECK(!l4_bootstrap_rollback(active,sequence,1000));CHECK(slots[2].present);CHECK(deletes==2);CHECK(!slots[0].present&&!slots[1].present);
    /* Missing CREATE_DONE after the SCM mutation is reconciled by fingerprint. */
    reset();CHECK(l4_bootstrap_save(active,&plan,&sequence));poison_create=true;CHECK(!l4_bootstrap_register(active,sequence));CHECK(active->poisoned);CHECK(creates==1);CHECK(reopen(&layout));CHECK(l4_bootstrap_register(active,sequence));CHECK(creates==4);CHECK(l4_bootstrap_rollback(active,sequence,1000));
    /* Readback error also leaves a recoverable owned stopped service. */
    reset();CHECK(l4_bootstrap_save(active,&plan,&sequence));failed_query=0;CHECK(!l4_bootstrap_register(active,sequence));CHECK(creates==1);CHECK(reopen(&layout));CHECK(l4_bootstrap_register(active,sequence));CHECK(creates==4);
    /* Running/changed fingerprint blocks removal of that object, not cleanup of others. */
    slots[1].state=SERVICE_RUNNING;CHECK(!l4_bootstrap_rollback(active,sequence,1000));CHECK(slots[1].present && deletes==3);slots[1].state=SERVICE_STOPPED;CHECK(l4_bootstrap_rollback(active,sequence,1000));
    reset();CHECK(l4_bootstrap_save(active,&plan,&sequence));CHECK(l4_bootstrap_register(active,sequence));wcscpy_s(slots[2].account,256,L"ChangedByOperator");CHECK(!l4_bootstrap_rollback(active,sequence,1000));CHECK(slots[2].present && deletes==3);
    /* Marked deletion is not completion while another SCM handle remains open. */
    reset();CHECK(l4_bootstrap_save(active,&plan,&sequence));CHECK(l4_bootstrap_register(active,sequence));slots[3].hold=true;CHECK(!l4_bootstrap_rollback(active,sequence,100));CHECK(GetLastError()==ERROR_TIMEOUT);CHECK(slots[3].marked && slots[3].present);CHECK(tick==200);
    slots[3].hold=false;CHECK(reopen(&layout));CHECK(l4_bootstrap_rollback(active,sequence,1000));CHECK(!slots[3].present);
    reset();CHECK(l4_bootstrap_save(active,&plan,&sequence));CHECK(l4_bootstrap_register(active,sequence));slots[3].hold=true;release_at=tick+100;CHECK(l4_bootstrap_rollback(active,sequence,1000));CHECK(tick==200);
    /* Missing DELETE_DONE after actual disappearance recovers as absence. */
    reset();CHECK(l4_bootstrap_save(active,&plan,&sequence));CHECK(l4_bootstrap_register(active,sequence));poison_delete=true;CHECK(!l4_bootstrap_rollback(active,sequence,1000));CHECK(active->poisoned);CHECK(!slots[3].present);CHECK(reopen(&layout));CHECK(l4_bootstrap_rollback(active,sequence,1000));
    /* Unknown RPC return after an SCM side effect must not duplicate creation. */
    reset();CHECK(l4_bootstrap_save(active,&plan,&sequence));unknown_create=true;
    CHECK(!l4_bootstrap_register(active,sequence));CHECK(creates==1);CHECK(reopen(&layout));CHECK(l4_bootstrap_register(active,sequence));CHECK(creates==4);CHECK(l4_bootstrap_rollback(active,sequence,1000));
    reset();CHECK(l4_bootstrap_save(active,&plan,&sequence));CHECK(l4_bootstrap_register(active,sequence));unknown_delete=true;
    CHECK(!l4_bootstrap_rollback(active,sequence,1000));CHECK(deletes==4);CHECK(reopen(&layout));CHECK(l4_bootstrap_rollback(active,sequence,1000));CHECK(deletes==4);
    reset();CHECK(l4_bootstrap_save(active,&plan,&sequence));CHECK(l4_bootstrap_register(active,sequence));failed_delete=true;
    CHECK(!l4_bootstrap_rollback(active,sequence,1000));CHECK(slots[3].present&&!slots[3].marked);CHECK(deletes==3);CHECK(l4_bootstrap_rollback(active,sequence,1000));CHECK(deletes==4);
    /* Failure at every bundle position: no service starts, resume/rollback is bounded. */
    for(int i=0;i<4;i++){
        reset();CHECK(l4_bootstrap_save(active,&plan,&sequence));failed_create=i;CHECK(!l4_bootstrap_register(active,sequence));CHECK(creates==(unsigned)i);
        failed_create=-1;CHECK(reopen(&layout));CHECK(l4_bootstrap_register(active,sequence));CHECK(creates==4);CHECK(l4_bootstrap_rollback(active,sequence,1000));
    }
    for(unsigned field=0;field<3;field++){
        reset();CHECK(l4_bootstrap_save(active,&plan,&sequence));CHECK(l4_bootstrap_register(active,sequence));
        if(field==0)wcscpy_s(slots[2].display,128,L"ChangedByOperator");
        else if(field==1)slots[2].image[0]=L'x';else slots[2].start=SERVICE_AUTO_START;
        CHECK(!l4_bootstrap_rollback(active,sequence,1000));CHECK(slots[2].present && deletes==3);
    }
    /* Tampered EXE prevents creation at that step but never obstructs owned rollback. */
    reset();CHECK(l4_bootstrap_save(active,&plan,&sequence));CHECK(l4_layout_component(&layout,L"l4con",L"l4con.exe",source));CHECK(write_file(source,"bad",3));
    CHECK(!l4_bootstrap_register(active,sequence));CHECK(creates==2);CHECK(l4_bootstrap_rollback(active,sequence,1000));CHECK(deletes==2);CHECK(write_file(source,"exe",3));
    L4BootstrapPlan bad=plan;bad.commands[0][0]=L'x';CHECK(!l4_bootstrap_save(active,&bad,NULL));bad=plan;bad.sizes[0]=0;CHECK(!l4_bootstrap_save(active,&bad,NULL));CHECK(!l4_bootstrap_register(active,sequence+1));
    CHECK(!l4_bootstrap_rollback(active,sequence,0));CHECK(!l4_bootstrap_rollback(active,sequence,300001));
    /* Activation uses real journal/hash pins and modeled SCM/processes/probes. */
    L4BootstrapChecks gates={application_probe,communication_barrier,slots,{1000,300000,1000,1000},1000};
    reset();CHECK(l4_bootstrap_save(active,&plan,&sequence));CHECK(!l4_bootstrap_activate(active,sequence,&gates));CHECK(starts==0);
    CHECK(l4_bootstrap_register(active,sequence));CHECK(!l4_bootstrap_activate(active,sequence,NULL));
    L4BootstrapChecks invalid=gates;invalid.barrier=NULL;CHECK(!l4_bootstrap_activate(active,sequence,&invalid));
    invalid=gates;invalid.service_ms[1]=299999;CHECK(!l4_bootstrap_activate(active,sequence,&invalid));CHECK(starts==0);
    CHECK(l4_bootstrap_activate(active,sequence,&gates));CHECK(starts==4 && probes==4 && barriers==2);
    {const unsigned expected[]={0,1,2,4,3,4};CHECK(event_count==6 && !memcmp(events,expected,sizeof(expected)));}
    CHECK(reopen(&layout));CHECK(l4_bootstrap_activate(active,sequence,&gates));CHECK(starts==4 && probes==8 && barriers==4);
    CHECK(!l4_bootstrap_rollback(active,sequence,1000));CHECK(deletes==0);
    for(unsigned i=0;i<4;i++){slots[i].state=SERVICE_STOPPED;slots[i].dead=true;}CHECK(l4_bootstrap_abort(active,sequence,1000));
    CHECK(!l4_bootstrap_activate(active,sequence,&gates));CHECK(GetLastError()==ERROR_CANCELLED);
    for(int i=0;i<4;i++){
        reset();CHECK(l4_bootstrap_save(active,&plan,&sequence));CHECK(l4_bootstrap_register(active,sequence));failed_probe=i;
        CHECK(!l4_bootstrap_activate(active,sequence,&gates));CHECK(starts==(unsigned)i+1);CHECK(GetLastError()==ERROR_NOT_READY);
        failed_probe=-1;CHECK(reopen(&layout));CHECK(l4_bootstrap_activate(active,sequence,&gates));CHECK(starts==4);
        reset();CHECK(l4_bootstrap_save(active,&plan,&sequence));CHECK(l4_bootstrap_register(active,sequence));failed_start=i;
        CHECK(!l4_bootstrap_activate(active,sequence,&gates));CHECK(GetLastError()==ERROR_SERVICE_LOGON_FAILED);CHECK(probes==(unsigned)i);
        failed_start=-1;CHECK(reopen(&layout));CHECK(l4_bootstrap_activate(active,sequence,&gates));
    }
    for(int i=1;i<=2;i++){
        reset();CHECK(l4_bootstrap_save(active,&plan,&sequence));CHECK(l4_bootstrap_register(active,sequence));failed_barrier=i;
        CHECK(!l4_bootstrap_activate(active,sequence,&gates));CHECK(starts==(i==1?3u:4u));CHECK(GetLastError()==ERROR_NOT_READY);
        failed_barrier=-1;CHECK(reopen(&layout));CHECK(l4_bootstrap_activate(active,sequence,&gates));CHECK(starts==4);
    }
    reset();CHECK(l4_bootstrap_save(active,&plan,&sequence));CHECK(l4_bootstrap_register(active,sequence));unknown_start=true;
    CHECK(!l4_bootstrap_activate(active,sequence,&gates));CHECK(starts==1 && probes==0);CHECK(reopen(&layout));CHECK(l4_bootstrap_activate(active,sequence,&gates));CHECK(starts==4);
    reset();CHECK(l4_bootstrap_save(active,&plan,&sequence));CHECK(l4_bootstrap_register(active,sequence));poison_start=true;
    CHECK(!l4_bootstrap_activate(active,sequence,&gates));CHECK(active->poisoned && starts==1);CHECK(reopen(&layout));CHECK(l4_bootstrap_activate(active,sequence,&gates));CHECK(starts==4);
    reset();CHECK(l4_bootstrap_save(active,&plan,&sequence));CHECK(l4_bootstrap_register(active,sequence));poison_probe=true;
    CHECK(!l4_bootstrap_activate(active,sequence,&gates));CHECK(active->poisoned && starts==1 && probes==1);
    CHECK(reopen(&layout));CHECK(l4_bootstrap_activate(active,sequence,&gates));CHECK(starts==4 && probes==5 && barriers==2);
    reset();CHECK(l4_bootstrap_save(active,&plan,&sequence));CHECK(l4_bootstrap_register(active,sequence));start_exits=true;
    CHECK(!l4_bootstrap_activate(active,sequence,&gates));CHECK(GetLastError()==ERROR_SERVICE_NOT_ACTIVE && starts==1 && probes==0);
    for(int i=1;i<=2;i++){
        reset();CHECK(l4_bootstrap_save(active,&plan,&sequence));CHECK(l4_bootstrap_register(active,sequence));dead_barrier=i;
        CHECK(!l4_bootstrap_activate(active,sequence,&gates));CHECK(GetLastError()==ERROR_PROCESS_ABORTED && starts==(i==1?3u:4u));
    }
    reset();CHECK(l4_bootstrap_save(active,&plan,&sequence));CHECK(l4_bootstrap_register(active,sequence));start_delay=950;
    CHECK(l4_bootstrap_activate(active,sequence,&gates));CHECK(starts==4);
    reset();CHECK(l4_bootstrap_save(active,&plan,&sequence));CHECK(l4_bootstrap_register(active,sequence));start_delay=1000;
    CHECK(!l4_bootstrap_activate(active,sequence,&gates));CHECK(GetLastError()==ERROR_TIMEOUT);CHECK(starts==1 && probes==0);
    start_delay=0;CHECK(reopen(&layout));CHECK(l4_bootstrap_activate(active,sequence,&gates));CHECK(starts==4);
    reset();CHECK(l4_bootstrap_save(active,&plan,&sequence));CHECK(l4_bootstrap_register(active,sequence));probe_delay=1000;
    CHECK(!l4_bootstrap_activate(active,sequence,&gates));CHECK(GetLastError()==ERROR_TIMEOUT && starts==1);
    reset();CHECK(l4_bootstrap_save(active,&plan,&sequence));CHECK(l4_bootstrap_register(active,sequence));barrier_delay=1000;
    CHECK(!l4_bootstrap_activate(active,sequence,&gates));CHECK(GetLastError()==ERROR_TIMEOUT && starts==3);
    /* Full five-minute broker window, separately from every other stage. */
    reset();CHECK(l4_bootstrap_save(active,&plan,&sequence));CHECK(l4_bootstrap_register(active,sequence));broker_delay=299950;
    CHECK(l4_bootstrap_activate(active,sequence,&gates));CHECK(starts==4 && tick==300050);
    reset();CHECK(l4_bootstrap_save(active,&plan,&sequence));CHECK(l4_bootstrap_register(active,sequence));broker_delay=300000;
    CHECK(!l4_bootstrap_activate(active,sequence,&gates));CHECK(GetLastError()==ERROR_TIMEOUT && starts==2 && probes==1 && barriers==0);
    /* Existing process without original start intent cannot be adopted. */
    reset();CHECK(l4_bootstrap_save(active,&plan,&sequence));CHECK(l4_bootstrap_register(active,sequence));slots[0].state=SERVICE_RUNNING;
    CHECK(!l4_bootstrap_activate(active,sequence,&gates));CHECK(starts==0);
    for(unsigned mode=0;mode<4;mode++){
        reset();CHECK(l4_bootstrap_save(active,&plan,&sequence));CHECK(l4_bootstrap_register(active,sequence));
        wrong_image=mode==0;wrong_user=mode==1;kill_previous=mode==2;swap_pid=mode==3;
        CHECK(!l4_bootstrap_activate(active,sequence,&gates));CHECK(starts<=2 && barriers==0);
    }
    reset();CHECK(l4_bootstrap_save(active,&plan,&sequence));CHECK(l4_bootstrap_register(active,sequence));slots[3].image[0]=L'x';
    CHECK(!l4_bootstrap_activate(active,sequence,&gates));CHECK(starts==0);
    reset();CHECK(l4_bootstrap_save(active,&plan,&sequence));CHECK(l4_bootstrap_register(active,sequence));
    CHECK(l4_layout_component(&layout,L"l4superv",L"l4superv.exe",source));CHECK(write_file(source,"bad",3));
    CHECK(!l4_bootstrap_activate(active,sequence,&gates));CHECK(starts==0);CHECK(write_file(source,"exe",3));
    /* Owned managed abort waits process exit separately from SCM STOPPED. */
    reset();CHECK(l4_bootstrap_save(active,&plan,&sequence));CHECK(l4_bootstrap_register(active,sequence));CHECK(l4_bootstrap_activate(active,sequence,&gates));
    stop_delay=50;exit_delay=100;CHECK(l4_bootstrap_abort(active,sequence,1000));CHECK(stops==4 && deletes==4 && tick==500);
    for(unsigned i=0;i<4;i++){CHECK(stop_order[i]==3-i);CHECK(!slots[i].present);}
    CHECK(l4_bootstrap_abort(active,sequence,1000));CHECK(stops==4 && deletes==4);CHECK(!l4_bootstrap_activate(active,sequence,&gates));
    /* A live supervisor blocks any lower stop or delete. Timeout can resume. */
    reset();CHECK(l4_bootstrap_save(active,&plan,&sequence));CHECK(l4_bootstrap_register(active,sequence));CHECK(l4_bootstrap_activate(active,sequence,&gates));
    exit_delay=200;CHECK(!l4_bootstrap_abort(active,sequence,100));CHECK(GetLastError()==ERROR_TIMEOUT);CHECK(stops==1 && deletes==0 && slots[3].state==SERVICE_STOPPED && !slots[3].dead);
    CHECK(reopen(&layout));exit_delay=0;tick+=100;CHECK(l4_bootstrap_abort(active,sequence,1000));CHECK(stops==4 && deletes==4);
    /* Unknown STOP result / missing STOP_DONE survives reopening. */
    for(unsigned mode=0;mode<2;mode++){
        reset();CHECK(l4_bootstrap_save(active,&plan,&sequence));CHECK(l4_bootstrap_register(active,sequence));CHECK(l4_bootstrap_activate(active,sequence,&gates));
        unknown_stop=mode==0;poison_stop=mode==1;CHECK(!l4_bootstrap_abort(active,sequence,1000));CHECK(stops==1 && deletes==0);
        CHECK(reopen(&layout));CHECK(l4_bootstrap_abort(active,sequence,1000));CHECK(stops==4 && deletes==4);
    }
    /* Every stop position can fail; no deletion until the entire bundle exits. */
    for(int i=3;i>=0;i--){
        reset();CHECK(l4_bootstrap_save(active,&plan,&sequence));CHECK(l4_bootstrap_register(active,sequence));CHECK(l4_bootstrap_activate(active,sequence,&gates));failed_stop=i;
        CHECK(!l4_bootstrap_abort(active,sequence,1000));CHECK(GetLastError()==ERROR_ACCESS_DENIED && stops==(unsigned)(3-i) && deletes==0);
        CHECK(reopen(&layout));failed_stop=-1;CHECK(l4_bootstrap_abort(active,sequence,1000));CHECK(stops==4 && deletes==4);
    }
    /* Reused PID while stopped proves the OLD process gone; never control replacement. */
    reset();CHECK(l4_bootstrap_save(active,&plan,&sequence));CHECK(l4_bootstrap_register(active,sequence));CHECK(l4_bootstrap_activate(active,sequence,&gates));
    slots[3].state=SERVICE_STOPPED;++slots[3].birth;CHECK(l4_bootstrap_abort(active,sequence,1000));CHECK(stops==3 && deletes==4);
    /* Replaced running identity, wrong account/image or foreign service is refused. */
    for(unsigned mode=0;mode<4;mode++){
        reset();CHECK(l4_bootstrap_save(active,&plan,&sequence));CHECK(l4_bootstrap_register(active,sequence));CHECK(l4_bootstrap_activate(active,sequence,&gates));
        if(mode==0)++slots[3].birth;else if(mode==1)slots[3].account[0]=L'x';else if(mode==2)wrong_image=true;else slots[3].display[0]=L'x';
        CHECK(!l4_bootstrap_abort(active,sequence,1000));CHECK(stops==0 && deletes==0);
    }
    /* No identity is fabricated from STOPPED PID; started-but-unobserved is uncertain. */
    reset();CHECK(l4_bootstrap_save(active,&plan,&sequence));CHECK(l4_bootstrap_register(active,sequence));start_exits=true;CHECK(!l4_bootstrap_activate(active,sequence,&gates));
    CHECK(!l4_bootstrap_abort(active,sequence,1000));CHECK(GetLastError()==ERROR_NOT_READY && stops==0 && deletes==0);
    /* Missing PID means original process is gone; access denial is never absence. */
    reset();CHECK(l4_bootstrap_save(active,&plan,&sequence));CHECK(l4_bootstrap_register(active,sequence));CHECK(l4_bootstrap_activate(active,sequence,&gates));
    slots[3].state=SERVICE_STOPPED;slots[3].pid=0;CHECK(l4_bootstrap_abort(active,sequence,1000));CHECK(stops==3 && deletes==4);
    reset();CHECK(l4_bootstrap_save(active,&plan,&sequence));CHECK(l4_bootstrap_register(active,sequence));CHECK(l4_bootstrap_activate(active,sequence,&gates));
    slots[3].state=SERVICE_STOPPED;deny_process=true;CHECK(!l4_bootstrap_abort(active,sequence,1000));CHECK(GetLastError()==ERROR_ACCESS_DENIED && stops==0 && deletes==0);
    /* An external pending stop without our durable intent cannot be adopted. */
    reset();CHECK(l4_bootstrap_save(active,&plan,&sequence));CHECK(l4_bootstrap_register(active,sequence));CHECK(l4_bootstrap_activate(active,sequence,&gates));
    slots[3].state=SERVICE_STOP_PENDING;CHECK(!l4_bootstrap_abort(active,sequence,1000));CHECK(GetLastError()==ERROR_NOT_READY && stops==0 && deletes==0);
    /* Observe both FILETIME halves through replay, then use ONE budget for all stops. */
    reset();CHECK(l4_bootstrap_save(active,&plan,&sequence));CHECK(l4_bootstrap_register(active,sequence));CHECK(l4_bootstrap_activate(active,sequence,&gates));
    L4BootstrapHistory captured;CHECK(l4_bootstrap_history(active,sequence,&captured));for(unsigned i=0;i<4;i++)CHECK(captured.pids[i]==slots[i].pid && captured.births[i]==slots[i].birth);
    stop_delay=75;exit_delay=75;CHECK(!l4_bootstrap_abort(active,sequence,150));CHECK(GetLastError()==ERROR_TIMEOUT && tick==250 && stops==2 && deletes==0);
    /* Stop identity/phase corruption refuses before any stop/delete action. */
    reset();CHECK(l4_bootstrap_save(active,&plan,&sequence));CHECK(l4_bootstrap_register(active,sequence));
    BYTE invalid_stop[12];l4_store_u64(invalid_stop,sequence);l4_store_u32(invalid_stop+8,3);
    CHECK(l4_journal_append(active,L4_RECORD_BOOTSTRAP_STOP_DONE,invalid_stop,sizeof(invalid_stop),NULL));CHECK(!l4_bootstrap_abort(active,sequence,1000));CHECK(stops==0 && deletes==0);
    /* Unknown start result can be captured while still RUNNING, then aborted. */
    reset();CHECK(l4_bootstrap_save(active,&plan,&sequence));CHECK(l4_bootstrap_register(active,sequence));unknown_start=true;CHECK(!l4_bootstrap_activate(active,sequence,&gates));
    CHECK(reopen(&layout));CHECK(l4_bootstrap_abort(active,sequence,1000));CHECK(stops==1 && deletes==4);
    CHECK(!l4_bootstrap_abort(active,sequence,0));CHECK(!l4_bootstrap_abort(active,sequence,300001));
    /* Final fresh installation commit never trusts prior READY/barrier records. */
    reset();CHECK(l4_bootstrap_save(active,&plan,&sequence));CHECK(l4_bootstrap_register(active,sequence));
    CHECK(!l4_bootstrap_commit(active,sequence,&gates,1000) && !type_changes);
    CHECK(l4_bootstrap_activate(active,sequence,&gates));CHECK(l4_bootstrap_commit(active,sequence,&gates,1000));
    CHECK(slots[3].crash_profile && !slots[3].noncrash);
    CHECK(type_changes==4 && probes==12 && barriers==4);for(unsigned i=0;i<4;i++)CHECK(slots[i].start==SERVICE_AUTO_START);
    CHECK(reopen(&layout));CHECK(l4_bootstrap_commit(active,sequence,&gates,1000));CHECK(type_changes==4 && barriers==5);
    CHECK(!l4_bootstrap_abort(active,sequence,1000) && !stops && !deletes);CHECK(!l4_bootstrap_rollback(active,sequence,1000));
    failed_barrier=(int)barriers+1;CHECK(!l4_bootstrap_commit(active,sequence,&gates,1000) && type_changes==4);
    failed_barrier=-1;slots[0].start=SERVICE_DEMAND_START;CHECK(!l4_bootstrap_commit(active,sequence,&gates,1000));
    reset();L4BootstrapPlan mixed=plan;mixed.start_types[2]=SERVICE_DEMAND_START;
    CHECK(l4_bootstrap_save(active,&mixed,&sequence));CHECK(l4_bootstrap_register(active,sequence));CHECK(l4_bootstrap_activate(active,sequence,&gates));
    CHECK(l4_bootstrap_commit(active,sequence,&gates,1000));for(unsigned i=0;i<4;i++)CHECK(slots[i].start==mixed.start_types[i]);
    /* Every partial SCM change has intent; interrupted commit can abort to manual. */
    for(int i=0;i<4;i++){
        reset();CHECK(l4_bootstrap_save(active,&plan,&sequence));CHECK(l4_bootstrap_register(active,sequence));CHECK(l4_bootstrap_activate(active,sequence,&gates));
        failed_type=i;CHECK(!l4_bootstrap_commit(active,sequence,&gates,1000) && type_changes==(unsigned)i);
        CHECK(reopen(&layout));CHECK(!l4_bootstrap_activate(active,sequence,&gates));failed_type=-1;
        CHECK(l4_bootstrap_abort(active,sequence,1000));CHECK(stops==4 && deletes==4 && type_changes==(unsigned)(i*2));
    }
    reset();CHECK(l4_bootstrap_save(active,&plan,&sequence));CHECK(l4_bootstrap_register(active,sequence));CHECK(l4_bootstrap_activate(active,sequence,&gates));
    unknown_type=true;CHECK(!l4_bootstrap_commit(active,sequence,&gates,1000) && type_changes==1);
    CHECK(reopen(&layout));CHECK(l4_bootstrap_commit(active,sequence,&gates,1000));CHECK(type_changes==5);
    /* Journal failure after mutation cannot authorize a later unmanaged start. */
    reset();CHECK(l4_bootstrap_save(active,&plan,&sequence));CHECK(l4_bootstrap_register(active,sequence));CHECK(l4_bootstrap_activate(active,sequence,&gates));
    poison_type=true;CHECK(!l4_bootstrap_commit(active,sequence,&gates,1000) && type_changes==1);
    CHECK(reopen(&layout));CHECK(l4_bootstrap_abort(active,sequence,1000));CHECK(type_changes==2 && stops==4 && deletes==4);
    /* Failed final fresh barrier leaves commit incomplete and abortable. */
    reset();CHECK(l4_bootstrap_save(active,&plan,&sequence));CHECK(l4_bootstrap_register(active,sequence));CHECK(l4_bootstrap_activate(active,sequence,&gates));
    failed_barrier=4;CHECK(!l4_bootstrap_commit(active,sequence,&gates,1000));CHECK(type_changes==4);
    CHECK(reopen(&layout));failed_barrier=-1;CHECK(l4_bootstrap_abort(active,sequence,1000));CHECK(type_changes==8);
    /* Changed original epoch or foreign config refuses before start-type writes. */
    for(unsigned fault=0;fault<3;fault++){
        reset();CHECK(l4_bootstrap_save(active,&plan,&sequence));CHECK(l4_bootstrap_register(active,sequence));CHECK(l4_bootstrap_activate(active,sequence,&gates));
        if(!fault)++slots[3].birth;else if(fault==1)slots[3].display[0]=L'x';else slots[3].start=SERVICE_AUTO_START;
        CHECK(!l4_bootstrap_commit(active,sequence,&gates,1000) && !type_changes);
    }
    /* One total commit budget covers all callbacks, not four renewed budgets. */
    reset();CHECK(l4_bootstrap_save(active,&plan,&sequence));CHECK(l4_bootstrap_register(active,sequence));CHECK(l4_bootstrap_activate(active,sequence,&gates));
    probe_delay=30;CHECK(!l4_bootstrap_commit(active,sequence,&gates,90) && !type_changes);
    CHECK(!l4_bootstrap_commit(active,sequence,&gates,0));CHECK(!l4_bootstrap_commit(active,sequence,&gates,600001));
    reset();CHECK(l4_bootstrap_save(active,&plan,&sequence));CHECK(l4_bootstrap_register(active,sequence));CHECK(l4_bootstrap_activate(active,sequence,&gates));
    type_delay=100;CHECK(!l4_bootstrap_commit(active,sequence,&gates,90) && type_changes==1);
    CHECK(reopen(&layout));type_delay=0;CHECK(l4_bootstrap_abort(active,sequence,1000));CHECK(type_changes==2);
    /* Corrupt completion/intent cannot fabricate successful start-type ownership. */
    for(unsigned fault=0;fault<2;fault++){
        reset();CHECK(l4_bootstrap_save(active,&plan,&sequence));CHECK(l4_bootstrap_register(active,sequence));CHECK(l4_bootstrap_activate(active,sequence,&gates));
        CHECK(fault?record(active,L4_RECORD_BOOTSTRAP_START_TYPE_INTENT,sequence,0):whole_record(active,L4_RECORD_BOOTSTRAP_COMMIT_DONE,sequence));
        CHECK(!l4_bootstrap_commit(active,sequence,&gates,1000) && !type_changes);
    }
    /* Local installation has no certificate/channel gates and records no READY.
     * Strict activation/commit cannot reuse this typed deployment terminal. */
    reset();CHECK(l4_bootstrap_save(active,&plan,&sequence));CHECK(l4_bootstrap_register(active,sequence));
    CHECK(slots[3].crash_profile && !slots[3].noncrash);
    slots[3].crash_profile=false;CHECK(!l4_bootstrap_local_commit(active,sequence,1000));slots[3].crash_profile=true;
    CHECK(l4_bootstrap_local_commit(active,sequence,1000));CHECK(!starts && !probes && !barriers && type_changes==4);
    bool local=false,committed=false,aborted=false;CHECK(l4_bootstrap_local_terminal(active,sequence,&local) && local);
    CHECK(l4_bootstrap_terminal(active,sequence,&committed,&aborted) && committed && !aborted);
    CHECK(slots[3].crash_profile && !slots[3].noncrash);
    CHECK(reopen(&layout));CHECK(!l4_bootstrap_activate(active,sequence,&gates));CHECK(!l4_bootstrap_commit(active,sequence,&gates,1000));
    CHECK(!l4_bootstrap_abort(active,sequence,1000) && !deletes);CHECK(l4_bootstrap_local_start(active,sequence,1000));CHECK(starts==4 && !probes && !barriers);
    /* A failed activation request after durable installation does not undo it. */
    reset();CHECK(l4_bootstrap_save(active,&plan,&sequence));CHECK(l4_bootstrap_register(active,sequence));CHECK(l4_bootstrap_local_commit(active,sequence,1000));
    failed_start=0;CHECK(!l4_bootstrap_local_start(active,sequence,1000));CHECK(l4_bootstrap_local_terminal(active,sequence,&local) && local);CHECK(!deletes);
    for(int i=0;i<4;i++){
        reset();CHECK(l4_bootstrap_save(active,&plan,&sequence));CHECK(l4_bootstrap_register(active,sequence));failed_type=i;
        CHECK(!l4_bootstrap_local_commit(active,sequence,1000));CHECK(!starts && !probes && !barriers);
        CHECK(reopen(&layout));failed_type=-1;CHECK(l4_bootstrap_abort(active,sequence,1000));CHECK(deletes==4 && !starts);
    }
    /* Existing foreign/running SCM state must never be adopted as local install. */
    for(unsigned fault=0;fault<2;fault++){
        reset();CHECK(l4_bootstrap_save(active,&plan,&sequence));CHECK(l4_bootstrap_register(active,sequence));
        if(fault)slots[3].display[0]=L'x';else slots[3].state=SERVICE_RUNNING;
        CHECK(!l4_bootstrap_local_commit(active,sequence,1000) && !type_changes);
    }
    /* Checksummed but invalid typed schema/phase cannot trigger SCM effects. */
    reset();CHECK(l4_bootstrap_save(active,&plan,&sequence));BYTE* bytes=NULL;DWORD length=0;ULONGLONG malformed=0;
    CHECK(l4_store_find_record(active,L4_RECORD_BOOTSTRAP_PLAN,sequence,&bytes,&length));
    if(bytes){l4_store_u32(bytes,2);CHECK(l4_journal_append(active,L4_RECORD_BOOTSTRAP_PLAN,bytes,length,&malformed));free(bytes);CHECK(!l4_bootstrap_register(active,malformed));CHECK(creates==0);}
    BYTE invalid_phase[12];l4_store_u64(invalid_phase,sequence);l4_store_u32(invalid_phase+8,0);
    CHECK(l4_journal_append(active,L4_RECORD_BOOTSTRAP_CREATE_DONE,invalid_phase,sizeof(invalid_phase),NULL));CHECK(!l4_bootstrap_register(active,sequence));CHECK(creates==0);
    l4_journal_close(active);active=NULL;reset();cleanup(root);CHECK(GetFileAttributesW(root)==INVALID_FILE_ATTRIBUTES);
    printf("bootstrap registration/recovery/activation/commit/abort: %u checks, %u failures; all SCM mutations mocked\n",checks,failures);return failures?1:0;
}
