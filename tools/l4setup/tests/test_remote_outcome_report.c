/* Actual protected journals, original request/ACK codecs and current process
 * epoch. Only authenticated64/opaque terminal producer, marker and SYSTEM token
 * APIs are modeled inside this fixture TU. No native completion authority. */
#define wmain original_status_main
#include "../../l4common/tests/test_remote_status.c"
#undef wmain
#include "../src/remote_outcome_internal.h"
#include "../../l4common/update_state.h"
struct SetupOperationPlan{L4Journal* journal;ULONGLONG sequence;BYTE hash[32];};
ULONGLONG setup_operation_sequence(const SetupOperationPlan* p){return p->sequence;}
const L4RoutePlan* setup_operation_route(const SetupOperationPlan* p){(void)p;return &modeled_route;}
bool setup_operation_binding(const SetupOperationPlan* p,L4Journal* j,ULONGLONG seq){BYTE* b=NULL;DWORD n=0;BYTE h[32];bool ok=p->journal==j&&p->sequence==seq&&l4_store_find_record(j,64,seq,&b,&n)&&l4_store_hash(b,n,NULL,0,h)&&!memcmp(h,p->hash,32);free(b);if(!ok)SetLastError(ERROR_REVISION_MISMATCH);return ok;}
static bool model_system=true;static DWORD model_session;static L4UpdateState model_marker;
static BOOL writer_thread(HANDLE h,DWORD access,BOOL self,PHANDLE out){(void)h;(void)access;(void)self;(void)out;SetLastError(ERROR_NO_TOKEN);return FALSE;}
static BOOL writer_token(HANDLE h,DWORD access,PHANDLE out){(void)h;(void)access;if(!model_system){SetLastError(ERROR_ACCESS_DENIED);return FALSE;}*out=(HANDLE)(ULONG_PTR)1;return TRUE;}
static BOOL writer_info(HANDLE h,TOKEN_INFORMATION_CLASS kind,LPVOID out,DWORD capacity,PDWORD needed){
    (void)h;static BYTE sid[SECURITY_MAX_SID_SIZE];DWORD size=sizeof(sid);if(kind==TokenUser){*needed=sizeof(TOKEN_USER);if(capacity<*needed)return FALSE;if(!CreateWellKnownSid(WinLocalSystemSid,NULL,sid,&size))return FALSE;((TOKEN_USER*)out)->User.Sid=sid;return TRUE;}
    if(kind==TokenSessionId){*needed=sizeof(DWORD);if(capacity<*needed)return FALSE;*(DWORD*)out=model_session;return TRUE;}SetLastError(ERROR_NOT_SUPPORTED);return FALSE;
}
static BOOL writer_close(HANDLE h){if(h==(HANDLE)(ULONG_PTR)1)return TRUE;return CloseHandle(h);}
static bool writer_marker(const L4Layout* layout,L4UpdateState* out){(void)layout;*out=model_marker;return true;}
#define OpenThreadToken writer_thread
#define OpenProcessToken writer_token
#define GetTokenInformation writer_info
#define CloseHandle writer_close
#define l4_update_state_read writer_marker
#include "../src/remote_outcome_report.c"
#undef OpenThreadToken
#undef OpenProcessToken
#undef GetTokenInformation
#undef CloseHandle
#undef l4_update_state_read
static bool owned(L4Journal* j,SetupOperationPlan* op,ULONGLONG* proof,BYTE hash[32],bool wrongbirth,DWORD proofkind){
    memset(op,0,sizeof(*op));op->journal=j;if(!l4_journal_append(j,64,"modeled-owner64",15,&op->sequence)||!l4_store_hash("modeled-owner64",15,NULL,0,op->hash))return false;
    BYTE ticket[144]={0};memcpy(ticket,"L4HAND01",8);memcpy(ticket+8,j->header+8,16);l4_store_u64(ticket+24,op->sequence);l4_store_u32(ticket+32,GetCurrentProcessId());FILETIME birth,end,k,u;
    if(!GetProcessTimes(GetCurrentProcess(),&birth,&end,&k,&u))return false;if(wrongbirth)birth.dwLowDateTime++;memcpy(ticket+36,&birth,8);BYTE receipt[32];
    if(!l4_store_hash(ticket,sizeof(ticket),NULL,0,receipt)||!l4_journal_append(j,68,ticket,sizeof(ticket),NULL)||!l4_journal_append(j,69,receipt,sizeof(receipt),NULL))return false;
    /* Whole-suite proof authority is deliberately modeled; the writer's concern
     * is exact already-flushed record/hash binding, not minting proof102. */
    BYTE bytes[16959]={0};memcpy(bytes,"L4SCMT01",8);l4_store_u64(bytes+8,op->sequence);memcpy(bytes+16,op->hash,32);
    *proof=0;if(proofkind && (!l4_journal_append(j,proofkind,bytes,proofkind==108?384:sizeof(bytes),proof)||
        !l4_store_hash(bytes,proofkind==108?384:sizeof(bytes),NULL,0,hash)))return false;
    memset(&model_marker,0,sizeof(model_marker));WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,wcsrchr(j->directory,L'\\')+1,-1,model_marker.owner,40,NULL,NULL);
    model_marker.window=2;model_marker.generation=2;model_marker.plan_sequence=op->sequence;FILETIME now;GetSystemTimeAsFileTime(&now);model_marker.deadline_utc=(((ULONGLONG)now.dwHighDateTime<<32)|now.dwLowDateTime)+600000000ull;return true;
}
int wmain(void){
    wchar_t temp[MAX_PATH],root[MAX_PATH],pf[MAX_PATH],pd[MAX_PATH],path[MAX_PATH];CHECK(GetTempPathW(MAX_PATH,temp));swprintf_s(root,MAX_PATH,L"%lsL4OutcomeWriter-%lu-%llu",temp,GetCurrentProcessId(),GetTickCount64());CHECK(CreateDirectoryW(root,NULL));
    swprintf_s(pf,MAX_PATH,L"%ls\\PF",root);swprintf_s(pd,MAX_PATH,L"%ls\\PD",root);L4Layout layout;CHECK(l4_layout_from_roots(&layout,pf,pd,L"1.13.6")&&l4_layout_prepare(&layout));
    L4Journal* j=NULL;L4RemoteRequest request;SetupOperationPlan op;ULONGLONG proof;BYTE hash[32];L4RemoteOutcome expected={0},out;expected.result.result=L4_REMOTE_OUTCOME_SUCCESS;
    CHECK(start(&layout,150,&j,&request));CHECK(ack(j,&request,false));CHECK(owned(j,&op,&proof,hash,false,102));ULONGLONG before=j->sequence;
    /* Production worker must retain the original source layout after handoff.
     * A neutral Known-Folders layout cannot authenticate the source ACK93. */
    L4Layout original_layout=j->layout,neutral_layout;CHECK(l4_layout_from_roots(&neutral_layout,original_layout.binaries,original_layout.data,L"0.0.0"));
    j->layout=neutral_layout;CHECK(!setup_remote_outcome_append_bound(j,&op,&expected,proof,hash,&out)&&j->sequence==before);
    j->layout=original_layout;
    model_system=false;CHECK(!setup_remote_outcome_append_bound(j,&op,&expected,proof,hash,&out)&&j->sequence==before);model_system=true;model_session=1;
    CHECK(!setup_remote_outcome_append_bound(j,&op,&expected,proof,hash,&out)&&j->sequence==before);model_session=0;
    BYTE badhash[32];memcpy(badhash,hash,32);badhash[0]^=1;CHECK(!setup_remote_outcome_append_bound(j,&op,&expected,proof,badhash,&out)&&j->sequence==before);
    CHECK(!setup_remote_outcome_append_bound(j,&op,&expected,proof-1,hash,&out)&&j->sequence==before);model_marker.owner[0]='2';CHECK(!setup_remote_outcome_append_bound(j,&op,&expected,proof,hash,&out)&&j->sequence==before);model_marker.owner[0]='1';
    CHECK(setup_remote_outcome_append_bound(j,&op,&expected,proof,hash,&out)&&j->sequence==before+1);L4RemoteOutcome saved=out;
    CHECK(out.result.started_at==request.accepted_utc&&!strcmp(out.result.previous_version,"1.13.6")&&!strcmp(out.result.resolved_version,"1.13.7")&&out.proof_sequence==proof&&!memcmp(out.proof_sha256,hash,32));
    CHECK(setup_remote_outcome_append_bound(j,&op,&expected,proof,hash,&out)&&j->sequence==before+1&&!memcmp(&out,&saved,sizeof(out)));
    expected.result.result=L4_REMOTE_OUTCOME_RECOVERY_REQUIRED;expected.result.error=ERROR_TIMEOUT;CHECK(!setup_remote_outcome_append_bound(j,&op,&expected,0,NULL,&out)&&j->sequence==before+1);
    remove_journal(&j);CHECK(start(&layout,151,&j,&request));CHECK(ack(j,&request,false));CHECK(owned(j,&op,&proof,hash,true,102));expected.result.result=L4_REMOTE_OUTCOME_SUCCESS;expected.result.error=0;before=j->sequence;
    CHECK(!setup_remote_outcome_append_bound(j,&op,&expected,proof,hash,&out)&&j->sequence==before);remove_journal(&j);
    CHECK(start(&layout,152,&j,&request));CHECK(ack(j,&request,false));CHECK(owned(j,&op,&proof,hash,false,108));before=j->sequence;expected.result.result=L4_REMOTE_OUTCOME_RESTORED;expected.result.error=ERROR_ACCESS_DENIED;
    CHECK(setup_remote_outcome_append_bound(j,&op,&expected,proof,hash,&out)&&out.result.result==L4_REMOTE_OUTCOME_RESTORED&&out.result.error==ERROR_ACCESS_DENIED&&j->sequence==before+1);saved=out;
    CHECK(setup_remote_outcome_append_bound(j,&op,&expected,proof,hash,&out)&&j->sequence==before+1&&!memcmp(&saved,&out,sizeof(out)));
    expected.result.error=ERROR_TIMEOUT;CHECK(!setup_remote_outcome_append_bound(j,&op,&expected,proof,hash,&out)&&j->sequence==before+1);remove_journal(&j);
    CHECK(start(&layout,153,&j,&request));CHECK(ack(j,&request,false));CHECK(owned(j,&op,&proof,hash,false,0));before=j->sequence;expected.result.result=L4_REMOTE_OUTCOME_RECOVERY_REQUIRED;
    CHECK(setup_remote_outcome_append_bound(j,&op,&expected,0,NULL,&out)&&out.result.result==L4_REMOTE_OUTCOME_RECOVERY_REQUIRED&&!out.proof_sequence&&j->sequence==before+1);saved=out;
    CHECK(setup_remote_outcome_append_bound(j,&op,&expected,0,NULL,&out)&&j->sequence==before+1&&!memcmp(&saved,&out,sizeof(out)));
    CHECK(l4_journal_append(j,103,"malformed duplicate",19,NULL));before=j->sequence;CHECK(!setup_remote_outcome_append_bound(j,&op,&expected,0,NULL,&out)&&j->sequence==before);remove_journal(&j);
    swprintf_s(path,MAX_PATH,L"%ls\\deployment.lock",layout.operations);CHECK(DeleteFileW(path));CHECK(RemoveDirectoryW(layout.operations));CHECK(RemoveDirectoryW(layout.cache));CHECK(RemoveDirectoryW(layout.staging));swprintf_s(path,MAX_PATH,L"%ls\\update",layout.data);CHECK(RemoveDirectoryW(path));
    CHECK(RemoveDirectoryW(layout.config));CHECK(RemoveDirectoryW(layout.state));CHECK(RemoveDirectoryW(layout.logs));swprintf_s(path,MAX_PATH,L"%ls\\releases",layout.binaries);CHECK(RemoveDirectoryW(path));CHECK(RemoveDirectoryW(layout.launchers));CHECK(RemoveDirectoryW(layout.binaries));CHECK(RemoveDirectoryW(layout.data));CHECK(RemoveDirectoryW(root));
    printf("Remote outcome writer: %u checks, %u failures; actual private92/93/64/68/69/102/103/hash/currentepoch; SYSTEM/opaqueproof/metadata/marker modeled, no native success claim\n",checks,failures);return failures?1:0;
}
