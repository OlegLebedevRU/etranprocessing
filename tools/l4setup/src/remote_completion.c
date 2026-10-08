#include "remote_completion.h"
#include "remote_outcome_internal.h"
#include "../../l4common/journal_internal.h"
#include "../../l4common/proxy_certificate.h"
#include "../../l4common/worker_handoff.h"
#include "../../l4common/recovery_store.h"
#include <stdlib.h>
#include <string.h>

struct SetupRemoteCompletion {
    L4Journal* original;const SetupOperationPlan* operation;
    SetupRemoteCommit value;SetupUpdaterIdentity* updater;
    L4BootstrapPlan target;L4AccessActors actors;
    L4UpdateState before;DWORD worker_pid;FILETIME worker_birth;
    L4WorkerAdmission admission;
    ULONGLONG committed_sequence;bool attempted,settlement_attempted;
};
static bool fail(DWORD e){SetLastError(e);return false;}
static ULONGLONG utc(void){FILETIME t;GetSystemTimeAsFileTime(&t);return ((ULONGLONG)t.dwHighDateTime<<32)|t.dwLowDateTime;}
static bool same_epoch(DWORD pid,const FILETIME* birth){
    FILETIME created,ended,kernel,user;
    return pid==GetCurrentProcessId() && GetProcessTimes(GetCurrentProcess(),&created,&ended,&kernel,&user) &&
        !CompareFileTime(&created,birth)?true:fail(ERROR_REVISION_MISMATCH);
}
static bool primary_system(void){
    HANDLE impersonation=NULL,token=NULL;BYTE bytes[sizeof(TOKEN_USER)+SECURITY_MAX_SID_SIZE];DWORD n=0,session=~0u;TOKEN_TYPE type=TokenImpersonation;
    if(OpenThreadToken(GetCurrentThread(),TOKEN_QUERY,TRUE,&impersonation)){CloseHandle(impersonation);return fail(ERROR_ACCESS_DENIED);}
    if(GetLastError()!=ERROR_NO_TOKEN)return false;
    bool ok=OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token) && GetTokenInformation(token,TokenUser,bytes,sizeof(bytes),&n) &&
        IsWellKnownSid(((TOKEN_USER*)bytes)->User.Sid,WinLocalSystemSid) && GetTokenInformation(token,TokenType,&type,sizeof(type),&n) && type==TokenPrimary &&
        GetTokenInformation(token,TokenSessionId,&session,sizeof(session),&n) && session==0;
    DWORD e=GetLastError();if(token)CloseHandle(token);return ok?true:fail(e?e:ERROR_ACCESS_DENIED);
}
void setup_remote_completion_free(SetupRemoteCompletion* p){
    if(!p)return;HANDLE tokens[]={p->actors.proxy,p->actors.broker,p->actors.console,p->actors.supervisor,p->actors.desktop};
    for(unsigned i=0;i<5;i++)if(tokens[i])CloseHandle(tokens[i]);setup_updater_identity_free(p->updater);free(p);
}
static bool copy_tokens(const L4AccessActors* from,L4AccessActors* to){
    if(!from)return fail(ERROR_INVALID_DATA);HANDLE tokens[]={from->proxy,from->broker,from->console,from->supervisor,from->desktop};
    HANDLE* outputs[]={&to->proxy,&to->broker,&to->console,&to->supervisor,&to->desktop};
    for(unsigned i=0;i<5;i++)if(tokens[i] && !DuplicateHandle(GetCurrentProcess(),tokens[i],GetCurrentProcess(),outputs[i],0,FALSE,DUPLICATE_SAME_ACCESS))return false;
    return true;
}
#define REQUIRE(call) do{SetLastError(0);if(!(call)){error=GetLastError()?GetLastError():ERROR_NOT_READY;goto done;}}while(0)
bool setup_remote_completion_prepare(L4Journal* j,SetupInstalledSource* source,const SetupOperationPlan* operation,SetupRemoteCompletion** result){
    if(!result)return fail(ERROR_INVALID_PARAMETER);*result=NULL;
    if(!j || !source || !operation || setup_operation_hops(operation)!=1)return fail(ERROR_NOT_SUPPORTED);
    DWORD error=ERROR_NOT_READY;SetupRemoteCompletion* p=calloc(1,sizeof(*p));if(!p)return fail(ERROR_NOT_ENOUGH_MEMORY);
    L4WorkerAdmission admission;REQUIRE(primary_system());REQUIRE(l4_worker_recheck(j,&admission));
    REQUIRE(setup_operation_binding(operation,j,admission.sequence));REQUIRE(setup_operation_verify_images(operation));
    REQUIRE(setup_installed_source_owned_by(source,j));REQUIRE(setup_installed_source_verify(source));
    REQUIRE(l4_update_state_read(&j->layout,&p->before));
    if(p->before.window || p->before.generation!=admission.generation){error=ERROR_INVALID_STATE;goto done;}
    const SetupRootManifest* old=setup_installed_source_root(source),*expected=setup_operation_root(operation),*root=setup_operation_target_root(operation,0);
    const L4BootstrapPlan* previous=setup_installed_source_plan(source);const SetupManifest* target=setup_operation_target(operation,0);
    const L4Layout* target_layout=setup_manifest_layout(target);const wchar_t* predecessor=setup_installed_source_operation(source);
    const BYTE* old_hash=setup_root_identity(old),*expected_hash=setup_root_identity(expected),*target_hash=setup_root_identity(root);
    const L4CatalogRoute* route=l4_route_steps(setup_operation_route(operation));
    if(!previous || !target_layout || !predecessor || !old_hash || !expected_hash || memcmp(old_hash,expected_hash,32) || !target_hash || !route || route->count!=1){error=ERROR_REVISION_MISMATCH;goto done;}
    const wchar_t* id=wcsrchr(j->directory,L'\\');if(!id){error=ERROR_INVALID_NAME;goto done;}
    if(!WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,id+1,-1,p->value.operation,37,NULL,NULL) ||
       !WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,predecessor,-1,p->value.predecessor,37,NULL,NULL)){error=ERROR_INVALID_DATA;goto done;}
    p->original=j;p->operation=operation;p->value.operation_sequence=admission.sequence;p->target.layout=*target_layout;
    strcpy_s(p->value.suite_version,32,route->releases[0].version);strcpy_s(p->value.arch,8,l4_route_arch(setup_operation_route(operation)));memcpy(p->value.suite_root,target_hash,32);
    REQUIRE(setup_updater_identity_from_source(source,&p->updater));REQUIRE(copy_tokens(setup_installed_source_actors(source),&p->actors));
    const SetupRootAsset* asset=setup_updater_identity_asset(p->updater);const BYTE* updater_root=setup_updater_identity_root(p->updater),*publisher=setup_updater_identity_publisher(p->updater);
    if(!asset || !updater_root || !publisher){error=ERROR_INVALID_DATA;goto done;}
    p->value.updater=*asset;memcpy(p->value.updater_root,updater_root,32);memcpy(p->value.publisher,publisher,32);
    strcpy_s(p->value.updater_version,32,setup_updater_identity_version(p->updater));
    if(!WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,setup_updater_identity_origin(p->updater),-1,p->value.updater_origin,37,NULL,NULL)){error=ERROR_INVALID_DATA;goto done;}
    unsigned count=0;const ULONGLONG* configs=setup_operation_configs(operation,&count);
    if(!configs || count!=12){error=ERROR_INVALID_DATA;goto done;}memcpy(p->value.configs,configs,sizeof(p->value.configs));
    for(unsigned s=0;s<4;s++){
        const L4ServiceSwitch* change=setup_operation_switch(operation,0,s);
        if(!change || wcscmp(previous->services[s],change->service) || wcscmp(previous->commands[s],change->before.image_path) || previous->start_types[s]!=change->before.start_type){error=ERROR_REVISION_MISMATCH;goto done;}
        wcscpy_s(p->target.services[s],32,change->service);wcscpy_s(p->target.commands[s],2048,change->after);p->target.start_types[s]=change->before.start_type;
        p->target.sizes[s]=change->size;memcpy(p->target.sha256[s],change->sha256,32);
        wcscpy_s(p->value.commands[s],2048,change->after);p->value.start_types[s]=change->before.start_type;
    }
    BYTE* raw=NULL;DWORD size=0;bool hashed=l4_store_find_record(j,L4_RECORD_OPERATION_PLAN,admission.sequence,&raw,&size) && l4_store_hash(raw,size,NULL,0,p->value.operation_sha256);free(raw);REQUIRE(hashed);
    FILETIME ended,kernel,user;p->worker_pid=GetCurrentProcessId();REQUIRE(GetProcessTimes(GetCurrentProcess(),&p->worker_birth,&ended,&kernel,&user));
    REQUIRE(setup_installed_source_verify(source));REQUIRE(l4_worker_recheck(j,&admission));p->admission=admission;*result=p;return true;
done:setup_remote_completion_free(p);return fail(error);
}
static bool state_equal(const L4UpdateState* a,const L4UpdateState* b){return !memcmp(a->owner,b->owner,40) && a->window==b->window && a->generation==b->generation && a->plan_sequence==b->plan_sequence && a->deadline_utc==b->deadline_utc;}
static DWORD remaining(ULONGLONG end,const L4UpdateState* state){
    ULONGLONG tick=GetTickCount64(),now=utc();if(tick>=end || now>=state->deadline_utc)return 0;
    ULONGLONG time=(state->deadline_utc-now)/10000,budget=end-tick;if(time<budget)budget=time;return budget<=300000?(DWORD)budget:300000;
}
static bool local(SetupRemoteCompletion* p,L4Journal* j,const SetupOperationPlan* op,SetupServiceTransaction* tx,SetupRemoteService* services[4],const L4UpdateState* state,ULONGLONG end,bool capture){
    if(p->original!=j || p->operation!=op || !primary_system() || !same_epoch(p->worker_pid,&p->worker_birth))return fail(ERROR_ACCESS_DENIED);
    DWORD budget=remaining(end,state);if(!budget)return fail(ERROR_TIMEOUT);L4UpdateState observed;
    if(!l4_update_state_read(&j->layout,&observed) || !state_equal(state,&observed))return fail(ERROR_REVISION_MISMATCH);
    if(!setup_operation_binding(op,j,p->value.operation_sequence) || !setup_operation_verify_images(op) || !setup_updater_identity_verify(p->updater))return false;
    for(unsigned i=0;i<12;i++)if(!remaining(end,state) || !l4_config_verify(j,p->value.configs[i],true))return false;
    if(!l4_access_verify(&p->target.layout,&p->actors) || !setup_service_transaction_complete(tx,j,op,services,state,remaining(end,state)))return false;
    for(unsigned i=0;i<4;i++){
        DWORD pid=0;FILETIME birth;if(!setup_remote_service_running(services[i],&pid,&birth))return false;
        if(capture){p->value.pids[i]=pid;p->value.births[i]=birth;}
        else if(pid!=p->value.pids[i] || CompareFileTime(&birth,&p->value.births[i]))return fail(ERROR_REVISION_MISMATCH);
    }
    if(!l4_update_state_read(&j->layout,&observed) || !state_equal(state,&observed))return fail(ERROR_REVISION_MISMATCH);
    return remaining(end,state)?true:fail(ERROR_TIMEOUT);
}
static bool settle_recovery(SetupRemoteCompletion* p,L4Journal* j,const L4UpdateState* state,ULONGLONG end){
    wchar_t operation[37];if(!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,p->value.operation,-1,operation,37))return false;
    DWORD budget=remaining(end,state);if(!budget)return fail(ERROR_TIMEOUT);
    L4CommunicationPin* communication=NULL;L4CommunicationDecision* decision=NULL;L4CommunicationPhase phase=L4_COMM_DEC_WAIT;DWORD code=0;
    bool ok=l4_communication_plan_open(&j->layout,operation,&communication);
    const L4CommunicationPlan* link=ok?l4_communication_pinned_plan(communication):NULL;
    if(ok)ok=link && link->sequence==p->value.operation_sequence && !memcmp(&link->operation,j->header+8,16) &&
        link->worker_pid==p->worker_pid && !CompareFileTime(&link->worker_created,&p->worker_birth) &&
        !memcmp(link->operation_sha256,p->value.operation_sha256,32);
    budget=remaining(end,state);if(ok)ok=budget && l4_communication_decision_open(&j->layout,operation,budget,&decision) &&
        l4_communication_decision_read(decision,&phase,&code) && phase==L4_COMM_DEC_COMMITTED && !code;
    DWORD error=GetLastError();l4_communication_decision_close(decision);l4_communication_plan_close(communication);
    if(!ok)return fail(error?error:ERROR_RECOVERY_FAILURE);
    budget=remaining(end,state);if(!budget)return fail(ERROR_TIMEOUT);L4RecoveryGuard* recovery=NULL;
    if(!l4_recovery_open(&j->layout,operation,budget,&recovery))return false;
    const L4RecoveryPlan* r=l4_recovery_plan(recovery);const L4WorkerAdmission* a=&p->admission;
    ok=r && r->sequence==p->value.operation_sequence && !memcmp(&r->operation,j->header+8,16) &&
        r->worker_pid==p->worker_pid && !CompareFileTime(&r->worker_created,&p->worker_birth) &&
        r->deadline_utc==a->deadline_utc && state->deadline_utc<r->deadline_utc &&
        r->supervisor_pid==a->supervisor_pid && !CompareFileTime(&r->supervisor_created,&a->supervisor_created) &&
        r->start_type==a->start_type && r->old_size==a->old_size && r->new_size==a->new_size &&
        !memcmp(r->old_sha256,a->old_sha256,32) && !memcmp(r->new_sha256,a->new_sha256,32) &&
        !wcscmp(r->before,a->before) && !wcscmp(r->after,a->after);
    if(ok && remaining(end,state)){p->settlement_attempted=true;ok=l4_recovery_finish(recovery,L4_RECOVERY_COMMITTED,0,utc());}
    else ok=false;
    error=GetLastError();l4_recovery_close(recovery);return ok?true:fail(error?error:ERROR_RECOVERY_FAILURE);
}
bool setup_remote_completion_settling(const SetupRemoteCompletion* p){return p && p->settlement_attempted;}
bool setup_remote_completion_commit(SetupRemoteCompletion* p,L4Journal* j,const SetupOperationPlan* op,SetupServiceTransaction* tx,SetupRemoteService* services[4],DWORD timeout,SetupRemoteCommit* result){
    if(result)memset(result,0,sizeof(*result));
    if(!p || !j || !op || !tx || !services || !result || !timeout || timeout>600000)return fail(ERROR_INVALID_PARAMETER);
    L4UpdateState state;if(!l4_update_state_read(&j->layout,&state))return false;
    if(state.window!=L4_UPDATE_OTHER_TOOLS || state.generation!=p->before.generation+2 || state.plan_sequence!=p->value.operation_sequence || strcmp(state.owner,p->value.operation))return fail(ERROR_REVISION_MISMATCH);
    ULONGLONG end=GetTickCount64()+timeout;bool capture=!p->attempted;
    if(!local(p,j,op,tx,services,&state,end,capture))return false;p->attempted=true;
    WORD http=0,mqtt=0;char certificate[64];L4Readiness options={0};L4BootstrapChecks checks;DWORD budgets[4]={60000,300000,60000,60000};
    if(!l4_proxy_signal_source(p->target.commands[0],&http,&mqtt,certificate) || mqtt!=18883)return fail(ERROR_NOT_SUPPORTED);
    options.proxy_port=http;options.broker_port=1883;
    if(!setup_readiness_checks(&options,budgets,60000,&checks))return false;
    for(unsigned i=0;i<4;i++){
        DWORD budget=remaining(end,&state);if(budget>checks.service_ms[i])budget=checks.service_ms[i];
        if(!budget)return fail(ERROR_TIMEOUT);if(!checks.probe(&p->target,i,budget,checks.context) || !local(p,j,op,tx,services,&state,end,false))return false;
    }
    DWORD budget=remaining(end,&state);if(budget>checks.barrier_ms)budget=checks.barrier_ms;
    if(!budget)return fail(ERROR_TIMEOUT);
    if(!checks.barrier(&p->target,budget,checks.context) || !local(p,j,op,tx,services,&state,end,false))return false;
    /* The immutable helper decision must win arbitration BEFORE publishing a
     * successful installed-source receipt. Once recovery STARTED, commit refuses.
     * A write failure afterwards leaves the active marker for reconciliation. */
    if(!settle_recovery(p,j,&state,end))return false;
    if(!p->value.finished_utc)p->value.finished_utc=utc();BYTE raw[SETUP_REMOTE_COMMIT_BYTES];
    if(!setup_remote_commit_encode(&p->value,raw))return false;
    if(p->committed_sequence){
        BYTE* recorded=NULL;DWORD size=0;bool ok=l4_store_find_record(j,SETUP_RECORD_REMOTE_COMMIT,p->committed_sequence,&recorded,&size) && size==sizeof(raw) && !memcmp(recorded,raw,size);free(recorded);
        if(!ok)return fail(ERROR_CRC);
    }else if(!l4_journal_append(j,SETUP_RECORD_REMOTE_COMMIT,raw,sizeof(raw),&p->committed_sequence))return false;
    *result=p->value;return true;
}
bool setup_remote_completion_finish(SetupRemoteCompletion* p,L4Journal* j,const SetupOperationPlan* op,SetupServiceTransaction* tx,SetupRemoteService* services[4],DWORD timeout,L4RemoteOutcome* result){
    if(result)memset(result,0,sizeof(*result));
    if(!p || !j || !op || !tx || !services || !result || timeout<100 || timeout>600000)return fail(ERROR_INVALID_PARAMETER);
    ULONGLONG end=GetTickCount64()+timeout;DWORD reserve=timeout/10;if(reserve>30000)reserve=30000;
    SetupRemoteCommit receipt;if(!setup_remote_completion_commit(p,j,op,tx,services,timeout-reserve,&receipt))return false;
    L4UpdateState active;if(!l4_update_state_read(&j->layout,&active) || active.window!=2 || !remaining(end,&active))return fail(ERROR_TIMEOUT);
    BYTE* bytes=NULL;DWORD size=0;BYTE proof[32];
    bool ok=l4_store_find_record(j,SETUP_RECORD_REMOTE_COMMIT,p->committed_sequence,&bytes,&size) &&
        size==SETUP_REMOTE_COMMIT_BYTES && l4_store_hash(bytes,size,NULL,0,proof);free(bytes);if(!ok)return false;
    L4RemoteOutcome expected={0},outcome;expected.result.result=L4_REMOTE_OUTCOME_SUCCESS;
    if(!setup_remote_outcome_append_bound(j,op,&expected,p->committed_sequence,proof,&outcome))return false;
    /* 103 is durable but does not itself reopen ordinary work. Repeat retained
     * epochs/configs/marker and settled recovery before the clear publication. */
    if(!local(p,j,op,tx,services,&active,end,false) || !settle_recovery(p,j,&active,end))return false;
    if(!remaining(end,&active) || !l4_update_state_publish(j,p->value.operation_sequence,active.generation,0,0))return false;
    L4UpdateState clear;if(!l4_update_state_read(&j->layout,&clear) || clear.window || clear.generation!=active.generation+1 ||
        clear.plan_sequence!=active.plan_sequence || clear.deadline_utc || strcmp(clear.owner,active.owner))return fail(ERROR_REVISION_MISMATCH);
    *result=outcome;return true;
}
