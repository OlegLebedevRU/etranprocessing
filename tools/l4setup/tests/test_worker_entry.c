#include "../src/worker_entry.h"
#include "../../l4common/active_updater.h"
#include "../src/remote_launch_failure.h"
#include "../../l4common/journal_internal.h"
#include "../../l4common/remote_host.h"
#include <stdio.h>
#include <string.h>
static unsigned checks,failures,accepts,rechecks,preflights,executions,signatures,receipt_loads,receipt_live,failure_reports;
static int fault;static ULONGLONG tick;static L4Journal* pending;
static L4Layout source_layout;static L4ServiceSwitch model_supervisor;static L4WorkerAdmission modeled_admission;
static SetupRootAsset model_installer;static BYTE model_publisher[32],system_sid[SECURITY_MAX_SID_SIZE],other_sid[SECURITY_MAX_SID_SIZE];
struct SetupOperationPlan{int unused;};static SetupOperationPlan operation;
struct SetupManifest{int unused;};static SetupManifest manifest;
struct SetupRootManifest{int unused;};static SetupRootManifest root_manifest;
struct L4RoutePlan{int unused;};static L4RoutePlan model_route;
struct SetupRecoveryReceipt{int unused;};static SetupRecoveryReceipt model_receipt;static L4RecoveryHelper model_helper;
static L4ActiveUpdaterInfo updater_info;static wchar_t updater_path[MAX_PATH];
#define CHECK(x) do{checks++;if(!(x)){failures++;printf("FAIL entry line%d err%lu: %s\n",__LINE__,GetLastError(),#x);}}while(0)
static ULONGLONG clock_now(void){return tick;}
static BOOL WINAPI thread_token(HANDLE thread,DWORD rights,BOOL self,PHANDLE out){(void)thread;(void)rights;(void)self;if(fault==1){*out=CreateEventW(NULL,FALSE,FALSE,NULL);return TRUE;}SetLastError(ERROR_NO_TOKEN);return FALSE;}
static BOOL WINAPI process_token(HANDLE process,DWORD rights,PHANDLE out){(void)process;(void)rights;*out=CreateEventW(NULL,FALSE,FALSE,NULL);return *out!=NULL;}
static BOOL WINAPI token_info(HANDLE token,TOKEN_INFORMATION_CLASS kind,LPVOID out,DWORD size,PDWORD needed){(void)token;(void)size;
    if(kind==TokenUser){*needed=sizeof(TOKEN_USER);((TOKEN_USER*)out)->User.Sid=fault==2?other_sid:system_sid;return TRUE;}
    if(kind==TokenType){*needed=sizeof(TOKEN_TYPE);*(TOKEN_TYPE*)out=fault==3?TokenImpersonation:TokenPrimary;return TRUE;}
    if(kind==TokenSessionId){*needed=sizeof(DWORD);*(DWORD*)out=fault==4?1:0;return TRUE;}SetLastError(ERROR_INVALID_PARAMETER);return FALSE;
}
bool l4_worker_accept(const wchar_t* version,const wchar_t* uuid,DWORD timeout,L4Journal** j){(void)uuid;CHECK(version && !wcscmp(version,L"1.13.6"));CHECK(timeout==150000);accepts++;
    if(fault==5){SetLastError(ERROR_ACCESS_DENIED);return false;}*j=pending;pending=NULL;return true;}
bool l4_worker_recheck(L4Journal* j,L4WorkerAdmission* out){rechecks++;if(fault==6 || j->sequence!=1){SetLastError(ERROR_INVALID_STATE);return false;}
    *out=modeled_admission;if(fault==16 && rechecks==2)out->parent_created.dwLowDateTime++;if(fault==23 && rechecks==2)out->helper.sha256[0]^=1;return true;}
bool setup_recovery_receipt_load(L4Journal* j,const char* arch,SetupRecoveryReceipt** out){CHECK(j && !strcmp(arch,"x86") && !receipt_live);receipt_loads++;
    if(fault==20){SetLastError(ERROR_FILE_NOT_FOUND);return false;}if(fault==24)tick+=150001;receipt_live=1;*out=&model_receipt;return true;}
const L4RecoveryHelper* setup_recovery_receipt_helper(const SetupRecoveryReceipt* r){CHECK(r==&model_receipt && receipt_live);if(fault==22)return NULL;return &model_helper;}
void setup_recovery_receipt_free(SetupRecoveryReceipt* r){if(r){CHECK(r==&model_receipt && receipt_live);receipt_live=0;}}
bool setup_update_load_operation(L4Journal* j,ULONGLONG sequence,SetupOperationPlan** out){(void)j;CHECK(sequence==64);
    if(fault==7){SetLastError(ERROR_CRC);return false;}*out=&operation;return true;}
void setup_operation_free(SetupOperationPlan* p){CHECK(!p || p==&operation);}
const SetupManifest* setup_operation_source(const SetupOperationPlan* p){CHECK(p==&operation);return &manifest;}
const L4Layout* setup_manifest_layout(const SetupManifest* p){CHECK(p==&manifest);return fault==8?NULL:&source_layout;}
const L4RoutePlan* setup_operation_route(const SetupOperationPlan* p){CHECK(p==&operation);return &model_route;}
bool l4_route_is_owner_trusted(const L4RoutePlan* p){CHECK(p==&model_route);return fault!=9;}
const char* l4_route_arch(const L4RoutePlan* p){CHECK(p==&model_route);return fault==10?"x64":"x86";}
const L4ServiceSwitch* setup_operation_switch(const SetupOperationPlan* p,unsigned hop,unsigned service){CHECK(p==&operation && hop==0 && service==3);return fault==11?NULL:&model_supervisor;}
const SetupRootManifest* setup_operation_root(const SetupOperationPlan* p){CHECK(p==&operation);return fault==12?NULL:&root_manifest;}
const SetupRootAsset* setup_root_installer(const SetupRootManifest* p){return p?&model_installer:NULL;}
const BYTE* setup_root_publisher(const SetupRootManifest* p){return p?model_publisher:NULL;}
bool l4_active_updater_open(L4Journal* j,L4ActiveUpdater** out){CHECK(j);if(fault==12){SetLastError(ERROR_CRC);return false;}*out=(L4ActiveUpdater*)1;return true;}
bool l4_active_updater_verify(L4ActiveUpdater* a){CHECK(a==(L4ActiveUpdater*)1);return true;}
const L4ActiveUpdaterInfo* l4_active_updater_info(const L4ActiveUpdater* a){CHECK(a==(L4ActiveUpdater*)1);updater_info.image_size=model_installer.size;memcpy(updater_info.image_sha256,model_installer.sha256,32);memcpy(updater_info.publisher,model_publisher,32);return &updater_info;}
const wchar_t* l4_active_updater_path(const L4ActiveUpdater* a){CHECK(a==(L4ActiveUpdater*)1);return updater_path;}
void l4_active_updater_close(L4ActiveUpdater* a){CHECK(!a || a==(L4ActiveUpdater*)1);}
bool setup_signed_executable(HANDLE file,const wchar_t* path,const BYTE digest[32]){(void)file;(void)path;CHECK(!memcmp(digest,model_publisher,32));signatures++;
    if(fault==13){SetLastError(ERROR_INVALID_IMAGE_HASH);return false;}if(fault==14)tick+=150001;return true;}
bool l4_journal_reader_open_live(const L4Layout* roots,const wchar_t* uuid,L4JournalReader** out){(void)uuid;CHECK(!memcmp(roots,&source_layout,sizeof(*roots)));*out=(L4JournalReader*)(ULONG_PTR)123;return true;}
bool l4_remote_host_snapshot(const L4JournalReader* r,const L4Layout* roots,L4RemoteHostReceipt* out){CHECK(r==(L4JournalReader*)(ULONG_PTR)123 && !memcmp(roots,&source_layout,sizeof(*roots)));
    memset(out,0,sizeof(*out));out->pid=modeled_admission.parent_pid+(fault==17?1:0);
    out->format_version=2;strcpy_s(out->source_version,32,"1.13.6");strcpy_s(out->updater_version,32,"1.13.5");
    out->birth=((ULONGLONG)modeled_admission.parent_created.dwHighDateTime<<32)|modeled_admission.parent_created.dwLowDateTime;strcpy_s(out->arch,8,"x86");
    out->executable_size=model_installer.size;memcpy(out->executable_sha256,model_installer.sha256,32);if(fault==19)out->executable_sha256[0]^=1;return true;}
void l4_journal_reader_close(L4JournalReader* r){CHECK(r==(L4JournalReader*)(ULONG_PTR)123);}
static bool preflight(L4Journal* j,const SetupOperationPlan* p,const L4WorkerAdmission* a,const L4RecoveryHelper* h){CHECK(j && p==&operation && a->sequence==64 && h==&model_helper && receipt_live);preflights++;
    if(fault==15){SetLastError(ERROR_CANCELLED);return false;}if(fault==18)CHECK(l4_journal_append(j,70,"x",1,NULL));return true;}
static DWORD apply(L4Journal** j,const SetupOperationPlan* p,const L4WorkerAdmission* a,const L4RecoveryHelper* h){CHECK(j && *j && p==&operation && a->parent_pid==777 && h==&model_helper && receipt_live);executions++;return ERROR_GEN_FAILURE; /* Never a fixture update success. */}
const SetupWorkerEngine* setup_remote_executor_engine(void){static const SetupWorkerEngine engine={preflight,apply};return &engine;}
#define OpenThreadToken thread_token
#define OpenProcessToken process_token
#define GetTokenInformation token_info
#define GetTickCount64 clock_now
#include "../src/worker_entry.c"
#undef OpenThreadToken
#undef OpenProcessToken
#undef GetTokenInformation
#undef GetTickCount64
static void reset(void){CHECK(!receipt_live);model_helper=modeled_admission.helper;fault=0;tick=1000;accepts=rechecks=preflights=executions=signatures=receipt_loads=0;}
static bool start(unsigned n,wchar_t directory[MAX_PATH]){wchar_t uuid[37];swprintf_s(uuid,37,L"17730000-0000-4000-8000-%012u",n);
    if(!l4_journal_open(&source_layout,uuid,true,&pending) || !l4_journal_append(pending,69,"technical fixture receipt",25,NULL))return false;
    wcscpy_s(directory,MAX_PATH,pending->directory);return true;}
static void remove_operation(const wchar_t* directory){if(pending){l4_journal_close(pending);pending=NULL;}wchar_t file[MAX_PATH];swprintf_s(file,MAX_PATH,L"%ls\\journal.bin",directory);CHECK(DeleteFileW(file));CHECK(RemoveDirectoryW(directory));}
int wmain(void){
    wchar_t temp[MAX_PATH],base[MAX_PATH],pf[MAX_PATH],pd[MAX_PATH],file[MAX_PATH],directory[MAX_PATH],alias[MAX_PATH];CHECK(GetTempPathW(MAX_PATH,temp));
    swprintf_s(base,MAX_PATH,L"%lsL4WorkerEntry-%lu-%llu",temp,GetCurrentProcessId(),GetTickCount64());CHECK(CreateDirectoryW(base,NULL));
    swprintf_s(pf,MAX_PATH,L"%ls\\PF",base);swprintf_s(pd,MAX_PATH,L"%ls\\PD",base);CHECK(l4_layout_from_roots(&source_layout,pf,pd,L"1.13.6") && l4_layout_prepare(&source_layout));
    L4Layout updater_layout;CHECK(l4_layout_from_roots(&updater_layout,pf,pd,L"1.13.5"));
    CHECK(l4_layout_prepare_installer(&updater_layout,file));wcscpy_s(updater_path,MAX_PATH,file);strcpy_s(updater_info.version,32,"1.13.5");strcpy_s(updater_info.arch,8,"x86");
    BYTE bytes[32];memset(bytes,7,sizeof(bytes));HANDLE h=CreateFileW(file,GENERIC_WRITE,0,NULL,CREATE_NEW,0,NULL);CHECK(h!=INVALID_HANDLE_VALUE);DWORD wrote;
    CHECK(WriteFile(h,bytes,sizeof(bytes),&wrote,NULL) && wrote==sizeof(bytes));CHECK(CloseHandle(h));model_installer.size=32;CHECK(l4_store_hash(bytes,sizeof(bytes),NULL,0,model_installer.sha256));
    DWORD sid_size=sizeof(system_sid);CHECK(CreateWellKnownSid(WinLocalSystemSid,NULL,system_sid,&sid_size));sid_size=sizeof(other_sid);CHECK(CreateWellKnownSid(WinBuiltinUsersSid,NULL,other_sid,&sid_size));
    modeled_admission.sequence=64;modeled_admission.parent_pid=777;FILETIME exit,kernel,user;CHECK(GetProcessTimes(GetCurrentProcess(),&modeled_admission.parent_created,&exit,&kernel,&user));
    modeled_admission.helper.size=123;memset(modeled_admission.helper.sha256,1,32);
    modeled_admission.old_size=12;modeled_admission.new_size=13;modeled_admission.start_type=SERVICE_AUTO_START;wcscpy_s(modeled_admission.before,2048,L"old model_supervisor");wcscpy_s(modeled_admission.after,2048,L"new model_supervisor");
    wcscpy_s(model_supervisor.service,32,L"L4Superv");wcscpy_s(model_supervisor.before.image_path,2048,modeled_admission.before);wcscpy_s(model_supervisor.after,2048,modeled_admission.after);
    model_supervisor.before.start_type=modeled_admission.start_type;model_supervisor.before_size=12;model_supervisor.size=13;
    SetupWorkerEngine engine={preflight,apply};WorkerEntry entry={0};entry.layout=source_layout;entry.arch="x86";entry.engine=&engine;wcscpy_s(entry.executable,MAX_PATH,file);wcscpy_s(entry.operation,37,L"17730000-0000-4000-8000-000000000001");
    wcscpy_s(entry.version,32,L"1.13.6");wcscpy_s(entry.updater_version,32,L"1.13.5");
    reset();entry.engine=NULL;CHECK(execute(&entry)==ERROR_CALL_NOT_IMPLEMENTED && !accepts);SetupWorkerEngine incomplete={preflight,NULL};entry.engine=&incomplete;
    CHECK(execute(&entry)==ERROR_CALL_NOT_IMPLEMENTED && !accepts);entry.engine=&engine;
    CHECK(setup_worker_compiled_engine() && setup_worker_compiled_engine()->preflight==preflight && setup_worker_compiled_engine()->execute==apply);
    CHECK(start(1,directory));CHECK(execute(&entry)==ERROR_GEN_FAILURE && executions==1 && preflights==1 && signatures==1 && rechecks==2 && receipt_loads==1 && !receipt_live);remove_operation(directory);
    for(int test=1;test<=24;test++){reset();fault=test;if(test==21)model_helper.sha256[0]^=1;CHECK(start(20+test,directory));DWORD result=execute(&entry);CHECK(result!=ERROR_SUCCESS && result!=ERROR_GEN_FAILURE && !executions && !receipt_live);remove_operation(directory);}
    reset();CHECK(start(60,directory));swprintf_s(alias,MAX_PATH,L"%ls\\alias.exe",base);CHECK(CreateHardLinkW(alias,file,NULL));CHECK(execute(&entry)==ERROR_INVALID_DATA && !signatures && !executions);CHECK(DeleteFileW(alias));remove_operation(directory);
    reset();CHECK(start(61,directory));model_installer.sha256[0]^=1;CHECK(execute(&entry)==ERROR_CRC && !signatures && !executions);model_installer.sha256[0]^=1;remove_operation(directory);
    reset();CHECK(start(62,directory));swprintf_s(alias,MAX_PATH,L"%ls:unexpected",file);h=CreateFileW(alias,GENERIC_WRITE,0,NULL,CREATE_NEW,0,NULL);CHECK(h!=INVALID_HANDLE_VALUE);
    CHECK(WriteFile(h,bytes,sizeof(bytes),&wrote,NULL) && wrote==sizeof(bytes));CHECK(CloseHandle(h));CHECK(execute(&entry)==ERROR_INVALID_DATA && !signatures && !executions);
    CHECK(DeleteFileW(alias));remove_operation(directory);
    DWORD result=99;wchar_t* plain[]={L"test.exe",L"--fresh-install"};CHECK(!setup_worker_entry(2,plain,&engine,&result) && result==99);
    wchar_t* args[]={L"test.exe",L"--update-worker",L"--source-version",L"1.13.6",L"--operation",L"17730000-0000-4000-8000-000000000031",L"--arch",L"x86",L"--updater-version",L"1.13.5"};
    CHECK(setup_worker_entry(10,args,&engine,&result) && result==ERROR_ACCESS_DENIED);CHECK(setup_worker_entry(7,args,&engine,&result) && result==ERROR_INVALID_PARAMETER);
    args[5]=L"00000000-0000-0000-0000-000000000000";CHECK(setup_worker_entry(10,args,&engine,&result) && result==ERROR_INVALID_PARAMETER);
    args[5]=L"17730000-0000-4000-8000-0000000000AB";CHECK(setup_worker_entry(10,args,&engine,&result) && result==ERROR_INVALID_PARAMETER);
    args[5]=L"17730000-0000-4000-8000-000000000031";args[7]=L"arm64";CHECK(setup_worker_entry(10,args,&engine,&result) && result==ERROR_INVALID_PARAMETER);
    args[7]=L"x86";args[3]=L"../escape";CHECK(setup_worker_entry(10,args,&engine,&result) && result==ERROR_INVALID_PARAMETER);
    CHECK(DeleteFileW(file));wchar_t* slash=wcsrchr(file,L'\\');*slash=0;CHECK(RemoveDirectoryW(file));slash=wcsrchr(file,L'\\');*slash=0;CHECK(RemoveDirectoryW(file));
    swprintf_s(file,MAX_PATH,L"%ls\\deployment.lock",source_layout.operations);CHECK(DeleteFileW(file));CHECK(RemoveDirectoryW(source_layout.operations));CHECK(RemoveDirectoryW(source_layout.cache));CHECK(RemoveDirectoryW(source_layout.staging));
    swprintf_s(file,MAX_PATH,L"%ls\\update",source_layout.data);CHECK(RemoveDirectoryW(file));CHECK(RemoveDirectoryW(source_layout.config));CHECK(RemoveDirectoryW(source_layout.state));CHECK(RemoveDirectoryW(source_layout.logs));
    swprintf_s(file,MAX_PATH,L"%ls\\releases",source_layout.binaries);CHECK(RemoveDirectoryW(file));CHECK(RemoveDirectoryW(source_layout.launchers));CHECK(RemoveDirectoryW(source_layout.binaries));CHECK(RemoveDirectoryW(source_layout.data));CHECK(RemoveDirectoryW(base));
    printf("Worker entry: %u checks, %u failures; actual file/ACL/CNG/journal, admission/SCM/signature modeled, no update success\n",checks,failures);return failures?1:0;
}
bool setup_remote_launch_failure_finish(L4Journal* j,DWORD error,DWORD stage,DWORD cleanup,L4RemoteLaunchFailure* result){
    CHECK(j && error && stage==L4_START_RUNNING && !cleanup);failure_reports++;memset(result,0,sizeof(*result));
    /* Report failure must never replace the original admission/execution error. */
    SetLastError(ERROR_WRITE_FAULT);return false;
}
