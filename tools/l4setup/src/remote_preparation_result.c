#include "remote_result.h"
#include "update_metadata.h"
#include "../../l4common/remote_host.h"
#include "../../l4common/journal_internal.h"
#include <string.h>
static bool fail(DWORD error){SetLastError(error);return false;}
typedef struct {bool request,ack,complete,terminal;ULONGLONG route;L4RemoteResult value;} Scan;
static bool visit(DWORD kind,ULONGLONG sequence,const void* bytes,DWORD size,void* context){
    Scan* s=context;if(s->terminal)return fail(ERROR_INVALID_STATE);
    if(kind==L4_RECORD_REMOTE_REQUEST){if(sequence!=1 || s->request)return fail(ERROR_INVALID_DATA);s->request=true;}
    else if(kind==L4_RECORD_REMOTE_HOST_ACK){if(sequence!=2 || !s->request || s->ack)return fail(ERROR_INVALID_DATA);s->ack=true;}
    else if(kind==L4_RECORD_ROUTE_PLAN){if(!s->ack || s->route)return fail(ERROR_INVALID_DATA);s->route=sequence;}
    else if(kind==L4_RECORD_PREPARED_PACKAGE){if(!s->route || s->complete)return fail(ERROR_INVALID_DATA);}
    else if(kind==L4_RECORD_PACKAGES_COMPLETE){if(!s->route || s->complete)return fail(ERROR_INVALID_DATA);s->complete=true;}
    /* These records describe immutable proposals only. No apply/worker record
     * is permitted. Their payload is NOT used as authority to stop or succeed. */
    else if(kind==L4_RECORD_CONFIG_PLAN || kind==L4_RECORD_SWITCH_PLAN || kind==L4_RECORD_INSTALLED_SOURCE || kind==L4_RECORD_OPERATION_PLAN){
        if(!s->complete)return fail(ERROR_INVALID_STATE);
    }
    else if(kind==L4_RECORD_REMOTE_PREPARATION_RESULT){
        if(!s->ack || !l4_remote_result_decode(bytes,size,&s->value))return fail(ERROR_INVALID_DATA);s->terminal=true;
    }else return fail(ERROR_INVALID_STATE);
    return true;
}
static bool original(L4Journal* j,Scan* scan,L4RemoteHostReceipt* ack,L4RemoteResult* expected){
    if(!j || !scan || !ack || !expected || j->poisoned || !j->lock || j->lock==INVALID_HANDLE_VALUE)return fail(ERROR_INVALID_STATE);
    L4RemoteRequest request={0};if(!l4_journal_replay(j,visit,scan))return false;
    if(!scan->request || !scan->ack)return fail(ERROR_INVALID_DATA);
    if(!l4_remote_request_load(j,&request) || !l4_remote_host_load(j,ack))return false;
    if(request.target!=L4_REMOTE_SUITE || ack->request.target!=request.target || strcmp(ack->request.version,request.version) ||
        ack->request.accepted_utc!=request.accepted_utc)return fail(ERROR_INVALID_DATA);
    memset(expected,0,sizeof(*expected));const wchar_t* operation=wcsrchr(j->directory,L'\\');
    if(!operation || !WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,operation+1,-1,expected->operation_id,37,NULL,NULL))return fail(ERROR_INVALID_DATA);
    expected->target=request.target;expected->started_at=request.accepted_utc;
    strcpy_s(expected->requested_version,32,request.version);strcpy_s(expected->previous_version,32,ack->source_version);
    if(scan->route){L4RoutePlan* route=NULL;L4CatalogRelease source={0};
        if(!l4_route_load_trusted(j,scan->route,&route))return false;
        const L4CatalogRoute* steps=l4_route_steps(route);
        bool ok=l4_route_is_owner_trusted(route) && l4_route_source(route,&source) && !strcmp(source.version,ack->source_version) &&
            !strcmp(l4_route_requested(route),request.version) && !strcmp(l4_route_arch(route),ack->arch) && steps && steps->count<=L4_CATALOG_MAX_RELEASES;
        if(ok)ok=strcpy_s(expected->resolved_version,32,steps->count?steps->releases[steps->count-1].version:source.version)==0;
        l4_route_free(route);if(!ok)return fail(ERROR_REVISION_MISMATCH);
    }return true;
}
static bool bound(const L4RemoteResult* value,const L4RemoteResult* expected){
    return !strcmp(value->operation_id,expected->operation_id) && value->target==expected->target &&
        value->started_at==expected->started_at && !strcmp(value->requested_version,expected->requested_version) &&
        !strcmp(value->previous_version,expected->previous_version) && !strcmp(value->resolved_version,expected->resolved_version);
}
bool setup_remote_preparation_result(L4Journal* j,L4RemoteResult* result){
    if(result)memset(result,0,sizeof(*result));if(!result)return fail(ERROR_INVALID_PARAMETER);
    Scan scan={0};L4RemoteHostReceipt ack={0};L4RemoteResult expected={0};
    if(!original(j,&scan,&ack,&expected))return false;if(!scan.terminal)return fail(ERROR_NOT_FOUND);
    if(!bound(&scan.value,&expected))return fail(ERROR_REVISION_MISMATCH);*result=scan.value;return true;
}
bool setup_remote_preparation_finish(L4Journal* j,DWORD error,L4RemoteResult* result){
    if(result)memset(result,0,sizeof(*result));if(!result || !error)return fail(ERROR_INVALID_PARAMETER);
    Scan scan={0};L4RemoteHostReceipt ack={0};L4RemoteResult value={0};
    if(!original(j,&scan,&ack,&value))return false;
    if(scan.terminal){if(!bound(&scan.value,&value))return fail(ERROR_REVISION_MISMATCH);
        if(scan.value.error!=error)return fail(ERROR_ALREADY_EXISTS);*result=scan.value;return true;}
    const wchar_t* operation=wcsrchr(j->directory,L'\\');FILETIME birth,exit,kernel,user;
    if(!operation || !l4_remote_host_self(&j->layout,operation+1,ack.arch) || !GetProcessTimes(GetCurrentProcess(),&birth,&exit,&kernel,&user))return false;
    if(ack.pid!=GetCurrentProcessId() || ack.birth!=(((ULONGLONG)birth.dwHighDateTime<<32)|birth.dwLowDateTime))return fail(ERROR_ACCESS_DENIED);
    FILETIME now;GetSystemTimeAsFileTime(&now);value.finished_at=((ULONGLONG)now.dwHighDateTime<<32)|now.dwLowDateTime;
    value.result=error==ERROR_CANCELLED?L4_REMOTE_RESULT_CANCELLED:L4_REMOTE_RESULT_FAILED;value.error=error;BYTE encoded[L4_REMOTE_RESULT_BYTES];
    if(!l4_remote_result_encode(&value,encoded) || !l4_journal_append(j,L4_RECORD_REMOTE_PREPARATION_RESULT,encoded,sizeof(encoded),NULL))return false;
    return setup_remote_preparation_result(j,result);
}
