#include "remote_host.h"
#include "journal_reader.h"
#include "journal_internal.h"
#include <aclapi.h>
#include <sddl.h>
#include <bcrypt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define HOST_RIGHTS (SERVICE_QUERY_CONFIG|SERVICE_QUERY_STATUS|SERVICE_START|DELETE|READ_CONTROL|WRITE_DAC)
struct L4RemoteHost {L4Layout layout;wchar_t operation[37],service[80],executable[MAX_PATH],command[1024];char arch[8];
    SC_HANDLE manager,service_handle;HANDLE executable_handle,process;L4FileFence fence;ULONGLONG size;BYTE hash[32];L4ActiveUpdater* updater;};
static bool fail(DWORD code){SetLastError(code);return false;}
static bool primary_system(void){HANDLE thread=NULL,token=NULL;BYTE user[sizeof(TOKEN_USER)+SECURITY_MAX_SID_SIZE];DWORD n=0;
    if(OpenThreadToken(GetCurrentThread(),TOKEN_QUERY,TRUE,&thread)){CloseHandle(thread);return fail(ERROR_ACCESS_DENIED);}if(GetLastError()!=ERROR_NO_TOKEN)return false;
    DWORD session=1;bool ok=OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token) && GetTokenInformation(token,TokenUser,user,sizeof(user),&n) && IsWellKnownSid(((TOKEN_USER*)user)->User.Sid,WinLocalSystemSid) && GetTokenInformation(token,TokenSessionId,&session,sizeof(session),&n) && session==0;
    if(token)CloseHandle(token);return ok?true:fail(ERROR_ACCESS_DENIED);
}
static bool canonical_uuid(const wchar_t* id){if(!id || wcslen(id)!=36)return false;bool any=false;for(unsigned i=0;i<36;i++){
    if(i==8 || i==13 || i==18 || i==23){if(id[i]!=L'-')return false;}else if(!((id[i]>=L'0' && id[i]<=L'9') || (id[i]>=L'a' && id[i]<=L'f')))return false;else if(id[i]!=L'0')any=true;}return any;}
static bool native_layout(const L4Layout* layout){const wchar_t* version=layout?wcsrchr(layout->release,L'\\'):NULL;L4Layout actual;
    return version && l4_layout_resolve(&actual,version+1) && !memcmp(layout,&actual,sizeof(actual))?true:fail(ERROR_INVALID_NAME);}
static bool identity(const L4Layout* layout,const wchar_t* operation,const char* arch,const L4ActiveUpdater* updater,wchar_t service[80],wchar_t executable[MAX_PATH],wchar_t command[1024]){
    if(!layout || !canonical_uuid(operation) || !arch || (strcmp(arch,"x86") && strcmp(arch,"x64")) || !service || !executable || !command)return fail(ERROR_INVALID_PARAMETER);
    const wchar_t* version=wcsrchr(layout->release,L'\\');L4Layout checked;
    if(!version || wcslen(version+1)>=32 || !l4_layout_from_roots(&checked,layout->binaries,layout->data,version+1) || memcmp(layout,&checked,sizeof(checked)))return fail(ERROR_INVALID_NAME);
    const L4ActiveUpdaterInfo* info=l4_active_updater_info(updater);const wchar_t* path=l4_active_updater_path(updater);
    if(!info || !path || strcmp(info->arch,arch))return fail(ERROR_REVISION_MISMATCH);
    return swprintf_s(service,80,L"L4UpdateHost_%ls",operation)>0 && wcscpy_s(executable,MAX_PATH,path)==0 &&
        swprintf_s(command,1024,L"\"%ls\" --remote-controller --source-version %ls --updater-version %hs --operation %ls --arch %hs",executable,version+1,info->version,operation,arch)>0?true:fail(ERROR_FILENAME_EXCED_RANGE);
}
bool l4_remote_host_identity(const L4Layout* layout,const wchar_t* operation,const char* arch,wchar_t service[80],wchar_t executable[MAX_PATH],wchar_t command[1024]){
    L4ActiveUpdater* updater=NULL;if(!l4_active_updater_open_fixed(layout,&updater))return false;
    bool ok=identity(layout,operation,arch,updater,service,executable,command);DWORD error=GetLastError();l4_active_updater_close(updater);return ok?true:fail(error);
}
static bool service_private(SC_HANDLE service){DWORD n=0;QueryServiceObjectSecurity(service,DACL_SECURITY_INFORMATION,NULL,0,&n);
    if(GetLastError()!=ERROR_INSUFFICIENT_BUFFER || n<SECURITY_DESCRIPTOR_MIN_LENGTH || n>65536)return fail(ERROR_INVALID_DATA);
    BYTE* bytes=malloc(n);if(!bytes)return fail(ERROR_NOT_ENOUGH_MEMORY);BOOL present=FALSE,defaulted=FALSE;PACL acl=NULL;SECURITY_DESCRIPTOR_CONTROL control=0;DWORD revision=0;
    bool ok=QueryServiceObjectSecurity(service,DACL_SECURITY_INFORMATION,bytes,n,&n) && GetSecurityDescriptorControl(bytes,&control,&revision) && (control&SE_DACL_PROTECTED) &&
        GetSecurityDescriptorDacl(bytes,&present,&acl,&defaulted) && present && acl && acl->AceCount==2;bool system=false,admins=false;
    for(unsigned i=0;ok && i<acl->AceCount;i++){void* raw=NULL;ok=GetAce(acl,i,&raw)!=0;if(!ok)break;ACCESS_ALLOWED_ACE* ace=raw;
        ok=ace->Header.AceType==ACCESS_ALLOWED_ACE_TYPE && !ace->Header.AceFlags && (ace->Mask==GENERIC_ALL || ace->Mask==SERVICE_ALL_ACCESS);
        PSID sid=&ace->SidStart;if(ok && IsWellKnownSid(sid,WinLocalSystemSid))system=true;else if(ok && IsWellKnownSid(sid,WinBuiltinAdministratorsSid))admins=true;else ok=false;
    }free(bytes);return ok && system && admins?true:fail(ERROR_ACCESS_DENIED);
}
static bool fingerprint(SC_HANDLE service,const L4RemoteHost* h){DWORD n=0;QueryServiceConfigW(service,NULL,0,&n);
    if(GetLastError()!=ERROR_INSUFFICIENT_BUFFER || n<sizeof(QUERY_SERVICE_CONFIGW) || n>65536)return fail(ERROR_INVALID_DATA);
    QUERY_SERVICE_CONFIGW* config=malloc(n);if(!config)return fail(ERROR_NOT_ENOUGH_MEMORY);wchar_t display[96];swprintf_s(display,96,L"L4Update %ls",h->operation);
    bool queried=QueryServiceConfigW(service,config,n,&n)!=0;bool ok=queried && config->lpBinaryPathName && !wcscmp(config->lpBinaryPathName,h->command) && config->lpDisplayName && !wcscmp(config->lpDisplayName,display) &&
        config->lpServiceStartName && !_wcsicmp(config->lpServiceStartName,L"LocalSystem") && config->dwServiceType==SERVICE_WIN32_OWN_PROCESS && config->dwStartType==SERVICE_DEMAND_START &&
        config->dwErrorControl==SERVICE_ERROR_NORMAL && (!config->lpDependencies || !*config->lpDependencies) && (!config->lpLoadOrderGroup || !*config->lpLoadOrderGroup);
    DWORD code=queried?ERROR_REVISION_MISMATCH:GetLastError();free(config);if(!ok)return fail(code);return service_private(service);
}
static bool status_of(SC_HANDLE service,SERVICE_STATUS_PROCESS* status){DWORD n=0;memset(status,0,sizeof(*status));return QueryServiceStatusEx(service,SC_STATUS_PROCESS_INFO,(BYTE*)status,sizeof(*status),&n)!=0;}
static bool held_hash(HANDLE file,ULONGLONG* size,BYTE digest[32]){BY_HANDLE_FILE_INFORMATION info;LARGE_INTEGER length={0},zero={0};BYTE* security=NULL;DWORD security_size=0;
    bool ok=GetFileInformationByHandle(file,&info) && info.nNumberOfLinks==1 && !(info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)) && GetFileSizeEx(file,&length) && length.QuadPart>0 &&
        l4_store_security(file,false,&security,&security_size) && SetFilePointerEx(file,zero,NULL,FILE_BEGIN);free(security);if(!ok)return fail(ERROR_INVALID_DATA);
    BCRYPT_ALG_HANDLE alg=NULL;BCRYPT_HASH_HANDLE hash=NULL;BYTE buffer[32768];DWORD n=0;ULONGLONG total=0;
    ok=BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,NULL,0)==0 && BCryptCreateHash(alg,&hash,NULL,0,NULL,0,0)==0;
    while(ok){ok=ReadFile(file,buffer,sizeof(buffer),&n,NULL)!=0;if(!ok || !n)break;total+=n;ok=total<=(ULONGLONG)length.QuadPart && BCryptHashData(hash,buffer,n,0)==0;}
    if(ok)ok=total==(ULONGLONG)length.QuadPart && BCryptFinishHash(hash,digest,32,0)==0;if(hash)BCryptDestroyHash(hash);if(alg)BCryptCloseAlgorithmProvider(alg,0);
    if(ok)*size=total;return ok?true:fail(ERROR_CRC);
}
static bool open_executable(L4RemoteHost* h){wchar_t directory[MAX_PATH];wcscpy_s(directory,MAX_PATH,h->executable);wchar_t* leaf=wcsrchr(directory,L'\\');if(!leaf)return fail(ERROR_INVALID_NAME);*leaf=0;
    if(!l4_store_pin(directory,h->layout.binaries,false,&h->fence))return false;
    h->executable_handle=CreateFileW(h->executable,GENERIC_READ|READ_CONTROL,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,NULL);
    const L4ActiveUpdaterInfo* updater=l4_active_updater_info(h->updater);
    return updater && l4_active_updater_verify(h->updater) && h->executable_handle!=INVALID_HANDLE_VALUE && held_hash(h->executable_handle,&h->size,h->hash) &&
        h->size==updater->image_size && !memcmp(h->hash,updater->image_sha256,32)?true:fail(ERROR_CRC);
}
static bool process_proof(L4RemoteHost* h,const L4RemoteHostReceipt* receipt){SERVICE_STATUS_PROCESS status={0};DWORD n=0;FILETIME birth,exit,kernel,user;
    const L4ActiveUpdaterInfo* updater=l4_active_updater_info(h->updater);
    if(!updater || receipt->format_version!=2 || strcmp(receipt->updater_version,updater->version))return fail(ERROR_REVISION_MISMATCH);
    if(!fingerprint(h->service_handle,h) || !status_of(h->service_handle,&status) || status.dwCurrentState!=SERVICE_RUNNING || status.dwProcessId!=receipt->pid)return fail(ERROR_NOT_READY);
    HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,receipt->pid);if(!process)return false;
    wchar_t image[MAX_PATH];DWORD capacity=MAX_PATH,session=1;HANDLE token=NULL;BYTE owner[sizeof(TOKEN_USER)+SECURITY_MAX_SID_SIZE];
    bool ok=WaitForSingleObject(process,0)==WAIT_TIMEOUT && GetProcessTimes(process,&birth,&exit,&kernel,&user) && (((ULONGLONG)birth.dwHighDateTime<<32)|birth.dwLowDateTime)==receipt->birth &&
        QueryFullProcessImageNameW(process,0,image,&capacity) && !_wcsicmp(image,h->executable) && OpenProcessToken(process,TOKEN_QUERY,&token) && GetTokenInformation(token,TokenUser,owner,sizeof(owner),&n) &&
        IsWellKnownSid(((TOKEN_USER*)owner)->User.Sid,WinLocalSystemSid) && GetTokenInformation(token,TokenSessionId,&session,sizeof(session),&n) && session==0 && receipt->executable_size==h->size && !memcmp(receipt->executable_sha256,h->hash,32);
    if(ok)ok=fingerprint(h->service_handle,h) && status_of(h->service_handle,&status) && status.dwCurrentState==SERVICE_RUNNING && status.dwProcessId==receipt->pid &&
        GetProcessTimes(process,&birth,&exit,&kernel,&user) && (((ULONGLONG)birth.dwHighDateTime<<32)|birth.dwLowDateTime)==receipt->birth && WaitForSingleObject(process,0)==WAIT_TIMEOUT;
    if(token)CloseHandle(token);if(ok && h->process)CloseHandle(h->process);if(ok)h->process=process;else CloseHandle(process);return ok?true:fail(ERROR_ACCESS_DENIED);
}
static bool initialize(L4RemoteHost* h,const L4Layout* layout,const wchar_t* operation,const char* arch){if(!h || !layout || !arch)return fail(ERROR_INVALID_PARAMETER);memset(h,0,sizeof(*h));h->executable_handle=INVALID_HANDLE_VALUE;h->layout=*layout;
    if(!native_layout(layout) || !primary_system() || !l4_active_updater_open_fixed(layout,&h->updater) || !identity(layout,operation,arch,h->updater,h->service,h->executable,h->command))return false;
    wcscpy_s(h->operation,37,operation);strcpy_s(h->arch,8,arch);return true;
}
void l4_remote_host_close(L4RemoteHost* h){if(!h)return;if(h->process)CloseHandle(h->process);if(h->service_handle)CloseServiceHandle(h->service_handle);if(h->manager)CloseServiceHandle(h->manager);
    if(h->executable_handle && h->executable_handle!=INVALID_HANDLE_VALUE)CloseHandle(h->executable_handle);l4_store_unpin(&h->fence);l4_active_updater_close(h->updater);free(h);}
typedef struct {const L4Layout* layout;L4RemoteHostReceipt receipt;bool request,ack;} Snapshot;
static bool snapshot_record(DWORD kind,ULONGLONG sequence,const void* data,DWORD size,void* context){Snapshot* s=context;const BYTE* bytes=data;
    if(kind==L4_RECORD_REMOTE_REQUEST){
        if(s->request || sequence!=1 || size!=56 || memcmp(bytes,"L4RPC031",8) || l4_store_get32(bytes+8)!=1 || l4_store_get32(bytes+12)!=L4_REMOTE_SUITE || !memchr(bytes+16,0,32))return fail(ERROR_INVALID_DATA);
        const char* version=(const char*)bytes+16;wchar_t wide[32];L4Layout checked;if(strcmp(version,"latest") && (!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,version,-1,wide,32) || !l4_layout_from_roots(&checked,s->layout->binaries,s->layout->data,wide)))return fail(ERROR_INVALID_DATA);
        for(size_t i=strlen(version)+1;i<32;i++)if(version[i])return fail(ERROR_INVALID_DATA);
        s->receipt.request.target=L4_REMOTE_SUITE;strcpy_s(s->receipt.request.version,32,version);s->receipt.request.accepted_utc=l4_store_get64(bytes+48);
        if(!s->receipt.request.accepted_utc || s->receipt.request.accepted_utc>2650467743999999999ULL)return fail(ERROR_INVALID_DATA);s->request=true;
    }else if(kind==L4_RECORD_REMOTE_HOST_ACK){
        if(!s->request || s->ack || sequence!=2 || (size!=112 && size!=144) || memcmp(bytes,"L4RHST01",8))return fail(ERROR_INVALID_DATA);
        DWORD schema=l4_store_get32(bytes+8);
        if(!((schema==1 && size==112)||(schema==2 && size==144)) || l4_store_get64(bytes+64)!=s->receipt.request.accepted_utc || !memchr(bytes+72,0,32) || !memchr(bytes+104,0,8) ||
            (schema==2 && !memchr(bytes+112,0,32)))return fail(ERROR_INVALID_DATA);
        s->receipt.pid=l4_store_get32(bytes+12);s->receipt.birth=l4_store_get64(bytes+16);s->receipt.executable_size=l4_store_get64(bytes+24);memcpy(s->receipt.executable_sha256,bytes+32,32);
        strcpy_s(s->receipt.source_version,32,(const char*)bytes+72);strcpy_s(s->receipt.arch,8,(const char*)bytes+104);const wchar_t* version=wcsrchr(s->layout->release,L'\\');char expected[32];BYTE any=0;
        s->receipt.format_version=schema;strcpy_s(s->receipt.updater_version,32,schema==2?(const char*)bytes+112:s->receipt.source_version);
        wchar_t updater_version[32];L4Layout updater_layout;
        if(!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s->receipt.updater_version,-1,updater_version,32) || !l4_layout_from_roots(&updater_layout,s->layout->binaries,s->layout->data,updater_version))return fail(ERROR_INVALID_DATA);
        if(schema==2)for(size_t i=strlen(s->receipt.updater_version)+1;i<32;i++)if(bytes[112+i])return fail(ERROR_INVALID_DATA);
        for(unsigned i=0;i<32;i++)any|=s->receipt.executable_sha256[i];for(size_t i=strlen(s->receipt.source_version)+1;i<32;i++)if(bytes[72+i])return fail(ERROR_INVALID_DATA);for(size_t i=strlen(s->receipt.arch)+1;i<8;i++)if(bytes[104+i])return fail(ERROR_INVALID_DATA);
        if(!version || !WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,version+1,-1,expected,32,NULL,NULL) || strcmp(expected,s->receipt.source_version) ||
            (strcmp(s->receipt.arch,"x86") && strcmp(s->receipt.arch,"x64")) || !s->receipt.pid || !s->receipt.birth || !s->receipt.executable_size || !any)return fail(ERROR_INVALID_DATA);s->ack=true;
    }return true;
}
bool l4_remote_host_load(L4Journal* j,L4RemoteHostReceipt* receipt){
    if(receipt)memset(receipt,0,sizeof(*receipt));if(!j || !receipt || !j->lock || j->lock==INVALID_HANDLE_VALUE || j->poisoned)return fail(ERROR_INVALID_STATE);
    Snapshot scan={0};scan.layout=&j->layout;if(!l4_journal_replay(j,snapshot_record,&scan))return false;
    if(!scan.request || !scan.ack)return fail(ERROR_INVALID_DATA);*receipt=scan.receipt;return true;
}
static bool snapshot(const L4Layout* layout,const wchar_t* operation,L4RemoteHostReceipt* receipt){L4JournalReader* reader=NULL;Snapshot scan={0};scan.layout=layout;
    if(!l4_journal_reader_open_live(layout,operation,&reader))return false;bool ok=l4_journal_reader_replay(reader,snapshot_record,&scan);DWORD code=GetLastError();l4_journal_reader_close(reader);
    if(!ok)return fail(code);if(!scan.request)return fail(ERROR_INVALID_DATA);if(!scan.ack)return fail(ERROR_IO_PENDING);*receipt=scan.receipt;return true;
}
bool l4_remote_host_snapshot(const L4JournalReader* reader,const L4Layout* layout,L4RemoteHostReceipt* receipt){
    if(receipt)memset(receipt,0,sizeof(*receipt));if(!reader || !layout || !receipt)return fail(ERROR_INVALID_PARAMETER);
    Snapshot scan={0};scan.layout=layout;if(!l4_journal_reader_replay(reader,snapshot_record,&scan))return false;
    if(!scan.request)return fail(ERROR_INVALID_DATA);if(!scan.ack)return fail(ERROR_IO_PENDING);*receipt=scan.receipt;return true;
}
static bool self_binding(L4RemoteHost* h){wchar_t module[MAX_PATH];SERVICE_STATUS_PROCESS status={0};
    h->manager=OpenSCManagerW(NULL,NULL,SC_MANAGER_CONNECT);if(!h->manager)return false;h->service_handle=OpenServiceW(h->manager,h->service,SERVICE_QUERY_CONFIG|SERVICE_QUERY_STATUS|READ_CONTROL|DELETE);
    return h->service_handle && GetModuleFileNameW(NULL,module,MAX_PATH) && !_wcsicmp(module,h->executable) && fingerprint(h->service_handle,h) && status_of(h->service_handle,&status) && status.dwCurrentState==SERVICE_RUNNING && status.dwProcessId==GetCurrentProcessId()?true:fail(ERROR_ACCESS_DENIED);
}
bool l4_remote_host_self(const L4Layout* layout,const wchar_t* operation,const char* arch){L4RemoteHost* h=calloc(1,sizeof(*h));if(!h)return fail(ERROR_NOT_ENOUGH_MEMORY);
    bool ok=initialize(h,layout,operation,arch) && self_binding(h);DWORD code=GetLastError();l4_remote_host_close(h);return ok?true:fail(code);
}
bool l4_remote_host_pin_self(const L4Layout* layout,const wchar_t* operation,const char* arch,L4RemoteHost** output,L4RemoteHostImage* image){
    if(output)*output=NULL;if(image)memset(image,0,sizeof(*image));if(!output || !image)return fail(ERROR_INVALID_PARAMETER);
    L4RemoteHost* h=calloc(1,sizeof(*h));if(!h)return fail(ERROR_NOT_ENOUGH_MEMORY);
    bool ok=initialize(h,layout,operation,arch) && self_binding(h) && open_executable(h);DWORD code=GetLastError();
    if(!ok){l4_remote_host_close(h);return fail(code);}image->file=h->executable_handle;wcscpy_s(image->path,MAX_PATH,h->executable);image->size=h->size;memcpy(image->sha256,h->hash,32);
    strcpy_s(image->updater_version,32,l4_active_updater_info(h->updater)->version);*output=h;return true;
}
bool l4_remote_host_retire(const L4Layout* layout,const wchar_t* operation,const char* arch){L4RemoteHost* h=calloc(1,sizeof(*h));if(!h)return fail(ERROR_NOT_ENOUGH_MEMORY);
    bool ok=initialize(h,layout,operation,arch) && self_binding(h) && DeleteService(h->service_handle);DWORD code=GetLastError();l4_remote_host_close(h);return ok?true:fail(code);
}
bool l4_remote_host_ack(L4Journal* j,const char* arch,L4RemoteHostReceipt* receipt){if(receipt)memset(receipt,0,sizeof(*receipt));if(!j || !receipt || !j->lock || j->lock==INVALID_HANDLE_VALUE || j->poisoned || j->sequence!=1)return fail(ERROR_INVALID_STATE);
    const wchar_t* operation=wcsrchr(j->directory,L'\\');L4RemoteHost* h=calloc(1,sizeof(*h));if(!h)return fail(ERROR_NOT_ENOUGH_MEMORY);L4RemoteRequest request={0};
    bool ok=operation && initialize(h,&j->layout,operation+1,arch) && l4_remote_request_load(j,&request) && request.target==L4_REMOTE_SUITE && self_binding(h) && open_executable(h);
    BYTE bytes[144]={0};FILETIME birth={0},exit={0},kernel={0},user={0};if(ok)ok=GetProcessTimes(GetCurrentProcess(),&birth,&exit,&kernel,&user)!=0;
    const wchar_t* version=wcsrchr(j->layout.release,L'\\');if(ok)ok=version && WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,version+1,-1,(char*)bytes+72,32,NULL,NULL)!=0;
    if(ok){memcpy(bytes,"L4RHST01",8);l4_store_u32(bytes+8,2);l4_store_u32(bytes+12,GetCurrentProcessId());l4_store_u64(bytes+16,((ULONGLONG)birth.dwHighDateTime<<32)|birth.dwLowDateTime);
        l4_store_u64(bytes+24,h->size);memcpy(bytes+32,h->hash,32);l4_store_u64(bytes+64,request.accepted_utc);strcpy_s((char*)bytes+104,8,arch);
        strcpy_s((char*)bytes+112,32,l4_active_updater_info(h->updater)->version);
        ok=l4_remote_host_self(&j->layout,operation+1,arch) && l4_journal_append(j,L4_RECORD_REMOTE_HOST_ACK,bytes,sizeof(bytes),NULL);
        if(ok){Snapshot scan={0};scan.layout=&j->layout;ok=l4_journal_replay(j,snapshot_record,&scan) && scan.ack;if(ok)*receipt=scan.receipt;}
    }DWORD code=GetLastError();l4_remote_host_close(h);return ok?true:fail(code?code:ERROR_NOT_READY);
}
static bool observe_owned(L4RemoteHost* h,DWORD timeout,L4RemoteHostReceipt* receipt){ULONGLONG end=GetTickCount64()+timeout;
    for(;;){if(snapshot(&h->layout,h->operation,receipt))return !strcmp(receipt->arch,h->arch) && process_proof(h,receipt)?true:fail(ERROR_ACCESS_DENIED);
        DWORD code=GetLastError();if(code!=ERROR_IO_PENDING)return fail(code);SERVICE_STATUS_PROCESS status={0};
        if(!fingerprint(h->service_handle,h) || !status_of(h->service_handle,&status))return false;
        if(status.dwCurrentState==SERVICE_STOPPED)return fail(status.dwWin32ExitCode==ERROR_SERVICE_SPECIFIC_ERROR?status.dwServiceSpecificExitCode:(status.dwWin32ExitCode?status.dwWin32ExitCode:ERROR_NOT_READY));
        ULONGLONG now=GetTickCount64();if(now>=end)return fail(ERROR_TIMEOUT);DWORD pause=(DWORD)(end-now);Sleep(pause<50?pause:50);
    }
}
bool l4_remote_host_observe(const L4Layout* layout,const wchar_t* operation,DWORD timeout,L4RemoteHostReceipt* receipt){if(receipt)memset(receipt,0,sizeof(*receipt));if(!layout || !receipt || !canonical_uuid(operation) || !timeout || timeout>300000)return fail(ERROR_INVALID_PARAMETER);
    if(!native_layout(layout) || !primary_system())return false;ULONGLONG end=GetTickCount64()+timeout;L4RemoteHostReceipt saved={0};
    for(;;){if(snapshot(layout,operation,&saved))break;DWORD code=GetLastError();if(code!=ERROR_IO_PENDING)return fail(code);ULONGLONG now=GetTickCount64();if(now>=end)return fail(ERROR_TIMEOUT);DWORD pause=(DWORD)(end-now);Sleep(pause<50?pause:50);}
    L4RemoteHost* h=calloc(1,sizeof(*h));if(!h)return fail(ERROR_NOT_ENOUGH_MEMORY);
    bool ok=initialize(h,layout,operation,saved.arch) && open_executable(h);if(ok){h->manager=OpenSCManagerW(NULL,NULL,SC_MANAGER_CONNECT);ok=h->manager!=NULL;}
    if(ok){h->service_handle=OpenServiceW(h->manager,h->service,SERVICE_QUERY_CONFIG|SERVICE_QUERY_STATUS|READ_CONTROL);ok=h->service_handle!=NULL;}
    ULONGLONG now=GetTickCount64();if(ok && now>=end)ok=fail(ERROR_TIMEOUT);
    if(ok)ok=observe_owned(h,(DWORD)(end-now),receipt);DWORD code=GetLastError();l4_remote_host_close(h);return ok?true:fail(code);
}
/* An ordinary RPC must never borrow a staged owner-local trial purpose. UUID
 * knowledge is not authority. Check every fixed sidecar under original lock;
 * absence requires held private ancestry and exact FILE_NOT_FOUND. */
static bool no_local_purpose(L4Journal* j){L4FileFence ancestry={0},fence={0};if(!j||!j->lock||j->lock==INVALID_HANDLE_VALUE)return false;
 bool ok=l4_store_pin(j->directory,j->layout.data,false,&ancestry)&&l4_store_pin(j->directory,j->directory,true,&fence);DWORD error=GetLastError();
 if(!ok){l4_store_unpin(&fence);l4_store_unpin(&ancestry);return fail(error?error:ERROR_ACCESS_DENIED);}
 const wchar_t* leaves[]={L"acceptance.local",L"acceptance.catalog.json",L"acceptance.catalog.json.sig",L"acceptance.authorization.json",L"acceptance.authorization.json.sig"};error=ERROR_ACCESS_DENIED;
 for(unsigned i=0;i<5;i++){wchar_t path[MAX_PATH];if(swprintf_s(path,MAX_PATH,L"%ls\\%ls",j->directory,leaves[i])<0){ok=false;error=ERROR_FILENAME_EXCED_RANGE;break;}DWORD attributes=GetFileAttributesW(path);if(attributes!=INVALID_FILE_ATTRIBUTES){ok=false;break;}DWORD e=GetLastError();if(e!=ERROR_FILE_NOT_FOUND){ok=false;error=e;break;}}
 l4_store_unpin(&fence);l4_store_unpin(&ancestry);return ok?true:fail(error);
}
static bool launch(L4Journal** journal,const char* arch,DWORD timeout,L4RemoteHost** output,L4RemoteHostReceipt* receipt,bool local){if(output)*output=NULL;if(receipt)memset(receipt,0,sizeof(*receipt));
    if(!journal || !*journal || !output || !receipt || !timeout || timeout>300000)return fail(ERROR_INVALID_PARAMETER);ULONGLONG end=GetTickCount64()+timeout;L4Journal* j=*journal;L4RemoteRequest request={0};
    if(!j->lock || j->lock==INVALID_HANDLE_VALUE || j->poisoned || j->sequence!=1 || !l4_remote_request_load(j,&request) || request.target!=L4_REMOTE_SUITE)return fail(ERROR_NOT_SUPPORTED);
    if(!local && !no_local_purpose(j))return false;
    const wchar_t* operation=wcsrchr(j->directory,L'\\');L4RemoteHost* h=calloc(1,sizeof(*h));if(!h)return fail(ERROR_NOT_ENOUGH_MEMORY);
    bool ok=operation && initialize(h,&j->layout,operation+1,arch);wchar_t module[MAX_PATH],expected[MAX_PATH];
    L4ActiveUpdater* owned=NULL;if(ok)ok=l4_active_updater_open(j,&owned) &&
        !memcmp(l4_active_updater_info(owned),l4_active_updater_info(h->updater),sizeof(L4ActiveUpdaterInfo));
    l4_active_updater_close(owned);
    if(ok){if(local)ok=wcscpy_s(expected,MAX_PATH,h->executable)==0;else ok=l4_layout_component(&j->layout,L"l4con",L"l4con.exe",expected);}
    if(ok)ok=GetModuleFileNameW(NULL,module,MAX_PATH) && !_wcsicmp(module,expected) && open_executable(h);
    if(ok){h->manager=OpenSCManagerW(NULL,NULL,SC_MANAGER_CONNECT|SC_MANAGER_CREATE_SERVICE);ok=h->manager!=NULL;}
    if(ok){h->service_handle=OpenServiceW(h->manager,h->service,HOST_RIGHTS);if(!h->service_handle && GetLastError()!=ERROR_SERVICE_DOES_NOT_EXIST)ok=false;}
    bool created=false;if(ok && !h->service_handle){wchar_t display[96];swprintf_s(display,96,L"L4Update %ls",h->operation);
        h->service_handle=CreateServiceW(h->manager,h->service,display,HOST_RIGHTS,SERVICE_WIN32_OWN_PROCESS,SERVICE_DEMAND_START,SERVICE_ERROR_NORMAL,h->command,NULL,NULL,NULL,L"LocalSystem",NULL);ok=h->service_handle!=NULL;created=ok;
        PSECURITY_DESCRIPTOR sd=NULL;if(ok)ok=ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(A;;GA;;;SY)(A;;GA;;;BA)",SDDL_REVISION_1,&sd,NULL) && SetServiceObjectSecurity(h->service_handle,DACL_SECURITY_INFORMATION|PROTECTED_DACL_SECURITY_INFORMATION,sd);if(sd)LocalFree(sd);
    }
    SERVICE_STATUS_PROCESS status={0};if(ok)ok=fingerprint(h->service_handle,h) && status_of(h->service_handle,&status);
    if(ok && status.dwCurrentState!=SERVICE_STOPPED)ok=fail(ERROR_BUSY);
    if(ok){l4_journal_close(j);*journal=NULL;ok=fingerprint(h->service_handle,h) && StartServiceW(h->service_handle,0,NULL);}
    ULONGLONG now=GetTickCount64();if(ok && now>=end)ok=fail(ERROR_TIMEOUT);
    if(ok)ok=observe_owned(h,(DWORD)(end-now),receipt);DWORD code=GetLastError();
    if(!ok && created && h->service_handle && fingerprint(h->service_handle,h) && status_of(h->service_handle,&status) && status.dwCurrentState==SERVICE_STOPPED)DeleteService(h->service_handle);
    if(!ok){l4_remote_host_close(h);return fail(code?code:ERROR_NOT_READY);}*output=h;return true;
}
bool l4_remote_host_launch(L4Journal** journal,const char* arch,DWORD timeout,L4RemoteHost** output,L4RemoteHostReceipt* receipt){return launch(journal,arch,timeout,output,receipt,false);}
bool l4_remote_host_launch_local(L4Journal** journal,const char* arch,DWORD timeout,L4RemoteHost** output,L4RemoteHostReceipt* receipt){return launch(journal,arch,timeout,output,receipt,true);}
bool l4_remote_host_wait(L4RemoteHost* h,DWORD timeout,DWORD* code){if(code)*code=STILL_ACTIVE;if(!h||!h->process||!code||!timeout||timeout>86400000||!primary_system())return fail(ERROR_INVALID_PARAMETER);FILETIME before,after,ended,kernel,user;if(!GetProcessTimes(h->process,&before,&ended,&kernel,&user))return false;DWORD wait=WaitForSingleObject(h->process,timeout);if(wait!=WAIT_OBJECT_0)return fail(wait==WAIT_TIMEOUT?ERROR_TIMEOUT:GetLastError());return GetProcessTimes(h->process,&after,&ended,&kernel,&user)&&!CompareFileTime(&before,&after)&&GetExitCodeProcess(h->process,code)&&*code!=STILL_ACTIVE?true:fail(ERROR_REVISION_MISMATCH);}
