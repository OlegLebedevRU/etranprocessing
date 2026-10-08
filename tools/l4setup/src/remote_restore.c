#include "remote_restore.h"
#include "remote_outcome_internal.h"
#include "readiness.h"
#include "remote_policy.h"
#include "../../l4common/journal_internal.h"
#include "../../l4common/probe_ipc.h"
#include "../../l4common/proxy_certificate.h"
#include "../../l4common/recovery_store.h"
#include "../../l4common/worker_handoff.h"
#include <stdlib.h>
#include <string.h>
struct SetupRemoteRestore {
 L4Journal* original;const SetupOperationPlan* operation;L4BootstrapPlan old;
 L4AccessActors actors;L4WorkerAdmission admission;L4UpdateState before;
 SetupRemoteRestoreProof proof;ULONGLONG proof_sequence;bool captured,settled;
};
static bool fail(DWORD e){SetLastError(e);return false;}
static ULONGLONG ft(FILETIME value){return ((ULONGLONG)value.dwHighDateTime<<32)|value.dwLowDateTime;}
static FILETIME timevalue(ULONGLONG value){FILETIME t={(DWORD)value,(DWORD)(value>>32)};return t;}
static ULONGLONG utc(void){FILETIME value;GetSystemTimeAsFileTime(&value);return ft(value);}
static bool system_owner(void){
 HANDLE thread=NULL,token=NULL;BYTE bytes[sizeof(TOKEN_USER)+SECURITY_MAX_SID_SIZE];DWORD n=0,session=~0u;TOKEN_TYPE type=TokenImpersonation;
 if(OpenThreadToken(GetCurrentThread(),TOKEN_QUERY,TRUE,&thread)){CloseHandle(thread);return fail(ERROR_ACCESS_DENIED);}if(GetLastError()!=ERROR_NO_TOKEN)return false;
 bool ok=OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token) && GetTokenInformation(token,TokenUser,bytes,sizeof(bytes),&n) && IsWellKnownSid(((TOKEN_USER*)bytes)->User.Sid,WinLocalSystemSid) && GetTokenInformation(token,TokenType,&type,sizeof(type),&n) && type==TokenPrimary && GetTokenInformation(token,TokenSessionId,&session,sizeof(session),&n) && !session;
 DWORD error=GetLastError();if(token)CloseHandle(token);return ok?true:fail(error?error:ERROR_ACCESS_DENIED);
}
static bool field(const char* s,unsigned capacity){unsigned n=0;for(;n<capacity && s[n];n++)if((unsigned char)s[n]<33 || (unsigned char)s[n]>126)return false;if(!n || n==capacity)return false;for(;n<capacity;n++)if(s[n])return false;return true;}
static bool version(const char* s){for(unsigned part=0;part<3;part++){if(*s<'0' || *s>'9' || (*s=='0' && s[1]>='0' && s[1]<='9'))return false;unsigned n=0;do{s++;if(++n>9)return false;}while(*s>='0' && *s<='9');if(part<2 && *s++!='.')return false;}return !*s;}
static bool valid(const SetupRemoteRestoreProof* p){
 if(!p || !field(p->operation,37) || !field(p->version,32) || !version(p->version) || !field(p->arch,8) || (strcmp(p->arch,"x86") && strcmp(p->arch,"x64")) || (p->window!=1 && p->window!=2) || p->verification_mode!=p->window || !p->generation || !p->operation_sequence || !p->deadline_utc || !p->finished_utc || p->finished_utc>=p->deadline_utc || !p->worker_pid || !ft(p->worker_birth))return false;
 bool nonzero=false;for(unsigned i=0;i<36;i++){char c=p->operation[i];if(i==8 || i==13 || i==18 || i==23){if(c!='-')return false;}else if((c>='0' && c<='9') || (c>='a' && c<='f')){if(c!='0')nonzero=true;}else return false;}if(!nonzero || p->operation[36])return false;
 for(unsigned i=0;i<4;i++){if(!p->pids[i] || !ft(p->births[i]) || !p->restores[i] || p->restores[i]<=p->operation_sequence)return false;for(unsigned k=0;k<i;k++)if(p->restores[k]==p->restores[i])return false;}
 for(unsigned i=0;i<12;i++){if(!p->configs[i] || p->configs[i]>=p->operation_sequence)return false;for(unsigned k=0;k<i;k++)if(p->configs[k]==p->configs[i])return false;}
 BYTE a=0,b=0;for(unsigned i=0;i<32;i++){a|=p->operation_sha256[i];b|=p->source_root[i];}return a && b;
}
static bool encode(const SetupRemoteRestoreProof* p,BYTE bytes[384]){
 if(!valid(p))return fail(ERROR_INVALID_DATA);memset(bytes,0,384);memcpy(bytes,"L4ROLD01",8);l4_store_u32(bytes+8,1);l4_store_u32(bytes+12,p->window);l4_store_u64(bytes+16,p->generation);l4_store_u64(bytes+24,p->deadline_utc);l4_store_u64(bytes+32,p->operation_sequence);memcpy(bytes+40,p->operation_sha256,32);memcpy(bytes+72,p->operation,37);memcpy(bytes+109,p->version,32);memcpy(bytes+141,p->arch,8);memcpy(bytes+149,p->source_root,32);l4_store_u64(bytes+181,p->finished_utc);l4_store_u32(bytes+189,p->worker_pid);l4_store_u64(bytes+193,ft(p->worker_birth));
 for(unsigned i=0;i<4;i++){l4_store_u32(bytes+201+i*4,p->pids[i]);l4_store_u64(bytes+217+i*8,ft(p->births[i]));l4_store_u64(bytes+345+i*8,p->restores[i]);}for(unsigned i=0;i<12;i++)l4_store_u64(bytes+249+i*8,p->configs[i]);l4_store_u32(bytes+377,p->verification_mode);return true;
}
bool setup_remote_restore_decode(const void* data,DWORD size,SetupRemoteRestoreProof* p){
 if(!p)return fail(ERROR_INVALID_PARAMETER);memset(p,0,sizeof(*p));const BYTE* b=data;if(!b || size!=384 || memcmp(b,"L4ROLD01",8) || l4_store_get32(b+8)!=1 || b[381] || b[382] || b[383])return fail(ERROR_INVALID_DATA);
 p->window=l4_store_get32(b+12);p->generation=l4_store_get64(b+16);p->deadline_utc=l4_store_get64(b+24);p->operation_sequence=l4_store_get64(b+32);memcpy(p->operation_sha256,b+40,32);memcpy(p->operation,b+72,37);memcpy(p->version,b+109,32);memcpy(p->arch,b+141,8);memcpy(p->source_root,b+149,32);p->finished_utc=l4_store_get64(b+181);p->worker_pid=l4_store_get32(b+189);p->worker_birth=timevalue(l4_store_get64(b+193));
 for(unsigned i=0;i<4;i++){p->pids[i]=l4_store_get32(b+201+i*4);p->births[i]=timevalue(l4_store_get64(b+217+i*8));p->restores[i]=l4_store_get64(b+345+i*8);}for(unsigned i=0;i<12;i++)p->configs[i]=l4_store_get64(b+249+i*8);p->verification_mode=l4_store_get32(b+377);return valid(p)?true:fail(ERROR_INVALID_DATA);
}
void setup_remote_restore_free(SetupRemoteRestore* p){if(!p)return;HANDLE tokens[]={p->actors.proxy,p->actors.broker,p->actors.console,p->actors.supervisor,p->actors.desktop};for(unsigned i=0;i<5;i++)if(tokens[i])CloseHandle(tokens[i]);free(p);}
static bool tokens(const L4AccessActors* from,L4AccessActors* to){if(!from)return fail(ERROR_INVALID_DATA);HANDLE input[]={from->proxy,from->broker,from->console,from->supervisor,from->desktop};HANDLE* output[]={&to->proxy,&to->broker,&to->console,&to->supervisor,&to->desktop};for(unsigned i=0;i<5;i++)if(input[i] && !DuplicateHandle(GetCurrentProcess(),input[i],GetCurrentProcess(),output[i],0,FALSE,DUPLICATE_SAME_ACCESS))return false;return true;}
#define REQUIRE(x) do{SetLastError(0);if(!(x)){error=GetLastError()?GetLastError():ERROR_NOT_READY;goto done;}}while(0)
bool setup_remote_restore_prepare(L4Journal* j,SetupInstalledSource* source,const SetupOperationPlan* op,SetupRemoteRestore** out){
 if(!out)return fail(ERROR_INVALID_PARAMETER);*out=NULL;if(!j || !source || !op || setup_operation_hops(op)!=1)return fail(ERROR_NOT_SUPPORTED);DWORD error=ERROR_NOT_READY;SetupRemoteRestore* p=calloc(1,sizeof(*p));if(!p)return fail(ERROR_NOT_ENOUGH_MEMORY);
 REQUIRE(system_owner());REQUIRE(l4_worker_recheck(j,&p->admission));REQUIRE(setup_operation_binding(op,j,p->admission.sequence));REQUIRE(setup_operation_verify_images(op));REQUIRE(setup_installed_source_owned_by(source,j));REQUIRE(setup_installed_source_verify(source));REQUIRE(l4_update_state_read(&j->layout,&p->before));
 if(p->before.window || p->before.generation!=p->admission.generation){error=ERROR_INVALID_STATE;goto done;}
 const L4BootstrapPlan* previous=setup_installed_source_plan(source);const BYTE* source_hash=setup_root_identity(setup_installed_source_root(source));const BYTE* expected=setup_root_identity(setup_operation_root(op));const char* version=setup_installed_source_suite_version(source),*arch=setup_installed_source_arch(source);const wchar_t* id=wcsrchr(j->directory,L'\\');
 if(!previous || !source_hash || !expected || memcmp(source_hash,expected,32) || !version || !arch || !id){error=ERROR_REVISION_MISMATCH;goto done;}p->old=*previous;p->original=j;p->operation=op;p->proof.operation_sequence=p->admission.sequence;memcpy(p->proof.source_root,source_hash,32);REQUIRE(strcpy_s(p->proof.version,32,version)==0);REQUIRE(strcpy_s(p->proof.arch,8,arch)==0);REQUIRE(WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,id+1,-1,p->proof.operation,37,NULL,NULL));REQUIRE(tokens(setup_installed_source_actors(source),&p->actors));
 unsigned count=0;const ULONGLONG* configs=setup_operation_configs(op,&count);if(!configs || count!=12){error=ERROR_INVALID_DATA;goto done;}memcpy(p->proof.configs,configs,sizeof(p->proof.configs));
 for(unsigned i=0;i<4;i++){const L4ServiceSwitch* change=setup_operation_switch(op,0,i);if(!change || wcscmp(p->old.services[i],change->service) || wcscmp(p->old.commands[i],change->before.image_path) || p->old.start_types[i]!=change->before.start_type){error=ERROR_REVISION_MISMATCH;goto done;}}
 BYTE* raw=NULL;DWORD length=0;bool ok=l4_store_find_record(j,64,p->proof.operation_sequence,&raw,&length) && l4_store_hash(raw,length,NULL,0,p->proof.operation_sha256);free(raw);REQUIRE(ok);p->proof.worker_pid=GetCurrentProcessId();FILETIME ended,kernel,user;REQUIRE(GetProcessTimes(GetCurrentProcess(),&p->proof.worker_birth,&ended,&kernel,&user));REQUIRE(setup_installed_source_verify(source));L4WorkerAdmission repeated;REQUIRE(l4_worker_recheck(j,&repeated));*out=p;return true;
done:setup_remote_restore_free(p);return fail(error);
}
static bool same(const L4UpdateState* a,const L4UpdateState* b){return !memcmp(a->owner,b->owner,40) && a->window==b->window && a->generation==b->generation && a->plan_sequence==b->plan_sequence && a->deadline_utc==b->deadline_utc;}
static DWORD left(ULONGLONG end,const L4UpdateState* s){ULONGLONG tick=GetTickCount64(),now=utc();if(tick>=end || now>=s->deadline_utc)return 0;ULONGLONG a=end-tick,b=(s->deadline_utc-now)/10000;if(b<a)a=b;return a>300000?300000:(DWORD)a;}
static bool local(SetupRemoteRestore* p,L4Journal* j,const SetupOperationPlan* op,SetupRemoteService* services[4],const L4UpdateState* s,ULONGLONG end,bool capture){
 L4UpdateState actual;FILETIME created,ended,kernel,user;if(p->original!=j || p->operation!=op || !system_owner() || p->proof.worker_pid!=GetCurrentProcessId() || !GetProcessTimes(GetCurrentProcess(),&created,&ended,&kernel,&user) || CompareFileTime(&created,&p->proof.worker_birth))return fail(ERROR_ACCESS_DENIED);
 if(!left(end,s) || !l4_update_state_read(&j->layout,&actual) || !same(s,&actual) || !setup_operation_binding(op,j,p->proof.operation_sequence) || !setup_operation_verify_images(op) || !l4_access_verify(&p->old.layout,&p->actors))return false;
 for(unsigned i=0;i<12;i++)if(!left(end,s) || !l4_config_verify(j,p->proof.configs[i],false))return false;
 for(unsigned i=0;i<4;i++){SetupRemoteServiceObservation value={0};if(!services[i] || !setup_remote_service_observe(services[i],&value))return false;
  if(value.phase!=SETUP_SERVICE_RUNNING_OLD || !same(s,&value.state) || value.switch_reference!=setup_operation_switch_reference(op,0,i))return fail(ERROR_REVISION_MISMATCH);
  if(capture){p->proof.pids[i]=value.pid;p->proof.births[i]=value.created;}else if(value.pid!=p->proof.pids[i] || CompareFileTime(&value.created,&p->proof.births[i]))return fail(ERROR_REVISION_MISMATCH);
 }
 return left(end,s) && l4_update_state_read(&j->layout,&actual) && same(s,&actual)?true:fail(ERROR_TIMEOUT);
}
static bool communication(SetupRemoteRestore* p,L4Journal* j,const L4UpdateState* s,ULONGLONG end,bool settle){
 wchar_t id[37];if(!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,p->proof.operation,-1,id,37))return false;L4CommunicationPin* pin=NULL;L4CommunicationDecision* guard=NULL;L4CommunicationPhase phase=L4_COMM_DEC_WAIT;DWORD code=0;
 bool ok=l4_communication_plan_open(&j->layout,id,&pin) && l4_communication_plan_verify_journal(j,pin);const L4CommunicationPlan* value=ok?l4_communication_pinned_plan(pin):NULL;
 if(ok)ok=value && value->sequence==p->proof.operation_sequence && !memcmp(&value->operation,j->header+8,16) && value->worker_pid==p->proof.worker_pid && !CompareFileTime(&value->worker_created,&p->proof.worker_birth) && !memcmp(value->operation_sha256,p->proof.operation_sha256,32) && value->generation==p->before.generation+1 && (s->window!=1 || value->deadline_utc==s->deadline_utc);
 if(ok)ok=left(end,s) && l4_communication_decision_open(&j->layout,id,left(end,s),&guard) && l4_communication_decision_read(guard,&phase,&code) && !code;
 if(ok && phase!=L4_COMM_DEC_COMMITTED)ok=settle && phase==L4_COMM_DEC_WAIT && utc()<value->deadline_utc && l4_communication_decision_finish(guard,L4_COMM_DEC_COMMITTED,0,utc());
 DWORD error=GetLastError();l4_communication_decision_close(guard);l4_communication_plan_close(pin);return ok?true:fail(error?error:ERROR_RECOVERY_FAILURE);
}
static bool recovery(SetupRemoteRestore* p,L4Journal* j,const L4UpdateState* s,ULONGLONG end,bool settle){
 wchar_t id[37];if(!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,p->proof.operation,-1,id,37))return false;L4RecoveryGuard* guard=NULL;if(!left(end,s) || !l4_recovery_open(&j->layout,id,left(end,s),&guard))return false;const L4RecoveryPlan* r=l4_recovery_plan(guard);const L4WorkerAdmission* a=&p->admission;
 bool ok=r && r->sequence==a->sequence && !memcmp(&r->operation,j->header+8,16) && r->worker_pid==p->proof.worker_pid && !CompareFileTime(&r->worker_created,&p->proof.worker_birth) && r->deadline_utc==a->deadline_utc && s->deadline_utc<r->deadline_utc && r->supervisor_pid==a->supervisor_pid && !CompareFileTime(&r->supervisor_created,&a->supervisor_created) && r->start_type==a->start_type && r->old_size==a->old_size && r->new_size==a->new_size && !memcmp(r->old_sha256,a->old_sha256,32) && !memcmp(r->new_sha256,a->new_sha256,32) && !wcscmp(r->before,a->before) && !wcscmp(r->after,a->after);
 L4RecoveryAction action=L4_RECOVERY_BLOCKED;if(ok)ok=l4_recovery_action(guard,utc(),false,&action);if(ok)ok=(action==L4_RECOVERY_DONE || (settle && action==L4_RECOVERY_WAIT)) && l4_recovery_finish(guard,L4_RECOVERY_COMMITTED,0,utc());DWORD error=GetLastError();l4_recovery_close(guard);return ok?true:fail(error?error:ERROR_RECOVERY_FAILURE);
}
static bool quiet(SetupRemoteRestore* p,const L4UpdateState* s,ULONGLONG end,bool armed){DWORD budget=left(end,s);if(budget>60000)budget=60000;if(!budget || !l4_probe_drain_call(L"superv",p->proof.pids[3],s,budget))return false;return !armed || (left(end,s) && l4_probe_recovery_call(p->proof.pids[3],s,left(end,s)));}
bool setup_remote_restore_finish(SetupRemoteRestore* p,L4Journal* j,const SetupOperationPlan* op,SetupServiceTransaction* tx,SetupRemoteService* services[4],DWORD original_error,DWORD timeout,L4RemoteOutcome* outcome){
 if(outcome)memset(outcome,0,sizeof(*outcome));if(!p || !j || !op || !tx || !services || !outcome || !original_error || !timeout || timeout>SETUP_REMOTE_RESTORE_RESERVE_MS)return fail(ERROR_INVALID_PARAMETER);L4UpdateState s;if(!l4_update_state_read(&j->layout,&s))return false;
 if((s.window!=1 && s.window!=2) || s.generation!=p->before.generation+s.window || s.plan_sequence!=p->proof.operation_sequence || strcmp(s.owner,p->proof.operation))return fail(ERROR_REVISION_MISMATCH);ULONGLONG end=GetTickCount64()+timeout;
 if(!p->captured){if(!setup_service_transaction_restore_records(tx,&s,left(end,&s),p->proof.restores) || !local(p,j,op,services,&s,end,true))return false;p->captured=true;p->proof.window=p->proof.verification_mode=s.window;p->proof.generation=s.generation;p->proof.deadline_utc=s.deadline_utc;}else if(p->proof.window!=s.window || p->proof.generation!=s.generation || p->proof.deadline_utc!=s.deadline_utc || !local(p,j,op,services,&s,end,false))return fail(ERROR_REVISION_MISMATCH);
 WORD http=0,mqtt=0;char certificate[64];L4Readiness options={0};L4BootstrapChecks checks;DWORD budgets[4]={60000,300000,60000,60000};if(!l4_proxy_signal_source(p->old.commands[0],&http,&mqtt,certificate) || mqtt!=18883)return fail(ERROR_NOT_SUPPORTED);options.proxy_port=http;options.broker_port=1883;if(!setup_readiness_checks(&options,budgets,60000,&checks))return false;
 for(unsigned i=0;i<4;i++){DWORD budget=left(end,&s);if(budget>budgets[i])budget=budgets[i];if(!budget)return fail(ERROR_TIMEOUT);if(i==3 && s.window==1){if(!quiet(p,&s,end,!p->settled))return false;}else if(!checks.probe(&p->old,i,budget,checks.context))return false;if(!local(p,j,op,services,&s,end,false))return false;}
 DWORD budget=left(end,&s);if(budget>60000)budget=60000;if(!budget || !checks.barrier(&p->old,budget,checks.context) || !local(p,j,op,services,&s,end,false))return false;
 if(!communication(p,j,&s,end,true) || !recovery(p,j,&s,end,true))return false;p->settled=true;if(!local(p,j,op,services,&s,end,false) || (s.window==1 && !quiet(p,&s,end,false)))return false;
 if(!p->proof.finished_utc)p->proof.finished_utc=utc();BYTE bytes[384];if(!encode(&p->proof,bytes))return false;
 if(p->proof_sequence){BYTE* raw=NULL;DWORD size=0;bool ok=l4_store_find_record(j,108,p->proof_sequence,&raw,&size) && size==384 && !memcmp(raw,bytes,384);free(raw);if(!ok)return fail(ERROR_CRC);}else if(!left(end,&s) || !l4_journal_append(j,108,bytes,384,&p->proof_sequence))return false;
 BYTE hash[32];L4RemoteOutcome expected={0};expected.result.result=L4_REMOTE_OUTCOME_RESTORED;expected.result.error=original_error;if(!l4_store_hash(bytes,384,NULL,0,hash) || !setup_remote_outcome_append_bound(j,op,&expected,p->proof_sequence,hash,outcome))return false;
 if(!local(p,j,op,services,&s,end,false) || !communication(p,j,&s,end,false) || !recovery(p,j,&s,end,false) || (s.window==1 && !quiet(p,&s,end,false)) || !left(end,&s) || !l4_update_state_publish(j,s.plan_sequence,s.generation,0,0))return false;
 L4UpdateState clear;if(!l4_update_state_read(&j->layout,&clear))return false;return !clear.window && !clear.deadline_utc && clear.generation==s.generation+1 && clear.plan_sequence==s.plan_sequence && !strcmp(clear.owner,s.owner)?true:fail(ERROR_REVISION_MISMATCH);
}
