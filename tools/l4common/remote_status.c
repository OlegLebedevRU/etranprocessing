#include "remote_status.h"
#include "route_plan.h"
#include "journal_internal.h"
#include "recovery_plan.h"
#include "communication_plan.h"
#include "update_state.h"
#include <objbase.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
static bool fail(DWORD code){SetLastError(code);return false;}
typedef struct {
    const L4Layout* roots;const L4JournalReader* reader;L4RemoteStatus status;L4Layout original;
    bool request,ack,route,complete,plan,terminal;
    const void* route_bytes;DWORD route_size;
    GUID operation;L4RecoveryPlan recovery;BYTE recovery_hash[32],operation_hash[32],ticket_hash[32];
    bool recovery_seen,task,communication,ticket,receipt;
    DWORD parent_pid;ULONGLONG parent_birth,communication_generation,communication_deadline;
    bool typed_plan,restoring;unsigned config_count,hops;
    ULONGLONG configs[16],switches[L4_CATALOG_MAX_RELEASES][4];
    L4UpdateState active,clear;
    BYTE pending[112];DWORD pending_kind;ULONGLONG pending_sequence;
    ULONGLONG config_pending,config_reference;DWORD config_direction;
    bool config_candidate[16];
    unsigned forward[4],restored[4];DWORD pids[4],old_pids[4];ULONGLONG births[4],old_births[4],forward_done[4],restore_done[4];
    ULONGLONG proof_sequence;DWORD proof_kind;BYTE proof_hash[32];
    char proof_version[32],proof_arch[8],proof_updater[32];BYTE proof_root[32];ULONGLONG proof_finished;
} Scan;
#include "remote_status_apply.inc"
static bool visit(DWORD kind,ULONGLONG sequence,const void* data,DWORD size,void* context){
    Scan* s=context;const BYTE* bytes=data;
    if(s->terminal && !(kind==65 && s->status.has_outcome && !s->status.outcome_clear_recorded &&
        s->status.outcome.result.result!=L4_REMOTE_OUTCOME_RECOVERY_REQUIRED))return fail(ERROR_INVALID_STATE);
    if(kind==L4_RECORD_REMOTE_REQUEST){
        if(s->request || sequence!=1 || !l4_remote_request_decode(s->roots,data,size,&s->status.request))return fail(ERROR_INVALID_DATA);
        if(s->status.request.target!=L4_REMOTE_SUITE)return fail(ERROR_NOT_SUPPORTED);
        s->request=true;s->status.recorded_phase=L4_REMOTE_RECORDED_REQUEST;
    }else if(kind==L4_RECORD_REMOTE_HOST_ACK){
        if(!s->request || s->ack || sequence!=2 || !((size==112 && l4_store_get32(bytes+8)==1) ||
            (size==144 && l4_store_get32(bytes+8)==2)) || !memchr(bytes+72,0,32))return fail(ERROR_INVALID_DATA);
        wchar_t version[32];if(!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,(const char*)bytes+72,-1,version,32) ||
            !l4_layout_from_roots(&s->original,s->roots->binaries,s->roots->data,version))return fail(ERROR_INVALID_DATA);
        s->ack=true;s->status.recorded_phase=L4_REMOTE_RECORDED_CONTROLLER;
    }else if(kind==L4_RECORD_ROUTE_PLAN){
        if(!s->ack || s->route)return fail(ERROR_INVALID_DATA);s->route=true;s->status.route_sequence=sequence;
        s->route_bytes=data;s->route_size=size;s->status.recorded_phase=L4_REMOTE_RECORDED_ROUTE;
    }else if(kind==61){
        if(!s->route || s->complete)return fail(ERROR_INVALID_DATA);s->status.recorded_phase=L4_REMOTE_RECORDED_PACKAGES_PROGRESS;
    }else if(kind==62){
        if(!s->route || s->complete)return fail(ERROR_INVALID_DATA);s->complete=true;s->status.packages_sequence=sequence;
        s->status.recorded_phase=L4_REMOTE_RECORDED_PACKAGES;
    }else if(kind==L4_RECORD_CONFIG_PLAN || kind==63 || kind==L4_RECORD_SWITCH_PLAN){
        if(!s->complete || s->plan)return fail(ERROR_INVALID_STATE);s->status.recorded_phase=L4_REMOTE_RECORDED_PLANNING;
    }else if(kind==64){
        if(!s->complete || s->plan)return fail(ERROR_INVALID_DATA);s->plan=true;s->status.plan_sequence=sequence;
        if(!l4_store_hash(data,size,NULL,0,s->operation_hash))return false;
        if(!apply_plan(s,sequence,bytes,size))return false;
        s->status.recorded_phase=L4_REMOTE_RECORDED_PLAN;
    }else if(kind==L4_RECORD_REMOTE_PREPARATION_RESULT){
        if(!s->ack || s->recovery_seen || !l4_remote_result_decode(data,size,&s->status.result))return fail(ERROR_INVALID_DATA);
        s->terminal=s->status.has_result=true;s->status.recorded_phase=L4_REMOTE_RECORDED_FINISHED;
    }else if(kind==66){
        if(!s->plan || s->recovery_seen || !l4_recovery_decode(&s->original,data,size,&s->recovery) ||
            memcmp(&s->recovery.operation,&s->operation,16) || s->recovery.sequence!=s->status.plan_sequence)return fail(ERROR_INVALID_DATA);
        memcpy(s->recovery_hash,bytes+size-32,32);s->recovery_seen=true;s->status.recorded_phase=L4_REMOTE_RECORDED_WORKER_STARTING;
    }else if(kind==67){
        /* Historical intent only: the native launcher separately audits exact
         * task XML/ACL. Do not invent a successful scheduler registration. */
        if(!s->recovery_seen || s->task || s->communication || s->ticket || size<2 || size>65536 || (size&1))return fail(ERROR_INVALID_DATA);
        for(DWORD i=0;i<size;i+=2)if(!bytes[i] && !bytes[i+1])return fail(ERROR_INVALID_DATA);
        s->task=true;
    }else if(kind==70){
        L4CommunicationPlan p;
        if(!s->task || s->communication || s->ticket || !l4_communication_plan_decode(&s->original,data,size,&p) ||
            memcmp(&p.operation,&s->operation,16) || p.sequence!=s->status.plan_sequence || memcmp(p.operation_sha256,s->operation_hash,32) ||
            p.worker_pid!=s->recovery.worker_pid || CompareFileTime(&p.worker_created,&s->recovery.worker_created) ||
            p.supervisor_pid!=s->recovery.supervisor_pid || CompareFileTime(&p.supervisor_created,&s->recovery.supervisor_created) ||
            p.armed_utc!=s->recovery.armed_utc || p.deadline_utc>=s->recovery.deadline_utc)return fail(ERROR_INVALID_DATA);
        s->communication=true;s->communication_generation=p.generation;s->communication_deadline=p.deadline_utc;
    }else if(kind==68){
        if(!s->communication || s->ticket || size!=144 || memcmp(bytes,"L4HAND01",8) || memcmp(bytes+8,&s->operation,16) ||
            l4_store_get64(bytes+24)!=s->status.plan_sequence || l4_store_get32(bytes+32)!=s->recovery.worker_pid ||
            memcmp(bytes+36,&s->recovery.worker_created,8) || memcmp(bytes+56,s->recovery_hash,32) ||
            s->communication_generation!=l4_store_get64(bytes+88)+1 || !l4_store_get64(bytes+96) || l4_store_get32(bytes+140) ||
            !l4_store_hash(bytes,size,NULL,0,s->ticket_hash))return fail(ERROR_INVALID_DATA);
        s->parent_pid=l4_store_get32(bytes+44);s->parent_birth=l4_store_get64(bytes+48);
        s->ticket=true;s->status.recorded_phase=L4_REMOTE_RECORDED_HANDOFF;
    }else if(kind==69){
        if(!s->ticket || s->receipt || size!=32 || memcmp(bytes,s->ticket_hash,32))return fail(ERROR_INVALID_DATA);
        s->receipt=true;s->status.recorded_phase=L4_REMOTE_RECORDED_WORKER;
    }else if(kind==L4_RECORD_REMOTE_LAUNCH_FAILURE){
        if(!s->recovery_seen || !l4_remote_launch_failure_decode(data,size,&s->status.launch_failure) ||
            s->status.launch_failure.plan_sequence!=s->status.plan_sequence)return fail(ERROR_INVALID_DATA);
        s->status.result=s->status.launch_failure.result;s->status.has_launch_failure=true;
        s->terminal=s->status.has_result=true;s->status.recorded_phase=L4_REMOTE_RECORDED_RECOVERY_REQUIRED;
    }else if(kind==L4_RECORD_CONFIG_INTENT || kind==L4_RECORD_CONFIG_DONE || kind==65 || (kind>=100 && kind<=108)){
        if(!apply_visit(s,kind,sequence,bytes,size))return false;
    }else return fail(ERROR_INVALID_DATA);
    s->status.last_sequence=sequence;return true;
}
static bool route_binding(const Scan* s,const L4RoutePlan* route,char resolved[32]){
    L4CatalogRelease source={0};const L4CatalogRoute* steps=l4_route_steps(route);
    if(!l4_route_is_owner_trusted(route) || !l4_route_source(route,&source) ||
        strcmp(source.version,s->status.host.source_version) || strcmp(l4_route_requested(route),s->status.request.version) ||
        strcmp(l4_route_arch(route),s->status.host.arch) || !steps || steps->count>L4_CATALOG_MAX_RELEASES)return fail(ERROR_REVISION_MISMATCH);
    const char* target=steps->count?steps->releases[steps->count-1].version:source.version;
    if(strlen(target)>=32)return fail(ERROR_INVALID_DATA);strcpy_s(resolved,32,target);return true;
}
bool l4_remote_status_snapshot(const L4JournalReader* reader,const L4Layout* roots,L4RemoteStatus* result){
    if(result)memset(result,0,sizeof(*result));if(!reader || !roots || !result)return fail(ERROR_INVALID_PARAMETER);
    Scan s={0};s.roots=roots;s.reader=reader;const wchar_t* directory=l4_journal_reader_directory(reader),*operation=directory?wcsrchr(directory,L'\\'):NULL;
    wchar_t relative[96],expected[MAX_PATH];
    if(!operation || wcslen(operation+1)!=36 || swprintf_s(relative,96,L"update\\operations\\%ls",operation+1)<1 ||
        !l4_layout_data_path(roots,relative,expected) || _wcsicmp(expected,directory) ||
        !WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,operation+1,-1,s.status.operation_id,37,NULL,NULL))return fail(ERROR_INVALID_NAME);
    wchar_t guid[40];if(swprintf_s(guid,40,L"{%ls}",operation+1)<1 || CLSIDFromString(guid,&s.operation)!=S_OK)return fail(ERROR_INVALID_NAME);
    if(!l4_journal_reader_replay(reader,visit,&s))return false;if(!s.request)return fail(ERROR_INVALID_DATA);
    if(s.ack){if(!l4_remote_host_snapshot(reader,&s.original,&s.status.host))return false;s.status.has_host=true;
        if(s.status.host.request.target!=s.status.request.target || strcmp(s.status.host.request.version,s.status.request.version) ||
            s.status.host.request.accepted_utc!=s.status.request.accepted_utc)return fail(ERROR_REVISION_MISMATCH);}
    if(s.ticket && (s.parent_pid!=s.status.host.pid || s.parent_birth!=s.status.host.birth))return fail(ERROR_REVISION_MISMATCH);
    if(s.proof_kind==102 && strcmp(s.proof_updater,s.status.host.updater_version))return fail(ERROR_REVISION_MISMATCH);
    char resolved[32]={0};
    if(s.route){L4RoutePlan* route=NULL;if(!l4_route_decode_trusted(s.route_bytes,s.route_size,&route))return false;
        bool ok=route_binding(&s,route,resolved);
        if(ok && s.proof_sequence){L4CatalogRelease source={0};const L4CatalogRoute* steps=l4_route_steps(route);
            ok=l4_route_source(route,&source) && !strcmp(s.proof_version,s.proof_kind==102?resolved:source.version) &&
                !strcmp(s.proof_arch,s.status.host.arch) && !memcmp(s.proof_root,s.proof_kind==102?
                    steps->releases[steps->count-1].manifest_sha256:source.manifest_sha256,32);}
        DWORD error=GetLastError();l4_route_free(route);if(!ok)return fail(error?error:ERROR_REVISION_MISMATCH);}
    strcpy_s(s.status.resolved_version,32,resolved);
    if(s.terminal){const L4RemoteResult* r=s.status.has_outcome?&s.status.outcome.result:&s.status.result;
        if(strcmp(r->operation_id,s.status.operation_id) || r->target!=s.status.request.target || r->started_at!=s.status.request.accepted_utc ||
            strcmp(r->requested_version,s.status.request.version) || strcmp(r->previous_version,s.status.host.source_version) ||
            strcmp(r->resolved_version,resolved))return fail(ERROR_REVISION_MISMATCH);}
    *result=s.status;return true;
}
bool l4_remote_status_observe(const L4Layout* layout,const wchar_t* operation,L4RemoteStatus* result){
    if(result)memset(result,0,sizeof(*result));if(!result)return fail(ERROR_INVALID_PARAMETER);L4JournalReader* reader=NULL;
    if(!l4_journal_reader_open_live(layout,operation,&reader))return false;
    bool ok=l4_remote_status_snapshot(reader,layout,result);DWORD error=GetLastError();l4_journal_reader_close(reader);
    if(ok && result->outcome_clear_recorded){L4UpdateState actual;ok=l4_update_state_read(layout,&actual);error=GetLastError();
        if(ok){ULONGLONG generation=result->outcome_clear_generation;
            result->outcome_cleared=actual.generation>generation || (actual.generation==generation && !actual.window &&
                actual.plan_sequence==result->plan_sequence && !strcmp(actual.owner,result->operation_id));
            if(result->outcome_cleared)result->recorded_phase=L4_REMOTE_RECORDED_FINISHED;}}
    if(!ok)memset(result,0,sizeof(*result));return ok?true:fail(error);
}
