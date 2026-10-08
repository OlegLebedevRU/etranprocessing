#include "worker_entry.h"
#include "remote_executor.h"
#include "remote_launch_failure.h"
#include "../../l4common/journal_internal.h"
#include "../../l4common/remote_host.h"
#include "../../l4common/active_updater.h"
#include "remote_policy.h"
#include <bcrypt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define WORKER_STARTUP_MS SETUP_REMOTE_WORKER_STARTUP_MS
const SetupWorkerEngine* setup_worker_compiled_engine(void){return setup_remote_executor_engine();}
typedef struct {L4Layout layout;wchar_t operation[37],version[32],updater_version[32],executable[MAX_PATH];const char* arch;const SetupWorkerEngine* engine;} WorkerEntry;
static bool fail(DWORD error){SetLastError(error);return false;}
static bool uuid(const wchar_t* id){if(!id || wcslen(id)!=36)return false;bool any=false;for(unsigned i=0;i<36;i++){
    if(i==8 || i==13 || i==18 || i==23){if(id[i]!=L'-')return false;}
    else if(!((id[i]>=L'0' && id[i]<=L'9') || (id[i]>=L'a' && id[i]<=L'f')))return false;else if(id[i]!=L'0')any=true;
}return any;}
static bool primary_system(void){HANDLE thread=NULL,token=NULL;BYTE user[sizeof(TOKEN_USER)+SECURITY_MAX_SID_SIZE];DWORD size=0,session=~0u;TOKEN_TYPE type=TokenImpersonation;
    if(OpenThreadToken(GetCurrentThread(),TOKEN_QUERY,TRUE,&thread)){CloseHandle(thread);return fail(ERROR_ACCESS_DENIED);}if(GetLastError()!=ERROR_NO_TOKEN)return false;
    SetLastError(ERROR_SUCCESS);bool ok=OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token) && GetTokenInformation(token,TokenUser,user,sizeof(user),&size) &&
        IsWellKnownSid(((TOKEN_USER*)user)->User.Sid,WinLocalSystemSid) && GetTokenInformation(token,TokenType,&type,sizeof(type),&size) && type==TokenPrimary &&
        GetTokenInformation(token,TokenSessionId,&session,sizeof(session),&size) && session==0;
    DWORD error=GetLastError();if(token)CloseHandle(token);return ok?true:fail(error?error:ERROR_ACCESS_DENIED);
}
static bool hash(HANDLE file,const SetupRootAsset* expected){
    BY_HANDLE_FILE_INFORMATION info;FILE_STREAM_INFO streams[128];LARGE_INTEGER size,zero={0};BYTE* sd=NULL;DWORD sd_size=0;
    bool ok=expected && GetFileInformationByHandle(file,&info) && info.nNumberOfLinks==1 && !(info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)) &&
        GetFileInformationByHandleEx(file,FileStreamInfo,streams,sizeof(streams)) && streams[0].NextEntryOffset==0 && streams[0].StreamNameLength==14 &&
        !memcmp(streams[0].StreamName,L"::$DATA",14) && l4_store_security(file,false,&sd,&sd_size) && GetFileSizeEx(file,&size) && size.QuadPart>0 &&
        (ULONGLONG)size.QuadPart==expected->size && SetFilePointerEx(file,zero,NULL,FILE_BEGIN);free(sd);
    if(!ok)return fail(ERROR_INVALID_DATA);BCRYPT_ALG_HANDLE alg=NULL;BCRYPT_HASH_HANDLE state=NULL;BYTE block[32768],actual[32];DWORD read=0;ULONGLONG total=0;
    ok=BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,NULL,0)==0 && BCryptCreateHash(alg,&state,NULL,0,NULL,0,0)==0;
    while(ok){ok=ReadFile(file,block,sizeof(block),&read,NULL)!=0;if(!ok || !read)break;total+=read;ok=total<=expected->size && BCryptHashData(state,block,read,0)==0;}
    if(ok)ok=total==expected->size && BCryptFinishHash(state,actual,32,0)==0 && !memcmp(actual,expected->sha256,32);
    if(state)BCryptDestroyHash(state);if(alg)BCryptCloseAlgorithmProvider(alg,0);return ok?true:fail(ERROR_CRC);
}
static bool binding(const WorkerEntry* entry,const SetupOperationPlan* plan,const L4WorkerAdmission* admission){
    const L4Layout* source=setup_manifest_layout(setup_operation_source(plan));const L4RoutePlan* route=setup_operation_route(plan);
    const L4ServiceSwitch* supervisor=setup_operation_switch(plan,0,3);
    if(!source || memcmp(source,&entry->layout,sizeof(*source)) || !route || !l4_route_is_owner_trusted(route) || strcmp(l4_route_arch(route),entry->arch) || !supervisor ||
        wcscmp(supervisor->service,L"L4Superv") || wcscmp(supervisor->before.image_path,admission->before) || wcscmp(supervisor->after,admission->after) ||
        supervisor->before.start_type!=admission->start_type || supervisor->before_size!=admission->old_size || supervisor->size!=admission->new_size ||
        memcmp(supervisor->before_sha256,admission->old_sha256,32) || memcmp(supervisor->sha256,admission->new_sha256,32))return fail(ERROR_REVISION_MISMATCH);
    return true;
}
static bool parent_binding(const WorkerEntry* entry,const L4WorkerAdmission* admission,const SetupRootAsset* installer){
    L4JournalReader* reader=NULL;L4RemoteHostReceipt receipt={0};
    if(!l4_journal_reader_open_live(&entry->layout,entry->operation,&reader))return false;
    bool ok=l4_remote_host_snapshot(reader,&entry->layout,&receipt);DWORD error=GetLastError();l4_journal_reader_close(reader);if(!ok)return fail(error);
    FILETIME birth={(DWORD)receipt.birth,(DWORD)(receipt.birth>>32)};
    wchar_t suite[32],updater[32];
    return receipt.format_version==2 && MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,receipt.source_version,-1,suite,32) && !wcscmp(suite,entry->version) &&
        MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,receipt.updater_version,-1,updater,32) && !wcscmp(updater,entry->updater_version) &&
        receipt.pid==admission->parent_pid && !CompareFileTime(&birth,&admission->parent_created) && !strcmp(receipt.arch,entry->arch) &&
        receipt.executable_size==installer->size && !memcmp(receipt.executable_sha256,installer->sha256,32)?true:fail(ERROR_REVISION_MISMATCH);
}
static bool helper_binding(const SetupRecoveryReceipt* receipt,const L4WorkerAdmission* admission){
    const L4RecoveryHelper* helper=setup_recovery_receipt_helper(receipt);
    return helper && helper->size==admission->helper.size && !memcmp(helper->sha256,admission->helper.sha256,32)?true:fail(ERROR_REVISION_MISMATCH);
}
#define REQUIRE(call) do{SetLastError(ERROR_SUCCESS);if(!(call)){result=GetLastError()?GetLastError():ERROR_NOT_READY;goto done;}}while(0)
static DWORD execute(const WorkerEntry* entry){
    if(!entry->engine || !entry->engine->preflight || !entry->engine->execute)return ERROR_CALL_NOT_IMPLEMENTED;
    ULONGLONG end=GetTickCount64()+WORKER_STARTUP_MS;DWORD result=ERROR_NOT_READY;L4Journal* journal=NULL;SetupOperationPlan* plan=NULL;L4WorkerAdmission admission={0};
    L4FileFence parents={0};HANDLE file=INVALID_HANDLE_VALUE;SetupRecoveryReceipt* recovery=NULL;L4ActiveUpdater* updater=NULL;
    REQUIRE(primary_system());REQUIRE(l4_worker_accept(entry->operation,WORKER_STARTUP_MS,&journal));REQUIRE(l4_worker_recheck(journal,&admission));
    REQUIRE(setup_update_load_operation(journal,admission.sequence,&plan));REQUIRE(binding(entry,plan,&admission));
    REQUIRE(l4_active_updater_open(journal,&updater));REQUIRE(l4_active_updater_verify(updater));
    const L4ActiveUpdaterInfo* info=l4_active_updater_info(updater);const wchar_t* path=l4_active_updater_path(updater);wchar_t selected[32];
    if(!info || !path || _wcsicmp(path,entry->executable) || strcmp(info->arch,entry->arch) || !MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,info->version,-1,selected,32) || wcscmp(selected,entry->updater_version)){result=ERROR_REVISION_MISMATCH;goto done;}
    SetupRootAsset image={0};image.size=info->image_size;memcpy(image.sha256,info->image_sha256,32);
    const SetupRootAsset* installer=&image;const BYTE* publisher=info->publisher;
    REQUIRE(parent_binding(entry,&admission,installer));
    wchar_t directory[MAX_PATH];wcscpy_s(directory,MAX_PATH,entry->executable);wchar_t* leaf=wcsrchr(directory,L'\\');if(!leaf){result=ERROR_INVALID_NAME;goto done;}*leaf=0;
    REQUIRE(l4_store_pin(directory,entry->layout.binaries,false,&parents));
    file=CreateFileW(entry->executable,GENERIC_READ|READ_CONTROL,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,NULL);REQUIRE(file!=INVALID_HANDLE_VALUE);
    REQUIRE(hash(file,installer));REQUIRE(setup_signed_executable(file,entry->executable,publisher));
    REQUIRE(setup_recovery_receipt_load(journal,entry->arch,&recovery));REQUIRE(helper_binding(recovery,&admission));
    if(GetTickCount64()>=end){result=ERROR_TIMEOUT;goto done;}REQUIRE(entry->engine->preflight(journal,plan,&admission,setup_recovery_receipt_helper(recovery)));
    REQUIRE(l4_worker_recheck(journal,&admission));REQUIRE(binding(entry,plan,&admission));REQUIRE(parent_binding(entry,&admission,installer));
    REQUIRE(helper_binding(recovery,&admission));
    REQUIRE(l4_active_updater_verify(updater));
    if(GetTickCount64()>=end){result=ERROR_TIMEOUT;goto done;}
    result=entry->engine->execute(&journal,plan,&admission,setup_recovery_receipt_helper(recovery));
done:
    if(result && journal){
        /* This producer admits clear pre-window69 only; it refuses any mutation
         * history. Original error stays intact even if reporting cannot flush. */
        L4RemoteLaunchFailure failure;setup_remote_launch_failure_finish(journal,result,L4_START_RUNNING,0,&failure);
    }
    l4_active_updater_close(updater);setup_recovery_receipt_free(recovery);if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);l4_store_unpin(&parents);setup_operation_free(plan);l4_journal_close(journal);return result;
}
bool setup_worker_entry(int argc,wchar_t** argv,const SetupWorkerEngine* engine,DWORD* result){
    bool selected=false;for(int i=1;i<argc;i++)if(!wcsncmp(argv[i],L"--update-worker",15))selected=true;if(!selected)return false;
    if(!result)return true;*result=ERROR_INVALID_PARAMETER;WorkerEntry entry={0};entry.engine=engine;
    if(argc!=10 || wcscmp(argv[1],L"--update-worker"))return true;unsigned seen=0;
    for(int i=2;i<argc;i+=2){unsigned flag=0;bool ok=false;
        if(!wcscmp(argv[i],L"--source-version")){flag=1;ok=wcslen(argv[i+1])>0 && wcslen(argv[i+1])<32;if(ok)wcscpy_s(entry.version,32,argv[i+1]);}
        else if(!wcscmp(argv[i],L"--updater-version")){flag=8;ok=wcslen(argv[i+1])>0 && wcslen(argv[i+1])<32;if(ok)wcscpy_s(entry.updater_version,32,argv[i+1]);}
        else if(!wcscmp(argv[i],L"--operation")){flag=2;ok=uuid(argv[i+1]);if(ok)wcscpy_s(entry.operation,37,argv[i+1]);}
        else if(!wcscmp(argv[i],L"--arch")){flag=4;ok=!wcscmp(argv[i+1],L"x86") || !wcscmp(argv[i+1],L"x64");if(ok)entry.arch=!wcscmp(argv[i+1],L"x86")?"x86":"x64";}
        if(!ok || (seen&flag))return true;seen|=flag;
    }
    L4Layout updater_layout;
    if(seen!=15 || !l4_layout_resolve(&entry.layout,entry.version) || !l4_layout_resolve(&updater_layout,entry.updater_version) || swprintf_s(entry.executable,MAX_PATH,L"%ls\\setup\\%ls\\l4setup.exe",updater_layout.binaries,entry.updater_version)<1)return true;
    wchar_t module[MAX_PATH];if(!GetModuleFileNameW(NULL,module,MAX_PATH) || _wcsicmp(module,entry.executable)){*result=ERROR_ACCESS_DENIED;return true;}
    *result=execute(&entry);return true;
}
