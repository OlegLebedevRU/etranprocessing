#include "remote_worker_start.h"
#include "remote_policy.h"
#include "remote_launch_failure.h"
#include "../../l4common/journal_internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
struct SetupRemoteWorkerLaunch {
    L4Layout layout;L4BootstrapPlan original;L4RemoteRequest request;
    wchar_t uuid[37],directory[MAX_PATH],command[1024],environment[2048];
    char arch[8],version[32],updater_version[32];BYTE root[32];ULONGLONG sequence;
    SetupRemoteWorkerPolicy policy;SetupSupervisorTemplate* supervisor;
    SetupRecoveryReceipt* receipt;L4RemoteHost* fence;L4RemoteHostImage image;
    L4WorkerJob* worker;bool attempted,transferred;
};
static bool fail(DWORD e){SetLastError(e);return false;}
static bool capable(void){const SetupWorkerEngine* e=setup_worker_compiled_engine();return e && e->preflight && e->execute?true:fail(ERROR_CALL_NOT_IMPLEMENTED);}
static bool policy_valid(const SetupRemoteWorkerPolicy* p){
    return p && p->timeout_ms>=100 && p->timeout_ms<=600000 && p->cleanup_ms>=1 && p->cleanup_ms<=300000 && p->recovery_ms>=1 && p->recovery_ms<=300000 && p->overhead_ms>=L4_RECOVERY_TASK_MIN_OVERHEAD_MS && p->overhead_ms<=60000 &&
        p->armed_utc && p->communication_deadline_utc>p->armed_utc && p->supervisor_deadline_utc>p->communication_deadline_utc &&
        l4_communication_native_budget_valid(&p->communication) && (ULONGLONG)p->communication.total_ms*10000<p->supervisor_deadline_utc-p->communication_deadline_utc;
}
static bool checkpoint(ULONGLONG end,const volatile LONG* c){
    if(c && InterlockedCompareExchange((volatile LONG*)c,0,0))return fail(ERROR_CANCELLED);
    return GetTickCount64()<end?true:fail(ERROR_TIMEOUT);
}
static DWORD remaining(ULONGLONG end){ULONGLONG now=GetTickCount64();return now<end?(DWORD)(end-now):0;}
static bool request_equal(const L4RemoteRequest* a,const L4RemoteRequest* b){return a && b && a->target==b->target && a->accepted_utc==b->accepted_utc && !strcmp(a->version,b->version);}
static bool original_equal(const L4BootstrapPlan* a,const L4BootstrapPlan* b){
    if(!a || !b || memcmp(&a->layout,&b->layout,sizeof(a->layout)))return false;
    for(unsigned i=0;i<L4_BOOTSTRAP_SERVICES;i++)if(wcscmp(a->services[i],b->services[i]) || wcscmp(a->commands[i],b->commands[i]) || a->start_types[i]!=b->start_types[i] || a->sizes[i]!=b->sizes[i] || memcmp(a->sha256[i],b->sha256[i],32))return false;
    return true;
}
static bool environment(SetupRemoteWorkerLaunch* p){
    wchar_t windows[MAX_PATH],system[MAX_PATH];UINT n=GetWindowsDirectoryW(windows,MAX_PATH),m=GetSystemDirectoryW(system,MAX_PATH);
    if(!n || n>=MAX_PATH || !m || m>=MAX_PATH || n<3 || windows[1]!=L':' || windows[2]!=L'\\')return fail(ERROR_BAD_PATHNAME);
    /* Native facts only. No inherited credentials, COMSPEC or user-controlled PATH. */
    /* Sorted case-insensitively, explicitly terminated by a second NUL. */
    size_t used=0;int count=swprintf_s(p->environment,2048,L"PATH=%ls;%ls;%ls",system,windows,p->layout.launchers);if(count<0)return fail(ERROR_BUFFER_OVERFLOW);used=(size_t)count+1;
    /* Known Folders expands the OS ProgramData path through SystemDrive even
     * for SYSTEM. Derive it from the native Windows directory, never getenv. */
    count=swprintf_s(p->environment+used,2048-used,L"SystemDrive=%.2ls",windows);if(count<0)return fail(ERROR_BUFFER_OVERFLOW);used+=(size_t)count+1;
    count=swprintf_s(p->environment+used,2048-used,L"SystemRoot=%ls",windows);if(count<0)return fail(ERROR_BUFFER_OVERFLOW);used+=(size_t)count+1;
    count=swprintf_s(p->environment+used,2048-used,L"WINDIR=%ls",windows);if(count<0)return fail(ERROR_BUFFER_OVERFLOW);used+=(size_t)count+1;
    if(used>=2048)return fail(ERROR_BUFFER_OVERFLOW);p->environment[used]=0;return true;
}
static bool source_check(L4Journal* j,SetupInstalledSource* source,const SetupRemoteWorkerLaunch* p){
    if(!j || !source || !p || !j->lock || j->poisoned || p->transferred || memcmp(&j->layout,&p->layout,sizeof(p->layout)))return fail(ERROR_INVALID_STATE);
    const wchar_t* id=wcsrchr(j->directory,L'\\');if(!id || wcscmp(id+1,p->uuid))return fail(ERROR_REVISION_MISMATCH);
    if(!setup_installed_source_verify(source))return false;
    const char* version=setup_installed_source_version(source),*arch=setup_installed_source_arch(source);
    const SetupRootManifest* root=setup_installed_source_updater_root(source);const SetupRootAsset* asset=setup_root_installer(root);const BYTE* hash=setup_root_identity(setup_installed_source_root(source));
    const char* updater=setup_installed_source_updater_version(source);
    if(!version || !arch || !updater || strcmp(updater,p->updater_version) || strcmp(p->image.updater_version,updater) || strcmp(version,p->version) || strcmp(arch,p->arch) || !hash || memcmp(hash,p->root,32) || !original_equal(setup_installed_source_plan(source),&p->original) || !asset || asset->size!=p->image.size || memcmp(asset->sha256,p->image.sha256,32))return fail(ERROR_REVISION_MISMATCH);
    L4RemoteRequest request;L4RemoteHostReceipt ack;FILETIME birth,exit,kernel,user;
    if(!l4_remote_request_load(j,&request) || !l4_remote_host_load(j,&ack) || !GetProcessTimes(GetCurrentProcess(),&birth,&exit,&kernel,&user))return false;
    if(ack.format_version!=2 || strcmp(ack.updater_version,p->updater_version) || !request_equal(&request,&p->request) || !request_equal(&ack.request,&p->request) || ack.pid!=GetCurrentProcessId() || ack.birth!=(((ULONGLONG)birth.dwHighDateTime<<32)|birth.dwLowDateTime) || strcmp(ack.arch,p->arch) || strcmp(ack.source_version,p->version) || ack.executable_size!=p->image.size || memcmp(ack.executable_sha256,p->image.sha256,32))return fail(ERROR_REVISION_MISMATCH);
    if(!l4_remote_host_self(&j->layout,p->uuid,p->arch) || !setup_signed_executable(p->image.file,p->image.path,setup_root_publisher(root)))return false;
    /* Repeat SCM/System/image epoch after expensive trust verification. */
    return l4_remote_host_self(&j->layout,p->uuid,p->arch) && setup_installed_source_verify(source);
}
bool setup_remote_worker_launch_wait(SetupRemoteWorkerLaunch* p,DWORD timeout,DWORD* exit_code){
    if(exit_code)*exit_code=STILL_ACTIVE;
    if(!p || !p->transferred || !p->worker || !exit_code || timeout<1 || timeout>SETUP_REMOTE_CONTROLLER_WAIT_MS)return fail(ERROR_INVALID_PARAMETER);
    HANDLE process=l4_worker_job_process(p->worker);if(!process)return fail(ERROR_INVALID_HANDLE);
    DWORD wait=WaitForSingleObject(process,timeout);
    if(wait!=WAIT_OBJECT_0)return fail(wait==WAIT_TIMEOUT?ERROR_TIMEOUT:GetLastError());
    return GetExitCodeProcess(process,exit_code)!=0;
}
bool setup_remote_worker_launch_close(SetupRemoteWorkerLaunch** owner,DWORD cleanup){
    if(!owner || cleanup<1 || cleanup>300000)return fail(ERROR_INVALID_PARAMETER);
    SetupRemoteWorkerLaunch* p=*owner;if(!p)return true;*owner=NULL;bool ok=true;DWORD error=0;
    if(p->worker && !l4_worker_job_close(&p->worker,cleanup)){ok=false;error=GetLastError();}
    setup_supervisor_template_free(p->supervisor);setup_recovery_receipt_free(p->receipt);l4_remote_host_close(p->fence);free(p);
    return ok?true:fail(error?error:ERROR_NOT_READY);
}
#define REQUIRE(call) do{SetLastError(0);if(!(call)){error=GetLastError()?GetLastError():ERROR_NOT_READY;goto done;}}while(0)
bool setup_remote_worker_launch_prepare(L4Journal* j,SetupInstalledSource* source,const SetupRemoteWorkerPlan* plan,const SetupRemoteWorkerPolicy* policy,const volatile LONG* cancel,SetupRemoteWorkerLaunch** result){
    if(!result)return fail(ERROR_INVALID_PARAMETER);*result=NULL;
    if(!j || !source || !plan || !policy_valid(policy))return fail(ERROR_INVALID_PARAMETER);
    /* The canonical child binary uses this same compile-time accessor. */
    if(!capable())return false;
    DWORD error=ERROR_NOT_READY;ULONGLONG end=GetTickCount64()+policy->timeout_ms;SetupRemoteWorkerLaunch* p=calloc(1,sizeof(*p));if(!p)return fail(ERROR_NOT_ENOUGH_MEMORY);
    p->policy=*policy;p->layout=j->layout;REQUIRE(checkpoint(end,cancel));REQUIRE(setup_installed_source_verify(source));
    const wchar_t* id=setup_remote_worker_plan_uuid(plan);const char* arch=setup_remote_worker_plan_arch(plan),*version=setup_installed_source_version(source),*updater=setup_installed_source_updater_version(source);
    const BYTE* root=setup_root_identity(setup_installed_source_root(source));const L4BootstrapPlan* original=setup_installed_source_plan(source);const L4RemoteRequest* request=setup_remote_worker_plan_request(plan);
    if(!id || wcslen(id)!=36 || !arch || (strcmp(arch,"x86") && strcmp(arch,"x64")) || !version || strlen(version)>=32 || !updater || !*updater || strlen(updater)>=32 || !root || !original || !request || request->target!=L4_REMOTE_SUITE || !original_equal(original,setup_remote_worker_plan_source(plan)) || strcmp(arch,setup_installed_source_arch(source))){error=ERROR_REVISION_MISMATCH;goto done;}
    wcscpy_s(p->uuid,37,id);strcpy_s(p->arch,8,arch);strcpy_s(p->version,32,version);strcpy_s(p->updater_version,32,updater);memcpy(p->root,root,32);p->original=*original;p->request=*request;p->sequence=setup_remote_worker_plan_sequence(plan);
    if(!p->sequence){error=ERROR_INVALID_DATA;goto done;}
    REQUIRE(l4_remote_host_pin_self(&j->layout,p->uuid,p->arch,&p->fence,&p->image));REQUIRE(source_check(j,source,p));
    wcscpy_s(p->directory,MAX_PATH,p->image.path);wchar_t* slash=wcsrchr(p->directory,L'\\');if(!slash){error=ERROR_BAD_PATHNAME;goto done;}*slash=0;
    wchar_t wide_version[32],wide_arch[8],wide_updater[32];if(!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,version,-1,wide_version,32) || !MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,arch,-1,wide_arch,8) || !MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,updater,-1,wide_updater,32)){error=ERROR_INVALID_DATA;goto done;}
    if(swprintf_s(p->command,1024,L"\"%ls\" --update-worker --source-version %ls --updater-version %ls --operation %ls --arch %ls",p->image.path,wide_version,wide_updater,p->uuid,wide_arch)<0){error=ERROR_BUFFER_OVERFLOW;goto done;}
    REQUIRE(environment(p));REQUIRE(checkpoint(end,cancel));REQUIRE(setup_recovery_receipt_load(j,p->arch,&p->receipt));
    REQUIRE(checkpoint(end,cancel));
    REQUIRE(setup_supervisor_template_prepare(j,plan,policy->armed_utc,policy->supervisor_deadline_utc,policy->recovery_ms,remaining(end),cancel,&p->supervisor));
    REQUIRE(source_check(j,source,p));REQUIRE(setup_supervisor_template_verify(j,p->supervisor));REQUIRE(checkpoint(end,cancel));
    *result=p;return true;
done:
    setup_remote_worker_launch_close(&p,policy->cleanup_ms);return fail(error);
}
bool setup_remote_worker_launch_start(L4Journal** journal,SetupInstalledSource* source,SetupRemoteWorkerLaunch* p,const L4BootstrapChecks* checks,const volatile LONG* cancel,SetupRemoteWorkerLaunchReport* report){
    if(!report)return fail(ERROR_INVALID_PARAMETER);memset(report,0,sizeof(*report));
    if(!journal || !*journal || !source || !p || p->attempted || p->transferred || !checks || !checks->probe || !checks->barrier){report->error=ERROR_INVALID_PARAMETER;return fail(report->error);}
    if(!capable()){report->error=GetLastError();return false;}
    L4Journal* j=*journal;DWORD error=ERROR_NOT_READY;ULONGLONG end=GetTickCount64()+p->policy.timeout_ms;
    const L4RecoveryHelper* helper=setup_recovery_receipt_helper(p->receipt);
    REQUIRE(checkpoint(end,cancel));REQUIRE(source_check(j,source,p));REQUIRE(setup_supervisor_template_verify(j,p->supervisor));
    REQUIRE(checkpoint(end,cancel));
    if(!helper){error=ERROR_INVALID_DATA;goto done;}
    L4WorkerStart request={0};request.executable=p->image.path;request.directory=p->directory;request.environment=p->environment;request.command=p->command;
    request.recovery=*setup_supervisor_template_plan(p->supervisor);request.helper=*helper;request.overhead_ms=p->policy.overhead_ms;request.cleanup_ms=p->policy.cleanup_ms;
    p->attempted=true;REQUIRE(l4_worker_start(j,&request,&p->worker,&report->startup));REQUIRE(checkpoint(end,cancel));
    REQUIRE(source_check(j,source,p));REQUIRE(setup_supervisor_template_verify_started(j,p->supervisor,p->worker,helper,p->policy.overhead_ms,false));
    REQUIRE(checkpoint(end,cancel));
    REQUIRE(setup_update_prepare_communication(j,p->sequence,p->worker,setup_installed_source_actors(source),checks,p->policy.armed_utc,p->policy.communication_deadline_utc,&p->policy.communication,remaining(end)));
    REQUIRE(checkpoint(end,cancel));REQUIRE(source_check(j,source,p));REQUIRE(setup_supervisor_template_verify_started(j,p->supervisor,p->worker,helper,p->policy.overhead_ms,true));
    REQUIRE(checkpoint(end,cancel));REQUIRE(l4_worker_transfer(journal,p->worker,helper,p->policy.overhead_ms));
    p->transferred=true;report->transferred=true;return true;
done:
    report->error=error;if(p->worker && !l4_worker_job_close(&p->worker,p->policy.cleanup_ms))report->cleanup_error=GetLastError()?GetLastError():ERROR_NOT_READY;
    if(!report->cleanup_error)report->cleanup_error=report->startup.cleanup_error;
    if(report->startup.plan_published || report->startup.task_attempted){
        L4RemoteLaunchFailure failure;
        report->recovery_reported=setup_remote_launch_failure_finish(j,error,report->startup.stage,report->cleanup_error,&failure);
        if(!report->recovery_reported)report->result_error=GetLastError()?GetLastError():ERROR_NOT_READY;
    }
    return fail(error);
}
