#include "install_entry.h"
#include "install_bundle.h"
#include "fresh_install.h"
#include "broker_environment.h"
#include "uac.h"
#include "cli.h"
#include "version.h"
#include "../../l4common/journal_internal.h"
#include "../../l4common/active_updater.h"
#include "../../l4pin/src/cert_discovery.h"
#include <wtsapi32.h>
#include <sddl.h>
#include <aclapi.h>
#include <objbase.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
enum {ACTION_VERIFY=1,ACTION_INSTALL=2};
typedef struct {unsigned mode;wchar_t version[64],operation[37],bundle[MAX_PATH],service[80],command[1024],host[MAX_PATH];const char* arch;L4Layout layout;} Entry;
static Entry worker_entry;static SERVICE_STATUS_HANDLE worker_status;static volatile LONG cancelled;
static bool fail(DWORD code){SetLastError(code);return false;}
static bool canonical_uuid(const wchar_t* id){if(!id || wcslen(id)!=36)return false;for(unsigned i=0;i<36;i++){
    if(i==8 || i==13 || i==18 || i==23){if(id[i]!=L'-')return false;}else if(!((id[i]>=L'0' && id[i]<=L'9') || (id[i]>=L'a' && id[i]<=L'f')))return false;}return true;}
static bool identity(HANDLE token,BYTE sid[SECURITY_MAX_SID_SIZE],DWORD* size){BYTE user[sizeof(TOKEN_USER)+SECURITY_MAX_SID_SIZE];DWORD n=0;
    if(!GetTokenInformation(token,TokenUser,user,sizeof(user),&n))return false;PSID s=((TOKEN_USER*)user)->User.Sid;*size=GetLengthSid(s);return *size<=SECURITY_MAX_SID_SIZE && CopySid(SECURITY_MAX_SID_SIZE,sid,s);}
static bool system_process(void){HANDLE thread=NULL,token=NULL;BYTE sid[SECURITY_MAX_SID_SIZE];DWORD size=0;
    if(OpenThreadToken(GetCurrentThread(),TOKEN_QUERY,TRUE,&thread)){CloseHandle(thread);return fail(ERROR_ACCESS_DENIED);}if(GetLastError()!=ERROR_NO_TOKEN)return false;
    bool ok=OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token) && identity(token,sid,&size) && IsWellKnownSid(sid,WinLocalSystemSid);if(token)CloseHandle(token);return ok?true:fail(ERROR_ACCESS_DENIED);}
static bool command(Entry* e,bool abort){
    if(swprintf_s(e->host,MAX_PATH,L"%ls\\setup\\%ls\\l4setup.exe",e->layout.binaries,e->version)<0 ||
       swprintf_s(e->service,_countof(e->service),L"L4SetupHost_%ls",e->operation)<0 ||
       swprintf_s(e->command,_countof(e->command),L"\"%ls\" %ls --fresh-version %ls --operation %ls --arch %hs",e->host,abort?L"--fresh-worker-abort":L"--fresh-worker",e->version,e->operation,e->arch)<0)return fail(ERROR_FILENAME_EXCED_RANGE);return true;
}
static bool services_absent(void){const wchar_t* names[]={L"Leo4Proxy",L"mosquitto",L"L4Con",L"L4Superv"};for(unsigned i=0;i<4;i++){L4ServiceInventory s;if(!l4_service_inventory(names[i],&s))return false;if(s.installed)return fail(ERROR_SERVICE_EXISTS);}return true;}
static bool request_path(L4Journal* j,wchar_t path[MAX_PATH]){return swprintf_s(path,MAX_PATH,L"%ls\\host.request",j->directory)>0;}
static bool request_write(L4Journal* j,const SetupInstallBundle* b,unsigned action){BYTE bytes[120]={0};HANDLE token=NULL;DWORD session=0,sid_size=0;
    bool ok=OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token) && identity(token,bytes+52,&sid_size) && ProcessIdToSessionId(GetCurrentProcessId(),&session) && session && WTSGetActiveConsoleSessionId()==session;
    if(token)CloseHandle(token);if(!ok)return fail(ERROR_ACCESS_DENIED);memcpy(bytes,"L4REQ001",8);l4_store_u32(bytes+8,action);l4_store_u32(bytes+12,session);
    const BYTE* root=setup_root_identity(setup_bundle_root(b));if(!root)return fail(ERROR_INVALID_DATA);memcpy(bytes+16,root,32);l4_store_u32(bytes+48,sid_size);
    wchar_t path[MAX_PATH];if(!request_path(j,path))return fail(ERROR_FILENAME_EXCED_RANGE);PSECURITY_DESCRIPTOR sd=NULL;
    if(!ConvertStringSecurityDescriptorToSecurityDescriptorW(L"O:BAD:P(A;;FA;;;SY)(A;;FA;;;BA)",SDDL_REVISION_1,&sd,NULL))return false;
    SECURITY_ATTRIBUTES sa={sizeof(sa),sd,FALSE};HANDLE previous=NULL;if(!l4_layout_owner_begin(&previous)){LocalFree(sd);return false;}
    HANDLE file=CreateFileW(path,GENERIC_WRITE|READ_CONTROL,FILE_SHARE_READ,&sa,CREATE_NEW,FILE_FLAG_OPEN_REPARSE_POINT,NULL);DWORD code=GetLastError();
    ok=file!=INVALID_HANDLE_VALUE && l4_store_write(file,bytes,sizeof(bytes)) && FlushFileBuffers(file);if(!ok)code=GetLastError();if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);
    if(!l4_layout_owner_end(previous)){ok=false;code=GetLastError();}LocalFree(sd);return ok?true:fail(code);
}
static bool request_read(L4Journal* j,BYTE bytes[120]){wchar_t path[MAX_PATH];if(!request_path(j,path))return false;
    HANDLE file=CreateFileW(path,GENERIC_READ|READ_CONTROL,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,NULL);if(file==INVALID_HANDLE_VALUE)return false;
    BY_HANDLE_FILE_INFORMATION info;LARGE_INTEGER size;DWORD read=0,sd_size=0;BYTE* sd=NULL;
    bool ok=GetFileInformationByHandle(file,&info) && info.nNumberOfLinks==1 && !(info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)) && GetFileSizeEx(file,&size) && size.QuadPart==120 &&
        l4_store_security(file,true,&sd,&sd_size) && ReadFile(file,bytes,120,&read,NULL) && read==120 && !memcmp(bytes,"L4REQ001",8);
    if(ok){DWORD n=l4_store_get32(bytes+48);ok=n>=8 && n<=SECURITY_MAX_SID_SIZE && IsValidSid(bytes+52) && GetLengthSid(bytes+52)==n &&
        (l4_store_get32(bytes+8)==ACTION_VERIFY || l4_store_get32(bytes+8)==ACTION_INSTALL) && l4_store_get32(bytes+12);}
    DWORD code=GetLastError();free(sd);CloseHandle(file);return ok?true:fail(code?code:ERROR_INVALID_DATA);
}
typedef struct {ULONGLONG receipt,bootstrap;} Saved;
static bool saved_record(DWORD kind,ULONGLONG sequence,const void* bytes,DWORD size,void* context){(void)bytes;(void)size;Saved* s=context;
    if(kind==85){if(s->receipt)return fail(ERROR_INVALID_DATA);s->receipt=sequence;}if(kind==L4_RECORD_BOOTSTRAP_PLAN){if(s->bootstrap)return fail(ERROR_INVALID_DATA);s->bootstrap=sequence;}return true;}
static bool token_actors(const BYTE request[120],L4AccessActors* actors){memset(actors,0,sizeof(*actors));DWORD session=l4_store_get32(request+12);HANDLE system=NULL,user=NULL;BYTE sid[SECURITY_MAX_SID_SIZE];DWORD size=0;
    if(!system_process() || !OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY|TOKEN_DUPLICATE,&system))return false;
    bool ok=DuplicateTokenEx(system,TOKEN_QUERY|TOKEN_DUPLICATE|TOKEN_IMPERSONATE,NULL,SecurityImpersonation,TokenImpersonation,&actors->proxy)!=0;
    if(ok)ok=WTSGetActiveConsoleSessionId()==session && WTSQueryUserToken(session,&user) && identity(user,sid,&size) && size==l4_store_get32(request+48) && EqualSid(sid,(PSID)(request+52));
    if(ok)ok=DuplicateTokenEx(user,TOKEN_QUERY|TOKEN_DUPLICATE|TOKEN_IMPERSONATE,NULL,SecurityImpersonation,TokenImpersonation,&actors->desktop)!=0;
    if(system)CloseHandle(system);if(user)CloseHandle(user);actors->broker=actors->console=actors->supervisor=actors->proxy;
    return ok?true:fail(ERROR_ACCESS_DENIED);
}
static DWORD execute(Entry* e){
    if(!system_process())return GetLastError();L4Journal* j=NULL;SetupInstallBundle* b=NULL;SetupManifest* manifest=NULL;L4CachedPackage* cached=NULL;SetupFreshInstall* fresh=NULL;L4AccessActors actors={0};BYTE request[120];Saved saved={0};DWORD result=ERROR_NOT_READY;
    if(!l4_journal_open(&e->layout,e->operation,false,&j) || !request_read(j,request) || !l4_journal_replay(j,saved_record,&saved))goto done;
    wchar_t inputs[MAX_PATH];if(swprintf_s(inputs,MAX_PATH,L"%ls\\inputs",j->directory)<0)goto done;char version[64];if(!WideCharToMultiByte(CP_UTF8,0,e->version,-1,version,sizeof(version),NULL,NULL))goto done;
    L4CatalogRelease selected={0};const L4CatalogRelease* original=NULL;
    if(e->mode==6){strcpy_s(selected.version,sizeof(selected.version),version);memcpy(selected.manifest_sha256,request+16,32);original=&selected;}
    if(!setup_bundle_open_local(inputs,version,e->arch,original,&b) || !setup_bundle_self(b,e->host) || memcmp(setup_root_identity(setup_bundle_root(b)),request+16,32))goto done;
    if(e->mode==6){
        if(!saved.receipt){result=j->sequence?ERROR_NOT_READY:ERROR_SUCCESS;goto done;}
        if(!setup_fresh_load_abort_local(j,saved.receipt,setup_bundle_root(b),&fresh))goto done;bool committed=false,aborted=false;
        if(!l4_bootstrap_terminal(j,saved.bootstrap,&committed,&aborted))goto done;
        if(committed){result=ERROR_ALREADY_EXISTS;goto done;}if(!setup_fresh_abort(j,fresh,300000))goto done;result=ERROR_SUCCESS;goto done;
    }
    if(j->sequence){result=ERROR_RETRY;goto done;}
    if(!setup_bundle_prepare_bootstrap(b,version) || !setup_bundle_check_bootstrap(b,j))goto done;
    if(!setup_bundle_manifest(b,&e->layout,&manifest) || !setup_bundle_cache(b,&e->layout,&cached) || !setup_manifest_prepare_policy(manifest,l4_package_path(cached),SETUP_ADMISSION_LOCAL_OFFLINE) || !setup_manifest_verify_policy(manifest,SETUP_ADMISSION_LOCAL_OFFLINE))goto done;
    cert_info cert;memset(&cert,0,sizeof(cert));cert_state state=cert_discover(NULL,&cert);
    bool enrolled=(state==CERT_VALID || state==CERT_EXPIRING) && cert.has_private_key && cert.sn[0] && cert.thumbprint_hex[0];
    if(!enrolled)memset(&cert,0,sizeof(cert));
    if(!token_actors(request,&actors))goto done;
    if(l4_store_get32(request+8)==ACTION_VERIFY){result=ERROR_SUCCESS;goto done;}
    if(InterlockedCompareExchange(&cancelled,0,0)){result=ERROR_CANCELLED;goto done;}
    if(!setup_bundle_install_bootstrap(b,j))goto done;
    CliOptions defaults;cli_init_defaults(&defaults);wchar_t proxy_args[512],identity_args[160]=L"";
    if(enrolled && swprintf_s(identity_args,_countof(identity_args),L" --cert-thumbprint %hs",cert.thumbprint_hex)<0)goto done;
    if(defaults.policy_bootstrap_ip[0]){unsigned a=0,b_ip=0,c=0,d=0;wchar_t extra=0;
        if(swscanf_s(defaults.policy_bootstrap_ip,L"%u.%u.%u.%u%lc",&a,&b_ip,&c,&d,&extra,1)!=4 || !a || a>=224 || b_ip>255 || c>255 || d>255){SetLastError(ERROR_INVALID_DATA);goto done;}
        if(swprintf_s(proxy_args,_countof(proxy_args),L"--service --rtp-tunnel%ls --policy-bootstrap-ip %ls",identity_args,defaults.policy_bootstrap_ip)<0)goto done;
    }else if(swprintf_s(proxy_args,_countof(proxy_args),L"--service --rtp-tunnel%ls",identity_args)<0)goto done;
    L4BootstrapProfile profiles[4]={{L"Leo4Proxy",L"LocalSystem",proxy_args,SERVICE_AUTO_START},{L"mosquitto",L"LocalSystem",L"run",SERVICE_AUTO_START},
        {L"L4Con",L"LocalSystem",L"--service",SERVICE_AUTO_START},{L"L4Superv",L"LocalSystem",L"",SERVICE_AUTO_START}};
    DWORD descriptor_size=0,signature_size=0;const void* descriptor=setup_bundle_descriptor(b,&descriptor_size);const BYTE* signature=setup_bundle_signature(b,&signature_size);
    if(!setup_fresh_prepare_broker_local(j,setup_bundle_root(b),descriptor,descriptor_size,signature,signature_size,l4_package_path(cached),profiles,&actors,cert.sn,NULL,0,&fresh))goto done;
    if(!setup_fresh_deploy_local(j,fresh,600000)){DWORD failure=GetLastError();bool committed=false,aborted=false;
        if(l4_bootstrap_terminal(j,setup_fresh_bootstrap_sequence(fresh),&committed,&aborted) && !committed){if(!setup_fresh_abort(j,fresh,300000))result=GetLastError();else result=failure?failure:ERROR_NOT_READY;}else result=failure?failure:ERROR_NOT_READY;goto done;}
    /* Fix the independent updater identity to this committed fresh origin.
     * Suite updates never select a new updater or recreate a missing anchor. */
    if(!l4_active_updater_initialize_fresh(j,e->arch)){result=GetLastError()?GetLastError():ERROR_NOT_READY;goto done;}
    /* Deployment is already durable. Missing identity/transport is enrollment or
     * activation work, never grounds for destructive installation rollback. */
    bool started=l4_bootstrap_local_start(j,setup_fresh_bootstrap_sequence(fresh),60000);
    printf("{\"deployment_committed\":true,\"communication_ready\":null,\"certificate_available\":%s,\"service_start_error\":%lu}\n",enrolled?"true":"false",started?0:GetLastError());
    result=ERROR_SUCCESS;
done:
    if(result==ERROR_NOT_READY && GetLastError())result=GetLastError();setup_fresh_free(fresh);setup_manifest_free(manifest);l4_package_close(cached);setup_bundle_free(b);if(actors.desktop)CloseHandle(actors.desktop);if(actors.proxy)CloseHandle(actors.proxy);l4_journal_close(j);return result;
}
static void report(DWORD state,DWORD result){SERVICE_STATUS s={0};s.dwServiceType=SERVICE_WIN32_OWN_PROCESS;s.dwCurrentState=state;s.dwControlsAccepted=state==SERVICE_RUNNING?SERVICE_ACCEPT_STOP:0;
    if(state==SERVICE_STOPPED && result){s.dwWin32ExitCode=ERROR_SERVICE_SPECIFIC_ERROR;s.dwServiceSpecificExitCode=result;}if(worker_status)SetServiceStatus(worker_status,&s);}
static DWORD WINAPI control(DWORD code,DWORD type,void* data,void* context){(void)type;(void)data;(void)context;if(code==SERVICE_CONTROL_STOP){InterlockedExchange(&cancelled,1);report(SERVICE_STOP_PENDING,0);}return NO_ERROR;}
static bool service_fingerprint(SC_HANDLE service,const Entry* e,bool alternative);
static bool worker_binding(void){SC_HANDLE scm=OpenSCManagerW(NULL,NULL,SC_MANAGER_CONNECT);if(!scm)return false;
    SC_HANDLE service=OpenServiceW(scm,worker_entry.service,SERVICE_QUERY_CONFIG|SERVICE_QUERY_STATUS);SERVICE_STATUS_PROCESS status={0};DWORD n=0;
    bool ok=service && service_fingerprint(service,&worker_entry,false) && QueryServiceStatusEx(service,SC_STATUS_PROCESS_INFO,(BYTE*)&status,sizeof(status),&n) && status.dwProcessId==GetCurrentProcessId();
    DWORD code=GetLastError();if(service)CloseServiceHandle(service);CloseServiceHandle(scm);return ok?true:fail(code?code:ERROR_ACCESS_DENIED);}
static void WINAPI service_main(DWORD argc,wchar_t** argv){(void)argc;(void)argv;worker_status=RegisterServiceCtrlHandlerExW(worker_entry.service,control,NULL);if(!worker_status)return;
    report(SERVICE_RUNNING,0);DWORD result=worker_binding()?execute(&worker_entry):GetLastError();report(SERVICE_STOPPED,result);
}
static bool service_fingerprint(SC_HANDLE service,const Entry* e,bool alternative){DWORD n=0;QueryServiceConfigW(service,NULL,0,&n);
    if(GetLastError()!=ERROR_INSUFFICIENT_BUFFER || n<sizeof(QUERY_SERVICE_CONFIGW) || n>65536)return fail(ERROR_INVALID_DATA);QUERY_SERVICE_CONFIGW* c=malloc(n);if(!c)return fail(ERROR_NOT_ENOUGH_MEMORY);
    bool ok=QueryServiceConfigW(service,c,n,&n)!=0;wchar_t owner[96];swprintf_s(owner,_countof(owner),L"L4Setup %ls",e->operation);Entry other=*e;if(alternative)command(&other,e->mode!=3);
    if(ok)ok=c->lpBinaryPathName && (!wcscmp(c->lpBinaryPathName,e->command) || (alternative && !wcscmp(c->lpBinaryPathName,other.command))) && c->lpDisplayName && !wcscmp(c->lpDisplayName,owner) &&
        c->lpServiceStartName && !_wcsicmp(c->lpServiceStartName,L"LocalSystem") && c->dwServiceType==SERVICE_WIN32_OWN_PROCESS && c->dwStartType==SERVICE_DEMAND_START && c->dwErrorControl==SERVICE_ERROR_NORMAL &&
        (!c->lpDependencies || !*c->lpDependencies) && (!c->lpLoadOrderGroup || !*c->lpLoadOrderGroup);free(c);return ok?true:fail(ERROR_REVISION_MISMATCH);
}
static DWORD host_run(Entry* e){SC_HANDLE scm=OpenSCManagerW(NULL,NULL,SC_MANAGER_CONNECT|SC_MANAGER_CREATE_SERVICE);if(!scm)return GetLastError();SC_HANDLE service=OpenServiceW(scm,e->service,SERVICE_ALL_ACCESS);DWORD result=ERROR_NOT_READY;bool created=false,deleted=false;
    if(service){SERVICE_STATUS_PROCESS status;DWORD n=0;if(!service_fingerprint(service,e,true) || !QueryServiceStatusEx(service,SC_STATUS_PROCESS_INFO,(BYTE*)&status,sizeof(status),&n))goto done;
        if(status.dwCurrentState!=SERVICE_STOPPED){result=ERROR_BUSY;goto done;}if(!DeleteService(service))goto done;CloseServiceHandle(service);service=NULL;
        ULONGLONG end=GetTickCount64()+30000;for(;;){SC_HANDLE old=OpenServiceW(scm,e->service,SERVICE_QUERY_STATUS);DWORD code=GetLastError();if(old)CloseServiceHandle(old);if(!old && code==ERROR_SERVICE_DOES_NOT_EXIST)break;
            if((!old && code!=ERROR_SERVICE_MARKED_FOR_DELETE) || GetTickCount64()>=end){result=code?code:ERROR_TIMEOUT;goto done;}Sleep(50);}
    }else if(GetLastError()!=ERROR_SERVICE_DOES_NOT_EXIST)goto done;
    wchar_t owner[96];swprintf_s(owner,_countof(owner),L"L4Setup %ls",e->operation);
    service=CreateServiceW(scm,e->service,owner,SERVICE_ALL_ACCESS,SERVICE_WIN32_OWN_PROCESS,SERVICE_DEMAND_START,SERVICE_ERROR_NORMAL,e->command,NULL,NULL,NULL,L"LocalSystem",NULL);if(!service)goto done;created=true;
    PSECURITY_DESCRIPTOR sd=NULL;bool secure=ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(A;;GA;;;SY)(A;;GA;;;BA)",SDDL_REVISION_1,&sd,NULL) && SetServiceObjectSecurity(service,DACL_SECURITY_INFORMATION,sd);if(sd)LocalFree(sd);
    if(!secure || !service_fingerprint(service,e,false) || !StartServiceW(service,0,NULL))goto done;
    ULONGLONG deadline=GetTickCount64()+1800000;for(;;){SERVICE_STATUS_PROCESS status;DWORD n=0;if(!service_fingerprint(service,e,false) || !QueryServiceStatusEx(service,SC_STATUS_PROCESS_INFO,(BYTE*)&status,sizeof(status),&n))goto done;
        if(status.dwCurrentState==SERVICE_STOPPED){result=status.dwWin32ExitCode==ERROR_SERVICE_SPECIFIC_ERROR?status.dwServiceSpecificExitCode:status.dwWin32ExitCode;deleted=DeleteService(service)!=0;if(!deleted && !result)result=GetLastError();break;}
        if(GetTickCount64()>=deadline){result=ERROR_TIMEOUT;break;}Sleep(250);}
done:
    if(result==ERROR_NOT_READY && GetLastError())result=GetLastError();
    if(service && created && !deleted){SERVICE_STATUS_PROCESS status;DWORD n=0;if(service_fingerprint(service,e,false) && QueryServiceStatusEx(service,SC_STATUS_PROCESS_INFO,(BYTE*)&status,sizeof(status),&n) && status.dwCurrentState==SERVICE_STOPPED)deleted=DeleteService(service)!=0;}
    if(service)CloseServiceHandle(service);
    if(deleted){ULONGLONG end=GetTickCount64()+30000;for(;;){SC_HANDLE old=OpenServiceW(scm,e->service,SERVICE_QUERY_STATUS);DWORD code=GetLastError();if(old)CloseServiceHandle(old);
        if(!old && code==ERROR_SERVICE_DOES_NOT_EXIST)break;if((!old && code!=ERROR_SERVICE_MARKED_FOR_DELETE) || GetTickCount64()>=end){if(!result)result=code?code:ERROR_TIMEOUT;break;}Sleep(50);}}
    CloseServiceHandle(scm);return result;
}
static void usage(void){puts("Fresh layout: --fresh-verify|--fresh-install --bundle <absolute signed kit> --fresh-version <version> [--operation <UUID>] [--arch x86|x64]\n"
    "Recovery: --fresh-recover|--fresh-status --fresh-version <original version> --operation <original UUID> [--arch x86|x64]\n"
    "Requires elevated interactive local operator. Certificate and transport are optional for local deployment; communication readiness is separate. Verify prepares signed files and runs owned SYSTEM host; never changes suite services/PATH. Install requires all four services absent. Recovery aborts original unfinished operation; no automatic epoch adoption or legacy migration.");}
bool setup_install_entry(int argc,wchar_t** argv,DWORD* result){bool selected=false;for(int i=1;i<argc;i++)if(!wcsncmp(argv[i],L"--fresh-",8))selected=true;if(!selected)return false;*result=ERROR_INVALID_PARAMETER;
    if(argc==2 && !wcscmp(argv[1],L"--fresh-help")){usage();*result=0;return true;}Entry e={0};e.arch="x86";
    const wchar_t* modes[]={L"--fresh-verify",L"--fresh-install",L"--fresh-recover",L"--fresh-status",L"--fresh-worker",L"--fresh-worker-abort"};for(unsigned i=0;i<_countof(modes);i++)if(!wcscmp(argv[1],modes[i]))e.mode=i+1;
    unsigned seen=0;bool ok=e.mode!=0;for(int i=2;ok && i<argc;i+=2){if(i+1>=argc){ok=false;break;}unsigned bit=0;
        if(!wcscmp(argv[i],L"--fresh-version")){bit=1;ok=wcslen(argv[i+1])<_countof(e.version);if(ok)wcscpy_s(e.version,_countof(e.version),argv[i+1]);}
        else if(!wcscmp(argv[i],L"--operation")){bit=2;ok=canonical_uuid(argv[i+1]);if(ok)wcscpy_s(e.operation,_countof(e.operation),argv[i+1]);}
        else if(!wcscmp(argv[i],L"--bundle")){bit=4;ok=wcslen(argv[i+1])<MAX_PATH;if(ok)wcscpy_s(e.bundle,MAX_PATH,argv[i+1]);}
        else if(!wcscmp(argv[i],L"--arch")){bit=8;ok=!wcscmp(argv[i+1],L"x86") || !wcscmp(argv[i+1],L"x64");if(ok)e.arch=!wcscmp(argv[i+1],L"x86")?"x86":"x64";}
        else ok=false;if(seen&bit)ok=false;seen|=bit;
    }
    if(!ok || !(seen&1) || wcscmp(e.version,L4SETUP_VERSION_WSTRING) || ((e.mode<=2)!=(bool)(seen&4)) || (e.mode>2 && !(seen&2))){usage();return true;}
    if(!(seen&2)){GUID guid;wchar_t braced[40];if(FAILED(CoCreateGuid(&guid)) || !StringFromGUID2(&guid,braced,40)){*result=ERROR_GEN_FAILURE;return true;}wcsncpy_s(e.operation,37,braced+1,36);CharLowerBuffW(e.operation,36);}
    if(!l4_layout_resolve(&e.layout,e.version) || !command(&e,e.mode==3 || e.mode==6)){*result=GetLastError();return true;}
    if(e.mode>=5){if(!system_process()){*result=GetLastError();return true;}wchar_t self[MAX_PATH];if(!GetModuleFileNameW(NULL,self,MAX_PATH) || _wcsicmp(self,e.host)){*result=ERROR_ACCESS_DENIED;return true;}
        worker_entry=e;SERVICE_TABLE_ENTRYW table[]={{worker_entry.service,service_main},{NULL,NULL}};*result=StartServiceCtrlDispatcherW(table)?0:GetLastError();return true;}
    if(!uac_is_elevated()){*result=ERROR_ACCESS_DENIED;return true;}
    const char* stage="entry";L4Journal* j=NULL;SetupInstallBundle* bundle=NULL;char version[64];WideCharToMultiByte(CP_UTF8,0,e.version,-1,version,sizeof(version),NULL,NULL);
    if(e.mode<=2){if(e.mode==2 && !services_absent()){*result=GetLastError();goto finish;}
        wchar_t self[MAX_PATH];if(!GetModuleFileNameW(NULL,self,MAX_PATH)){*result=GetLastError();goto finish;}
        stage="bundle";if(!setup_bundle_open_local(e.bundle,version,e.arch,NULL,&bundle)){*result=GetLastError();goto finish;}
        stage="installer";if(!setup_bundle_self(bundle,self)){*result=GetLastError();goto finish;}
        stage="bootstrap";if(!setup_bundle_prepare_bootstrap(bundle,version)){*result=GetLastError();goto finish;}
        stage="roots";if(!l4_layout_prepare(&e.layout)){*result=GetLastError();goto finish;}
        stage="journal";if(!l4_journal_open(&e.layout,e.operation,true,&j)){*result=GetLastError();goto finish;}
        stage="staging";if(!setup_bundle_stage(bundle,j,e.host)){*result=GetLastError();goto finish;}
        stage="request";if(!request_write(j,bundle,e.mode==1?ACTION_VERIFY:ACTION_INSTALL)){*result=GetLastError();goto finish;}}
    else {if(!l4_journal_open(&e.layout,e.operation,false,&j)){*result=GetLastError();
        if(e.mode==4 && *result==ERROR_SHARING_VIOLATION){SC_HANDLE scm=OpenSCManagerW(NULL,NULL,SC_MANAGER_CONNECT);
            SC_HANDLE service=scm?OpenServiceW(scm,e.service,SERVICE_QUERY_CONFIG|SERVICE_QUERY_STATUS):NULL;SERVICE_STATUS_PROCESS state={0};DWORD n=0;
            if(service && service_fingerprint(service,&e,true) && QueryServiceStatusEx(service,SC_STATUS_PROCESS_INFO,(BYTE*)&state,sizeof(state),&n)){
                printf("{\"operation_id\":\"%ls\",\"journal_busy\":true,\"host_state\":%lu,\"communication_ready\":null,\"live_health_checked\":false}\n",e.operation,state.dwCurrentState);*result=0;}
            if(service)CloseServiceHandle(service);if(scm)CloseServiceHandle(scm);}
        goto finish;}BYTE request[120];if(!request_read(j,request)){*result=GetLastError();goto finish;}
        if(e.mode==4){Saved saved={0};bool committed=false,aborted=false,deployed=false;if(!l4_journal_replay(j,saved_record,&saved) || (saved.bootstrap && (!l4_bootstrap_terminal(j,saved.bootstrap,&committed,&aborted) || !l4_bootstrap_local_terminal(j,saved.bootstrap,&deployed)))){*result=GetLastError();goto finish;}
            printf("{\"operation_id\":\"%ls\",\"committed\":%s,\"aborted\":%s,\"receipt\":%llu,\"deployment_committed\":%s,\"local_deployment\":%s,\"communication_ready\":null,\"live_health_checked\":false}\n",e.operation,committed?"true":"false",aborted?"true":"false",saved.receipt,committed?"true":"false",deployed?"true":"false");*result=0;goto finish;}
        wchar_t inputs[MAX_PATH];swprintf_s(inputs,MAX_PATH,L"%ls\\inputs",j->directory);L4CatalogRelease source={0};strcpy_s(source.version,sizeof(source.version),version);memcpy(source.manifest_sha256,request+16,32);
        if(!setup_bundle_open_local(inputs,version,e.arch,&source,&bundle) || !setup_bundle_self(bundle,e.host)){*result=GetLastError();goto finish;}}
    l4_journal_close(j);j=NULL;stage="SYSTEM-host";*result=host_run(&e);
    if(!*result && e.mode!=1){DWORD_PTR ignored=0;SendMessageTimeoutW(HWND_BROADCAST,WM_SETTINGCHANGE,0,(LPARAM)L"Environment",SMTO_ABORTIFHUNG,2000,&ignored);}
    printf("{\"operation_id\":\"%ls\",\"mode\":%u,\"error\":%lu,\"suite_install_requested\":%s}\n",e.operation,e.mode,*result,e.mode==2?"true":"false");
finish:
    if(*result)fprintf(stderr,"Fresh setup stopped: phase=%s; error=%lu; operation=%ls\n",stage,*result,e.operation);
    setup_bundle_free(bundle);l4_journal_close(j);return true;
}
