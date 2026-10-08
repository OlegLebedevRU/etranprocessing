#include "remote_service_transaction.h"
#include "remote_commit.h"
#include "../../l4common/remote_outcome.h"
#include "../../l4common/journal_internal.h"
#include "../../l4common/worker_handoff.h"
#include "../../l4common/recovery_store.h"
#include <stdlib.h>
#include <string.h>
/* Fixed ABI1: magic/version/action/service/window/generation/deadline/64/10/
 * retained PID/reserved/birth/intent-reference/reserved. Journal owns UUID. */
#define ACTION_BYTES 112u
typedef struct { DWORD action,service,window,pid,phase;ULONGLONG generation,deadline,plan,reference,birth,intent; } Action;
struct SetupServiceTransaction { L4Journal* journal;const SetupOperationPlan* operation;
    SetupRemoteService* services[4];unsigned done[4];Action pending;ULONGLONG pending_sequence;
    bool recovery_required,blocked,readonly,sealed,restoring;DWORD pending_kind,config_direction;
    ULONGLONG config_pending,config_intent,restore_sequences[4],commit_sequence;BYTE commit_hash[32];bool outcome_seen,clear_seen;unsigned restore_done[4];Action windows[2],completed[4],restored[4]; };
static bool fail(DWORD error){SetLastError(error);return false;}
static ULONGLONG birth(FILETIME t){return ((ULONGLONG)t.dwHighDateTime<<32)|t.dwLowDateTime;}
static bool same_state(const L4UpdateState* a,const L4UpdateState* b){return !strcmp(a->owner,b->owner) && a->window==b->window && a->generation==b->generation && a->plan_sequence==b->plan_sequence && a->deadline_utc==b->deadline_utc;}
static bool authority(SetupServiceTransaction* t){if(!t || !t->journal || !t->journal->lock || t->journal->lock==INVALID_HANDLE_VALUE || t->journal->poisoned)return fail(ERROR_INVALID_STATE);if(setup_operation_hops(t->operation)!=1)return fail(ERROR_NOT_SUPPORTED);return setup_operation_binding(t->operation,t->journal,setup_operation_sequence(t->operation)) && setup_operation_verify_images(t->operation);}
static void encode(const Action* a,BYTE b[ACTION_BYTES]){memset(b,0,ACTION_BYTES);memcpy(b,"L4SCA001",8);l4_store_u32(b+8,1);l4_store_u32(b+12,a->action);l4_store_u32(b+16,a->service);l4_store_u32(b+20,a->window);l4_store_u64(b+24,a->generation);l4_store_u64(b+32,a->deadline);l4_store_u64(b+40,a->plan);l4_store_u64(b+48,a->reference);l4_store_u32(b+56,a->pid);l4_store_u64(b+64,a->birth);l4_store_u64(b+72,a->intent);l4_store_u32(b+80,a->phase);}
static bool decode(const BYTE* b,DWORD size,Action* a,DWORD kind){
    if(size!=ACTION_BYTES || memcmp(b,"L4SCA001",8) || l4_store_get32(b+8)!=1)return fail(ERROR_INVALID_DATA);
    for(unsigned i=60;i<64;i++)if(b[i])return fail(ERROR_INVALID_DATA);for(unsigned i=84;i<ACTION_BYTES;i++)if(b[i])return fail(ERROR_INVALID_DATA);
    memset(a,0,sizeof(*a));a->action=l4_store_get32(b+12);a->service=l4_store_get32(b+16);a->window=l4_store_get32(b+20);a->generation=l4_store_get64(b+24);a->deadline=l4_store_get64(b+32);a->plan=l4_store_get64(b+40);a->reference=l4_store_get64(b+48);a->pid=l4_store_get32(b+56);a->birth=l4_store_get64(b+64);a->intent=l4_store_get64(b+72);
    a->phase=l4_store_get32(b+80);bool extended=kind>=104 && kind<=106;
    return a->action>=1 && a->action<=(extended?4u:3u) && a->service<4 && (extended?(a->window==1 || a->window==2):a->window==(a->service<2?1u:2u)) && (extended?(a->phase>=1 && a->phase<=4):!a->phase) && a->generation && a->deadline && a->plan && a->reference && a->pid && a->birth?true:fail(ERROR_INVALID_DATA);
}
static bool committed(SetupServiceTransaction* t,const void* bytes,DWORD size){
    SetupRemoteCommit* c=malloc(sizeof(*c));if(!c)return fail(ERROR_NOT_ENOUGH_MEMORY);bool ok=setup_remote_commit_decode(bytes,size,c);BYTE* raw=NULL;DWORD length=0;BYTE hash[32];
    if(ok)ok=c->operation_sequence==setup_operation_sequence(t->operation) && l4_store_find_record(t->journal,64,c->operation_sequence,&raw,&length) && l4_store_hash(raw,length,NULL,0,hash) && !memcmp(hash,c->operation_sha256,32);free(raw);
    char id[37];const wchar_t* leaf=wcsrchr(t->journal->directory,L'\\');FILETIME now;GetSystemTimeAsFileTime(&now);ULONGLONG utc=birth(now);
    if(ok)ok=leaf && WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,leaf+1,-1,id,37,NULL,NULL) && !strcmp(id,c->operation) && c->finished_utc<=utc;
    unsigned count=0;const ULONGLONG* configs=setup_operation_configs(t->operation,&count);if(ok)ok=count==12 && configs;
    for(unsigned i=0;ok && i<12;i++)ok=configs[i]==c->configs[i];
    const SetupManifest* target=setup_operation_target(t->operation,0);const SetupRootManifest* root=setup_operation_target_root(t->operation,0);const BYTE* identity=root?setup_root_identity(root):NULL;
    const L4Layout* layout=target?setup_manifest_layout(target):NULL;const wchar_t* version=layout?wcsrchr(layout->release,L'\\'):NULL;char target_version[32];
    const char* arch=target?setup_manifest_arch(target):NULL;
    if(ok)ok=identity && version && arch && WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,version+1,-1,target_version,32,NULL,NULL) && !strcmp(target_version,c->suite_version) && !strcmp(arch,c->arch) && !memcmp(identity,c->suite_root,32);
    for(unsigned i=0;ok && i<4;i++){const L4ServiceSwitch* p=setup_operation_switch(t->operation,0,i);ok=t->done[i]==3 && p && !wcscmp(p->after,c->commands[i]) && p->before.start_type==c->start_types[i] && c->pids[i]==t->completed[i].pid && birth(c->births[i])==t->completed[i].birth;}
    free(c);return ok?true:fail(ERROR_REVISION_MISMATCH);
}
static bool history(DWORD kind,ULONGLONG sequence,const void* bytes,DWORD size,void* context){
    SetupServiceTransaction* t=context;ULONGLONG plan=setup_operation_sequence(t->operation);
    if(t->sealed){
        if(t->readonly && t->outcome_seen && !t->clear_seen && kind==65){
            const BYTE* b=bytes;BYTE digest[32];char owner[40]={0};const wchar_t* leaf=wcsrchr(t->journal->directory,L'\\');
            bool ok=leaf && WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,leaf+1,-1,owner,40,NULL,NULL) && size==112 && !memcmp(b,"L4UPD01",8) && !memcmp(b+8,owner,40) && !l4_store_get32(b+48) && !l4_store_get32(b+52) && t->windows[1].generation!=~0ull && l4_store_get64(b+56)==t->windows[1].generation+1 && l4_store_get64(b+64)==plan && !l4_store_get64(b+72) && l4_store_hash(b,80,NULL,0,digest) && !memcmp(digest,b+80,32);
            if(!ok)return fail(ERROR_REVISION_MISMATCH);t->clear_seen=true;return true;
        }
        L4RemoteOutcome value;
        if(!t->readonly || t->outcome_seen || kind!=103 || !l4_remote_outcome_decode(bytes,size,&value) || value.result.result!=L4_REMOTE_OUTCOME_SUCCESS || value.result.error || value.plan_sequence!=plan || value.proof_sequence!=t->commit_sequence || memcmp(value.proof_sha256,t->commit_hash,32))return fail(ERROR_INVALID_STATE);
        BYTE* raw=NULL;DWORD length=0;BYTE hash[32];bool ok=l4_store_find_record(t->journal,64,plan,&raw,&length) && l4_store_hash(raw,length,NULL,0,hash) && !memcmp(value.plan_sha256,hash,32);free(raw);
        const wchar_t* leaf=wcsrchr(t->journal->directory,L'\\');char id[37];if(ok)ok=leaf && WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,leaf+1,-1,id,37,NULL,NULL) && !strcmp(id,value.result.operation_id);
        if(!ok)return fail(ERROR_REVISION_MISMATCH);t->outcome_seen=true;return true;
    }
    if(kind==102){if(!t->readonly || t->restoring || sequence<=plan || t->pending_sequence || t->config_pending || !committed(t,bytes,size) || !l4_store_hash(bytes,size,NULL,0,t->commit_hash))return fail(ERROR_INVALID_STATE);t->commit_sequence=sequence;t->sealed=true;return true;}
    if(kind==100 || kind==101){Action a;if(t->restoring || sequence<=plan || !decode(bytes,size,&a,kind) || a.plan!=plan || a.reference!=setup_operation_switch_reference(t->operation,0,a.service) || t->config_pending)return fail(ERROR_INVALID_DATA);
        if(kind==100){if(t->pending_sequence || a.intent || a.action!=t->done[a.service]+1)return fail(ERROR_INVALID_STATE);
            if(a.window==2 && (t->done[0]!=3 || t->done[1]!=3))return fail(ERROR_INVALID_STATE);
            Action* window=&t->windows[a.window-1];if(window->generation && (window->generation!=a.generation || window->deadline!=a.deadline))return fail(ERROR_REVISION_MISMATCH);
            if(a.window==2 && t->windows[0].generation && (t->windows[0].generation==~0ull || a.generation!=t->windows[0].generation+1))return fail(ERROR_REVISION_MISMATCH);
            if(a.action>1 && (a.pid!=t->completed[a.service].pid || a.birth!=t->completed[a.service].birth))return fail(ERROR_REVISION_MISMATCH);
            *window=a;t->pending=a;t->pending_sequence=sequence;t->pending_kind=100;}
        else{Action expected=t->pending;if(!t->pending_sequence || a.intent!=t->pending_sequence)return fail(ERROR_INVALID_STATE);expected.intent=a.intent;
            if(a.action==3){if(a.pid==expected.pid && a.birth==expected.birth)return fail(ERROR_REVISION_MISMATCH);expected.pid=a.pid;expected.birth=a.birth;}BYTE first[ACTION_BYTES],second[ACTION_BYTES];encode(&expected,first);encode(&a,second);if(memcmp(first,second,ACTION_BYTES))return fail(ERROR_REVISION_MISMATCH);
            t->done[a.service]=a.action;t->completed[a.service]=a;t->pending_sequence=0;t->pending_kind=0;memset(&t->pending,0,sizeof(t->pending));}return true;
    }
    if(kind>=104 && kind<=106){Action a;if(sequence<=plan || !decode(bytes,size,&a,kind) || a.plan!=plan || a.reference!=setup_operation_switch_reference(t->operation,0,a.service) || t->config_pending)return fail(ERROR_INVALID_DATA);
        if(kind==106){if(t->pending_kind!=100 || !t->pending_sequence || a.intent!=t->pending_sequence || a.service!=t->pending.service || a.action!=t->pending.action || a.reference!=t->pending.reference)return fail(ERROR_REVISION_MISMATCH);
            if(a.window==t->pending.window){if(a.generation!=t->pending.generation || a.deadline!=t->pending.deadline)return fail(ERROR_REVISION_MISMATCH);}else if(t->pending.window!=1 || a.window!=2 || t->pending.generation==~0ull || a.generation!=t->pending.generation+1)return fail(ERROR_REVISION_MISMATCH);
            t->pending_sequence=0;t->pending_kind=0;memset(&t->pending,0,sizeof(t->pending));}
        else if(kind==104){if(t->pending_sequence || a.intent || t->restore_done[a.service]>=3 || (a.action==4?t->restore_done[a.service]!=0:a.action!=t->restore_done[a.service]+1))return fail(ERROR_INVALID_STATE);
            if((a.action==1 && a.phase!=1 && a.phase!=2 && a.phase!=4) || (a.action==2 && a.phase!=1 && a.phase!=2) || (a.action==3 && a.phase!=1) || (a.action==4 && a.phase!=3))return fail(ERROR_INVALID_DATA);
            if(a.action==2 || a.action==3){if(a.pid!=t->restored[a.service].pid || a.birth!=t->restored[a.service].birth)return fail(ERROR_REVISION_MISMATCH);}t->pending=a;t->pending_sequence=sequence;t->pending_kind=104;}
        else{if(t->pending_kind!=104 || !t->pending_sequence || a.intent!=t->pending_sequence)return fail(ERROR_INVALID_STATE);Action expected=t->pending;expected.intent=a.intent;expected.phase=a.phase;
            if(a.action==3){if(a.pid==expected.pid && a.birth==expected.birth)return fail(ERROR_REVISION_MISMATCH);expected.pid=a.pid;expected.birth=a.birth;}
            BYTE first[ACTION_BYTES],second[ACTION_BYTES];encode(&expected,first);encode(&a,second);if(memcmp(first,second,ACTION_BYTES))return fail(ERROR_REVISION_MISMATCH);
            if((a.action==1 && a.phase!=1 && a.phase!=2) || (a.action==2 && a.phase!=1) || (a.action>=3 && a.phase!=3))return fail(ERROR_INVALID_DATA);
            t->restore_done[a.service]=a.action;t->restored[a.service]=a;t->restore_sequences[a.service]=sequence;t->pending_sequence=0;t->pending_kind=0;memset(&t->pending,0,sizeof(t->pending));}t->restoring=true;return true;
    }
    if(kind==107){const BYTE* b=bytes;if(size!=112 || memcmp(b,"L4CFG001",8) || l4_store_get32(b+8)!=1 || l4_store_get32(b+12)<1 || l4_store_get32(b+12)>2 || l4_store_get32(b+16) || l4_store_get32(b+20)<1 || l4_store_get32(b+20)>2 || !l4_store_get64(b+24) || !l4_store_get64(b+32) || l4_store_get64(b+40)!=plan || l4_store_get64(b+48)!=t->config_pending || !t->config_pending || !l4_store_get32(b+56) || l4_store_get32(b+60) || !l4_store_get64(b+64) || l4_store_get64(b+72)!=t->config_intent || l4_store_get32(b+80)!=t->config_direction || t->pending_sequence)return fail(ERROR_INVALID_DATA);
        for(unsigned i=84;i<112;i++)if(b[i])return fail(ERROR_INVALID_DATA);t->config_pending=t->config_intent=0;t->restoring=true;return true;}
    if(sequence<=plan)return true;
    if(kind==21 || kind==22){if(t->pending_sequence || size!=12)return fail(ERROR_INVALID_STATE);DWORD direction=l4_store_get32((const BYTE*)bytes+8);if(direction>1 || (!direction && !t->restoring))return fail(ERROR_INVALID_STATE);
        ULONGLONG ref=l4_store_get64(bytes);unsigned count=0;const ULONGLONG* configs=setup_operation_configs(t->operation,&count);bool found=false;for(unsigned i=0;i<count;i++)if(configs[i]==ref)found=true;if(!found)return fail(ERROR_REVISION_MISMATCH);
        if(kind==21){if(t->config_pending)return fail(ERROR_INVALID_STATE);t->config_pending=ref;t->config_intent=sequence;t->config_direction=direction;}else{if(t->config_pending!=ref || t->config_direction!=direction)return fail(ERROR_INVALID_STATE);t->config_pending=t->config_intent=0;}return true;
    }
    /* Existing typed worker/watchdog/state producers own these records. Unknown
     * mutation or terminal histories cannot be interpreted as resumable. */
    if(kind>=65 && kind<=70)return t->pending_sequence?fail(ERROR_INVALID_STATE):true;
    return fail(ERROR_INVALID_STATE);
}
static bool reload(SetupServiceTransaction* t){memset(t->done,0,sizeof(t->done));memset(t->restore_done,0,sizeof(t->restore_done));memset(t->restore_sequences,0,sizeof(t->restore_sequences));memset(t->restored,0,sizeof(t->restored));memset(t->windows,0,sizeof(t->windows));memset(t->completed,0,sizeof(t->completed));memset(&t->pending,0,sizeof(t->pending));t->pending_sequence=t->config_pending=t->config_intent=0;t->pending_kind=0;t->sealed=t->restoring=false;
    t->commit_sequence=0;t->outcome_seen=t->clear_seen=false;memset(t->commit_hash,0,32);
    if(!authority(t) || !l4_journal_replay(t->journal,history,t)){t->recovery_required=true;return false;}t->recovery_required=t->blocked || t->pending_sequence!=0 || t->config_pending!=0;return true;}
bool setup_service_transaction_open(L4Journal* j,const SetupOperationPlan* op,SetupRemoteService* services[4],SetupServiceTransaction** out){
    if(!out)return fail(ERROR_INVALID_PARAMETER);*out=NULL;if(!j || !op || !services || setup_operation_hops(op)!=1)return fail(ERROR_NOT_SUPPORTED);
    SetupServiceTransaction* t=calloc(1,sizeof(*t));if(!t)return fail(ERROR_NOT_ENOUGH_MEMORY);t->journal=j;t->operation=op;
    for(unsigned i=0;i<4;i++){if(!services[i]){free(t);return fail(ERROR_INVALID_PARAMETER);}for(unsigned k=0;k<i;k++)if(services[i]==services[k]){free(t);return fail(ERROR_INVALID_PARAMETER);}t->services[i]=services[i];}
    if(!reload(t)){DWORD e=GetLastError();free(t);return fail(e);}*out=t;return true;
}
static bool observe(SetupServiceTransaction* t,unsigned service,const L4UpdateState* active,SetupRemoteServiceObservation* o){
    L4UpdateState current;if(!active || active->plan_sequence!=setup_operation_sequence(t->operation) || active->window!=(service<2?1u:2u) || !l4_update_state_read(&t->journal->layout,&current) || !same_state(active,&current))return fail(ERROR_REVISION_MISMATCH);
    if(!setup_remote_service_observe(t->services[service],o))return false;
    return o->pid && birth(o->created) && same_state(active,&o->state) && o->switch_reference==setup_operation_switch_reference(t->operation,0,service)?true:fail(ERROR_REVISION_MISMATCH);
}
static bool postcondition(const Action* a,const SetupRemoteServiceObservation* o){SetupRemoteServicePhase phase=a->action==1?SETUP_SERVICE_STOPPED_OLD:a->action==2?SETUP_SERVICE_STOPPED_TARGET:SETUP_SERVICE_RUNNING_TARGET;
    return o->phase==phase && (a->action==3 || (a->pid==o->pid && a->birth==birth(o->created)))?true:fail(ERROR_NOT_READY);}
static bool complete(SetupServiceTransaction* t,const L4UpdateState* active,ULONGLONG deadline){SetupRemoteServiceObservation o;
    if(!authority(t) || !observe(t,t->pending.service,active,&o) || !postcondition(&t->pending,&o))return false;if(GetTickCount64()>=deadline)return fail(ERROR_TIMEOUT);
    if(t->pending.action==3 && o.pid==t->pending.pid && birth(o.created)==t->pending.birth)return fail(ERROR_REVISION_MISMATCH);
    Action a=t->pending;a.intent=t->pending_sequence;a.pid=o.pid;a.birth=birth(o.created);BYTE bytes[ACTION_BYTES];encode(&a,bytes);
    if(!l4_journal_append(t->journal,101,bytes,sizeof(bytes),NULL))return false;
    t->done[a.service]=a.action;t->completed[a.service]=a;t->pending_sequence=0;t->pending_kind=0;memset(&t->pending,0,sizeof(t->pending));t->recovery_required=false;
    SetupRemoteServiceObservation after;if(!authority(t) || !observe(t,a.service,active,&after) || !postcondition(&a,&after) || after.pid!=a.pid || birth(after.created)!=a.birth){t->blocked=t->recovery_required=true;return false;}
    if(GetTickCount64()>=deadline){t->blocked=t->recovery_required=true;return fail(ERROR_TIMEOUT);}return true;
}
bool setup_service_transaction_execute(SetupServiceTransaction* t,unsigned service,SetupServiceAction action,const L4UpdateState* active,DWORD timeout){
    if(!t || service>=4 || action<1 || action>3 || timeout<1 || timeout>600000)return fail(ERROR_INVALID_PARAMETER);ULONGLONG end=GetTickCount64()+timeout;
    if(!reload(t))return false;if(t->restoring)return fail(ERROR_INVALID_STATE);if(t->recovery_required)return fail(ERROR_RECOVERY_FAILURE);
    SetupRemoteServiceObservation o;if(!observe(t,service,active,&o))return false;
    Action a={0};a.action=action;a.service=service;a.window=active->window;a.generation=active->generation;a.deadline=active->deadline_utc;a.plan=active->plan_sequence;a.reference=o.switch_reference;a.pid=o.pid;a.birth=birth(o.created);
    if(t->done[service]==(unsigned)action){if(o.pid!=t->completed[service].pid || birth(o.created)!=t->completed[service].birth)return fail(ERROR_REVISION_MISMATCH);return postcondition(&a,&o);}if((unsigned)action!=t->done[service]+1)return fail(ERROR_INVALID_STATE);
    SetupRemoteServicePhase before=action==1?SETUP_SERVICE_RUNNING_OLD:action==2?SETUP_SERVICE_STOPPED_OLD:SETUP_SERVICE_STOPPED_TARGET;
    if(o.phase!=before || GetTickCount64()>=end)return fail(ERROR_NOT_READY);
    if(a.window==2 && (t->done[0]!=3 || t->done[1]!=3))return fail(ERROR_INVALID_STATE);
    Action* window=&t->windows[a.window-1];if(window->generation && (window->generation!=a.generation || window->deadline!=a.deadline))return fail(ERROR_REVISION_MISMATCH);
    if(a.window==2 && t->windows[0].generation && (t->windows[0].generation==~0ull || a.generation!=t->windows[0].generation+1))return fail(ERROR_REVISION_MISMATCH);
    if(a.action>1 && (a.pid!=t->completed[service].pid || a.birth!=t->completed[service].birth))return fail(ERROR_REVISION_MISMATCH);
    BYTE bytes[ACTION_BYTES];encode(&a,bytes);
    t->recovery_required=true;if(!l4_journal_append(t->journal,100,bytes,sizeof(bytes),&t->pending_sequence)){t->blocked=true;return false;}t->pending=a;t->pending_kind=100;
    if(GetTickCount64()>=end)return fail(ERROR_TIMEOUT);DWORD remaining=(DWORD)(end-GetTickCount64());if(!remaining)return fail(ERROR_TIMEOUT);
    bool ok=action==1?setup_remote_service_stop(t->services[service],remaining):action==2?setup_remote_service_switch(t->services[service],true,remaining):setup_remote_service_start(t->services[service],true,remaining);
    if(!ok)return false;return complete(t,active,end);
}
bool setup_service_transaction_reconcile(SetupServiceTransaction* t,const L4UpdateState* active,DWORD timeout){
    if(!t || !active || timeout<1 || timeout>600000)return fail(ERROR_INVALID_PARAMETER);ULONGLONG end=GetTickCount64()+timeout;
    if(!reload(t))return false;if(t->restoring)return fail(ERROR_INVALID_STATE);L4UpdateState current;if(!l4_update_state_read(&t->journal->layout,&current))return false;
    if(!same_state(active,&current) || active->plan_sequence!=setup_operation_sequence(t->operation))return fail(ERROR_REVISION_MISMATCH);
    if(!t->pending_sequence)return t->recovery_required?fail(ERROR_RECOVERY_FAILURE):true;
    if(t->pending.window!=active->window || t->pending.generation!=active->generation || t->pending.deadline!=active->deadline_utc || t->pending.plan!=active->plan_sequence)return fail(ERROR_REVISION_MISMATCH);
    if(t->pending.action==3 && !setup_remote_service_observe_start(t->services[t->pending.service],timeout))return false;
    return complete(t,active,end);
}
bool setup_service_transaction_recovery_required(const SetupServiceTransaction* t){return !t || t->recovery_required;}
bool setup_service_transaction_pending(SetupServiceTransaction* t,DWORD* kind){if(!kind)return fail(ERROR_INVALID_PARAMETER);*kind=0;if(!t || !reload(t))return false;*kind=t->config_pending?21u:t->pending_kind;return true;}
bool setup_service_transaction_complete(SetupServiceTransaction* t,L4Journal* j,const SetupOperationPlan* op,SetupRemoteService* services[4],const L4UpdateState* active,DWORD timeout){
    if(!t || t->journal!=j || t->operation!=op || !services || !active || active->window!=2 || timeout<1 || timeout>600000)return fail(ERROR_INVALID_PARAMETER);
    ULONGLONG end=GetTickCount64()+timeout;t->readonly=true;bool valid=reload(t);t->readonly=false;if(!valid)return false;if(t->restoring)return fail(ERROR_INVALID_STATE);if(t->recovery_required)return fail(ERROR_RECOVERY_FAILURE);
    L4UpdateState current;if(!l4_update_state_read(&j->layout,&current) || !same_state(&current,active) || active->plan_sequence!=setup_operation_sequence(op))return fail(ERROR_REVISION_MISMATCH);
    for(unsigned i=0;i<4;i++){if(services[i]!=t->services[i] || t->done[i]!=3)return fail(ERROR_NOT_READY);SetupRemoteServiceObservation o;
        if(!setup_remote_service_observe(services[i],&o))return false;
        if(o.phase!=SETUP_SERVICE_RUNNING_TARGET || !same_state(&o.state,active) || o.switch_reference!=setup_operation_switch_reference(op,0,i) || o.pid!=t->completed[i].pid || birth(o.created)!=t->completed[i].birth)return fail(ERROR_REVISION_MISMATCH);
        if(GetTickCount64()>=end)return fail(ERROR_TIMEOUT);
    }
    if(!authority(t) || !l4_update_state_read(&j->layout,&current))return false;if(!same_state(&current,active))return fail(ERROR_REVISION_MISMATCH);
    return GetTickCount64()<end?true:fail(ERROR_TIMEOUT);
}
void setup_service_transaction_close(SetupServiceTransaction* t){free(t);}
static DWORD restore_remaining(ULONGLONG end){ULONGLONG now=GetTickCount64();return now<end?(DWORD)(end-now):0;}
static bool restore_audit(SetupServiceTransaction* t,const L4UpdateState* active){L4WorkerAdmission admission;if(!authority(t))return false;if(!active || (active->window!=1 && active->window!=2))return fail(ERROR_INVALID_PARAMETER);if(!l4_worker_recheck_active(t->journal,active,&admission))return false;return admission.sequence==setup_operation_sequence(t->operation)?true:fail(ERROR_REVISION_MISMATCH);}
static bool restore_guard(SetupServiceTransaction* t,const L4UpdateState* active,ULONGLONG end,L4RecoveryGuard** result){
    *result=NULL;DWORD budget=restore_remaining(end);if(!budget)return fail(ERROR_TIMEOUT);if(!restore_audit(t,active))return false;budget=restore_remaining(end);if(!budget)return fail(ERROR_TIMEOUT);
    if(budget>300000)budget=300000;const wchar_t* operation=wcsrchr(t->journal->directory,L'\\');if(!operation || !l4_recovery_open(&t->journal->layout,operation+1,budget,result))return false;
    const L4RecoveryPlan* r=l4_recovery_plan(*result);const L4ServiceSwitch* s=setup_operation_switch(t->operation,0,3);L4RecoveryAction action;FILETIME created,exit,kernel,user,now;GetSystemTimeAsFileTime(&now);
    bool ok=r && s && !memcmp(&r->operation,t->journal->header+8,16) && r->sequence==setup_operation_sequence(t->operation) && active->deadline_utc<r->deadline_utc && r->worker_pid==GetCurrentProcessId() && GetProcessTimes(GetCurrentProcess(),&created,&exit,&kernel,&user) && !CompareFileTime(&created,&r->worker_created) && !wcscmp(r->before,s->before.image_path) && !wcscmp(r->after,s->after) && r->start_type==s->before.start_type && r->old_size==s->before_size && r->new_size==s->size && !memcmp(r->old_sha256,s->before_sha256,32) && !memcmp(r->new_sha256,s->sha256,32) && l4_recovery_action(*result,birth(now),false,&action) && action==L4_RECOVERY_WAIT && restore_remaining(end);
    if(!ok){l4_recovery_close(*result);*result=NULL;return fail(ERROR_RECOVERY_FAILURE);}return true;
}
static bool restore_observe(SetupServiceTransaction* t,unsigned service,const L4UpdateState* active,SetupRemoteServiceObservation* out){L4UpdateState current;
    if(!l4_update_state_read(&t->journal->layout,&current) || !same_state(active,&current) || !setup_remote_service_observe(t->services[service],out))return false;
    return out->pid && birth(out->created) && same_state(active,&out->state) && out->switch_reference==setup_operation_switch_reference(t->operation,0,service)?true:fail(ERROR_REVISION_MISMATCH);}
static bool restore_post(const Action* a,const SetupRemoteServiceObservation* out){bool phase=a->action==1?(out->phase==1 || out->phase==2):a->action==2?out->phase==1:out->phase==3;
    return phase && (a->action==3?(out->pid!=a->pid || birth(out->created)!=a->birth):(out->pid==a->pid && birth(out->created)==a->birth))?true:fail(ERROR_NOT_READY);}
static Action restore_value(SetupServiceTransaction* t,unsigned service,DWORD action,const L4UpdateState* active,const SetupRemoteServiceObservation* out){Action a={0};a.action=action;a.service=service;a.window=active->window;a.generation=active->generation;a.deadline=active->deadline_utc;a.plan=setup_operation_sequence(t->operation);a.reference=out->switch_reference;a.pid=out->pid;a.birth=birth(out->created);a.phase=out->phase;return a;}
static bool restore_complete(SetupServiceTransaction* t,const L4UpdateState* active,ULONGLONG end){
    L4RecoveryGuard* guard=NULL;if(!restore_guard(t,active,end,&guard))return false;SetupRemoteServiceObservation out={0};Action a=t->pending;
    bool ok=t->pending_kind==104 && a.window==active->window && a.generation==active->generation && a.deadline==active->deadline_utc && restore_observe(t,a.service,active,&out) && restore_post(&a,&out) && restore_remaining(end);
    if(ok){a.intent=t->pending_sequence;a.pid=out.pid;a.birth=birth(out.created);a.phase=out.phase;BYTE bytes[112];encode(&a,bytes);ok=l4_journal_append(t->journal,105,bytes,sizeof(bytes),&t->restore_sequences[a.service]);}
    DWORD error=GetLastError();l4_recovery_close(guard);if(!ok)return fail(error?error:ERROR_NOT_READY);
    t->pending_sequence=0;t->pending_kind=0;t->restore_done[a.service]=a.action;t->restored[a.service]=a;t->recovery_required=false;
    if(!restore_audit(t,active) || !restore_observe(t,a.service,active,&out) || (DWORD)out.phase!=a.phase || out.pid!=a.pid || birth(out.created)!=a.birth || !restore_remaining(end)){t->blocked=t->recovery_required=true;return fail(GetLastError()?GetLastError():ERROR_TIMEOUT);}return true;
}
bool setup_service_transaction_restore(SetupServiceTransaction* t,unsigned service,SetupServiceRestore action,const L4UpdateState* active,DWORD timeout){
    if(!t || !active || service>=4 || action<1 || action>4 || timeout<1 || timeout>600000)return fail(ERROR_INVALID_PARAMETER);ULONGLONG end=GetTickCount64()+timeout;
    if(!reload(t))return false;if(t->pending_sequence || t->config_pending)return fail(ERROR_RECOVERY_FAILURE);L4RecoveryGuard* guard=NULL;if(!restore_guard(t,active,end,&guard))return false;
    SetupRemoteServiceObservation out={0};bool ok=restore_observe(t,service,active,&out);DWORD error=GetLastError();
    if(ok && t->restore_done[service]==(unsigned)action){ok=out.phase==SETUP_SERVICE_RUNNING_OLD && out.pid==t->restored[service].pid && birth(out.created)==t->restored[service].birth;l4_recovery_close(guard);return ok?restore_audit(t,active):fail(ERROR_REVISION_MISMATCH);}
    if(ok)ok=action==4?t->restore_done[service]==0 && out.phase==3:(unsigned)action==t->restore_done[service]+1;
    if(ok && action==1)ok=out.phase==1 || out.phase==2 || out.phase==4;if(ok && action==2)ok=out.phase==1 || out.phase==2;if(ok && action==3)ok=out.phase==1;
    if(ok){Action a=restore_value(t,service,action,active,&out);BYTE bytes[112];encode(&a,bytes);t->recovery_required=true;ok=l4_journal_append(t->journal,104,bytes,sizeof(bytes),&t->pending_sequence);
        if(ok){t->pending=a;t->pending_kind=104;t->restoring=true;t->blocked=false;DWORD budget=restore_remaining(end);ok=budget!=0;
            if(ok && action==1 && out.phase==4)ok=setup_remote_service_stop(t->services[service],budget);
            else if(ok && action==2 && out.phase==2)ok=setup_remote_service_switch(t->services[service],false,budget);
            else if(ok && action==3)ok=setup_remote_service_start(t->services[service],false,budget);
        }else t->blocked=true;
    }error=GetLastError();l4_recovery_close(guard);return ok?restore_complete(t,active,end):fail(error?error:ERROR_NOT_READY);
}
bool setup_service_transaction_restore_reconcile(SetupServiceTransaction* t,const L4UpdateState* active,DWORD timeout){
    if(!t || !active || timeout<1 || timeout>600000)return fail(ERROR_INVALID_PARAMETER);ULONGLONG end=GetTickCount64()+timeout;if(!reload(t))return false;if(t->pending_kind!=104)return fail(ERROR_INVALID_STATE);
    L4RecoveryGuard* guard=NULL;if(!restore_guard(t,active,end,&guard))return false;bool ok=true;if(t->pending.action==3){SetupRemoteServiceObservation out={0};if(!restore_observe(t,t->pending.service,active,&out) || out.phase!=3)ok=setup_remote_service_observe_start(t->services[t->pending.service],restore_remaining(end));}
    DWORD error=GetLastError();l4_recovery_close(guard);return ok?restore_complete(t,active,end):fail(error?error:ERROR_NOT_READY);
}
bool setup_service_transaction_abandon(SetupServiceTransaction* t,const L4UpdateState* active,DWORD timeout){
    if(!t || !active || timeout<1 || timeout>600000)return fail(ERROR_INVALID_PARAMETER);ULONGLONG end=GetTickCount64()+timeout;if(!reload(t))return false;if(t->pending_kind!=100 || t->blocked || t->config_pending)return fail(ERROR_INVALID_STATE);
    L4RecoveryGuard* guard=NULL;if(!restore_guard(t,active,end,&guard))return false;SetupRemoteServiceObservation out={0};bool ok=restore_observe(t,t->pending.service,active,&out);
    if(!ok && t->pending.action==3){DWORD budget=restore_remaining(end);ok=budget && setup_remote_service_observe_start(t->services[t->pending.service],budget) && restore_observe(t,t->pending.service,active,&out);}
    if(ok){Action a=restore_value(t,t->pending.service,t->pending.action,active,&out);a.intent=t->pending_sequence;BYTE bytes[112];encode(&a,bytes);ok=restore_remaining(end) && l4_journal_append(t->journal,106,bytes,sizeof(bytes),NULL);}
    DWORD error=GetLastError();l4_recovery_close(guard);if(!ok)return fail(error?error:ERROR_NOT_READY);return reload(t) && restore_audit(t,active);
}
bool setup_service_transaction_abandon_config(SetupServiceTransaction* t,const L4UpdateState* active,DWORD timeout){
    if(!t || !active || timeout<1 || timeout>600000)return fail(ERROR_INVALID_PARAMETER);ULONGLONG end=GetTickCount64()+timeout;if(!reload(t))return false;if(!t->config_pending || t->pending_sequence || t->blocked)return fail(ERROR_INVALID_STATE);
    L4RecoveryGuard* guard=NULL;if(!restore_guard(t,active,end,&guard))return false;DWORD state=0;if(l4_config_verify(t->journal,t->config_pending,false))state=1;else if(l4_config_verify(t->journal,t->config_pending,true))state=2;
    FILETIME created={0},exit,kernel,user;bool ok=state && GetProcessTimes(GetCurrentProcess(),&created,&exit,&kernel,&user) && restore_remaining(end);
    if(ok){BYTE bytes[112]={0};memcpy(bytes,"L4CFG001",8);l4_store_u32(bytes+8,1);l4_store_u32(bytes+12,state);l4_store_u32(bytes+20,active->window);l4_store_u64(bytes+24,active->generation);l4_store_u64(bytes+32,active->deadline_utc);l4_store_u64(bytes+40,setup_operation_sequence(t->operation));l4_store_u64(bytes+48,t->config_pending);l4_store_u32(bytes+56,GetCurrentProcessId());l4_store_u64(bytes+64,birth(created));l4_store_u64(bytes+72,t->config_intent);l4_store_u32(bytes+80,t->config_direction);ok=l4_journal_append(t->journal,107,bytes,sizeof(bytes),NULL);}
    DWORD error=GetLastError();l4_recovery_close(guard);if(!ok)return fail(error?error:ERROR_NOT_READY);return reload(t) && restore_audit(t,active);
}
bool setup_service_transaction_restored(SetupServiceTransaction* t,const L4UpdateState* active,DWORD timeout){
    if(!t || !active || timeout<1 || timeout>600000)return fail(ERROR_INVALID_PARAMETER);ULONGLONG end=GetTickCount64()+timeout;if(!reload(t))return false;if(!t->restoring || t->recovery_required)return fail(ERROR_RECOVERY_FAILURE);
    L4RecoveryGuard* guard=NULL;if(!restore_guard(t,active,end,&guard))return false;bool ok=true;
    for(unsigned i=0;ok && i<4;i++){SetupRemoteServiceObservation out={0};ok=t->restore_done[i]>=3 && restore_observe(t,i,active,&out) && out.phase==3 && out.pid==t->restored[i].pid && birth(out.created)==t->restored[i].birth && restore_remaining(end);}
    DWORD error=GetLastError();l4_recovery_close(guard);return ok?restore_audit(t,active):fail(error?error:ERROR_NOT_READY);
}
bool setup_service_transaction_restore_records(SetupServiceTransaction* t,const L4UpdateState* active,DWORD timeout,ULONGLONG sequences[4]){
    if(!sequences)return fail(ERROR_INVALID_PARAMETER);memset(sequences,0,4*sizeof(*sequences));if(!setup_service_transaction_restored(t,active,timeout))return false;for(unsigned i=0;i<4;i++)if(!t->restore_sequences[i])return fail(ERROR_INVALID_DATA);memcpy(sequences,t->restore_sequences,4*sizeof(*sequences));return true;
}
