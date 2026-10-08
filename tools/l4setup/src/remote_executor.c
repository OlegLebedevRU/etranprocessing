#include "remote_executor.h"
#include "acceptance_local.h"
#include "remote_policy.h"
#include "remote_restore.h"
#include "remote_outcome_internal.h"
#include "readiness.h"
#include "../../l4common/proxy_certificate.h"
#include "../../l4common/journal_internal.h"
#include "../../l4common/recovery_store.h"
#include <bcrypt.h>
#include <sddl.h>
#include <stdio.h>
#include <string.h>
typedef struct {
 L4Journal* journal;const SetupOperationPlan* operation;SetupInstalledSource* source;
 L4BootstrapPlan old,target,mixed;L4AccessActors actors;
 L4Readiness readiness;L4BootstrapChecks checks;SetupStopGate* stop;
 SetupRemoteCompletion* completion;SetupRemoteRestore* restore;
 SetupRemoteService* services[4];SetupServiceTransaction* transaction;
 L4WorkerAdmission admission;L4CommunicationPin* link;SetupRemoteWorkerPolicy policy;
 L4UpdateState state;ULONGLONG forward_end;
} Executor;
static bool fail(DWORD error){SetLastError(error);return false;}
/* Diagnostic only: never a journal proof, admission input or clear authority.
 * Keep original failure and recovery failure separately when completion refuses. */
static void failure_diagnostic(L4Journal* j,DWORD line,DWORD original,DWORD recovery){
 if(!j || !j->file || j->file==INVALID_HANDLE_VALUE || !j->lock || j->lock==INVALID_HANDLE_VALUE || j->poisoned)return;
 DWORD saved=GetLastError();wchar_t path[MAX_PATH];PSECURITY_DESCRIPTOR sd=NULL;
 if(swprintf_s(path,MAX_PATH,L"%ls\\executor.failure",j->directory)>0 && ConvertStringSecurityDescriptorToSecurityDescriptorW(L"O:SYG:SYD:P(A;;FA;;;SY)(A;;FA;;;BA)",SDDL_REVISION_1,&sd,NULL)){
  SECURITY_ATTRIBUTES attrs={sizeof(attrs),sd,FALSE};HANDLE file=CreateFileW(path,GENERIC_WRITE,FILE_SHARE_READ,&attrs,CREATE_NEW,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_WRITE_THROUGH,NULL);
  if(file!=INVALID_HANDLE_VALUE){BYTE bytes[24]={0};DWORD fields[4]={1,line,original,recovery};memcpy(bytes,"L4XERR01",8);memcpy(bytes+8,fields,sizeof(fields));DWORD written=0;if(WriteFile(file,bytes,sizeof(bytes),&written,NULL) && written==sizeof(bytes))FlushFileBuffers(file);CloseHandle(file);}
 }
 if(sd)LocalFree(sd);SetLastError(saved);
}
static ULONGLONG utc(void){FILETIME t;GetSystemTimeAsFileTime(&t);return ((ULONGLONG)t.dwHighDateTime<<32)|t.dwLowDateTime;}
static bool helper_equal(const L4RecoveryHelper* a,const L4RecoveryHelper* b){return a && b && a->size==b->size && !memcmp(a->sha256,b->sha256,32);}
static DWORD budget(Executor* e,DWORD maximum,bool forward){ULONGLONG now=utc(),tick=GetTickCount64(),deadline=e->state.deadline_utc;if(forward){ULONGLONG reserve=(ULONGLONG)SETUP_REMOTE_RESTORE_RESERVE_MS*10000;if(deadline<=reserve)return 0;deadline-=reserve;}if(now>=deadline || (forward && tick>=e->forward_end))return 0;ULONGLONG available=(deadline-now)/10000;if(forward && e->forward_end-tick<available)available=e->forward_end-tick;return available>maximum?maximum:(DWORD)available;}
static void cleanup(Executor* e){setup_service_transaction_close(e->transaction);for(unsigned i=0;i<4;i++)setup_remote_service_close(e->services[i]);setup_update_stop_gate_free(e->stop);setup_remote_completion_free(e->completion);setup_remote_restore_free(e->restore);l4_communication_plan_close(e->link);HANDLE tokens[]={e->actors.proxy,e->actors.broker,e->actors.console,e->actors.supervisor,e->actors.desktop};for(unsigned i=0;i<5;i++)if(tokens[i])CloseHandle(tokens[i]);setup_installed_source_close(e->source);}
static bool copy_actors(const L4AccessActors* from,L4AccessActors* to){if(!from)return fail(ERROR_INVALID_DATA);HANDLE input[]={from->proxy,from->broker,from->console,from->supervisor,from->desktop};HANDLE* output[]={&to->proxy,&to->broker,&to->console,&to->supervisor,&to->desktop};for(unsigned i=0;i<5;i++)if(input[i] && !DuplicateHandle(GetCurrentProcess(),input[i],GetCurrentProcess(),output[i],0,FALSE,DUPLICATE_SAME_ACCESS))return false;return true;}
static bool initialize(Executor* e,L4Journal* j,const SetupOperationPlan* op,const L4WorkerAdmission* admitted,const L4RecoveryHelper* helper){
 memset(e,0,sizeof(*e));e->journal=j;e->operation=op;if(!j || !op || !admitted || !helper || setup_operation_hops(op)!=1)return fail(ERROR_NOT_SUPPORTED);
 if(!l4_worker_recheck(j,&e->admission) || memcmp(&e->admission,admitted,sizeof(*admitted)) || !helper_equal(helper,&e->admission.helper) || !setup_operation_binding(op,j,e->admission.sequence) || !setup_operation_verify_images(op))return fail(ERROR_REVISION_MISMATCH);
 if(!setup_installed_source_open(j,&e->source) || !setup_installed_source_verify(e->source) || !setup_installed_source_owned_by(e->source,j))return false;const L4BootstrapPlan* source=setup_installed_source_plan(e->source);if(!source)return fail(ERROR_INVALID_DATA);e->old=*source;e->target=*source;
 if(!copy_actors(setup_installed_source_actors(e->source),&e->actors))return false;const L4Layout* layout=setup_manifest_layout(setup_operation_target(op,0));if(!layout)return fail(ERROR_INVALID_DATA);e->target.layout=*layout;
 for(unsigned i=0;i<4;i++){const L4ServiceSwitch* s=setup_operation_switch(op,0,i);if(!s || wcscmp(s->service,e->old.services[i]) || wcscmp(s->before.image_path,e->old.commands[i]) || s->before.start_type!=e->old.start_types[i])return fail(ERROR_REVISION_MISMATCH);wcscpy_s(e->target.commands[i],2048,s->after);e->target.sizes[i]=s->size;memcpy(e->target.sha256[i],s->sha256,32);}e->mixed=e->old;for(unsigned i=0;i<2;i++){wcscpy_s(e->mixed.commands[i],2048,e->target.commands[i]);e->mixed.sizes[i]=e->target.sizes[i];memcpy(e->mixed.sha256[i],e->target.sha256[i],32);}
 WORD http,mqtt;char thumb[64];if(!l4_proxy_signal_source(e->old.commands[0],&http,&mqtt,thumb) || mqtt!=18883)return fail(ERROR_NOT_SUPPORTED);e->readiness.proxy_port=http;e->readiness.broker_port=1883;DWORD probes[4]={60000,300000,60000,60000};if(!setup_readiness_checks(&e->readiness,probes,60000,&e->checks))return false;
 const wchar_t* id=wcsrchr(j->directory,L'\\');if(!id || !l4_communication_plan_open(&j->layout,id+1,&e->link) || !l4_communication_plan_verify_journal(j,e->link))return false;const L4CommunicationPlan* link=l4_communication_pinned_plan(e->link);
 if(!link || !setup_remote_policy_at(link->armed_utc,&e->policy) || link->sequence!=e->admission.sequence || link->generation!=e->admission.generation+1 || link->deadline_utc!=e->policy.communication_deadline_utc || e->admission.deadline_utc!=e->policy.supervisor_deadline_utc || memcmp(&link->budget,&e->policy.communication,sizeof(link->budget)))return fail(ERROR_REVISION_MISMATCH);
 FILETIME birth,ended,kernel,user;if(link->worker_pid!=GetCurrentProcessId() || !GetProcessTimes(GetCurrentProcess(),&birth,&ended,&kernel,&user) || CompareFileTime(&birth,&link->worker_created))return fail(ERROR_REVISION_MISMATCH);
 if(!l4_update_state_read(&j->layout,&e->state) || e->state.window || e->state.generation!=e->admission.generation)return fail(ERROR_BUSY);
 return setup_installed_source_verify(e->source) && l4_worker_recheck(j,&e->admission);
}
static bool preflight(L4Journal* j,const SetupOperationPlan* op,const L4WorkerAdmission* a,const L4RecoveryHelper* helper){Executor e;bool ok=initialize(&e,j,op,a,helper);DWORD error=GetLastError();cleanup(&e);return ok?true:fail(error?error:ERROR_NOT_READY);}
static bool checkpoint(Executor* e){L4WorkerAdmission a;if(!budget(e,1,true))return fail(ERROR_TIMEOUT);return l4_worker_recheck_active(e->journal,&e->state,&a) && setup_operation_binding(e->operation,e->journal,a.sequence) && setup_operation_verify_images(e->operation);}
static bool service(Executor* e,unsigned index){for(unsigned action=1;action<=3;action++){if(!checkpoint(e) || !setup_service_transaction_execute(e->transaction,index,(SetupServiceAction)action,&e->state,budget(e,60000,true)))return false;}return checkpoint(e);}
static bool probe(Executor* e,unsigned index,const L4BootstrapPlan* plan){DWORD remaining=budget(e,e->checks.service_ms[index],true);return remaining && e->checks.probe(plan,index,remaining,e->checks.context) && checkpoint(e);}
static bool fresh(Executor* e,const L4BootstrapPlan* plan){DWORD remaining=budget(e,60000,true);return remaining && e->checks.barrier(plan,remaining,e->checks.context) && checkpoint(e);}
static bool config(Executor* e,unsigned index){unsigned count=0;const ULONGLONG* refs=setup_operation_configs(e->operation,&count);if(!refs || count!=12 || index>=count)return fail(ERROR_INVALID_DATA);return checkpoint(e) && l4_config_apply(e->journal,refs[index]) && checkpoint(e);}
static DWORD phase_left(Executor* e,ULONGLONG end,DWORD maximum,bool forward){ULONGLONG now=GetTickCount64();DWORD ms=budget(e,maximum,forward);if(now>=end)return 0;return end-now<ms?(DWORD)(end-now):ms;}
static bool pair_service(Executor* e,unsigned index){if(index!=1)return service(e,index);if(!checkpoint(e) || !setup_service_transaction_execute(e->transaction,1,SETUP_SERVICE_ACTION_STOP,&e->state,budget(e,60000,true)))return false;if(!config(e,1) || !config(e,2))return false;if(!checkpoint(e) || !setup_service_transaction_execute(e->transaction,1,SETUP_SERVICE_ACTION_SWITCH,&e->state,budget(e,60000,true)))return false;
 ULONGLONG end=GetTickCount64()+300000;DWORD ms=phase_left(e,end,300000,true);if(!ms || !setup_service_transaction_execute(e->transaction,1,SETUP_SERVICE_ACTION_START,&e->state,ms))return false;ms=phase_left(e,end,300000,true);return ms && e->checks.probe(&e->mixed,1,ms,e->checks.context) && phase_left(e,end,1,true) && checkpoint(e);
}
static bool link_done(Executor* e){const wchar_t* id=wcsrchr(e->journal->directory,L'\\');L4CommunicationDecision* guard=NULL;DWORD remaining=budget(e,60000,true);if(!id || !remaining || !l4_communication_decision_open(&e->journal->layout,id+1,remaining,&guard))return false;L4CommunicationPhase phase;DWORD code=0;bool ok=l4_communication_decision_read(guard,&phase,&code) && !code && (phase==L4_COMM_DEC_WAIT || phase==L4_COMM_DEC_COMMITTED) && l4_communication_decision_finish(guard,L4_COMM_DEC_COMMITTED,0,utc());DWORD error=GetLastError();l4_communication_decision_close(guard);return ok?true:fail(error?error:ERROR_RECOVERY_FAILURE);}
static bool rollback(Executor* e,DWORD error,L4RemoteOutcome* outcome){
 if(!e->transaction)return fail(ERROR_RECOVERY_FAILURE);DWORD pending=0;if(!setup_service_transaction_pending(e->transaction,&pending))return false;DWORD ms=budget(e,60000,false);if(!ms)return fail(ERROR_TIMEOUT);
 if(pending==100 && !setup_service_transaction_abandon(e->transaction,&e->state,ms))return false;if(pending==21 && !setup_service_transaction_abandon_config(e->transaction,&e->state,ms))return false;if(pending==104)return fail(ERROR_RECOVERY_FAILURE);
 ULONGLONG end=GetTickCount64()+SETUP_REMOTE_RESTORE_RESERVE_MS;
 for(unsigned i=0;i<4;i++){SetupRemoteServiceObservation observed;if(!setup_remote_service_observe(e->services[i],&observed))return false;ms=phase_left(e,end,60000,false);if(!ms)return fail(ERROR_TIMEOUT);
  bool unchanged=observed.phase==SETUP_SERVICE_RUNNING_OLD;
  if(unchanged){if(!setup_service_transaction_restore(e->transaction,i,SETUP_SERVICE_RESTORE_VERIFY,&e->state,ms))return false;}
  else{if(!setup_service_transaction_restore(e->transaction,i,SETUP_SERVICE_RESTORE_STOP,&e->state,ms))return false;
  unsigned count=0;const ULONGLONG* refs=setup_operation_configs(e->operation,&count);if(!refs || count!=12)return fail(ERROR_INVALID_DATA);if(i==3 && !l4_config_rollback(e->journal,refs[0]))return false;if(i==1 && (!l4_config_rollback(e->journal,refs[1]) || !l4_config_rollback(e->journal,refs[2])))return false;
  ms=phase_left(e,end,60000,false);if(!ms || !setup_service_transaction_restore(e->transaction,i,SETUP_SERVICE_RESTORE_SWITCH,&e->state,ms))return false;
  ULONGLONG startup=GetTickCount64()+(i==1?300000u:60000u);if(startup>end)startup=end;ms=phase_left(e,startup,i==1?300000u:60000u,false);if(!ms || !setup_service_transaction_restore(e->transaction,i,SETUP_SERVICE_RESTORE_START,&e->state,ms))return false;
  if(i==1){ms=phase_left(e,startup,300000,false);if(!ms || !e->checks.probe(&e->old,1,ms,e->checks.context) || !phase_left(e,startup,1,false))return false;}
  }
  if(i==0){ms=phase_left(e,end,60000,false);if(!ms || !e->checks.probe(&e->old,0,ms,e->checks.context))return false;}
  if(i==1 && unchanged){ms=phase_left(e,end,60000,false);if(!ms || !e->checks.probe(&e->old,1,ms,e->checks.context))return false;}
 }
 unsigned count=0;const ULONGLONG* refs=setup_operation_configs(e->operation,&count);if(!refs || count!=12)return fail(ERROR_INVALID_DATA);for(unsigned i=0;i<count;i++){if(GetTickCount64()>=end || !budget(e,1,false))return fail(ERROR_TIMEOUT);if(!l4_config_verify(e->journal,refs[i],false) && !l4_config_rollback(e->journal,refs[i]))return false;}
 ULONGLONG tick=GetTickCount64();ms=budget(e,600000,false);if(tick>=end || !ms)return fail(ERROR_TIMEOUT);if(end-tick<ms)ms=(DWORD)(end-tick);return setup_remote_restore_finish(e->restore,e->journal,e->operation,e->transaction,e->services,error,ms,outcome);
}
static bool terminal_scan(DWORD kind,ULONGLONG sequence,const void* bytes,DWORD size,void* context){(void)sequence;(void)bytes;(void)size;if(kind==102 || kind==103 || kind==108)*(bool*)context=true;return true;}
#define REQUIRE(x) do{SetLastError(0);if(!(x)){error=GetLastError()?GetLastError():ERROR_NOT_READY;failure_line=__LINE__;goto done;}}while(0)
static DWORD execute(L4Journal** owner,const SetupOperationPlan* op,const L4WorkerAdmission* admitted,const L4RecoveryHelper* helper){
 if(!owner || !*owner)return ERROR_INVALID_PARAMETER;Executor e;DWORD error=ERROR_NOT_READY,failure_line=0,recovery_error=0;L4RemoteOutcome outcome={0};bool active=false,finishing=false;REQUIRE(initialize(&e,*owner,op,admitted,helper));
 REQUIRE(setup_remote_completion_prepare(e.journal,e.source,op,&e.completion));REQUIRE(setup_remote_restore_prepare(e.journal,e.source,op,&e.restore));
 WORD ports[2];REQUIRE(BCryptGenRandom(NULL,(PUCHAR)ports,sizeof(ports),BCRYPT_USE_SYSTEM_PREFERRED_RNG)==0);ports[0]=(WORD)(49152+(ports[0]%16384));ports[1]=(WORD)(49152+(ports[1]%16384));if(ports[0]==ports[1])ports[1]=ports[0]==65535?49152:ports[0]+1;REQUIRE(setup_update_candidate_probe(e.journal,admitted->sequence,0,ports[0],ports[1],180000));
 REQUIRE(setup_update_worker_capture_stop(e.journal,admitted->sequence,&e.actors,&e.checks,300000,&e.stop));REQUIRE(setup_update_watch_ready(e.journal,e.stop,e.policy.communication_deadline_utc,60000));
 if(utc()+(ULONGLONG)(900000+SETUP_REMOTE_RESTORE_RESERVE_MS)*10000>=e.policy.communication_deadline_utc){error=ERROR_TIMEOUT;goto done;}
 REQUIRE(l4_update_state_publish(e.journal,admitted->sequence,e.state.generation,1,e.policy.communication_deadline_utc));active=true;REQUIRE(l4_update_state_read(&e.journal->layout,&e.state));e.forward_end=GetTickCount64()+900000;
 REQUIRE(setup_update_confirm_stop(e.journal,e.stop,&e.actors,&e.checks,budget(&e,300000,true)));
 for(unsigned i=0;i<4;i++)REQUIRE(setup_remote_service_open(e.journal,setup_operation_switch_reference(op,0,i),setup_operation_switch(op,0,i),&e.state,budget(&e,60000,true),&e.services[i]));REQUIRE(setup_service_transaction_open(e.journal,op,e.services,&e.transaction));
 REQUIRE(pair_service(&e,0));REQUIRE(probe(&e,0,&e.mixed));REQUIRE(pair_service(&e,1));REQUIRE(fresh(&e,&e.mixed));REQUIRE(link_done(&e));
 REQUIRE(l4_update_state_publish(e.journal,admitted->sequence,e.state.generation,2,e.policy.supervisor_deadline_utc-(ULONGLONG)e.policy.overhead_ms*10000));REQUIRE(l4_update_state_read(&e.journal->layout,&e.state));e.forward_end=GetTickCount64()+1800000;
 for(unsigned i=0;i<4;i++)REQUIRE(setup_remote_service_rebind_state(e.services[i],&e.state,admitted->deadline_utc));REQUIRE(service(&e,2));REQUIRE(probe(&e,2,&e.target));
 REQUIRE(checkpoint(&e));REQUIRE(setup_service_transaction_execute(e.transaction,3,SETUP_SERVICE_ACTION_STOP,&e.state,budget(&e,60000,true)));REQUIRE(config(&e,0));for(unsigned a=2;a<=3;a++)REQUIRE(setup_service_transaction_execute(e.transaction,3,(SetupServiceAction)a,&e.state,budget(&e,60000,true)));REQUIRE(probe(&e,3,&e.target));for(unsigned i=3;i<12;i++)REQUIRE(config(&e,i));
 REQUIRE(checkpoint(&e));bool force_rollback=false;REQUIRE(setup_acceptance_forcepoint(e.journal,op,&force_rollback));if(force_rollback){error=ERROR_CANCELLED;goto done;}
 finishing=true;REQUIRE(setup_remote_completion_finish(e.completion,e.journal,op,e.transaction,e.services,budget(&e,600000,true),&outcome));error=0;
done:
 if(error && active){DWORD original=error;
  if(finishing){DWORD ms=budget(&e,600000,true);if(ms && setup_remote_completion_finish(e.completion,e.journal,op,e.transaction,e.services,ms,&outcome)){cleanup(&e);return 0;}}
  bool terminal=false;if(!l4_journal_replay(e.journal,terminal_scan,&terminal) || terminal){cleanup(&e);return original;}
  if(finishing && setup_remote_completion_settling(e.completion)){
   L4RemoteOutcome required={0};required.result.result=L4_REMOTE_OUTCOME_RECOVERY_REQUIRED;required.result.error=original;
   setup_remote_outcome_append_bound(e.journal,op,&required,0,NULL,&outcome);cleanup(&e);return original;
  }
  if(rollback(&e,original,&outcome))error=original;else{recovery_error=GetLastError()?GetLastError():ERROR_RECOVERY_FAILURE;L4RemoteOutcome required={0};required.result.result=L4_REMOTE_OUTCOME_RECOVERY_REQUIRED;required.result.error=original;setup_remote_outcome_append_bound(e.journal,op,&required,0,NULL,&outcome);error=original;}}
 if(error)failure_diagnostic(e.journal,failure_line,error,recovery_error);
 cleanup(&e);return error;
}
const SetupWorkerEngine* setup_remote_executor_engine(void){static const SetupWorkerEngine engine={preflight,execute};return &engine;}
