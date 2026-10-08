#include "remote_entry.h"
#include "acceptance_local.h"
#include "../../l4common/journal_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct {L4Layout layout;wchar_t operation[37],service[80],version[32],updater_version[32];const char* arch;const SetupRemoteEngine* engine;} RemoteEntry;
static RemoteEntry entry;static SERVICE_STATUS_HANDLE status_handle;static volatile LONG cancelled;
static bool uuid(const wchar_t* id){if(!id || wcslen(id)!=36)return false;bool any=false;for(unsigned i=0;i<36;i++){
    if(i==8 || i==13 || i==18 || i==23){if(id[i]!=L'-')return false;}else if(!((id[i]>=L'0' && id[i]<=L'9') || (id[i]>=L'a' && id[i]<=L'f')))return false;else if(id[i]!=L'0')any=true;}return any;}
static void report(DWORD state,DWORD error){SERVICE_STATUS status={0};status.dwServiceType=SERVICE_WIN32_OWN_PROCESS;status.dwCurrentState=state;
    status.dwControlsAccepted=state==SERVICE_RUNNING?SERVICE_ACCEPT_STOP:0;if(state==SERVICE_STOPPED && error){status.dwWin32ExitCode=ERROR_SERVICE_SPECIFIC_ERROR;status.dwServiceSpecificExitCode=error;}
    if(status_handle)SetServiceStatus(status_handle,&status);
}
static DWORD WINAPI control(DWORD code,DWORD type,void* data,void* context){(void)type;(void)data;(void)context;
    if(code==SERVICE_CONTROL_STOP){InterlockedExchange(&cancelled,1);report(SERVICE_STOP_PENDING,0);}return NO_ERROR;}
#define REQUIRE(call) do{SetLastError(ERROR_SUCCESS);if(!(call)){result=GetLastError()?GetLastError():ERROR_NOT_READY;goto done;}}while(0)
static DWORD execute(void){
    /* No persisted ACK/acceptance when actual implementation is absent. */
    if(!entry.engine || !entry.engine->preflight || !entry.engine->execute)return ERROR_CALL_NOT_IMPLEMENTED;
    L4Journal* journal=NULL;SetupInstalledSource* source=NULL;SetupAcceptancePin* acceptance=NULL;L4RemoteHost* fence=NULL;L4RemoteHostImage image={0};L4RemoteRequest request={0};DWORD result=ERROR_NOT_READY;
    REQUIRE(l4_remote_host_self(&entry.layout,entry.operation,entry.arch));REQUIRE(l4_journal_open(&entry.layout,entry.operation,false,&journal));
    if(journal->sequence!=1){result=ERROR_INVALID_STATE;goto done;}REQUIRE(l4_remote_request_load(journal,&request));
    if(request.target!=L4_REMOTE_SUITE){result=ERROR_NOT_SUPPORTED;goto done;}
    REQUIRE(setup_installed_source_open(journal,&source));REQUIRE(setup_installed_source_verify(source));
    const char* source_arch=setup_installed_source_arch(source);const char* version=setup_installed_source_version(source);wchar_t source_version[32];
    if(!source_arch || strcmp(source_arch,entry.arch) || !version || !MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,version,-1,source_version,32) || wcscmp(source_version,entry.version)){result=ERROR_REVISION_MISMATCH;goto done;}
    REQUIRE(l4_remote_host_pin_self(&entry.layout,entry.operation,entry.arch,&fence,&image));
    const char* updater=setup_installed_source_updater_version(source);wchar_t updater_version[32];
    if(!updater || strcmp(updater,image.updater_version) || !MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,updater,-1,updater_version,32) || wcscmp(updater_version,entry.updater_version)){result=ERROR_REVISION_MISMATCH;goto done;}
    const SetupRootManifest* root=setup_installed_source_updater_root(source);const SetupRootAsset* installer=setup_root_installer(root);
    if(!installer || image.size!=installer->size || memcmp(image.sha256,installer->sha256,32)){result=ERROR_CRC;goto done;}
    REQUIRE(setup_signed_executable(image.file,image.path,setup_root_publisher(root)));
    if(InterlockedCompareExchange(&cancelled,0,0)){result=ERROR_CANCELLED;goto done;}
    REQUIRE(setup_acceptance_open_optional(journal,&acceptance));if(acceptance)REQUIRE(setup_acceptance_bind(acceptance,journal,source));
    REQUIRE(entry.engine->preflight(journal,source,&request));REQUIRE(setup_installed_source_verify(source));REQUIRE(l4_remote_host_self(&entry.layout,entry.operation,entry.arch));
    if(InterlockedCompareExchange(&cancelled,0,0)){result=ERROR_CANCELLED;goto done;}
    L4RemoteHostReceipt receipt;REQUIRE(l4_remote_host_ack(journal,entry.arch,&receipt));
    if(acceptance)REQUIRE(setup_acceptance_route(acceptance,journal,source));
    result=entry.engine->execute(&journal,source,&request,&cancelled);
done:
    setup_acceptance_close(acceptance);l4_remote_host_close(fence);setup_installed_source_close(source);l4_journal_close(journal);return result;
}
static void WINAPI service_main(DWORD argc,wchar_t** argv){(void)argc;(void)argv;status_handle=RegisterServiceCtrlHandlerExW(entry.service,control,NULL);if(!status_handle)return;
    report(SERVICE_RUNNING,0);DWORD result=execute();
    /* Mark only this exact owned transient host, never a suite service. Retiring
     * cannot replace durable operation results, which belong to the engine. */
    if(l4_remote_host_self(&entry.layout,entry.operation,entry.arch))l4_remote_host_retire(&entry.layout,entry.operation,entry.arch);
    report(SERVICE_STOPPED,result);
}
bool setup_remote_entry(int argc,wchar_t** argv,const SetupRemoteEngine* engine,DWORD* result){
    bool selected=false;for(int i=1;i<argc;i++)if(!wcsncmp(argv[i],L"--remote-",9))selected=true;if(!selected)return false;
    if(!result)return true;*result=ERROR_INVALID_PARAMETER;RemoteEntry parsed={0};parsed.arch="x86";parsed.engine=engine;
    if(argc!=10 || wcscmp(argv[1],L"--remote-controller"))return true;unsigned seen=0;
    for(int i=2;i<argc;i+=2){unsigned flag=0;bool ok=false;
        if(!wcscmp(argv[i],L"--source-version")){flag=1;ok=wcslen(argv[i+1])>0 && wcslen(argv[i+1])<32;if(ok)wcscpy_s(parsed.version,32,argv[i+1]);}
        else if(!wcscmp(argv[i],L"--updater-version")){flag=8;ok=wcslen(argv[i+1])>0 && wcslen(argv[i+1])<32;if(ok)wcscpy_s(parsed.updater_version,32,argv[i+1]);}
        else if(!wcscmp(argv[i],L"--operation")){flag=2;ok=uuid(argv[i+1]);if(ok)wcscpy_s(parsed.operation,37,argv[i+1]);}
        else if(!wcscmp(argv[i],L"--arch")){flag=4;ok=!wcscmp(argv[i+1],L"x86") || !wcscmp(argv[i+1],L"x64");if(ok)parsed.arch=!wcscmp(argv[i+1],L"x86")?"x86":"x64";}
        if(!ok || (seen&flag))return true;seen|=flag;
    }
    L4Layout updater_layout;
    if(seen!=15 || !l4_layout_resolve(&parsed.layout,parsed.version) || !l4_layout_resolve(&updater_layout,parsed.updater_version))return true;
    wchar_t candidate[MAX_PATH],module[MAX_PATH];
    if(swprintf_s(candidate,MAX_PATH,L"%ls\\setup\\%ls\\l4setup.exe",updater_layout.binaries,parsed.updater_version)<0 ||
       !GetModuleFileNameW(NULL,module,MAX_PATH) || _wcsicmp(module,candidate)){*result=ERROR_ACCESS_DENIED;return true;}
    wchar_t executable[MAX_PATH],command[1024];if(!l4_remote_host_identity(&parsed.layout,parsed.operation,parsed.arch,parsed.service,executable,command)){*result=GetLastError();return true;}
    /* Refuse a developer/interactive invocation; no elevation/service creation. */
    wchar_t self[MAX_PATH];if(!GetModuleFileNameW(NULL,self,MAX_PATH) || _wcsicmp(self,executable)){*result=ERROR_ACCESS_DENIED;return true;}
    entry=parsed;SERVICE_TABLE_ENTRYW table[]={{entry.service,service_main},{NULL,NULL}};*result=StartServiceCtrlDispatcherW(table)?0:GetLastError();return true;
}
