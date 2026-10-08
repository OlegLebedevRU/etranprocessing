#include "remote_outcome_internal.h"
#include "../../l4common/remote_host.h"
#include "../../l4common/journal_internal.h"
#include "../../l4common/update_state.h"
#include <stdlib.h>
#include <string.h>
static bool fail(DWORD e){SetLastError(e);return false;}
typedef struct {ULONGLONG ticket,receipt,outcome,proof;DWORD proof_kind;bool duplicate,other_terminal;} Records;
static bool scan(DWORD kind,ULONGLONG seq,const void* bytes,DWORD size,void* ctx){
    (void)bytes;(void)size;Records* r=ctx;ULONGLONG* ref=kind==68?&r->ticket:kind==69?&r->receipt:kind==103?&r->outcome:NULL;
    if(ref){if(*ref)r->duplicate=true;*ref=seq;}
    if(kind==102 || kind==108){if(r->proof)r->duplicate=true;r->proof=seq;r->proof_kind=kind;}
    if(kind==94 || kind==95)r->other_terminal=true;return true;
}
static bool worker(L4Journal* j,const Records* r,ULONGLONG plan){
    HANDLE thread=NULL,token=NULL;BYTE user[sizeof(TOKEN_USER)+SECURITY_MAX_SID_SIZE];DWORD n=0,session=1;
    if(OpenThreadToken(GetCurrentThread(),TOKEN_QUERY,TRUE,&thread)){CloseHandle(thread);return fail(ERROR_ACCESS_DENIED);}
    if(GetLastError()!=ERROR_NO_TOKEN)return false;
    bool ok=OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token)&&GetTokenInformation(token,TokenUser,user,sizeof(user),&n)&&
        IsWellKnownSid(((TOKEN_USER*)user)->User.Sid,WinLocalSystemSid)&&GetTokenInformation(token,TokenSessionId,&session,sizeof(session),&n)&&!session;
    if(token)CloseHandle(token);if(!ok)return fail(ERROR_ACCESS_DENIED);
    BYTE *ticket=NULL,*receipt=NULL;DWORD tn=0,rn=0;FILETIME birth,end,kernel,cpu;BYTE hash[32];
    ok=r->ticket && r->receipt>r->ticket && l4_store_find_record(j,68,r->ticket,&ticket,&tn)&&
        l4_store_find_record(j,69,r->receipt,&receipt,&rn)&&tn==144&&rn==32&&!memcmp(ticket,"L4HAND01",8)&&
        !memcmp(ticket+8,j->header+8,16)&&l4_store_get64(ticket+24)==plan&&l4_store_get32(ticket+32)==GetCurrentProcessId()&&
        GetProcessTimes(GetCurrentProcess(),&birth,&end,&kernel,&cpu)&&!memcmp(ticket+36,&birth,8)&&
        l4_store_hash(ticket,tn,NULL,0,hash)&&!memcmp(hash,receipt,32);
    DWORD error=GetLastError();free(ticket);free(receipt);return ok?true:fail(error?error:ERROR_INVALID_STATE);
}
bool setup_remote_outcome_append_bound(L4Journal* j,const SetupOperationPlan* op,const L4RemoteOutcome* expected,
    ULONGLONG proofref,const BYTE proofhash[32],L4RemoteOutcome* out){
    if(out)memset(out,0,sizeof(*out));if(!j||!op||!expected||!out||!j->lock||j->lock==INVALID_HANDLE_VALUE||j->poisoned)return fail(ERROR_INVALID_PARAMETER);
    ULONGLONG plan=setup_operation_sequence(op);if(!setup_operation_binding(op,j,plan))return false;
    Records r={0};if(!l4_journal_replay(j,scan,&r)||r.duplicate||r.other_terminal||!worker(j,&r,plan))return fail(ERROR_INVALID_STATE);
    L4RemoteRequest request;L4RemoteHostReceipt host;L4RemoteOutcome value={0};BYTE* bytes=NULL;DWORD size=0;
    if(!l4_remote_request_load(j,&request)||!l4_remote_host_load(j,&host))return false;
    L4RemoteResult* identity=&value.result;identity->target=request.target;identity->result=expected->result.result;identity->error=expected->result.error;
    const wchar_t* leaf=wcsrchr(j->directory,L'\\');if(!leaf||!WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,leaf+1,-1,identity->operation_id,37,NULL,NULL))return fail(ERROR_INVALID_NAME);
    identity->started_at=request.accepted_utc;FILETIME now;GetSystemTimeAsFileTime(&now);identity->finished_at=((ULONGLONG)now.dwHighDateTime<<32)|now.dwLowDateTime;
    strcpy_s(identity->requested_version,32,request.version);strcpy_s(identity->previous_version,32,host.source_version);
    const L4CatalogRoute* route=l4_route_steps(setup_operation_route(op));if(!route||!route->count||strlen(route->releases[route->count-1].version)>=32)return fail(ERROR_INVALID_DATA);
    strcpy_s(identity->resolved_version,32,route->releases[route->count-1].version);value.plan_sequence=plan;
    if(!l4_store_find_record(j,64,plan,&bytes,&size))return false;bool ok=l4_store_hash(bytes,size,NULL,0,value.plan_sha256);free(bytes);bytes=NULL;if(!ok)return false;
    if(identity->result!=L4_REMOTE_OUTCOME_RECOVERY_REQUIRED){
        if(!proofhash||proofref!=r.proof||r.proof_kind!=(identity->result==L4_REMOTE_OUTCOME_SUCCESS?102u:108u)||proofref<=r.receipt||(r.outcome && r.outcome<=proofref)||
            !l4_store_find_record(j,r.proof_kind,proofref,&bytes,&size))return fail(ERROR_INVALID_DATA);
        ok=l4_store_hash(bytes,size,NULL,0,value.proof_sha256)&&!memcmp(value.proof_sha256,proofhash,32);free(bytes);bytes=NULL;if(!ok)return fail(ERROR_CRC);
        value.proof_sequence=proofref;
    }else if(proofref || proofhash || r.proof)return fail(ERROR_INVALID_PARAMETER);
    L4UpdateState active;if(!l4_update_state_read(&j->layout,&active)||!active.window||active.plan_sequence!=plan||strcmp(active.owner,identity->operation_id))return fail(ERROR_INVALID_STATE);
    if(identity->result!=L4_REMOTE_OUTCOME_RECOVERY_REQUIRED && identity->finished_at>=active.deadline_utc)return fail(ERROR_TIMEOUT);
    BYTE encoded[L4_REMOTE_OUTCOME_BYTES];if(!l4_remote_outcome_encode(&value,encoded))return false;
    if(r.outcome){L4RemoteOutcome saved;if(!l4_store_find_record(j,103,r.outcome,&bytes,&size))return false;
        ok=l4_remote_outcome_decode(bytes,size,&saved);free(bytes);if(!ok)return false;
        value.result.finished_at=saved.result.finished_at;if(!l4_remote_outcome_encode(&value,encoded))return false;
        BYTE canonical[L4_REMOTE_OUTCOME_BYTES];ok=l4_remote_outcome_encode(&saved,canonical)&&!memcmp(encoded,canonical,sizeof(encoded));if(!ok)return fail(ERROR_ALREADY_EXISTS);*out=saved;return true;
    }
    if(!l4_journal_append(j,103,encoded,sizeof(encoded),NULL))return false;*out=value;return true;
}
