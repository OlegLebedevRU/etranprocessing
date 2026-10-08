#include "acceptance_export.h"
#include "acceptance_local.h"
#include "remote_commit.h"
#include "remote_restore.h"
#include "../../l4common/journal_internal.h"
#include "../../l4common/remote_status.h"
#include "../../l4common/update_state.h"
#include <sddl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool fail(DWORD error){SetLastError(error);return false;}
static ULONGLONG ticks(FILETIME value){return ((ULONGLONG)value.dwHighDateTime<<32)|value.dwLowDateTime;}
static ULONGLONG utc(void){FILETIME value;GetSystemTimeAsFileTime(&value);return ticks(value);}
static bool system_owner(void){HANDLE token=NULL,thread=NULL;BYTE user[sizeof(TOKEN_USER)+SECURITY_MAX_SID_SIZE];DWORD n=0,session=1;TOKEN_TYPE type=TokenImpersonation;
 if(OpenThreadToken(GetCurrentThread(),TOKEN_QUERY,TRUE,&thread)){CloseHandle(thread);return fail(ERROR_ACCESS_DENIED);}if(GetLastError()!=ERROR_NO_TOKEN)return false;
 bool ok=OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token)&&GetTokenInformation(token,TokenUser,user,sizeof(user),&n)&&IsWellKnownSid(((TOKEN_USER*)user)->User.Sid,WinLocalSystemSid)&&GetTokenInformation(token,TokenType,&type,sizeof(type),&n)&&type==TokenPrimary&&GetTokenInformation(token,TokenSessionId,&session,sizeof(session),&n)&&!session;
 if(token)CloseHandle(token);return ok?true:fail(ERROR_ACCESS_DENIED);
}
static bool safe(HANDLE file){BY_HANDLE_FILE_INFORMATION info;FILE_STREAM_INFO streams[128];BYTE* sd=NULL;DWORD n=0;
 bool ok=GetFileInformationByHandle(file,&info)&&info.nNumberOfLinks==1&&!(info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))&&l4_store_security(file,true,&sd,&n)&&GetFileInformationByHandleEx(file,FileStreamInfo,streams,sizeof(streams))&&streams[0].NextEntryOffset==0&&streams[0].StreamNameLength==14&&!memcmp(streams[0].StreamName,L"::$DATA",14);free(sd);return ok?true:fail(ERROR_ACCESS_DENIED);
}
static void hex(const BYTE* digest,char text[65]){for(unsigned i=0;i<32;i++)sprintf_s(text+2*i,65-2*i,"%02x",digest[i]);}
typedef struct{ULONGLONG sequence;unsigned count;} OutcomeRecord;
static bool find_outcome(DWORD kind,ULONGLONG sequence,const void* bytes,DWORD size,void* context){(void)bytes;(void)size;OutcomeRecord* found=context;if(kind==103){found->sequence=sequence;found->count++;}return true;}
static bool exact_clear(const L4Layout* roots,const L4RemoteStatus* status){L4UpdateState state;
 return l4_update_state_read(roots,&state)&&!state.window&&state.generation==status->outcome_clear_generation&&state.plan_sequence==status->plan_sequence&&!strcmp(state.owner,status->operation_id)?true:fail(ERROR_REVISION_MISMATCH);
}
static bool digest_record(const L4JournalReader* reader,DWORD kind,ULONGLONG sequence,BYTE digest[32],BYTE** bytes,DWORD* size){
 *bytes=NULL;*size=0;return l4_journal_reader_find(reader,kind,sequence,bytes,size)&&l4_store_hash(*bytes,*size,NULL,0,digest);
}
static bool write_result(const wchar_t* directory,const char* bytes,DWORD size){wchar_t path[MAX_PATH];PSECURITY_DESCRIPTOR sd=NULL;HANDLE file=INVALID_HANDLE_VALUE;bool ok=false;
 if(swprintf_s(path,MAX_PATH,L"%ls\\acceptance.result.json",directory)<1)return fail(ERROR_BAD_PATHNAME);
 if(!ConvertStringSecurityDescriptorToSecurityDescriptorW(L"O:SYG:SYD:P(A;;FA;;;SY)(A;;FR;;;BA)",SDDL_REVISION_1,&sd,NULL))return false;
 SECURITY_ATTRIBUTES attributes={sizeof(attributes),sd,FALSE};file=CreateFileW(path,GENERIC_READ|GENERIC_WRITE|READ_CONTROL,0,&attributes,CREATE_NEW,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_WRITE_THROUGH,NULL);
 if(file!=INVALID_HANDLE_VALUE){PSID owner=NULL;BOOL defaulted=FALSE;BYTE* actual=NULL;DWORD n=0;ok=safe(file)&&l4_store_security(file,true,&actual,&n)&&GetSecurityDescriptorOwner(actual,&owner,&defaulted)&&IsWellKnownSid(owner,WinLocalSystemSid);free(actual);
  DWORD written=0;if(ok)ok=WriteFile(file,bytes,size,&written,NULL)&&written==size&&FlushFileBuffers(file);
 }DWORD error=GetLastError();if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);LocalFree(sd);return ok?true:fail(error?error:ERROR_WRITE_FAULT);
}
#define REQUIRE(x) do{SetLastError(0);if(!(x)){error=GetLastError()?GetLastError():ERROR_INVALID_DATA;goto done;}}while(0)
bool setup_acceptance_export(const L4Layout* roots,const wchar_t* operation){
 HANDLE lock=INVALID_HANDLE_VALUE;L4FileFence fence={0};L4JournalReader* reader=NULL;SetupOperationPlan *plan=NULL,*commit_plan=NULL;SetupAcceptancePin* purpose=NULL;BYTE *raw=NULL,*proof=NULL,*outcome=NULL;DWORD raw_size=0,proof_size=0,outcome_size=0,error=ERROR_NOT_READY;
 L4RemoteStatus status={0},live={0};SetupRemoteCommit commit={0};SetupRemoteRestoreProof restore={0};L4PlatformFacts platform={0};BYTE plan_hash[32],proof_hash[32],outcome_hash[32];char document[SETUP_ACCEPTANCE_EXPORT_LIMIT],epochs[512],configs[512];DWORD pids[4];FILETIME births[4];ULONGLONG finished=0;
 if(!roots||!operation)return fail(ERROR_INVALID_PARAMETER);REQUIRE(system_owner());
 wchar_t lock_path[MAX_PATH];REQUIRE(l4_layout_data_path(roots,L"update\\operations\\deployment.lock",lock_path));
 REQUIRE(l4_store_pin(roots->operations,roots->data,false,&fence));lock=CreateFileW(lock_path,GENERIC_READ|READ_CONTROL,0,NULL,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,NULL);REQUIRE(lock!=INVALID_HANDLE_VALUE&&safe(lock));
 REQUIRE(l4_journal_reader_open_fixed_immutable_reference(roots,operation,&reader));REQUIRE(l4_journal_reader_is_immutable(reader));REQUIRE(l4_remote_status_snapshot(reader,roots,&status));
 if(!status.has_outcome||!status.has_host||!status.outcome_clear_recorded||status.has_result||status.has_launch_failure){error=ERROR_INVALID_STATE;goto done;}
 REQUIRE(l4_remote_status_observe(roots,operation,&live));if(!live.outcome_cleared||memcmp(&status.outcome,&live.outcome,sizeof(status.outcome))||status.last_sequence!=live.last_sequence){error=ERROR_REVISION_MISMATCH;goto done;}REQUIRE(exact_clear(roots,&status));
 REQUIRE(setup_update_load_operation_snapshot(reader,roots,status.plan_sequence,&plan));if(setup_operation_hops(plan)!=1){error=ERROR_NOT_SUPPORTED;goto done;}
 REQUIRE(setup_acceptance_open_snapshot(reader,roots,plan,&purpose));if(!purpose){error=ERROR_ACCESS_DENIED;goto done;}
 const SetupAcceptanceFacts* facts=setup_acceptance_facts(purpose);if(!facts){error=ERROR_INVALID_DATA;goto done;}
 DWORD kind=facts->force_rollback?108:102,expected=facts->force_rollback?L4_REMOTE_OUTCOME_RESTORED:L4_REMOTE_OUTCOME_SUCCESS;
 if(status.outcome.result.result!=expected||!!status.outcome.result.error!=facts->force_rollback||strcmp(status.operation_id,facts->operation)||strcmp(status.host.source_version,facts->source_version)||strcmp(status.resolved_version,facts->target_version)){error=ERROR_REVISION_MISMATCH;goto done;}
 REQUIRE(digest_record(reader,64,status.plan_sequence,plan_hash,&raw,&raw_size));REQUIRE(digest_record(reader,kind,status.outcome.proof_sequence,proof_hash,&proof,&proof_size));
 OutcomeRecord found={0};REQUIRE(l4_journal_reader_replay(reader,find_outcome,&found));if(found.count!=1){error=ERROR_INVALID_DATA;goto done;}REQUIRE(digest_record(reader,103,found.sequence,outcome_hash,&outcome,&outcome_size));
 if(memcmp(plan_hash,status.outcome.plan_sha256,32)||memcmp(proof_hash,status.outcome.proof_sha256,32)){error=ERROR_CRC;goto done;}
 unsigned config_count=0;const ULONGLONG* refs=setup_operation_configs(plan,&config_count);if(!refs||config_count!=12){error=ERROR_INVALID_DATA;goto done;}
 if(kind==102){REQUIRE(setup_remote_commit_admit_target(reader,roots,&commit,&commit_plan));if(commit.operation_sequence!=status.plan_sequence||memcmp(commit.operation_sha256,plan_hash,32)){error=ERROR_REVISION_MISMATCH;goto done;}memcpy(pids,commit.pids,sizeof(pids));memcpy(births,commit.births,sizeof(births));finished=commit.finished_utc;for(unsigned i=0;i<12;i++)if(refs[i]!=commit.configs[i]){error=ERROR_REVISION_MISMATCH;goto done;}}
 else{REQUIRE(setup_remote_restore_decode(proof,proof_size,&restore));if(restore.operation_sequence!=status.plan_sequence||strcmp(restore.operation,facts->operation)||strcmp(restore.version,facts->source_version)||memcmp(restore.operation_sha256,plan_hash,32)||memcmp(restore.source_root,facts->source_root,32)){error=ERROR_REVISION_MISMATCH;goto done;}memcpy(pids,restore.pids,sizeof(pids));memcpy(births,restore.births,sizeof(births));finished=restore.finished_utc;for(unsigned i=0;i<12;i++)if(refs[i]!=restore.configs[i]){error=ERROR_REVISION_MISMATCH;goto done;}}
 if(finished>status.outcome.result.finished_at||finished<status.request.accepted_utc||status.outcome.result.finished_at>utc()||facts->configured_terminal!=773||facts->configured_tenant!=1){error=ERROR_INVALID_DATA;goto done;}
 const SetupRootManifest *source=setup_operation_root(plan),*target=setup_operation_target_root(plan,0);const SetupRootAsset *source_descriptor=setup_root_asset(source,0),*target_descriptor=setup_root_asset(target,0);if(!source_descriptor||!target_descriptor){error=ERROR_INVALID_DATA;goto done;}
 REQUIRE(l4_platform_read(&platform));char profile[L4_PLATFORM_PROFILE_SIZE];REQUIRE(l4_platform_format(&platform,profile));if(strcmp(profile,facts->profile)||(platform.native_arch!=PROCESSOR_ARCHITECTURE_INTEL&&platform.native_arch!=PROCESSOR_ARCHITECTURE_AMD64)){error=ERROR_REVISION_MISMATCH;goto done;}
 char sr[65],tr[65],si[65],ti[65],ch[65],ih[65],ah[65],ph[65],pr[65],oh[65],eh[65];hex(facts->source_root,sr);hex(facts->target_root,tr);hex(source_descriptor->sha256,si);hex(target_descriptor->sha256,ti);hex(facts->catalog_sha256,ch);hex(facts->intent_sha256,ih);hex(facts->authorization_sha256,ah);hex(plan_hash,ph);hex(proof_hash,pr);hex(outcome_hash,oh);hex(status.host.executable_sha256,eh);
 size_t at=0;for(unsigned i=0;i<4;i++){if(!pids[i]||!ticks(births[i])||ticks(births[i])>finished){error=ERROR_INVALID_DATA;goto done;}int n=snprintf(epochs+at,sizeof(epochs)-at,"%s{\"pid\":%lu,\"birth_utc\":%llu}",i?",":"",pids[i],ticks(births[i]));if(n<0||(size_t)n>=sizeof(epochs)-at){error=ERROR_INSUFFICIENT_BUFFER;goto done;}at+=(size_t)n;}
 at=0;for(unsigned i=0;i<12;i++){int n=snprintf(configs+at,sizeof(configs)-at,"%s%llu",i?",":"",refs[i]);if(n<0||(size_t)n>=sizeof(configs)-at){error=ERROR_INSUFFICIENT_BUFFER;goto done;}at+=(size_t)n;}
 const char *installed_version=facts->force_rollback?facts->source_version:facts->target_version,*installed_root=facts->force_rollback?sr:tr,*installed_inventory=facts->force_rollback?si:ti;
 int length=snprintf(document,sizeof(document),"{\"schema\":1,\"kind\":\"l4tools-local-acceptance-result\",\"operation_id\":\"%s\",\"arch\":\"%s\",\"profile\":\"%s\",\"platform\":{\"major\":%lu,\"minor\":%lu,\"build\":%lu,\"native_arch\":\"%s\",\"product_type\":%u},\"terminal_id\":%u,\"tenant_id\":%u,\"from\":{\"version\":\"%s\",\"root_sha256\":\"%s\",\"inventory_sha256\":\"%s\"},\"to\":{\"version\":\"%s\",\"root_sha256\":\"%s\",\"inventory_sha256\":\"%s\"},\"force_rollback\":%s,\"catalog\":{\"revision\":%llu,\"sha256\":\"%s\"},\"intent_sha256\":\"%s\",\"authorization_sha256\":\"%s\",\"executor\":{\"version\":\"%s\",\"sha256\":\"%s\",\"size\":%llu},\"run\":{\"operation_id\":\"%s\",\"plan_sha256\":\"%s\",\"proof_kind\":%lu,\"proof_sha256\":\"%s\",\"outcome_sha256\":\"%s\",\"result\":\"%s\",\"error\":%lu,\"installed\":{\"version\":\"%s\",\"root_sha256\":\"%s\",\"inventory_sha256\":\"%s\"},\"finished_utc\":%llu},\"clear_generation\":%llu,\"configs\":[%s],\"epochs\":[%s],\"started_utc\":%llu,\"exported_utc\":%llu}\n",
 facts->operation,facts->arch,profile,platform.major,platform.minor,platform.build,platform.native_arch==PROCESSOR_ARCHITECTURE_INTEL?"x86":"x64",(unsigned)platform.product_type,facts->configured_terminal,facts->configured_tenant,facts->source_version,sr,si,facts->target_version,tr,ti,facts->force_rollback?"true":"false",facts->catalog_revision,ch,ih,ah,status.host.updater_version,eh,status.host.executable_size,facts->operation,ph,kind,pr,oh,facts->force_rollback?"RESTORED":"SUCCESS",status.outcome.result.error,installed_version,installed_root,installed_inventory,status.outcome.result.finished_at,status.outcome_clear_generation,configs,epochs,status.request.accepted_utc,utc());
 if(length<0||(unsigned)length>=sizeof(document)){error=ERROR_INSUFFICIENT_BUFFER;goto done;}REQUIRE(setup_acceptance_verify_snapshot(purpose,reader,roots,plan));REQUIRE(exact_clear(roots,&status));REQUIRE(write_result(l4_journal_reader_directory(reader),document,(DWORD)length));error=0;
done:
 free(raw);free(proof);free(outcome);setup_acceptance_close(purpose);setup_operation_free(commit_plan);setup_operation_free(plan);l4_journal_reader_close(reader);if(lock!=INVALID_HANDLE_VALUE)CloseHandle(lock);l4_store_unpin(&fence);return error?fail(error):true;
}
