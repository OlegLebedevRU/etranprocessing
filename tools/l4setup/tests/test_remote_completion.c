/* Final-stage fault composition. Trust/source admission and SCM/IPC responses
 * are modeled; production code, codec and canonical Windows epoch/clock are
 * exercised. This fixture does not prove live barrier/recovery acceptance. */
#include "../src/remote_completion.h"
#include "../../l4common/journal_internal.h"
#include "../../l4common/recovery_store.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned assertions,failures,appends,probes,barriers,local_calls;
static unsigned fault;static L4UpdateState marker;static BYTE recorded_bytes[SETUP_REMOTE_COMMIT_BYTES];
static L4CommunicationPlan modeled_link;static L4RecoveryPlan modeled_recovery;static bool recovery_committed;
static bool outcome_recorded;static unsigned clears;
static bool budget_case;static ULONGLONG spent_ms;
static ULONGLONG fixture_ticks(void){return GetTickCount64()+spent_ms;}
static VOID WINAPI fixture_utc(LPFILETIME result){GetSystemTimeAsFileTime(result);ULONGLONG n=(((ULONGLONG)result->dwHighDateTime<<32)|result->dwLowDateTime)+spent_ms*10000;result->dwLowDateTime=(DWORD)n;result->dwHighDateTime=(DWORD)(n>>32);}
#define CHECK(x) do{++assertions;if(!(x)){++failures;printf("FAIL completion %u: %s error=%lu\n",__LINE__,#x,GetLastError());}}while(0)
static BOOL WINAPI no_thread_token(HANDLE a,DWORD b,BOOL c,PHANDLE d){(void)a;(void)b;(void)c;(void)d;SetLastError(ERROR_NO_TOKEN);return FALSE;}
static BOOL WINAPI system_token(HANDLE a,DWORD b,PHANDLE c){(void)a;(void)b;return DuplicateHandle(GetCurrentProcess(),GetCurrentProcess(),GetCurrentProcess(),c,0,FALSE,DUPLICATE_SAME_ACCESS);}
static BOOL WINAPI token_info(HANDLE h,TOKEN_INFORMATION_CLASS kind,LPVOID out,DWORD size,PDWORD used){
    (void)h;*used=0;if(fault==1){SetLastError(ERROR_ACCESS_DENIED);return FALSE;}
    if(kind==TokenUser){TOKEN_USER* user=out;DWORD n=SECURITY_MAX_SID_SIZE;CHECK(size>=sizeof(TOKEN_USER)+SECURITY_MAX_SID_SIZE);user->User.Sid=(BYTE*)out+sizeof(TOKEN_USER);return CreateWellKnownSid(WinLocalSystemSid,NULL,user->User.Sid,&n);}
    if(kind==TokenType){*(TOKEN_TYPE*)out=TokenPrimary;return TRUE;}if(kind==TokenSessionId){*(DWORD*)out=0;return TRUE;}return FALSE;
}
#define OpenThreadToken no_thread_token
#define OpenProcessToken system_token
#define GetTokenInformation token_info
static bool fixture_state(const L4Layout* l,L4UpdateState* out){(void)l;if(fault==2){SetLastError(ERROR_INVALID_DATA);return false;}*out=marker;return true;}
static bool binding(const SetupOperationPlan* p,L4Journal* j,ULONGLONG seq){(void)p;(void)j;CHECK(seq==64);return fault!=3;}
static bool images(const SetupOperationPlan* p){(void)p;return fault!=4;}
static bool updater(SetupUpdaterIdentity* p){(void)p;return fault!=5;}
static bool config(L4Journal* j,ULONGLONG seq,bool candidate){(void)j;CHECK(seq>=10&&seq<22&&candidate);return fault!=6;}
static bool access(const L4Layout* l,const L4AccessActors* a){(void)l;(void)a;return fault!=7;}
static bool complete(SetupServiceTransaction* tx,L4Journal* j,const SetupOperationPlan* op,SetupRemoteService* sv[4],const L4UpdateState* s,DWORD ms){
    (void)tx;(void)j;(void)op;(void)sv;CHECK(s->window==2&&ms>0);++local_calls;return fault!=8;
}
static bool running(SetupRemoteService* s,DWORD* pid,FILETIME* birth){*pid=(DWORD)(ULONG_PTR)s;birth->dwLowDateTime=*pid;birth->dwHighDateTime=1;if(fault==9)return false;if((fault==10&&probes)||(fault==25&&outcome_recorded))*pid+=100;return true;}
static bool signal(const wchar_t* cmd,WORD* http,WORD* mqtt,char expected[64]){CHECK(cmd&&*cmd);*http=18443;*mqtt=fault==11?18884:18883;expected[0]=0;return true;}
static bool probe(const L4BootstrapPlan* p,unsigned service,DWORD ms,void* c){(void)p;(void)c;CHECK(service<4&&ms>0);++probes;if(fault==13&&service==2)marker.generation++;if(fault==14)Sleep(30);if(budget_case){CHECK(ms==(service==1?300000u:60000u));spent_ms+=ms;}return fault!=12;}
static bool barrier(const L4BootstrapPlan* p,DWORD ms,void* c){(void)p;(void)c;CHECK(ms>0);++barriers;if(fault==16)marker.window=0;if(budget_case){CHECK(ms==60000);spent_ms+=ms;}return fault!=15;}
static bool readiness(L4Readiness* options,const DWORD ms[4],DWORD b,L4BootstrapChecks* out){CHECK(options->proxy_port==18443&&options->broker_port==1883&&ms[1]==300000&&b==60000);memset(out,0,sizeof(*out));out->probe=probe;out->barrier=barrier;memcpy(out->service_ms,ms,4*sizeof(DWORD));out->barrier_ms=b;return true;}
static bool append(L4Journal* j,DWORD kind,const void* bytes,DWORD size,ULONGLONG* seq){(void)j;CHECK(kind==102&&size==sizeof(recorded_bytes)&&recovery_committed);++appends;if(fault==17){SetLastError(ERROR_WRITE_FAULT);return false;}memcpy(recorded_bytes,bytes,size);*seq=90;return true;}
static bool find_record(L4Journal* j,DWORD kind,ULONGLONG seq,BYTE** bytes,DWORD* size){(void)j;CHECK(kind==102&&seq==90);*bytes=malloc(sizeof(recorded_bytes));*size=sizeof(recorded_bytes);if(!*bytes)return false;memcpy(*bytes,recorded_bytes,sizeof(recorded_bytes));if(fault==18)(*bytes)[100]^=1;return true;}
#define l4_update_state_read fixture_state
#define setup_operation_binding binding
#define setup_operation_verify_images images
#define setup_updater_identity_verify updater
#define l4_config_verify config
#define l4_access_verify access
#define setup_service_transaction_complete complete
#define setup_remote_service_running running
#define l4_proxy_signal_source signal
#define setup_readiness_checks readiness
#define l4_journal_append append
#define GetTickCount64 fixture_ticks
#define GetSystemTimeAsFileTime fixture_utc
#define l4_store_find_record find_record
#include "../src/remote_completion.c"
/* This fixture constructs only final-stage internal contexts. Every unused
 * admission adapter fails loudly if the test accidentally calls prepare(). */
#define UNUSED_ADMISSION() CHECK(false)
bool setup_installed_source_verify(SetupInstalledSource* s){(void)s;UNUSED_ADMISSION();return false;}
bool setup_installed_source_owned_by(const SetupInstalledSource* s,const L4Journal* j){(void)s;(void)j;UNUSED_ADMISSION();return false;}
const L4BootstrapPlan* setup_installed_source_plan(const SetupInstalledSource* s){(void)s;UNUSED_ADMISSION();return NULL;}
const SetupRootManifest* setup_installed_source_root(const SetupInstalledSource* s){(void)s;UNUSED_ADMISSION();return NULL;}
const wchar_t* setup_installed_source_operation(const SetupInstalledSource* s){(void)s;UNUSED_ADMISSION();return NULL;}
const L4AccessActors* setup_installed_source_actors(const SetupInstalledSource* s){(void)s;UNUSED_ADMISSION();return NULL;}
const L4Layout* setup_manifest_layout(const SetupManifest* s){(void)s;UNUSED_ADMISSION();return NULL;}
const BYTE* setup_root_identity(const SetupRootManifest* s){(void)s;UNUSED_ADMISSION();return NULL;}
bool setup_updater_identity_from_source(SetupInstalledSource* s,SetupUpdaterIdentity** out){(void)s;(void)out;UNUSED_ADMISSION();return false;}
void setup_updater_identity_free(SetupUpdaterIdentity* s){(void)s;UNUSED_ADMISSION();}
const char* setup_updater_identity_version(const SetupUpdaterIdentity* s){(void)s;UNUSED_ADMISSION();return NULL;}
const BYTE* setup_updater_identity_root(const SetupUpdaterIdentity* s){(void)s;UNUSED_ADMISSION();return NULL;}
const BYTE* setup_updater_identity_publisher(const SetupUpdaterIdentity* s){(void)s;UNUSED_ADMISSION();return NULL;}
const SetupRootAsset* setup_updater_identity_asset(const SetupUpdaterIdentity* s){(void)s;UNUSED_ADMISSION();return NULL;}
const wchar_t* setup_updater_identity_origin(const SetupUpdaterIdentity* s){(void)s;UNUSED_ADMISSION();return NULL;}
const L4CatalogRoute* l4_route_steps(const L4RoutePlan* s){(void)s;UNUSED_ADMISSION();return NULL;}
const char* l4_route_arch(const L4RoutePlan* s){(void)s;UNUSED_ADMISSION();return NULL;}
const SetupRootManifest* setup_operation_root(const SetupOperationPlan* s){(void)s;UNUSED_ADMISSION();return NULL;}
const L4RoutePlan* setup_operation_route(const SetupOperationPlan* s){(void)s;UNUSED_ADMISSION();return NULL;}
const SetupManifest* setup_operation_target(const SetupOperationPlan* s,unsigned h){(void)s;(void)h;UNUSED_ADMISSION();return NULL;}
const SetupRootManifest* setup_operation_target_root(const SetupOperationPlan* s,unsigned h){(void)s;(void)h;UNUSED_ADMISSION();return NULL;}
const ULONGLONG* setup_operation_configs(const SetupOperationPlan* s,unsigned* n){(void)s;(void)n;UNUSED_ADMISSION();return NULL;}
unsigned setup_operation_hops(const SetupOperationPlan* s){(void)s;UNUSED_ADMISSION();return 0;}
const L4ServiceSwitch* setup_operation_switch(const SetupOperationPlan* s,unsigned h,unsigned i){(void)s;(void)h;(void)i;UNUSED_ADMISSION();return NULL;}
bool l4_worker_recheck(L4Journal* j,L4WorkerAdmission* a){(void)j;(void)a;UNUSED_ADMISSION();return false;}
bool setup_manifest_verify(const SetupManifest* s){(void)s;UNUSED_ADMISSION();return false;}
bool setup_update_load_operation_snapshot(const L4JournalReader* r,const L4Layout* l,ULONGLONG seq,SetupOperationPlan** out){(void)r;(void)l;(void)seq;(void)out;UNUSED_ADMISSION();return false;}
void setup_operation_free(SetupOperationPlan* p){(void)p;UNUSED_ADMISSION();}
ULONGLONG setup_operation_switch_reference(const SetupOperationPlan* p,unsigned h,unsigned s){(void)p;(void)h;(void)s;UNUSED_ADMISSION();return 0;}
bool l4_recovery_decode(const L4Layout* l,const BYTE* b,DWORD n,L4RecoveryPlan* p){(void)l;(void)b;(void)n;(void)p;UNUSED_ADMISSION();return false;}
bool l4_recovery_hash(const void* b,DWORD n,BYTE hash[32]){(void)b;(void)n;(void)hash;UNUSED_ADMISSION();return false;}
bool l4_recovery_result_decode(const L4RecoveryPlan* p,const BYTE digest[32],const BYTE* b,DWORD n,L4RecoveryStatus* s,DWORD* e){(void)p;(void)digest;(void)b;(void)n;(void)s;(void)e;UNUSED_ADMISSION();return false;}
bool l4_communication_plan_decode(const L4Layout* l,const BYTE* b,DWORD n,L4CommunicationPlan* p){(void)l;(void)b;(void)n;(void)p;UNUSED_ADMISSION();return false;}
bool l4_communication_plan_open(const L4Layout* l,const wchar_t* id,L4CommunicationPin** out){(void)l;CHECK(id&&wcslen(id)==36);*out=(L4CommunicationPin*)1;return true;}
const L4CommunicationPlan* l4_communication_pinned_plan(const L4CommunicationPin* pin){CHECK(pin);return &modeled_link;}
void l4_communication_plan_close(L4CommunicationPin* pin){(void)pin;}
bool l4_communication_decision_open(const L4Layout* l,const wchar_t* id,DWORD ms,L4CommunicationDecision** out){(void)l;(void)id;CHECK(ms>0);*out=(L4CommunicationDecision*)2;return true;}
bool l4_communication_decision_read(L4CommunicationDecision* d,L4CommunicationPhase* phase,DWORD* error){CHECK(d);*phase=fault==20?L4_COMM_DEC_STARTED:L4_COMM_DEC_COMMITTED;*error=0;return true;}
void l4_communication_decision_close(L4CommunicationDecision* d){(void)d;}
bool l4_recovery_open(const L4Layout* l,const wchar_t* id,DWORD ms,L4RecoveryGuard** out){(void)l;(void)id;CHECK(ms>0);if(fault==19)return false;*out=(L4RecoveryGuard*)3;return true;}
const L4RecoveryPlan* l4_recovery_plan(const L4RecoveryGuard* guard){CHECK(guard);if(fault==21)modeled_recovery.worker_pid++;return &modeled_recovery;}
bool l4_recovery_finish(L4RecoveryGuard* guard,L4RecoveryStatus status,DWORD error,ULONGLONG now){CHECK(guard&&status==L4_RECOVERY_COMMITTED&&!error&&now);if(fault==22){SetLastError(ERROR_INVALID_STATE);return false;}recovery_committed=true;return true;}
void l4_recovery_close(L4RecoveryGuard* guard){(void)guard;}
bool setup_remote_outcome_append_bound(L4Journal* j,const SetupOperationPlan* op,const L4RemoteOutcome* expected,ULONGLONG reference,const BYTE digest[32],L4RemoteOutcome* out){
    CHECK(j && op && reference==90 && expected->result.result==L4_REMOTE_OUTCOME_SUCCESS && !expected->result.error && recovery_committed);
    BYTE actual[32];CHECK(l4_store_hash(recorded_bytes,sizeof(recorded_bytes),NULL,0,actual) && !memcmp(actual,digest,32));
    if(fault==23){SetLastError(ERROR_WRITE_FAULT);return false;}outcome_recorded=true;*out=*expected;out->plan_sequence=64;out->proof_sequence=reference;memcpy(out->proof_sha256,digest,32);return true;
}
bool l4_update_state_publish(L4Journal* j,ULONGLONG sequence,ULONGLONG generation,DWORD window,ULONGLONG deadline){
    CHECK(j && sequence==64 && generation==marker.generation && !window && !deadline && outcome_recorded && recovery_committed);++clears;
    if(fault==24){SetLastError(ERROR_ACCESS_DENIED);return false;}marker.window=0;marker.deadline_utc=0;marker.generation++;return true;
}
static void reset(SetupRemoteCompletion* p,L4Journal* j,SetupOperationPlan* op){
    spent_ms=0;budget_case=false;memset(p,0,sizeof(*p));memset(&marker,0,sizeof(marker));probes=barriers=appends=local_calls=clears=0;fault=0;recovery_committed=outcome_recorded=false;
    strcpy_s(marker.owner,40,"17730000-0000-4000-8000-000000000001");marker.window=2;marker.generation=2;marker.plan_sequence=64;marker.deadline_utc=utc()+600000000ull;
    p->original=j;p->operation=op;p->worker_pid=GetCurrentProcessId();FILETIME e,k,u;CHECK(GetProcessTimes(GetCurrentProcess(),&p->worker_birth,&e,&k,&u));
    p->value.operation_sequence=64;strcpy_s(p->value.operation,37,marker.owner);strcpy_s(p->value.predecessor,37,"17730000-0000-4000-8000-000000000002");strcpy_s(p->value.updater_origin,37,p->value.predecessor);
    strcpy_s(p->value.suite_version,32,"1.13.8");strcpy_s(p->value.updater_version,32,"1.13.7");strcpy_s(p->value.arch,8,"x86");
    p->value.suite_root[0]=1;p->value.updater_root[0]=2;p->value.publisher[0]=3;p->value.operation_sha256[0]=4;strcpy_s(p->value.updater.name,40,"l4setup.exe");p->value.updater.size=100;p->value.updater.sha256[0]=5;
    for(unsigned i=0;i<12;i++)p->value.configs[i]=10+i;
    for(unsigned i=0;i<4;i++){p->value.start_types[i]=SERVICE_AUTO_START;wcscpy_s(p->value.commands[i],2048,L"\"C:\\Program Files\\Leo4\\Tools\\releases\\1.13.8\\fixture.exe\"");wcscpy_s(p->target.commands[i],2048,p->value.commands[i]);}
    memset(&modeled_link,0,sizeof(modeled_link));memset(&modeled_recovery,0,sizeof(modeled_recovery));
    modeled_link.sequence=modeled_recovery.sequence=64;memcpy(&modeled_link.operation,j->header+8,16);memcpy(&modeled_recovery.operation,j->header+8,16);
    modeled_link.worker_pid=modeled_recovery.worker_pid=p->worker_pid;modeled_link.worker_created=modeled_recovery.worker_created=p->worker_birth;
    memcpy(modeled_link.operation_sha256,p->value.operation_sha256,32);p->admission.deadline_utc=modeled_recovery.deadline_utc=marker.deadline_utc+600000000ull;
}
int main(void){
    SetupRemoteCompletion p;L4Journal j={0};SetupOperationPlan* op=(SetupOperationPlan*)1;SetupServiceTransaction* tx=(SetupServiceTransaction*)2;
    SetupRemoteService* services[4]={(SetupRemoteService*)100,(SetupRemoteService*)101,(SetupRemoteService*)102,(SetupRemoteService*)103};SetupRemoteCommit result;
    for(unsigned f=1;f<=22;f++){
        if(f==18)continue;
        reset(&p,&j,op);fault=f;CHECK(!setup_remote_completion_commit(&p,&j,op,tx,services,f==14?10:1000,&result));CHECK(!result.finished_utc);CHECK(!p.committed_sequence);CHECK(appends==(f==17?1u:0u));
        CHECK(setup_remote_completion_settling(&p)==(f==17 || f==22));
    }
    reset(&p,&j,op);CHECK(setup_remote_completion_commit(&p,&j,op,tx,services,1000,&result));CHECK(appends==1&&probes==4&&barriers==1&&local_calls==6);CHECK(result.finished_utc&&result.pids[0]==100);CHECK(strcmp(result.suite_version,result.updater_version));
    unsigned prior=barriers;CHECK(setup_remote_completion_commit(&p,&j,op,tx,services,1000,&result));CHECK(appends==1&&barriers==prior+1);
    fault=18;CHECK(!setup_remote_completion_commit(&p,&j,op,tx,services,1000,&result));CHECK(GetLastError()==ERROR_CRC&&appends==1);
    reset(&p,&j,op);p.worker_birth.dwLowDateTime^=1;CHECK(!setup_remote_completion_commit(&p,&j,op,tx,services,1000,&result));CHECK(!appends);
    reset(&p,&j,op);marker.deadline_utc=1;CHECK(!setup_remote_completion_commit(&p,&j,op,tx,services,1000,&result));CHECK(GetLastError()==ERROR_TIMEOUT&&!appends);
    reset(&p,&j,op);marker.generation++;CHECK(!setup_remote_completion_commit(&p,&j,op,tx,services,1000,&result));CHECK(!appends);
    reset(&p,&j,op);CHECK(!setup_remote_completion_commit(&p,&j,(SetupOperationPlan*)3,tx,services,1000,&result));CHECK(!appends);
    L4RemoteOutcome outcome;
    reset(&p,&j,op);CHECK(setup_remote_completion_finish(&p,&j,op,tx,services,1000,&outcome));
    CHECK(setup_remote_completion_settling(&p));CHECK(!setup_remote_completion_settling(NULL));
    CHECK(outcome.result.result==L4_REMOTE_OUTCOME_SUCCESS && outcome.proof_sequence==90 && outcome_recorded && clears==1 && !marker.window && marker.generation==3);
    for(unsigned f=23;f<=25;f++){
        reset(&p,&j,op);fault=f;CHECK(!setup_remote_completion_finish(&p,&j,op,tx,services,1000,&outcome));
        CHECK(!outcome.proof_sequence && marker.window==2 && recovery_committed);
        CHECK(setup_remote_completion_settling(&p));
        CHECK(clears==(f==24?1u:0u));CHECK(outcome_recorded==(f!=23));
    }
    reset(&p,&j,op);budget_case=true;marker.deadline_utc=utc()+12000000000ull;
    p.admission.deadline_utc=modeled_recovery.deadline_utc=marker.deadline_utc+600000000ull;
    CHECK(setup_remote_completion_finish(&p,&j,op,tx,services,600000,&outcome));
    CHECK(spent_ms==540000 && probes==4 && barriers==1 && clears==1 && !marker.window);
    printf("Final completion: %u passed, %u failed, %u total (admission/SCM/IPC modeled)\n",assertions-failures,failures,assertions);return failures?1:0;
}
