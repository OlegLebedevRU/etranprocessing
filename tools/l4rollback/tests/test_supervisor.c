#include "../src/rollback.h"
#include "../../l4common/probe_ipc.h"
#include <stdio.h>
#include <string.h>
#include <objbase.h>
static unsigned checks,failures,stops,changes,starts,configs,probes;
#define CHECK(x) do{++checks;if(!(x)){++failures;printf("FAIL supervisor %u: %s (%lu)\n",__LINE__,#x,GetLastError());}}while(0)
static L4RecoveryPlan fixture_plan;static L4UpdateState fixture_state;static ULONGLONG tick;static wchar_t fixture_image[2048];
static DWORD stage,fixture_pid;static bool config_bad,account_bad,token_bad,probe_bad,stop_hang,process_hang,original_alive,original_reused,state_drift,wrong_image,start_fail;
static SC_HANDLE WINAPI mock_manager(LPCWSTR a,LPCWSTR b,DWORD access){CHECK(!a&&!b&&access==SC_MANAGER_CONNECT);return (SC_HANDLE)1;}
static SC_HANDLE WINAPI open_service(SC_HANDLE h,LPCWSTR name,DWORD access){CHECK(h==(SC_HANDLE)1 && !wcscmp(name,L"L4Superv") && access==(SERVICE_QUERY_CONFIG|SERVICE_QUERY_STATUS|SERVICE_STOP|SERVICE_CHANGE_CONFIG|SERVICE_START));return (SC_HANDLE)2;}
static BOOL WINAPI close_service(SC_HANDLE h){CHECK(h==(SC_HANDLE)1 || h==(SC_HANDLE)2);return TRUE;}
static BOOL WINAPI query_config(SC_HANDLE h,QUERY_SERVICE_CONFIGW* result,DWORD count,DWORD* needed){CHECK(h==(SC_HANDLE)2);*needed=sizeof(*result);if(!result || count<sizeof(*result)){SetLastError(ERROR_INSUFFICIENT_BUFFER);return FALSE;}
    memset(result,0,sizeof(*result));result->dwServiceType=SERVICE_WIN32_OWN_PROCESS;result->dwStartType=SERVICE_AUTO_START;result->lpServiceStartName=account_bad?L"Other":L"LocalSystem";result->lpBinaryPathName=fixture_image;return TRUE;}
static BOOL WINAPI query_status(SC_HANDLE h,SC_STATUS_TYPE type,BYTE* result,DWORD count,DWORD* needed){CHECK(h==(SC_HANDLE)2 && type==SC_STATUS_PROCESS_INFO && count>=sizeof(SERVICE_STATUS_PROCESS));
    SERVICE_STATUS_PROCESS* s=(SERVICE_STATUS_PROCESS*)result;memset(s,0,sizeof(*s));s->dwCurrentState=stage;s->dwProcessId=fixture_pid;*needed=sizeof(*s);return TRUE;}
static BOOL WINAPI control(SC_HANDLE h,DWORD command,SERVICE_STATUS* result){CHECK(h==(SC_HANDLE)2 && command==SERVICE_CONTROL_STOP);++stops;memset(result,0,sizeof(*result));stage=stop_hang?SERVICE_STOP_PENDING:SERVICE_STOPPED;if(!stop_hang)fixture_pid=0;return TRUE;}
static BOOL WINAPI change(SC_HANDLE h,DWORD a,DWORD b,DWORD c,LPCWSTR path,LPCWSTR group,DWORD* tag,LPCWSTR dependencies,LPCWSTR user,LPCWSTR password,LPCWSTR display){
    CHECK(h==(SC_HANDLE)2 && a==SERVICE_NO_CHANGE && b==SERVICE_NO_CHANGE && c==SERVICE_NO_CHANGE && !group&&!tag&&!dependencies&&!user&&!password&&!display && !wcscmp(path,fixture_plan.before) && stage==SERVICE_STOPPED);++changes;wcscpy_s(fixture_image,2048,path);return TRUE;}
static BOOL WINAPI start(SC_HANDLE h,DWORD argc,LPCWSTR* argv){CHECK(h==(SC_HANDLE)2 && !argc&&!argv);++starts;if(start_fail){SetLastError(ERROR_SERVICE_LOGON_FAILED);return FALSE;}stage=SERVICE_RUNNING;fixture_pid=33;return TRUE;}
static HANDLE WINAPI open_process(DWORD rights,BOOL inherit,DWORD id){CHECK(rights==(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE) && !inherit && (id==11 || id==22 || id==33));return (HANDLE)(ULONG_PTR)id;}
static BOOL WINAPI times(HANDLE h,FILETIME* created,FILETIME* exit,FILETIME* kernel,FILETIME* user){(void)exit;(void)kernel;(void)user;ULONGLONG value=(ULONGLONG)(ULONG_PTR)h*10+(original_reused&&h==(HANDLE)11?1:0);memcpy(created,&value,8);return TRUE;}
static BOOL WINAPI process_path(HANDLE h,DWORD flags,LPWSTR result,DWORD* count){CHECK(!flags);const wchar_t* command=h==(HANDLE)22?fixture_plan.after:fixture_plan.before;const wchar_t* end=wcschr(command+1,L'"');DWORD length=(DWORD)(end-command-1);CHECK(*count>length);wcsncpy_s(result,*count,command+1,length);if(wrong_image && h==(HANDLE)22)wcscpy_s(result,*count,L"C:\\foreign.exe");*count=(DWORD)wcslen(result);return TRUE;}
static BOOL WINAPI open_token(HANDLE h,DWORD rights,HANDLE* token){CHECK(rights==TOKEN_QUERY);*token=(HANDLE)((ULONG_PTR)h+1000);return TRUE;}
static BOOL WINAPI token_info(HANDLE h,TOKEN_INFORMATION_CLASS type,void* result,DWORD count,DWORD* needed){(void)h;CHECK(type==TokenUser && count>=64);TOKEN_USER* user=result;user->User.Sid=(BYTE*)result+sizeof(*user);DWORD size=count-(DWORD)sizeof(*user);*needed=64;return CreateWellKnownSid(token_bad?WinBuiltinUsersSid:WinLocalSystemSid,NULL,user->User.Sid,&size);}
static BOOL WINAPI close_handle(HANDLE h){CHECK(h);return TRUE;}
static DWORD WINAPI wait(HANDLE h,DWORD timeout){bool alive=h==(HANDLE)11?original_alive:(h==(HANDLE)33 || (h==(HANDLE)22 && ((stage!=SERVICE_STOPPED && stage!=SERVICE_RUNNING) || fixture_pid==22 || process_hang)));
    if(alive && timeout){tick+=timeout;return WAIT_TIMEOUT;}return alive?WAIT_TIMEOUT:WAIT_OBJECT_0;}
static VOID WINAPI sleep_ms(DWORD ms){tick+=ms;}
static bool read_state(const L4Layout* roots,L4UpdateState* result){(void)roots;*result=fixture_state;if(state_drift && probes)result->generation++;return true;}
static bool probe(const wchar_t* component,DWORD expected,DWORD mode,DWORD timeout){CHECK(!wcscmp(component,L"superv") && expected==33 && !mode && timeout);++probes;tick+=5;return !probe_bad;}
DWORD rollback_remaining(ULONGLONG deadline){return tick<deadline?(DWORD)(deadline-tick):0;}
bool rollback_image(const L4Layout* roots,const L4RecoveryPlan* p,HANDLE* h,L4FileFence* fence,wchar_t output[MAX_PATH]){(void)roots;CHECK(p==&fixture_plan);*h=(HANDLE)900;memset(fence,0,sizeof(*fence));wcscpy_s(output,MAX_PATH,L"old");return true;}
bool rollback_config(const L4Layout* roots,const L4RecoveryPlan* p){(void)roots;CHECK(p==&fixture_plan && stage==SERVICE_STOPPED);++configs;return !config_bad;}
static void unpin(L4FileFence* fence){CHECK(!fence->count);}
bool rollback_exclusive(const L4RecoveryPlan* p,DWORD allowed,ULONGLONG deadline){CHECK(p==&fixture_plan && (allowed==0 || allowed==33) && deadline>tick);return true;}
#define OpenSCManagerW mock_manager
#define OpenServiceW open_service
#define CloseServiceHandle close_service
#define QueryServiceConfigW query_config
#define QueryServiceStatusEx query_status
#define ControlService control
#define ChangeServiceConfigW change
#define StartServiceW start
#define OpenProcess open_process
#define GetProcessTimes times
#define QueryFullProcessImageNameW process_path
#define OpenProcessToken open_token
#define GetTokenInformation token_info
#define CloseHandle close_handle
#define WaitForSingleObject wait
#define Sleep sleep_ms
#define l4_update_state_read read_state
#define l4_probe_call probe
#define l4_store_unpin unpin
#include "../src/supervisor.c"
static void reset(void){tick=0;stops=changes=starts=configs=probes=0;stage=SERVICE_RUNNING;fixture_pid=22;wcscpy_s(fixture_image,2048,fixture_plan.after);
    config_bad=account_bad=token_bad=probe_bad=stop_hang=process_hang=original_alive=original_reused=state_drift=wrong_image=start_fail=false;fixture_state.generation=1;}
int main(void){
    memset(&fixture_plan,0,sizeof(fixture_plan));fixture_plan.operation.Data1=0x17730000;fixture_plan.operation.Data3=0x4000;fixture_plan.operation.Data4[0]=0x80;fixture_plan.operation.Data4[7]=1;
    fixture_plan.sequence=64;fixture_plan.start_type=SERVICE_AUTO_START;fixture_plan.supervisor_pid=11;fixture_plan.supervisor_created.dwLowDateTime=110;
    wcscpy_s(fixture_plan.before,2048,L"\"C:\\Programs\\releases\\1.13.2\\l4superv\\l4superv.exe\"");wcscpy_s(fixture_plan.after,2048,L"\"C:\\Programs\\releases\\1.13.3\\l4superv\\l4superv.exe\"");
    strcpy_s(fixture_state.owner,40,"17730000-0000-4000-8000-000000000001");fixture_state.window=2;fixture_state.plan_sequence=64;fixture_state.deadline_utc=1;L4Layout roots={0};
    reset();CHECK(rollback_supervisor(&roots,&fixture_plan,100));CHECK(stops==1 && changes==1 && starts==1 && configs==1 && probes==1); /* Expired marker is not extended. */
    reset();account_bad=true;CHECK(!rollback_supervisor(&roots,&fixture_plan,100));CHECK(!stops&&!changes);
    reset();token_bad=true;CHECK(!rollback_supervisor(&roots,&fixture_plan,100));CHECK(!stops&&!changes);
    reset();wrong_image=true;CHECK(!rollback_supervisor(&roots,&fixture_plan,100));CHECK(!stops&&!changes);
    reset();stop_hang=true;CHECK(!rollback_supervisor(&roots,&fixture_plan,100));CHECK(tick<=100 && !configs&&!changes&&!starts);
    reset();process_hang=true;CHECK(!rollback_supervisor(&roots,&fixture_plan,100));CHECK(tick<=100 && !configs&&!changes);
    reset();original_alive=true;CHECK(!rollback_supervisor(&roots,&fixture_plan,100));CHECK(!configs&&!changes);
    reset();original_alive=true;original_reused=true;CHECK(rollback_supervisor(&roots,&fixture_plan,100));CHECK(changes==1);
    reset();config_bad=true;CHECK(!rollback_supervisor(&roots,&fixture_plan,100));CHECK(!changes&&!starts);
    reset();start_fail=true;CHECK(!rollback_supervisor(&roots,&fixture_plan,100));CHECK(changes==1 && starts==1 && !probes);
    reset();probe_bad=true;CHECK(!rollback_supervisor(&roots,&fixture_plan,100));CHECK(probes==1);
    reset();state_drift=true;CHECK(!rollback_supervisor(&roots,&fixture_plan,100));CHECK(probes==1);
    reset();fixture_state.plan_sequence++;CHECK(!rollback_supervisor(&roots,&fixture_plan,100));CHECK(!stops&&!changes);fixture_state.plan_sequence--;
    reset();CHECK(!rollback_supervisor(&roots,&fixture_plan,0));CHECK(!stops&&!changes);
    printf("Helper fixed SCM/epoch/config/health: %u passed, %u failed; ALL SCM/process actions modeled, no live mutations\n",checks-failures,failures);return failures?1:0;
}
