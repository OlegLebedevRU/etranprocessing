#include "service_token.h"
#include <userenv.h>
#include <stdlib.h>
#include <wchar.h>

struct L4ServiceToken {L4ServiceInventory expected;HANDLE process,primary;DWORD pid;FILETIME created;LUID authentication;};
static bool fail(DWORD code){SetLastError(code);return false;}
typedef struct {HANDLE saved;bool active;} Scope;
/* Never adjust caller process/service privileges: duplicate to this thread only. */
static bool scope_begin(HANDLE source,const wchar_t* const* names,unsigned count,Scope* scope) {
    scope->saved=NULL;scope->active=false;HANDLE own=NULL,temporary=NULL;
    if(!OpenThreadToken(GetCurrentThread(),TOKEN_QUERY|TOKEN_DUPLICATE|TOKEN_IMPERSONATE,TRUE,&scope->saved) && GetLastError()!=ERROR_NO_TOKEN)return false;
    if(!source){if(scope->saved)source=scope->saved;else {if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY|TOKEN_DUPLICATE,&own))return false;source=own;}}
    bool ok=DuplicateTokenEx(source,TOKEN_QUERY|TOKEN_ADJUST_PRIVILEGES|TOKEN_IMPERSONATE,NULL,SecurityImpersonation,TokenImpersonation,&temporary)!=0;
    TOKEN_PRIVILEGES privileges={0};privileges.PrivilegeCount=1;
    for(unsigned i=0;ok && i<count;i++) {
        ok=LookupPrivilegeValueW(NULL,names[i],&privileges.Privileges[0].Luid)!=0;privileges.Privileges[0].Attributes=SE_PRIVILEGE_ENABLED;
        if(ok){SetLastError(ERROR_SUCCESS);ok=AdjustTokenPrivileges(temporary,FALSE,&privileges,0,NULL,NULL) && GetLastError()==ERROR_SUCCESS;}
    }
    if(ok)ok=SetThreadToken(NULL,temporary)!=0;DWORD code=GetLastError();scope->active=ok;
    if(temporary)CloseHandle(temporary);if(own)CloseHandle(own);
    if(!ok && scope->saved){CloseHandle(scope->saved);scope->saved=NULL;}return ok?true:fail(code);
}
static bool scope_end(Scope* scope,PROCESS_INFORMATION* child) {
    if(!scope->active)return true;
    bool ok=SetThreadToken(NULL,scope->saved)!=0;DWORD code=GetLastError();
    if(scope->saved)CloseHandle(scope->saved);scope->saved=NULL;scope->active=false;
    /* Restoration failure must never return to update code as SYSTEM. The
     * source is untouched; cancel only this suspended child, then fail fast. */
    if(!ok){if(child && child->hProcess)TerminateProcess(child->hProcess,ERROR_CANCELLED);
        if(!TerminateProcess(GetCurrentProcess(),ERROR_CANNOT_IMPERSONATE))RaiseFailFastException(NULL,NULL,0);}
    return ok?true:fail(code);
}
static bool identity(HANDLE token,LUID* authentication) {
    BYTE user[sizeof(TOKEN_USER)+SECURITY_MAX_SID_SIZE];DWORD size,session;TOKEN_TYPE type;TOKEN_STATISTICS stats;
    if(!GetTokenInformation(token,TokenUser,user,sizeof(user),&size) ||
       !GetTokenInformation(token,TokenType,&type,sizeof(type),&size) ||
       !GetTokenInformation(token,TokenSessionId,&session,sizeof(session),&size) ||
       !GetTokenInformation(token,TokenStatistics,&stats,sizeof(stats),&size))return false;
    if(!IsWellKnownSid(((TOKEN_USER*)user)->User.Sid,WinLocalSystemSid) || type!=TokenPrimary || session!=0)return fail(ERROR_ACCESS_DENIED);
    *authentication=stats.AuthenticationId;return true;
}
static bool current(const L4ServiceInventory* expected,DWORD* pid) {
    SC_HANDLE manager=OpenSCManagerW(NULL,NULL,SC_MANAGER_CONNECT);if(!manager)return false;
    SC_HANDLE service=OpenServiceW(manager,L"Leo4Proxy",SERVICE_QUERY_CONFIG|SERVICE_QUERY_STATUS);DWORD code=GetLastError();CloseServiceHandle(manager);
    if(!service)return fail(code);DWORD size=0;
    QueryServiceConfigW(service,NULL,0,&size);bool ok=GetLastError()==ERROR_INSUFFICIENT_BUFFER && size>=sizeof(QUERY_SERVICE_CONFIGW) && size<=65536;
    QUERY_SERVICE_CONFIGW* config=ok?malloc(size):NULL;if(ok && !config){ok=false;SetLastError(ERROR_NOT_ENOUGH_MEMORY);}
    if(ok)ok=QueryServiceConfigW(service,config,size,&size)!=0;
    if(ok && (config->dwServiceType!=SERVICE_WIN32_OWN_PROCESS || config->dwStartType!=expected->start_type ||
       !config->lpServiceStartName || _wcsicmp(config->lpServiceStartName,L"LocalSystem") ||
       !config->lpBinaryPathName || wcscmp(config->lpBinaryPathName,expected->image_path))){ok=false;SetLastError(ERROR_REVISION_MISMATCH);}
    SERVICE_STATUS_PROCESS status={0};if(ok)ok=QueryServiceStatusEx(service,SC_STATUS_PROCESS_INFO,(BYTE*)&status,sizeof(status),&size)!=0;
    if(ok && (status.dwCurrentState!=SERVICE_RUNNING || !status.dwProcessId)){ok=false;SetLastError(ERROR_SERVICE_NOT_ACTIVE);}
    if(ok)*pid=status.dwProcessId;code=GetLastError();free(config);CloseServiceHandle(service);return ok?true:fail(code?code:ERROR_INVALID_DATA);
}
void l4_service_token_close(L4ServiceToken* token){if(!token)return;if(token->primary)CloseHandle(token->primary);if(token->process)CloseHandle(token->process);free(token);}
bool l4_service_token_verify(const L4ServiceToken* token) {
    if(!token)return fail(ERROR_INVALID_PARAMETER);DWORD pid;FILETIME created,exit,kernel,user;
    if(!current(&token->expected,&pid) || pid!=token->pid || WaitForSingleObject(token->process,0)!=WAIT_TIMEOUT ||
       !GetProcessTimes(token->process,&created,&exit,&kernel,&user) || CompareFileTime(&created,&token->created))return fail(ERROR_RETRY);
    LUID auth;if(!identity(token->primary,&auth))return false;
    if(auth.LowPart!=token->authentication.LowPart || auth.HighPart!=token->authentication.HighPart)return fail(ERROR_ACCESS_DENIED);
    return true;
}
bool l4_service_token_capture(const L4ServiceInventory* expected,L4ServiceToken** result) {
    if(result)*result=NULL;
    if(!expected || !result || !expected->installed || !wmemchr(expected->account,0,_countof(expected->account)) ||
       !wmemchr(expected->image_path,0,_countof(expected->image_path)) || !*expected->image_path || _wcsicmp(expected->account,L"LocalSystem") ||
       (expected->start_type!=SERVICE_AUTO_START && expected->start_type!=SERVICE_DEMAND_START))return fail(ERROR_INVALID_PARAMETER);
    L4ServiceToken* token=calloc(1,sizeof(*token));if(!token)return fail(ERROR_NOT_ENOUGH_MEMORY);token->expected=*expected;
    Scope scope={0};const wchar_t* privileges[]={L"SeDebugPrivilege"};HANDLE original=NULL;FILETIME exit,kernel,user;
    bool ok=current(expected,&token->pid) && scope_begin(NULL,privileges,1,&scope);
    if(ok){token->process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,token->pid);ok=token->process!=NULL;}
    if(ok)ok=GetProcessTimes(token->process,&token->created,&exit,&kernel,&user)!=0 &&
        OpenProcessToken(token->process,TOKEN_QUERY|TOKEN_DUPLICATE,&original)!=0 && identity(original,&token->authentication);
    if(ok)ok=DuplicateTokenEx(original,TOKEN_QUERY|TOKEN_DUPLICATE|TOKEN_ASSIGN_PRIMARY,NULL,SecurityImpersonation,TokenPrimary,&token->primary)!=0;
    DWORD code=GetLastError();if(original)CloseHandle(original);
    if(!scope_end(&scope,NULL)){ok=false;code=GetLastError();}
    if(ok && !l4_service_token_verify(token)){ok=false;code=GetLastError();}
    if(!ok){l4_service_token_close(token);return fail(code?code:ERROR_INVALID_DATA);}*result=token;return true;
}
bool l4_service_token_create(const L4ServiceToken* token,const wchar_t* exe,const wchar_t* directory,wchar_t* command,PROCESS_INFORMATION* child) {
    if(child)ZeroMemory(child,sizeof(*child));
    if(!token || !exe || !*exe || !directory || !*directory || !command || !*command || !child)return fail(ERROR_INVALID_PARAMETER);
    if(!l4_service_token_verify(token))return false;
    LPVOID environment=NULL;Scope scope={0};const wchar_t* privileges[]={L"SeAssignPrimaryTokenPrivilege",L"SeIncreaseQuotaPrivilege"};
    bool ok=CreateEnvironmentBlock(&environment,token->primary,FALSE)!=0;
    if(ok)ok=scope_begin(token->primary,privileges,2,&scope);
    STARTUPINFOW startup={0};startup.cb=sizeof(startup);startup.lpDesktop=L"";
    if(ok)ok=CreateProcessAsUserW(token->primary,exe,command,NULL,NULL,FALSE,CREATE_SUSPENDED|CREATE_NO_WINDOW|CREATE_UNICODE_ENVIRONMENT,
                                environment,directory,&startup,child)!=0;
    DWORD code=GetLastError();if(!scope_end(&scope,child)){ok=false;code=GetLastError();}
    if(environment)DestroyEnvironmentBlock(environment);
    if(ok && !l4_service_token_verify(token)){ok=false;code=GetLastError();}
    HANDLE primary=NULL;LUID auth={0};
    if(ok){ok=OpenProcessToken(child->hProcess,TOKEN_QUERY,&primary)!=0 && identity(primary,&auth);if(!ok)code=GetLastError();}
    if(ok && (auth.LowPart!=token->authentication.LowPart || auth.HighPart!=token->authentication.HighPart)){ok=false;code=ERROR_ACCESS_DENIED;}
    if(primary)CloseHandle(primary);
    if(!ok){if(child->hProcess){BOOL killed=TerminateProcess(child->hProcess,ERROR_CANCELLED);if(!killed || WaitForSingleObject(child->hProcess,5000)!=WAIT_OBJECT_0)code=ERROR_TIMEOUT;CloseHandle(child->hThread);CloseHandle(child->hProcess);ZeroMemory(child,sizeof(*child));}return fail(code?code:ERROR_INVALID_DATA);}
    return true;
}
