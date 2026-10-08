/* Actual protected journal/hash-chain/FlushFileBuffers/108 codec and Windows
 * process epoch. Admission, signed source, native SCM/IPC and task decisions
 * are modeled refusal/success composition, never signed fault acceptance. */
#include "../src/remote_restore.h"
#include "../../l4common/journal_internal.h"
#include "../../l4common/recovery_store.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned assertions,failures,fault,probes,barriers,drains,arm_queries,clears;static bool link_done,helper_done,outcome_saved;
static L4UpdateState marker;static L4CommunicationPlan link;static L4RecoveryPlan helper;static ULONGLONG configrefs[12];static BYTE stored[384];
#define CHECK(x) do{assertions++;if(!(x)){failures++;printf("FAIL restore line%u: %s error%lu\n",__LINE__,#x,GetLastError());}}while(0)
static BOOL WINAPI no_thread_token(HANDLE a,DWORD b,BOOL c,PHANDLE d){(void)a;(void)b;(void)c;(void)d;SetLastError(ERROR_NO_TOKEN);return FALSE;}
static BOOL WINAPI system_token(HANDLE a,DWORD b,PHANDLE c){(void)a;(void)b;return DuplicateHandle(GetCurrentProcess(),GetCurrentProcess(),GetCurrentProcess(),c,0,FALSE,DUPLICATE_SAME_ACCESS);}
static BOOL WINAPI token_info(HANDLE h,TOKEN_INFORMATION_CLASS kind,LPVOID out,DWORD size,PDWORD used){(void)h;*used=0;if(fault==1)return FALSE;if(kind==TokenUser){TOKEN_USER* user=out;DWORD n=SECURITY_MAX_SID_SIZE;CHECK(size>=sizeof(TOKEN_USER)+SECURITY_MAX_SID_SIZE);user->User.Sid=(BYTE*)out+sizeof(TOKEN_USER);return CreateWellKnownSid(WinLocalSystemSid,NULL,user->User.Sid,&n);}if(kind==TokenType){*(TOKEN_TYPE*)out=TokenPrimary;return TRUE;}if(kind==TokenSessionId){*(DWORD*)out=0;return TRUE;}return FALSE;}
#define OpenThreadToken no_thread_token
#define OpenProcessToken system_token
#define GetTokenInformation token_info
#include "../src/remote_restore.c"
#undef OpenThreadToken
#undef OpenProcessToken
#undef GetTokenInformation
bool l4_update_state_read(const L4Layout* layout,L4UpdateState* out){(void)layout;*out=marker;return fault!=2;}
bool setup_operation_binding(const SetupOperationPlan* op,L4Journal* j,ULONGLONG sequence){CHECK(op && j && sequence==20);return fault!=3;}
bool setup_operation_verify_images(const SetupOperationPlan* op){CHECK(op);return fault!=4;}
bool l4_access_verify(const L4Layout* l,const L4AccessActors* a){CHECK(l && a);return fault!=5;}
bool l4_config_verify(L4Journal* j,ULONGLONG ref,bool candidate){CHECK(j && ref>=1 && ref<=12 && !candidate);return fault!=6;}
ULONGLONG setup_operation_switch_reference(const SetupOperationPlan* op,unsigned hop,unsigned i){CHECK(op && !hop && i<4);return 13+i;}
bool setup_remote_service_observe(SetupRemoteService* service,SetupRemoteServiceObservation* out){memset(out,0,sizeof(*out));unsigned i=(unsigned)(ULONG_PTR)service-100;CHECK(i<4);out->phase=SETUP_SERVICE_RUNNING_OLD;out->state=marker;out->switch_reference=13+i;out->pid=1000+i;out->created.dwLowDateTime=2000+i;if(fault==7)out->phase=SETUP_SERVICE_RUNNING_TARGET;if(fault==8 && probes)out->pid++;if(fault==20 && outcome_saved)out->pid++;return true;}
bool setup_service_transaction_restore_records(SetupServiceTransaction* tx,const L4UpdateState* state,DWORD ms,ULONGLONG refs[4]){CHECK(tx && state && ms);for(unsigned i=0;i<4;i++)refs[i]=30+i;return fault!=9;}
bool l4_proxy_signal_source(const wchar_t* command,WORD* http,WORD* mqtt,char expected[64]){CHECK(command && *command);*http=18443;*mqtt=18883;expected[0]=0;return fault!=10;}
static bool probe(const L4BootstrapPlan* p,unsigned i,DWORD ms,void* context){(void)context;CHECK(p && i<4 && ms);probes++;if(fault==12)marker.generation++;return fault!=11;}
static bool barrier(const L4BootstrapPlan* p,DWORD ms,void* context){(void)context;CHECK(p && ms);barriers++;return fault!=13;}
bool setup_readiness_checks(L4Readiness* options,const DWORD budgets[4],DWORD ms,L4BootstrapChecks* checks_out){CHECK(options->proxy_port==18443 && options->broker_port==1883 && budgets[1]==300000 && ms==60000);memset(checks_out,0,sizeof(*checks_out));checks_out->probe=probe;checks_out->barrier=barrier;return true;}
bool l4_probe_drain_call(const wchar_t* component,DWORD pid,const L4UpdateState* state,DWORD ms){CHECK(!wcscmp(component,L"superv") && pid==1003 && state->window==1 && ms);drains++;return fault!=14;}
bool l4_probe_recovery_call(DWORD pid,const L4UpdateState* state,DWORD ms){CHECK(pid==1003 && state->window==1 && ms && !link_done);arm_queries++;return fault!=15;}
bool l4_communication_plan_open(const L4Layout* l,const wchar_t* id,L4CommunicationPin** out){CHECK(l && id && wcslen(id)==36);*out=(L4CommunicationPin*)1;return true;}
bool l4_communication_plan_verify_journal(L4Journal* j,const L4CommunicationPin* p){CHECK(j && p);return fault!=16;}
const L4CommunicationPlan* l4_communication_pinned_plan(const L4CommunicationPin* p){CHECK(p);return &link;}
void l4_communication_plan_close(L4CommunicationPin* p){(void)p;}
bool l4_communication_decision_open(const L4Layout* l,const wchar_t* id,DWORD ms,L4CommunicationDecision** out){CHECK(l && id && ms);*out=(L4CommunicationDecision*)1;return true;}
bool l4_communication_decision_read(L4CommunicationDecision* guard,L4CommunicationPhase* phase,DWORD* error){CHECK(guard);*phase=fault==17?L4_COMM_DEC_STARTED:link_done?L4_COMM_DEC_COMMITTED:L4_COMM_DEC_WAIT;*error=0;return true;}
bool l4_communication_decision_finish(L4CommunicationDecision* guard,L4CommunicationPhase phase,DWORD error,ULONGLONG now){CHECK(guard && phase==L4_COMM_DEC_COMMITTED && !error && now && barriers);link_done=true;return true;}
void l4_communication_decision_close(L4CommunicationDecision* guard){(void)guard;}
bool l4_recovery_open(const L4Layout* l,const wchar_t* id,DWORD ms,L4RecoveryGuard** out){CHECK(l && id && ms);*out=(L4RecoveryGuard*)1;return true;}
const L4RecoveryPlan* l4_recovery_plan(const L4RecoveryGuard* guard){CHECK(guard);return &helper;}
bool l4_recovery_action(L4RecoveryGuard* guard,ULONGLONG now,bool boot,L4RecoveryAction* action){CHECK(guard && now && !boot);*action=fault==18?L4_RECOVERY_BLOCKED:helper_done?L4_RECOVERY_DONE:L4_RECOVERY_WAIT;return true;}
bool l4_recovery_finish(L4RecoveryGuard* guard,L4RecoveryStatus status,DWORD error,ULONGLONG now){CHECK(guard && status==L4_RECOVERY_COMMITTED && !error && now && barriers && link_done);helper_done=true;return true;}
void l4_recovery_close(L4RecoveryGuard* guard){(void)guard;}
bool setup_remote_outcome_append_bound(L4Journal* j,const SetupOperationPlan* op,const L4RemoteOutcome* expected,ULONGLONG seq,const BYTE hash[32],L4RemoteOutcome* out){CHECK(j && op && expected->result.result==L4_REMOTE_OUTCOME_RESTORED && expected->result.error && link_done && helper_done);BYTE* raw=NULL;DWORD size=0;BYTE actual[32];CHECK(l4_store_find_record(j,108,seq,&raw,&size) && size==384 && l4_store_hash(raw,size,NULL,0,actual) && !memcmp(actual,hash,32));if(raw && size==384)memcpy(stored,raw,384);free(raw);if(fault==19)return fail(ERROR_WRITE_FAULT);*out=*expected;out->proof_sequence=seq;outcome_saved=true;return true;}
bool l4_update_state_publish(L4Journal* j,ULONGLONG plan,ULONGLONG generation,DWORD window,ULONGLONG deadline){CHECK(j && plan==20 && generation==marker.generation && !window && !deadline && outcome_saved && link_done && helper_done);clears++;if(fault==21)return fail(ERROR_WRITE_FAULT);marker.window=0;marker.deadline_utc=0;marker.generation++;return true;}
/* Admission adapters are deliberately not used by the constructed trusted
 * fixture context. Any accidental prepare() call fails loudly. */
#define UNUSED() CHECK(false)
unsigned setup_operation_hops(const SetupOperationPlan* p){(void)p;UNUSED();return 0;}
bool l4_worker_recheck(L4Journal* j,L4WorkerAdmission* a){(void)j;(void)a;UNUSED();return false;}
bool setup_installed_source_verify(SetupInstalledSource* p){(void)p;UNUSED();return false;}
bool setup_installed_source_owned_by(const SetupInstalledSource* p,const L4Journal* j){(void)p;(void)j;UNUSED();return false;}
const L4BootstrapPlan* setup_installed_source_plan(const SetupInstalledSource* p){(void)p;UNUSED();return NULL;}
const SetupRootManifest* setup_installed_source_root(const SetupInstalledSource* p){(void)p;UNUSED();return NULL;}
const SetupRootManifest* setup_operation_root(const SetupOperationPlan* p){(void)p;UNUSED();return NULL;}
const BYTE* setup_root_identity(const SetupRootManifest* p){(void)p;UNUSED();return NULL;}
const char* setup_installed_source_suite_version(const SetupInstalledSource* p){(void)p;UNUSED();return NULL;}
const char* setup_installed_source_arch(const SetupInstalledSource* p){(void)p;UNUSED();return NULL;}
const L4AccessActors* setup_installed_source_actors(const SetupInstalledSource* p){(void)p;UNUSED();return NULL;}
const ULONGLONG* setup_operation_configs(const SetupOperationPlan* p,unsigned* count){(void)p;(void)count;UNUSED();return NULL;}
const L4ServiceSwitch* setup_operation_switch(const SetupOperationPlan* p,unsigned h,unsigned i){(void)p;(void)h;(void)i;UNUSED();return NULL;}
static void reset(SetupRemoteRestore* p,L4Journal* j,const SetupOperationPlan* op,unsigned window){
 memset(p,0,sizeof(*p));fault=probes=barriers=drains=arm_queries=clears=0;link_done=helper_done=outcome_saved=false;memset(&marker,0,sizeof(marker));strcpy_s(marker.owner,40,"17730000-0000-4000-8000-000000000001");marker.window=window;marker.generation=window;marker.plan_sequence=20;marker.deadline_utc=utc()+6000000000ull;p->original=j;p->operation=op;p->proof.operation_sequence=20;strcpy_s(p->proof.operation,37,marker.owner);strcpy_s(p->proof.version,32,"1.13.6");strcpy_s(p->proof.arch,8,"x86");p->proof.source_root[0]=1;p->proof.operation_sha256[0]=2;p->proof.worker_pid=GetCurrentProcessId();FILETIME e,k,u;CHECK(GetProcessTimes(GetCurrentProcess(),&p->proof.worker_birth,&e,&k,&u));for(unsigned i=0;i<12;i++)p->proof.configs[i]=configrefs[i];wcscpy_s(p->old.commands[0],2048,L"signed-old-proxy");
 memset(&link,0,sizeof(link));memset(&helper,0,sizeof(helper));memcpy(&link.operation,j->header+8,16);memcpy(&helper.operation,j->header+8,16);link.sequence=helper.sequence=20;link.generation=1;link.deadline_utc=marker.deadline_utc;link.worker_pid=helper.worker_pid=p->proof.worker_pid;link.worker_created=helper.worker_created=p->proof.worker_birth;memcpy(link.operation_sha256,p->proof.operation_sha256,32);p->admission.sequence=20;p->admission.deadline_utc=helper.deadline_utc=marker.deadline_utc+6000000000ull;
}
static bool remove_tree(const wchar_t* root){wchar_t pattern[MAX_PATH],path[MAX_PATH];swprintf_s(pattern,MAX_PATH,L"%ls\\*",root);WIN32_FIND_DATAW data;HANDLE search=FindFirstFileW(pattern,&data);bool ok=true;if(search!=INVALID_HANDLE_VALUE){do{if(!wcscmp(data.cFileName,L".") || !wcscmp(data.cFileName,L".."))continue;swprintf_s(path,MAX_PATH,L"%ls\\%ls",root,data.cFileName);if(data.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT){ok=false;break;}ok=data.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY?remove_tree(path):DeleteFileW(path)!=0;if(!ok)break;}while(FindNextFileW(search,&data));FindClose(search);}return ok && RemoveDirectoryW(root);}
int wmain(void){
 wchar_t tmp[MAX_PATH],root[MAX_PATH],pf[MAX_PATH],pd[MAX_PATH];CHECK(GetTempPathW(MAX_PATH,tmp));swprintf_s(root,MAX_PATH,L"%lsL4Restore-%lu-%llu",tmp,GetCurrentProcessId(),GetTickCount64());CHECK(CreateDirectoryW(root,NULL));swprintf_s(pf,MAX_PATH,L"%ls\\PF",root);swprintf_s(pd,MAX_PATH,L"%ls\\PD",root);L4Layout layout;CHECK(l4_layout_from_roots(&layout,pf,pd,L"1.13.6") && l4_layout_prepare(&layout));L4Journal* j=NULL;CHECK(l4_journal_open(&layout,L"17730000-0000-4000-8000-000000000001",true,&j));for(unsigned i=0;i<12;i++)CHECK(l4_journal_append(j,20,"oldconfig",9,&configrefs[i]));
 SetupRemoteRestore p;SetupOperationPlan* op=(SetupOperationPlan*)1;SetupServiceTransaction* tx=(SetupServiceTransaction*)2;SetupRemoteService* services[4]={(SetupRemoteService*)100,(SetupRemoteService*)101,(SetupRemoteService*)102,(SetupRemoteService*)103};L4RemoteOutcome out;
 for(unsigned window=1;window<=2;window++){for(unsigned f=1;f<=21;f++){if(window==2 && (f==14 || f==15))continue;reset(&p,j,op,window);fault=f;ULONGLONG seq=j->sequence;CHECK(!setup_remote_restore_finish(&p,j,op,tx,services,ERROR_CONNECTION_ABORTED,1000,&out));CHECK(marker.window==window);CHECK(clears==(f==21?1u:0u));CHECK(j->sequence==seq+(f>=19?1u:0u));}
  reset(&p,j,op,window);CHECK(setup_remote_restore_finish(&p,j,op,tx,services,ERROR_CONNECTION_ABORTED,1000,&out));CHECK(clears==1 && !marker.window && link_done && helper_done && barriers==1 && probes==(window==1?3u:4u));CHECK(arm_queries==(window==1?1u:0u));SetupRemoteRestoreProof decoded;CHECK(setup_remote_restore_decode(stored,384,&decoded));CHECK(decoded.verification_mode==window && decoded.window==window && !strcmp(decoded.version,"1.13.6"));
  for(unsigned mode=0;mode<12;mode++){BYTE mutated[384];memcpy(mutated,stored,384);if(mode<3)mutated[381+mode]=1;else if(mode==3)l4_store_u32(mutated+8,2);else if(mode==4)l4_store_u32(mutated+12,0);else if(mode==5)l4_store_u64(mutated+16,0);else if(mode==6)l4_store_u64(mutated+181,l4_store_get64(mutated+24));else if(mode==7)l4_store_u32(mutated+201,0);else if(mode==8)l4_store_u64(mutated+345,20);else if(mode==9)memcpy(mutated+257,mutated+249,8);else if(mode==10)mutated[72]='z';else l4_store_u32(mutated+377,window==1?2:1);CHECK(!setup_remote_restore_decode(mutated,384,&decoded));}
  reset(&p,j,op,window);fault=21;CHECK(!setup_remote_restore_finish(&p,j,op,tx,services,ERROR_CONNECTION_ABORTED,1000,&out));fault=0;CHECK(setup_remote_restore_finish(&p,j,op,tx,services,ERROR_CONNECTION_ABORTED,1000,&out));CHECK(barriers==2 && !marker.window);
 }
 wchar_t file[MAX_PATH],directory[MAX_PATH];wcscpy_s(directory,MAX_PATH,j->directory);l4_journal_close(j);swprintf_s(file,MAX_PATH,L"%ls\\journal.bin",directory);CHECK(DeleteFileW(file));CHECK(RemoveDirectoryW(directory));
 /* Only our fixed temporary root, refusing any reparse child. */
 CHECK(!wcsncmp(root,tmp,wcslen(tmp)) && !wcsncmp(root+wcslen(tmp),L"L4Restore-",10));CHECK(remove_tree(root));
 printf("Whole-old restoration: %u passed, %u failed; journal/codec/epochs actual, admission/SCM/IPC/guards modeled\n",assertions-failures,failures);return failures?1:0;
}
