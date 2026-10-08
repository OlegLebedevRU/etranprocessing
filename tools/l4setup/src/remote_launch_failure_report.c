#include "remote_launch_failure.h"
#include "../../l4common/remote_status.h"
#include "../../l4common/route_plan.h"
#include "../../l4common/worker_handoff.h"
#include "../../l4common/update_state.h"
#include "../../l4common/journal_internal.h"
#include <string.h>
static bool fail(DWORD error){SetLastError(error);return false;}
static bool observe(L4Journal* j,L4RemoteStatus* result){
    const wchar_t* id=wcsrchr(j->directory,L'\\');L4JournalReader* reader=NULL;
    if(!id || !l4_journal_reader_open_live(&j->layout,id+1,&reader))return false;
    bool ok=l4_remote_status_snapshot(reader,&j->layout,result);DWORD error=GetLastError();l4_journal_reader_close(reader);return ok?true:fail(error);
}
static bool controller(const L4Journal* j,const L4RemoteStatus* status){
    const wchar_t* id=wcsrchr(j->directory,L'\\');FILETIME c,e,k,u;
    if(!id || !l4_remote_host_self(&j->layout,id+1,status->host.arch) || !GetProcessTimes(GetCurrentProcess(),&c,&e,&k,&u))return false;
    return status->host.pid==GetCurrentProcessId() && status->host.birth==(((ULONGLONG)c.dwHighDateTime<<32)|c.dwLowDateTime)?true:fail(ERROR_ACCESS_DENIED);
}
bool setup_remote_launch_failure_finish(L4Journal* j,DWORD error,DWORD stage,DWORD cleanup,L4RemoteLaunchFailure* result){
    if(result)memset(result,0,sizeof(*result));
    if(!j || !result || !error || stage<1 || stage>8 || j->poisoned || !j->lock || j->lock==INVALID_HANDLE_VALUE)return fail(ERROR_INVALID_PARAMETER);
    L4RemoteStatus s;if(!observe(j,&s))return false;
    if(s.has_launch_failure){
        if(s.launch_failure.result.error!=error || s.launch_failure.stage!=stage || s.launch_failure.cleanup_error!=cleanup)return fail(ERROR_ALREADY_EXISTS);
        *result=s.launch_failure;return true;
    }
    if(!s.has_host || !s.plan_sequence || s.has_result || s.recorded_phase<L4_REMOTE_RECORDED_WORKER_STARTING ||
        s.recorded_phase>L4_REMOTE_RECORDED_WORKER)return fail(ERROR_INVALID_STATE);
    if(s.host.pid==GetCurrentProcessId()){
        if(!controller(j,&s))return false;
    }else{
        L4WorkerAdmission admission;
        if(s.recorded_phase!=L4_REMOTE_RECORDED_WORKER || !l4_worker_recheck(j,&admission) || admission.sequence!=s.plan_sequence)return fail(ERROR_ACCESS_DENIED);
    }
    L4UpdateState state;if(!l4_update_state_read(&j->layout,&state))return false;if(state.window)return fail(ERROR_INVALID_STATE);
    L4RemoteLaunchFailure value={0};value.plan_sequence=s.plan_sequence;value.stage=stage;value.cleanup_error=cleanup;
    L4RemoteResult* r=&value.result;strcpy_s(r->operation_id,37,s.operation_id);r->target=s.request.target;r->started_at=s.request.accepted_utc;
    strcpy_s(r->requested_version,32,s.request.version);strcpy_s(r->previous_version,32,s.host.source_version);
    L4RoutePlan* route=NULL;if(!l4_route_load_trusted(j,s.route_sequence,&route))return false;
    const L4CatalogRoute* steps=l4_route_steps(route);bool ok=l4_route_is_owner_trusted(route) && steps && steps->count && steps->count<=L4_CATALOG_MAX_RELEASES;
    if(ok)ok=strcpy_s(r->resolved_version,32,steps->releases[steps->count-1].version)==0;
    l4_route_free(route);if(!ok)return fail(ERROR_REVISION_MISMATCH);
    FILETIME now;GetSystemTimeAsFileTime(&now);r->finished_at=((ULONGLONG)now.dwHighDateTime<<32)|now.dwLowDateTime;
    r->error=error;r->result=error==ERROR_CANCELLED?L4_REMOTE_RESULT_CANCELLED:L4_REMOTE_RESULT_FAILED;
    BYTE encoded[L4_REMOTE_LAUNCH_FAILURE_BYTES];
    /* Recheck clear immediately before append; never clear or assume no task. */
    if(!l4_remote_launch_failure_encode(&value,encoded) || !l4_update_state_read(&j->layout,&state))return false;
    if(state.window)return fail(ERROR_INVALID_STATE);
    if(!l4_journal_append(j,L4_RECORD_REMOTE_LAUNCH_FAILURE,encoded,sizeof(encoded),NULL) || !observe(j,&s))return false;
    if(!s.has_launch_failure)return fail(ERROR_INVALID_STATE);*result=s.launch_failure;return true;
}
