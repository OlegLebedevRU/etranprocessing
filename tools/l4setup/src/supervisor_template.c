#include "supervisor_template.h"
#include "../../l4common/journal_internal.h"
#include "../../l4common/update_state.h"
#include "../../l4common/communication_plan.h"
#include <stdlib.h>
#include <string.h>
struct SetupSupervisorTemplate{L4RecoveryPlan plan;SetupOperationPlan* operation;BYTE* config;DWORD config_size;HANDLE process;ULONGLONG config_sequence,generation;wchar_t uuid[37];char arch[8];};
static bool fail(DWORD error){SetLastError(error);return false;}
static ULONGLONG utc(void){FILETIME t;GetSystemTimeAsFileTime(&t);return ((ULONGLONG)t.dwHighDateTime<<32)|t.dwLowDateTime;}
static bool checkpoint(ULONGLONG end,const volatile LONG* cancelled){if(cancelled && InterlockedCompareExchange((volatile LONG*)cancelled,0,0))return fail(ERROR_CANCELLED);return GetTickCount64()<end?true:fail(ERROR_TIMEOUT);}
static bool history_visit(DWORD kind,ULONGLONG seq,const void* bytes,DWORD size,void* context){
    (void)seq;(void)bytes;(void)size;(void)context;
    switch(kind){case 92:case 93:case 60:case 61:case 62:case 20:case 63:case 10:case 64:return true;default:return fail(ERROR_INVALID_STATE);}
}
/* No logon/SeDebug/caller-token fallback. Query only exact existing supervisor. */
static bool current(const L4ServiceSwitch* expected,DWORD* pid){
    if(!expected || wcscmp(expected->service,L"L4Superv"))return fail(ERROR_INVALID_DATA);
    SC_HANDLE manager=OpenSCManagerW(NULL,NULL,SC_MANAGER_CONNECT);if(!manager)return false;
    SC_HANDLE service=OpenServiceW(manager,L"L4Superv",SERVICE_QUERY_CONFIG|SERVICE_QUERY_STATUS);DWORD code=GetLastError();CloseServiceHandle(manager);if(!service)return fail(code);
    DWORD size=0;QueryServiceConfigW(service,NULL,0,&size);bool ok=GetLastError()==ERROR_INSUFFICIENT_BUFFER && size>=sizeof(QUERY_SERVICE_CONFIGW) && size<=65536;
    QUERY_SERVICE_CONFIGW* config=ok?malloc(size):NULL;if(ok && !config){ok=false;SetLastError(ERROR_NOT_ENOUGH_MEMORY);}
    if(ok)ok=QueryServiceConfigW(service,config,size,&size)!=0;
    if(ok && (config->dwServiceType!=SERVICE_WIN32_OWN_PROCESS || config->dwStartType!=expected->before.start_type ||
        !config->lpBinaryPathName || wcscmp(config->lpBinaryPathName,expected->before.image_path) || !config->lpServiceStartName || _wcsicmp(config->lpServiceStartName,L"LocalSystem")))ok=fail(ERROR_REVISION_MISMATCH);
    SERVICE_STATUS_PROCESS status={0};if(ok)ok=QueryServiceStatusEx(service,SC_STATUS_PROCESS_INFO,(BYTE*)&status,sizeof(status),&size)!=0;
    if(ok && (status.dwCurrentState!=SERVICE_RUNNING || !status.dwProcessId))ok=fail(ERROR_SERVICE_NOT_ACTIVE);if(ok)*pid=status.dwProcessId;
    code=GetLastError();free(config);CloseServiceHandle(service);return ok?true:fail(code?code:ERROR_INVALID_DATA);
}
static bool process_identity(const SetupSupervisorTemplate* t){
    const L4ServiceSwitch* source=setup_operation_switch(t->operation,0,3);DWORD pid=0;FILETIME birth,exit,kernel,user;
    if(!current(source,&pid) || pid!=t->plan.supervisor_pid || WaitForSingleObject(t->process,0)!=WAIT_TIMEOUT ||
        !GetProcessTimes(t->process,&birth,&exit,&kernel,&user) || CompareFileTime(&birth,&t->plan.supervisor_created))return fail(ERROR_RETRY);
    wchar_t image[MAX_PATH],expected[MAX_PATH];DWORD n=MAX_PATH;const wchar_t* end=wcschr(source->before.image_path+1,L'"');
    if(source->before.image_path[0]!=L'"' || !end || end-source->before.image_path-1>=MAX_PATH)return fail(ERROR_INVALID_DATA);
    wcsncpy_s(expected,MAX_PATH,source->before.image_path+1,(size_t)(end-source->before.image_path-1));
    if(!QueryFullProcessImageNameW(t->process,0,image,&n) || _wcsicmp(image,expected))return fail(ERROR_REVISION_MISMATCH);
    HANDLE token=NULL;BYTE owner[sizeof(TOKEN_USER)+SECURITY_MAX_SID_SIZE];DWORD size=0,session=~0u;TOKEN_TYPE type=TokenImpersonation;
    SetLastError(ERROR_SUCCESS);bool ok=OpenProcessToken(t->process,TOKEN_QUERY,&token) && GetTokenInformation(token,TokenUser,owner,sizeof(owner),&size) &&
        IsWellKnownSid(((TOKEN_USER*)owner)->User.Sid,WinLocalSystemSid) && GetTokenInformation(token,TokenType,&type,sizeof(type),&size) && type==TokenPrimary &&
        GetTokenInformation(token,TokenSessionId,&session,sizeof(session),&size) && session==0;
    DWORD code=GetLastError();if(token)CloseHandle(token);if(!ok)return fail(code?code:ERROR_ACCESS_DENIED);
    return current(source,&pid) && pid==t->plan.supervisor_pid && GetProcessTimes(t->process,&birth,&exit,&kernel,&user) &&
        !CompareFileTime(&birth,&t->plan.supervisor_created) && WaitForSingleObject(t->process,0)==WAIT_TIMEOUT?true:fail(ERROR_RETRY);
}
static bool config_decode(SetupSupervisorTemplate* t){
    const BYTE* b=t->config;DWORD size=t->config_size;if(size<24)return fail(ERROR_INVALID_DATA);
    DWORD version=l4_store_get32(b),exists=l4_store_get32(b+4),path=l4_store_get32(b+8),old=l4_store_get32(b+12),candidate=l4_store_get32(b+16),sd=l4_store_get32(b+20);
    const char* fixed="l4superv.json";
    if(version!=1 || exists>1 || path!=strlen(fixed) || old>L4_RECOVERY_CONFIG_LIMIT || candidate>L4_RECOVERY_CONFIG_LIMIT ||
        (!exists && old) || !sd || sd>4096 || (ULONGLONG)24+path+old+candidate+sd!=size || memcmp(b+24,fixed,path))return fail(ERROR_INVALID_DATA);
    t->plan.old_exists=exists!=0;t->plan.old_config=b+24+path;t->plan.old_config_size=old;t->plan.new_config=t->plan.old_config+old;
    t->plan.new_config_size=candidate;t->plan.config_sd=t->plan.new_config+candidate;t->plan.config_sd_size=sd;return true;
}
static bool config_member(L4Journal* j,SetupSupervisorTemplate* t){
    BYTE* record=NULL;DWORD size=0;if(!l4_store_find_record(j,64,t->plan.sequence,&record,&size))return false;
    DWORD configs=size>=28?l4_store_get32(record+24):0,hops=size>=28?l4_store_get32(record+20):0;
    bool ok=configs<=L4_OPERATION_CONFIG_LIMIT && hops && hops<=L4_CATALOG_MAX_RELEASES && size==28+8*configs+32*hops;
    for(unsigned i=0;ok && i<configs;i++){ULONGLONG seq=l4_store_get64(record+28+i*8);BYTE* b=NULL;DWORD n=0;ok=l4_store_find_record(j,20,seq,&b,&n);
        if(ok && n>=24 && l4_store_get32(b+8)==sizeof("l4superv.json")-1 && n>=24+sizeof("l4superv.json")-1 && !memcmp(b+24,"l4superv.json",sizeof("l4superv.json")-1)){
            if(t->config){free(b);ok=fail(ERROR_DUP_NAME);break;}t->config=b;t->config_size=n;t->config_sequence=seq;b=NULL;
        }free(b);
    }DWORD code=GetLastError();free(record);if(!ok)return fail(code?code:ERROR_INVALID_DATA);
    return t->config && l4_config_verify(j,t->config_sequence,false) && config_decode(t)?true:fail(GetLastError()?GetLastError():ERROR_FILE_NOT_FOUND);
}
void setup_supervisor_template_free(SetupSupervisorTemplate* t){if(t){if(t->process)CloseHandle(t->process);setup_operation_free(t->operation);free(t->config);free(t);}}
const L4RecoveryPlan* setup_supervisor_template_plan(const SetupSupervisorTemplate* t){return t?&t->plan:NULL;}
static bool verify_current(L4Journal* j,const SetupSupervisorTemplate* t){
    if(!j || !t || !j->lock || j->lock==INVALID_HANDLE_VALUE || j->poisoned || memcmp(j->header+8,&t->plan.operation,16) ||
        t->plan.worker_pid || t->plan.worker_created.dwLowDateTime || t->plan.worker_created.dwHighDateTime)return fail(ERROR_INVALID_STATE);
    const wchar_t* uuid=wcsrchr(j->directory,L'\\');if(!uuid || wcscmp(uuid+1,t->uuid))return fail(ERROR_REVISION_MISMATCH);
    if(!l4_remote_host_self(&j->layout,t->uuid,t->arch))return false;
    ULONGLONG now=utc();if(now<t->plan.armed_utc || now>=t->plan.deadline_utc)return fail(ERROR_TIME_SKEW);
    L4UpdateState state={0};if(!l4_update_state_read(&j->layout,&state))return false;if(state.window || state.generation!=t->generation)return fail(ERROR_INVALID_STATE);
    SetupOperationPlan* live=NULL;bool ok=setup_update_load_operation(j,t->plan.sequence,&live);
    if(ok){const L4ServiceSwitch* a=setup_operation_switch(live,0,3),*b=setup_operation_switch(t->operation,0,3);ok=a && b && !memcmp(a,b,sizeof(*a));if(!ok)SetLastError(ERROR_REVISION_MISMATCH);}
    DWORD code=GetLastError();setup_operation_free(live);if(!ok)return fail(code?code:ERROR_INVALID_DATA);
    if(!l4_config_verify(j,t->config_sequence,false) || !process_identity(t))return false;
    now=utc();if(now<t->plan.armed_utc || now>=t->plan.deadline_utc)return fail(ERROR_TIME_SKEW);
    memset(&state,0,sizeof(state));if(!l4_update_state_read(&j->layout,&state))return false;
    return !state.window && state.generation==t->generation?true:fail(ERROR_INVALID_STATE);
}
bool setup_supervisor_template_verify(L4Journal* j,const SetupSupervisorTemplate* t){
    if(!j || !t)return fail(ERROR_INVALID_PARAMETER);return l4_journal_replay(j,history_visit,NULL) && verify_current(j,t);
}
typedef struct {const BYTE* recovery;DWORD size;const L4RecoveryTask* task;ULONGLONG sequence;unsigned phase;bool communication,selected;} StartedHistory;
static bool started_history(DWORD kind,ULONGLONG sequence,const void* bytes,DWORD size,void* context){
    StartedHistory* h=context;
    if(!h->phase && kind==66){if(!h->selected || sequence<=h->sequence || size!=h->size || !bytes || memcmp(bytes,h->recovery,size))return fail(ERROR_REVISION_MISMATCH);h->phase=1;return true;}
    if(h->phase==1 && kind==67){if(!bytes || size!=wcslen(h->task->xml)*sizeof(wchar_t) || memcmp(bytes,h->task->xml,size))return fail(ERROR_REVISION_MISMATCH);h->phase=2;return true;}
    if(h->phase==2 && h->communication && kind==70){h->phase=3;return true;}
    if(h->phase || kind==67 || kind==70)return fail(ERROR_INVALID_STATE);
    if(h->selected)return fail(ERROR_INVALID_STATE);if(kind==64){if(sequence!=h->sequence)return fail(ERROR_REVISION_MISMATCH);h->selected=true;}
    return history_visit(kind,sequence,bytes,size,NULL);
}
bool setup_supervisor_template_verify_started(L4Journal* j,const SetupSupervisorTemplate* t,L4WorkerJob* worker,
    const L4RecoveryHelper* helper,DWORD overhead,bool communication){
    if(!j || !t || !worker || !helper)return fail(ERROR_INVALID_PARAMETER);if(!verify_current(j,t))return false;
    L4RecoveryGuard* guard=NULL;L4CommunicationPin* pin=NULL;BYTE *expected=NULL,*stored=NULL;DWORD expected_size=0,stored_size=0;
    L4RecoveryPlan actual=t->plan;HANDLE process=l4_worker_job_process(worker);FILETIME exit,kernel,user;L4RecoveryAction action;L4RecoveryTask task;
    bool ok=process && (actual.worker_pid=GetProcessId(process)) && GetProcessTimes(process,&actual.worker_created,&exit,&kernel,&user) &&
        l4_worker_job_verify(worker,&actual) && l4_recovery_encode(&j->layout,&actual,&expected,&expected_size) &&
        l4_recovery_open(&j->layout,t->uuid,1000,&guard);
    if(ok)ok=l4_recovery_action(guard,utc(),false,&action) && action==L4_RECOVERY_WAIT &&
        l4_recovery_encode(&j->layout,l4_recovery_plan(guard),&stored,&stored_size) && stored_size==expected_size && !memcmp(stored,expected,expected_size) &&
        l4_recovery_task_spec(&j->layout,&actual,helper,overhead,&task);
    if(guard)l4_recovery_release(guard);
    StartedHistory history={expected,expected_size,&task,t->plan.sequence,0,communication,false};
    if(ok)ok=l4_journal_replay(j,started_history,&history) && history.phase==(communication?3u:2u);
    if(ok && communication){ok=l4_communication_plan_open(&j->layout,t->uuid,&pin);const L4CommunicationPlan* p=pin?l4_communication_pinned_plan(pin):NULL;
        if(ok)ok=p && l4_communication_plan_verify_journal(j,pin) && !memcmp(&p->operation,&actual.operation,16) && p->sequence==actual.sequence &&
            t->generation!=~0ull && p->generation==t->generation+1 && p->worker_pid==actual.worker_pid && !CompareFileTime(&p->worker_created,&actual.worker_created) &&
            p->supervisor_pid==actual.supervisor_pid && !CompareFileTime(&p->supervisor_created,&actual.supervisor_created) && p->armed_utc==actual.armed_utc &&
            p->deadline_utc<actual.deadline_utc && (ULONGLONG)p->budget.total_ms*10000<actual.deadline_utc-p->deadline_utc;
    }
    if(ok)ok=l4_recovery_task_audit(j,helper,overhead) && l4_worker_job_verify(worker,&actual) && verify_current(j,t) &&
        l4_recovery_relock(guard,1000) && l4_recovery_action(guard,utc(),false,&action) && action==L4_RECOVERY_WAIT;
    DWORD error=GetLastError();l4_communication_plan_close(pin);l4_recovery_close(guard);free(expected);free(stored);return ok?true:fail(error?error:ERROR_REVISION_MISMATCH);
}
#define REQUIRE(call) do{SetLastError(ERROR_SUCCESS);if(!(call)){error=GetLastError()?GetLastError():ERROR_INVALID_DATA;goto done;}}while(0)
bool setup_supervisor_template_prepare(L4Journal* j,const SetupRemoteWorkerPlan* worker,ULONGLONG armed,ULONGLONG deadline,DWORD recovery,DWORD timeout,
    const volatile LONG* cancelled,SetupSupervisorTemplate** result){
    if(!result)return fail(ERROR_INVALID_PARAMETER);*result=NULL;if(!j || !worker || !armed || deadline<=armed || !recovery || recovery>300000 || timeout<100 || timeout>600000 ||
        !j->lock || j->lock==INVALID_HANDLE_VALUE || j->poisoned)return fail(ERROR_INVALID_PARAMETER);
    ULONGLONG end=GetTickCount64()+timeout;DWORD error=ERROR_INVALID_DATA;SetupSupervisorTemplate* t=NULL;L4UpdateState state={0};
    REQUIRE(checkpoint(end,cancelled));const wchar_t* uuid=setup_remote_worker_plan_uuid(worker);const char* arch=setup_remote_worker_plan_arch(worker);
    const wchar_t* actual=wcsrchr(j->directory,L'\\');if(!uuid || wcslen(uuid)!=36 || !actual || wcscmp(actual+1,uuid) || !arch || (strcmp(arch,"x86") && strcmp(arch,"x64"))){error=ERROR_REVISION_MISMATCH;goto done;}
    REQUIRE(l4_remote_host_self(&j->layout,uuid,arch));REQUIRE(l4_journal_replay(j,history_visit,NULL));REQUIRE(l4_update_state_read(&j->layout,&state));
    if(state.window){error=ERROR_INVALID_STATE;goto done;}t=calloc(1,sizeof(*t));if(!t){error=ERROR_NOT_ENOUGH_MEMORY;goto done;}
    wcscpy_s(t->uuid,37,uuid);strcpy_s(t->arch,8,arch);t->generation=state.generation;memcpy(&t->plan.operation,j->header+8,16);t->plan.sequence=setup_remote_worker_plan_sequence(worker);
    t->plan.armed_utc=armed;t->plan.deadline_utc=deadline;t->plan.recovery_ms=recovery;
    REQUIRE(setup_update_load_operation(j,t->plan.sequence,&t->operation));const L4ServiceSwitch* s=setup_operation_switch(t->operation,0,3);
    if(!s || wcscmp(s->service,L"L4Superv")){error=ERROR_NO_MORE_ITEMS;goto done;}
    t->plan.old_size=s->before_size;t->plan.new_size=s->size;t->plan.start_type=s->before.start_type;
    memcpy(t->plan.old_sha256,s->before_sha256,32);memcpy(t->plan.new_sha256,s->sha256,32);wcscpy_s(t->plan.before,2048,s->before.image_path);wcscpy_s(t->plan.after,2048,s->after);
    REQUIRE(config_member(j,t));REQUIRE(checkpoint(end,cancelled));REQUIRE(current(s,&t->plan.supervisor_pid));
    t->process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,t->plan.supervisor_pid);REQUIRE(t->process);
    FILETIME exit,kernel,user;REQUIRE(GetProcessTimes(t->process,&t->plan.supervisor_created,&exit,&kernel,&user));
    REQUIRE(setup_supervisor_template_verify(j,t));REQUIRE(checkpoint(end,cancelled));*result=t;t=NULL;error=ERROR_SUCCESS;
done:
    setup_supervisor_template_free(t);return error?fail(error):true;
}
