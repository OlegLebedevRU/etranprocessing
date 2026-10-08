/* Controller composition only. No installed source, services, task, helper,
 * journal or MQTT changes; owned preparation/Job behavior modeled. */
#include "../src/remote_controller.c"
#include <stdio.h>
static unsigned passed,failed,source_reads,prepares,configs_seen,plans,starts,waits,finishes,closes,freed;
static unsigned fault,route_count=1;static bool enabled=true,transferred;static DWORD child_code;
static L4Journal journal;static L4BootstrapPlan original;static L4CatalogRoute route;static SetupWorkerEngine worker;
#define CHECK(x) do{if(x)passed++;else{failed++;printf("FAIL controller %u %s error=%lu\n",__LINE__,#x,GetLastError());}}while(0)
static bool worker_preflight(L4Journal* j,const SetupOperationPlan* p,const L4WorkerAdmission* a,const L4RecoveryHelper* h){(void)j;(void)p;(void)a;(void)h;return true;}
static DWORD worker_execute(L4Journal** j,const SetupOperationPlan* p,const L4WorkerAdmission* a,const L4RecoveryHelper* h){(void)j;(void)p;(void)a;(void)h;return ERROR_CALL_NOT_IMPLEMENTED;}
const SetupWorkerEngine* setup_worker_compiled_engine(void){return enabled?&worker:NULL;}
static bool owner(L4Journal* j,SetupInstalledSource* s){CHECK(j==&journal&&s==(SetupInstalledSource*)1&&!transferred);return fault==1?fail(ERROR_ACCESS_DENIED):true;}
bool setup_installed_source_owned_by(const SetupInstalledSource* s,const L4Journal* j){return owner((L4Journal*)j,(SetupInstalledSource*)s);}
bool setup_installed_source_verify(SetupInstalledSource* s){CHECK(s==(SetupInstalledSource*)1&&!transferred);source_reads++;return fault==2?fail(ERROR_CRC):true;}
const char* setup_installed_source_arch(const SetupInstalledSource* s){CHECK(s==(SetupInstalledSource*)1&&!transferred);source_reads++;return "x86";}
const L4BootstrapPlan* setup_installed_source_plan(const SetupInstalledSource* s){CHECK(s==(SetupInstalledSource*)1&&!transferred);source_reads++;return &original;}
bool l4_platform_current(char output[L4_PLATFORM_PROFILE_SIZE]){strcpy_s(output,L4_PLATFORM_PROFILE_SIZE,"windows-x86");return fault==3?fail(ERROR_NOT_SUPPORTED):true;}
bool l4_update_state_read(const L4Layout* roots,L4UpdateState* state){CHECK(roots==&journal.layout);memset(state,0,sizeof(*state));state->window=fault==4?1:0;return true;}
bool setup_recovery_receipt_load(L4Journal* j,const char* arch,SetupRecoveryReceipt** out){CHECK(j==&journal&&!strcmp(arch,"x86"));*out=NULL;if(fault==5)return fail(ERROR_CRC);*out=(SetupRecoveryReceipt*)1;return true;}
void setup_recovery_receipt_free(SetupRecoveryReceipt* receipt){CHECK(!receipt||receipt==(SetupRecoveryReceipt*)1);}
bool setup_remote_prepare(L4Journal* j,SetupInstalledSource* s,DWORD budget,const volatile LONG* cancel,SetupRemotePreparation** out){CHECK(owner(j,s)&&budget==SETUP_REMOTE_PREPARE_MS&&cancel);prepares++;*out=NULL;if(fault==6||fault==18)return fail(ERROR_TIMEOUT);*out=(SetupRemotePreparation*)1;return true;}
void setup_remote_preparation_free(SetupRemotePreparation* p){CHECK(!p||p==(SetupRemotePreparation*)1);freed++;}
const SetupPreparedPlan* setup_remote_preparation_packages(const SetupRemotePreparation* p){CHECK(p==(SetupRemotePreparation*)1);return (SetupPreparedPlan*)1;}
const L4RoutePlan* setup_prepared_route(const SetupPreparedPlan* p){CHECK(p==(SetupPreparedPlan*)1);return (L4RoutePlan*)1;}
const L4CatalogRoute* l4_route_steps(const L4RoutePlan* p){CHECK(p==(L4RoutePlan*)1);route.count=route_count;return &route;}
const SetupManifest* setup_prepared_manifest(const SetupPreparedPlan* p,unsigned hop){CHECK(p==(SetupPreparedPlan*)1&&hop==0);return (SetupManifest*)1;}
bool setup_remote_config_prepare(L4Journal* j,SetupInstalledSource* s,const SetupManifest* target,SetupRemoteConfigProposals* c){CHECK(owner(j,s)&&target==(SetupManifest*)1);configs_seen++;if(fault==7)return fail(ERROR_INVALID_DATA);c->supervisor=1;c->broker=2;c->acl=3;for(unsigned i=0;i<9;i++)c->launchers[i]=4+i;return true;}
bool setup_remote_worker_plan_prepare(L4Journal* j,SetupInstalledSource* s,const SetupRemotePreparation* p,const SetupRemoteConfigProposals* c,DWORD budget,const volatile LONG* cancel,SetupRemoteWorkerPlan** out){CHECK(owner(j,s)&&p==(SetupRemotePreparation*)1&&budget==SETUP_REMOTE_PREPARE_MS&&cancel);CHECK(c->supervisor==1&&c->broker==2&&c->acl==3);for(unsigned i=0;i<9;i++)CHECK(c->launchers[i]==4+i);plans++;*out=NULL;if(fault==8)return fail(ERROR_CRC);*out=(SetupRemoteWorkerPlan*)1;return true;}
void setup_remote_worker_plan_free(SetupRemoteWorkerPlan* p){CHECK(!p||p==(SetupRemoteWorkerPlan*)1);freed++;}
bool l4_proxy_signal_source(const wchar_t* command,WORD* http,WORD* mqtt,char thumbprint[64]){CHECK(command==original.commands[0]);*http=12345;*mqtt=fault==9?9999:18883;memset(thumbprint,0,64);return true;}
bool setup_readiness_checks(L4Readiness* r,const DWORD budgets[4],DWORD barrier,L4BootstrapChecks* checks){CHECK(r->proxy_port==12345&&r->broker_port==1883&&budgets[0]==60000&&budgets[1]==300000&&budgets[2]==60000&&budgets[3]==60000&&barrier==60000);memset(checks,0,sizeof(*checks));return fault==10?fail(ERROR_TIMEOUT):true;}
bool setup_remote_policy_at(ULONGLONG now,SetupRemoteWorkerPolicy* p){CHECK(now>0);memset(p,0,sizeof(*p));return fault==11?fail(ERROR_TIMEOUT):true;}
bool setup_remote_worker_launch_prepare(L4Journal* j,SetupInstalledSource* s,const SetupRemoteWorkerPlan* p,const SetupRemoteWorkerPolicy* policy,const volatile LONG* cancel,SetupRemoteWorkerLaunch** out){CHECK(owner(j,s)&&p==(SetupRemoteWorkerPlan*)1&&policy&&cancel);*out=NULL;if(fault==12)return fail(ERROR_ACCESS_DENIED);*out=(SetupRemoteWorkerLaunch*)1;return true;}
bool setup_remote_worker_launch_start(L4Journal** j,SetupInstalledSource* s,SetupRemoteWorkerLaunch* launch,const L4BootstrapChecks* checks,const volatile LONG* cancel,SetupRemoteWorkerLaunchReport* report){CHECK(owner(*j,s)&&launch==(SetupRemoteWorkerLaunch*)1&&checks&&cancel);starts++;
 if(fault==13){report->startup.plan_published=true;return fail(ERROR_WRITE_FAULT);}if(fault==19){report->startup.task_attempted=true;return fail(ERROR_TIMEOUT);}if(fault==14){report->recovery_reported=true;return fail(ERROR_TIMEOUT);}
 report->startup.plan_published=true;report->startup.task_attempted=true;report->transferred=true;transferred=true;*j=NULL;return fault==15?fail(ERROR_TIMEOUT):true;}
bool setup_remote_worker_launch_wait(SetupRemoteWorkerLaunch* launch,DWORD timeout,DWORD* code){CHECK(launch==(SetupRemoteWorkerLaunch*)1&&transferred&&timeout==SETUP_REMOTE_CONTROLLER_WAIT_MS);waits++;*code=child_code;return fault==16?fail(ERROR_TIMEOUT):true;}
bool setup_remote_worker_launch_close(SetupRemoteWorkerLaunch** launch,DWORD timeout){CHECK(timeout==60000);if(*launch)CHECK(*launch==(SetupRemoteWorkerLaunch*)1);closes++;*launch=NULL;return fault==17?fail(ERROR_TIMEOUT):true;}
bool setup_remote_preparation_finish(L4Journal* j,DWORD error,L4RemoteResult* result){CHECK(j==&journal&&!transferred&&error);finishes++;memset(result,0,sizeof(*result));return fault==18?fail(ERROR_WRITE_FAULT):true;}
static void reset(void){fault=0;route_count=1;enabled=true;transferred=false;child_code=0;prepares=configs_seen=plans=starts=waits=finishes=closes=freed=source_reads=0;worker.preflight=worker_preflight;worker.execute=worker_execute;}
int main(void){const SetupRemoteEngine* controller=setup_remote_controller_engine();CHECK(controller&&controller->preflight&&controller->execute);L4RemoteRequest request={0};request.target=L4_REMOTE_SUITE;volatile LONG cancel=0;
 reset();enabled=false;CHECK(!controller->preflight(&journal,(SetupInstalledSource*)1,&request)&&GetLastError()==ERROR_CALL_NOT_IMPLEMENTED&&!prepares&&!starts&&!finishes);
 reset();worker.execute=NULL;CHECK(!controller->preflight(&journal,(SetupInstalledSource*)1,&request));reset();request.target=L4_REMOTE_UPDATER;CHECK(!controller->preflight(&journal,(SetupInstalledSource*)1,&request)&&GetLastError()==ERROR_NOT_SUPPORTED);request.target=L4_REMOTE_SUITE;
 for(unsigned stage=1;stage<=19;stage++){reset();fault=stage;L4Journal* owned=&journal;DWORD error=controller->execute(&owned,(SetupInstalledSource*)1,&request,&cancel);CHECK(error!=0);CHECK(closes==1&&freed==2);
  if(stage<=12||stage==18)CHECK(finishes==1&&!transferred);else CHECK(!finishes);if(stage>=15&&stage<=17)CHECK(transferred&&owned==NULL);else CHECK(owned==&journal);
 }
 reset();route_count=2;L4Journal* owned=&journal;CHECK(controller->execute(&owned,(SetupInstalledSource*)1,&request,&cancel)==ERROR_NOT_SUPPORTED&&prepares==1&&!configs_seen&&!plans&&!starts&&finishes==1);
 reset();owned=&journal;CHECK(controller->execute(&owned,(SetupInstalledSource*)1,&request,&cancel)==0&&owned==NULL&&transferred&&waits==1&&!finishes&&closes==1);
 reset();owned=&journal;child_code=ERROR_CRC;CHECK(controller->execute(&owned,(SetupInstalledSource*)1,&request,&cancel)==ERROR_CRC&&waits==1&&!finishes);
 reset();owned=&journal;child_code=ERROR_CRC;fault=17;CHECK(controller->execute(&owned,(SetupInstalledSource*)1,&request,&cancel)==ERROR_CRC&&!finishes);
 printf("Controller composition: %u checks, %u failures; no live SCM/Job/MQTT; child exit diagnostic only\n",passed,failed);return failed?1:0;}
