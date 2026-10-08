/* SCM responses modeled; process/token/epoch/privilege/thread operations real.
 * Never reads/writes live SCM or fabricates a successful SYSTEM identity. */
#include "../service_token.h"
#include "../child_probe.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks,failures;static unsigned mode;
static bool restoration_fault;
#define CHECK(x) do {++checks;if(!(x)){++failures;printf("FAIL token %u %s (%lu)\n",__LINE__,#x,GetLastError());}}while(0)
static SC_HANDLE WINAPI fake_manager(LPCWSTR machine,LPCWSTR database,DWORD access) {(void)machine;(void)database;(void)access;return (SC_HANDLE)1;}
static SC_HANDLE WINAPI fake_service(SC_HANDLE manager,LPCWSTR name,DWORD access) {(void)manager;(void)name;(void)access;return (SC_HANDLE)2;}
static BOOL WINAPI fake_close(SC_HANDLE service){(void)service;return TRUE;}
static BOOL WINAPI fake_config(SC_HANDLE service,LPQUERY_SERVICE_CONFIGW config,DWORD size,LPDWORD needed) {
    (void)service;*needed=sizeof(*config);if(!config || size<sizeof(*config)){SetLastError(ERROR_INSUFFICIENT_BUFFER);return FALSE;}
    ZeroMemory(config,sizeof(*config));config->dwServiceType=mode==1?SERVICE_WIN32_SHARE_PROCESS:SERVICE_WIN32_OWN_PROCESS;
    config->dwStartType=mode==2?SERVICE_DEMAND_START:SERVICE_AUTO_START;
    config->lpBinaryPathName=mode==3?L"changed":L"\"C:\\fixture\\leo4proxy.exe\" --service";
    config->lpServiceStartName=mode==4?L"LocalService":L"LocalSystem";return TRUE;
}
static BOOL WINAPI fake_status(SC_HANDLE service,SC_STATUS_TYPE type,LPBYTE bytes,DWORD size,LPDWORD needed) {
    (void)service;(void)type;(void)size;*needed=sizeof(SERVICE_STATUS_PROCESS);SERVICE_STATUS_PROCESS* status=(SERVICE_STATUS_PROCESS*)bytes;
    ZeroMemory(status,sizeof(*status));status->dwCurrentState=mode==5?SERVICE_STOPPED:SERVICE_RUNNING;
    status->dwProcessId=mode==6?0:GetCurrentProcessId()+(mode==7?1:0);return TRUE;
}
#define OpenSCManagerW fake_manager
#define OpenServiceW fake_service
#define CloseServiceHandle fake_close
#define QueryServiceConfigW fake_config
#define QueryServiceStatusEx fake_status
static BOOL WINAPI fault_set_thread(PHANDLE thread,HANDLE token) {
    if(restoration_fault && !token){SetLastError(ERROR_ACCESS_DENIED);return FALSE;}return SetThreadToken(thread,token);
}
#define SetThreadToken fault_set_thread
#include "../service_token.c"
static BYTE* privileges(HANDLE token,DWORD* size) {
    *size=0;GetTokenInformation(token,TokenPrivileges,NULL,0,size);BYTE* result=malloc(*size);
    CHECK(result && GetTokenInformation(token,TokenPrivileges,result,*size,size));return result;
}
static bool fault_exit(HANDLE process,DWORD pid,ULONGLONG deadline,void* context) {
    (void)pid;DWORD* code=context;ULONGLONG now=GetTickCount64();
    if(now<deadline && WaitForSingleObject(process,(DWORD)(deadline-now))==WAIT_OBJECT_0)GetExitCodeProcess(process,code);
    return false;
}
int main(int argc,char** argv) {
    if(argc==2 && !strcmp(argv[1],"--restore-fault")) {
        Scope scope={0};if(!scope_begin(NULL,NULL,0,&scope))return 7;restoration_fault=true;scope_end(&scope,NULL);return 8;
    }
    L4ServiceInventory expected={0};expected.installed=true;expected.start_type=SERVICE_AUTO_START;
    wcscpy_s(expected.account,256,L"LocalSystem");wcscpy_s(expected.image_path,2048,L"\"C:\\fixture\\leo4proxy.exe\" --service");
    L4ServiceToken* token=(L4ServiceToken*)1;CHECK(!l4_service_token_capture(NULL,&token));CHECK(!token);
    CHECK(!l4_service_token_capture(&expected,NULL));CHECK(!l4_service_token_verify(NULL));l4_service_token_close(NULL);
    L4ServiceInventory changed=expected;changed.start_type=SERVICE_DISABLED;CHECK(!l4_service_token_capture(&changed,&token));CHECK(!token);
    wcscpy_s(changed.account,256,L"LocalService");CHECK(!l4_service_token_capture(&changed,&token));CHECK(!token);
    for(mode=1;mode<=6;mode++){CHECK(!l4_service_token_capture(&expected,&token));CHECK(!token);}mode=0;
    HANDLE primary=NULL;CHECK(OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY|TOKEN_DUPLICATE,&primary));
    DWORD before_size,after_size;BYTE* before=privileges(primary,&before_size);
    HANDLE saved=NULL;CHECK(DuplicateTokenEx(primary,TOKEN_QUERY|TOKEN_DUPLICATE|TOKEN_IMPERSONATE,NULL,SecurityImpersonation,TokenImpersonation,&saved));
    CHECK(SetThreadToken(NULL,saved));TOKEN_STATISTICS original,after;DWORD size;CHECK(GetTokenInformation(saved,TokenStatistics,&original,sizeof(original),&size));
    BYTE user[sizeof(TOKEN_USER)+SECURITY_MAX_SID_SIZE];DWORD session=99;CHECK(GetTokenInformation(primary,TokenUser,user,sizeof(user),&size));
    CHECK(GetTokenInformation(primary,TokenSessionId,&session,sizeof(session),&size));
    bool system=IsWellKnownSid(((TOKEN_USER*)user)->User.Sid,WinLocalSystemSid) && session==0;
    bool captured=l4_service_token_capture(&expected,&token);CHECK(captured==system);l4_service_token_close(token);token=NULL;
    HANDLE restored=NULL;CHECK(OpenThreadToken(GetCurrentThread(),TOKEN_QUERY,TRUE,&restored));
    CHECK(GetTokenInformation(restored,TokenStatistics,&after,sizeof(after),&size));CHECK(after.TokenId.LowPart==original.TokenId.LowPart && after.TokenId.HighPart==original.TokenId.HighPart);
    CloseHandle(restored);CHECK(SetThreadToken(NULL,NULL));CloseHandle(saved);
    BYTE* final=privileges(primary,&after_size);CHECK(before_size==after_size && !memcmp(before,final,before_size));free(before);free(final);
    L4ServiceToken held={0};held.expected=expected;held.pid=GetCurrentProcessId();held.primary=primary;
    held.process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,held.pid);CHECK(held.process!=NULL);
    FILETIME exit,kernel,time;CHECK(GetProcessTimes(held.process,&held.created,&exit,&kernel,&time));
    mode=7;CHECK(!l4_service_token_verify(&held));mode=0;held.created.dwLowDateTime^=1;CHECK(!l4_service_token_verify(&held));
    PROCESS_INFORMATION child;CHECK(!l4_service_token_create(&held,L"C:\\fixture.exe",L"C:\\",L"fixture",&child));CHECK(!child.hProcess && !child.hThread);
    CloseHandle(held.process);CloseHandle(primary);
    wchar_t exe[MAX_PATH],directory[MAX_PATH],command[512];CHECK(GetModuleFileNameW(NULL,exe,MAX_PATH));
    wcscpy_s(directory,MAX_PATH,exe);wchar_t* slash=wcsrchr(directory,L'\\');if(!slash)return 1;*slash=0;
    swprintf_s(command,512,L"\"%ls\" --restore-fault",exe);DWORD code=STILL_ACTIVE;
    CHECK(!l4_child_probe(exe,directory,command,3000,fault_exit,&code));CHECK(code==ERROR_CANNOT_IMPERSONATE);
    printf("service token: %u passed, %u failed; modeled SCM, real tokens/epoch/restoration\n",checks-failures,failures);return failures?1:0;
}
