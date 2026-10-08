/* Orchestration model only: no live SCM/Registry/trust substitution evidence.
 * Actual process epoch; copied snapshot survives source/journal invalidation. */
#include "../src/remote_worker_plan.h"
#include "../../l4common/journal_internal.h"
#include "../../l4common/platform_profile.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
struct SetupInstalledSource{int unused;};struct SetupRemotePreparation{int unused;};struct SetupPreparedPlan{int unused;};struct SetupOperationPlan{int unused;};struct L4RoutePlan{int unused;};
static SetupPreparedPlan model_packages;static SetupOperationPlan model_operation;static L4RoutePlan model_route;static L4CatalogRoute model_hops;
static L4BootstrapPlan model_source;static L4ServiceSwitch model_switches[2][4];static L4RemoteRequest model_request;static L4RemoteHostReceipt model_ack;static SetupRootAsset model_installer,model_suite_installer;static BYTE model_digest[32];
static unsigned checks,failures,planner_calls,source_fetches,source_verifies;static int fault;static ULONGLONG tick,saved;static volatile LONG model_cancel;static bool source_dead;
#define CHECK(x) do{++checks;if(!(x)){++failures;printf("FAIL %u %s (%lu)\n",__LINE__,#x,GetLastError());}}while(0)
static ULONGLONG model_clock(void){return tick;}
bool setup_remote_preparation_matches(L4Journal* j,const SetupRemotePreparation* p){(void)j;(void)p;if(fault==1){SetLastError(ERROR_REVISION_MISMATCH);return false;}return true;}
ULONGLONG setup_remote_preparation_route_sequence(const SetupRemotePreparation* p){(void)p;return 3;}
ULONGLONG setup_remote_preparation_packages_sequence(const SetupRemotePreparation* p){(void)p;return 5;}
bool setup_installed_source_verify(SetupInstalledSource* s){(void)s;CHECK(!source_dead);source_verifies++;tick+=10;if(fault==2 || (fault==12 && source_verifies==2)){SetLastError(ERROR_CRC);return false;}return true;}
const char* setup_installed_source_version(const SetupInstalledSource* s){(void)s;CHECK(!source_dead);return "1.13.6";}
const char* setup_installed_source_arch(const SetupInstalledSource* s){(void)s;CHECK(!source_dead);return "x86";}
const L4BootstrapPlan* setup_installed_source_plan(const SetupInstalledSource* s){(void)s;CHECK(!source_dead);return &model_source;}
const SetupRootManifest* setup_installed_source_root(const SetupInstalledSource* s){CHECK(!source_dead);return (const SetupRootManifest*)s;}
const SetupRootManifest* setup_installed_source_updater_root(const SetupInstalledSource* s){(void)s;CHECK(!source_dead);return fault==18?NULL:(const SetupRootManifest*)100;}
const char* setup_installed_source_updater_version(const SetupInstalledSource* s){(void)s;CHECK(!source_dead);return fault==19?NULL:"1.13.5";}
const BYTE* setup_root_identity(const SetupRootManifest* r){(void)r;return model_digest;}
const SetupRootAsset* setup_root_installer(const SetupRootManifest* r){return r==(const SetupRootManifest*)100?&model_installer:&model_suite_installer;}
bool l4_remote_request_load(L4Journal* j,L4RemoteRequest* r){(void)j;*r=model_request;return true;}
bool l4_remote_host_load(L4Journal* j,L4RemoteHostReceipt* r){(void)j;*r=model_ack;if(fault==3)r->birth++;if(fault==15)r->format_version=1;if(fault==16)strcpy_s(r->updater_version,32,"1.13.6");if(fault==17){r->executable_size=model_suite_installer.size;memcpy(r->executable_sha256,model_suite_installer.sha256,32);}return true;}
bool l4_remote_host_self(const L4Layout* l,const wchar_t* uuid,const char* arch){(void)l;CHECK(!wcscmp(uuid,L"17730000-0000-4000-8000-000000000031") && !strcmp(arch,"x86"));return true;}
bool l4_journal_replay(L4Journal* j,L4JournalVisitor visit,void* c){(void)j;
    if(!visit(92,1,NULL,0,c) || !visit(93,2,NULL,0,c) || !visit(60,3,NULL,0,c) || !visit(61,4,NULL,0,c) || !visit(62,fault==5?6:5,NULL,0,c))return false;
    if(fault==4)return visit(94,6,NULL,0,c); /* Valid prep prefix, terminal closes it. */
    return visit(20,6,NULL,0,c);
}
bool l4_platform_current(char profile[L4_PLATFORM_PROFILE_SIZE]){strcpy_s(profile,L4_PLATFORM_PROFILE_SIZE,"windows-nt-10.0.19045-x64-client");return true;}
bool setup_update_load_prepared(L4Journal* j,ULONGLONG seq,SetupPreparedPlan** p){(void)j;CHECK(seq==5);*p=&model_packages;return true;}
void setup_prepared_free(SetupPreparedPlan* p){CHECK(!p || p==&model_packages);}
const L4RoutePlan* setup_prepared_route(const SetupPreparedPlan* p){CHECK(p==&model_packages);return &model_route;}
bool l4_route_is_owner_trusted(const L4RoutePlan* r){(void)r;return fault!=6;}
bool l4_route_source(const L4RoutePlan* r,L4CatalogRelease* s){(void)r;memset(s,0,sizeof(*s));strcpy_s(s->version,64,"1.13.6");memcpy(s->manifest_sha256,model_digest,32);if(fault==7)s->manifest_sha256[0]^=1;return true;}
const char* l4_route_requested(const L4RoutePlan* r){(void)r;return model_request.version;}
const char* l4_route_arch(const L4RoutePlan* r){(void)r;return "x86";}
const char* l4_route_profile(const L4RoutePlan* r){(void)r;return fault==8?"windows-7-x86":"windows-nt-10.0.19045-x64-client";}
const L4CatalogRoute* l4_route_steps(const L4RoutePlan* r){(void)r;return &model_hops;}
bool l4_config_verify(L4Journal* j,ULONGLONG seq,bool candidate){(void)j;CHECK(!candidate && seq>=6 && seq<=17);if(fault==9){SetLastError(ERROR_CRC);return false;}return true;}
DWORD l4_store_get32(const BYTE* b){return (DWORD)b[0]|((DWORD)b[1]<<8)|((DWORD)b[2]<<16)|((DWORD)b[3]<<24);}
bool l4_store_find_record(L4Journal* j,DWORD kind,ULONGLONG seq,BYTE** bytes,DWORD* size){(void)j;CHECK(kind==20 && seq>=6 && seq<=17);
    const char* paths[]={"l4superv.json","mosquitto\\mosquitto.conf","mosquitto\\acl.conf","launchers\\leo4proxy.target","launchers\\mosquitto.target","launchers\\l4con.target","launchers\\l4superv.target","launchers\\l4desk.target","launchers\\l4sql.target","launchers\\l4pin.target","launchers\\l4capture.target","launchers\\ffmpeg.target"};
    const char* path=fault==10?"wrong.json":paths[seq-6];DWORD n=(DWORD)strlen(path);*size=24+n;*bytes=calloc(1,*size);(*bytes)[8]=(BYTE)n;memcpy(*bytes+24,path,n);return true;
}
bool l4_proxy_signal_source(const wchar_t* c,WORD* http,WORD* mqtt,char thumb[64]){CHECK(!wcscmp(c,model_source.commands[0]));*http=17731;*mqtt=17732;thumb[0]=0;return true;}
bool setup_update_plan_operation(L4Journal* j,ULONGLONG seq,const ULONGLONG* refs,unsigned n,WORD port,DWORD timeout,ULONGLONG* result){(void)j;planner_calls++;
    CHECK(seq==5 && n==12 && refs[0]==6 && refs[1]==7 && refs[2]==8 && port==17731 && timeout>=100 && timeout<=1000);
    for(unsigned i=0;i<9;i++)CHECK(refs[3+i]==9+i);
    if(!saved){saved=30;source_fetches++;}*result=saved;tick+=20;if(fault==11)model_cancel=1;if(fault==13)tick+=600000;return true;
}
bool setup_update_load_operation(L4Journal* j,ULONGLONG seq,SetupOperationPlan** p){(void)j;CHECK(seq==30);*p=&model_operation;if(fault==14)model_switches[0][0].before_sha256[0]^=1;return true;}
void setup_operation_free(SetupOperationPlan* p){CHECK(!p || p==&model_operation);}
const L4ServiceSwitch* setup_operation_switch(const SetupOperationPlan* p,unsigned hop,unsigned svc){CHECK(p==&model_operation);return hop<2 && svc<4?&model_switches[hop][svc]:NULL;}
#define GetTickCount64 model_clock
#include "../src/remote_worker_plan.c"
#undef GetTickCount64
static void reset(void){fault=0;tick=1000;saved=0;planner_calls=source_fetches=source_verifies=0;model_cancel=0;source_dead=false;
    memset(&model_source,0,sizeof(model_source));memset(model_switches,0,sizeof(model_switches));memset(&model_request,0,sizeof(model_request));memset(&model_ack,0,sizeof(model_ack));model_hops.count=2;
    model_request.target=L4_REMOTE_SUITE;strcpy_s(model_request.version,32,"latest");model_request.accepted_utc=123;model_ack.request=model_request;model_ack.pid=GetCurrentProcessId();FILETIME b,e,k,u;CHECK(GetProcessTimes(GetCurrentProcess(),&b,&e,&k,&u));model_ack.birth=((ULONGLONG)b.dwHighDateTime<<32)|b.dwLowDateTime;
    strcpy_s(model_ack.arch,8,"x86");strcpy_s(model_ack.source_version,32,"1.13.6");strcpy_s(model_ack.updater_version,32,"1.13.5");model_ack.format_version=2;
    model_ack.executable_size=model_installer.size=42;memset(model_installer.sha256,1,32);memcpy(model_ack.executable_sha256,model_installer.sha256,32);memset(model_digest,2,32);model_suite_installer.size=99;memset(model_suite_installer.sha256,9,32);
    const wchar_t* names[]={L"Leo4Proxy",L"mosquitto",L"L4Con",L"L4Superv"};for(unsigned i=0;i<4;i++){
        wcscpy_s(model_source.services[i],32,names[i]);wcscpy_s(model_source.commands[i],2048,names[i]);model_source.start_types[i]=SERVICE_AUTO_START;model_source.sizes[i]=50+i;memset(model_source.sha256[i],(int)i+2,32);
        wcscpy_s(model_switches[0][i].service,32,names[i]);wcscpy_s(model_switches[0][i].before.image_path,2048,names[i]);wcscpy_s(model_switches[0][i].before.account,64,L"LocalSystem");model_switches[0][i].before.start_type=SERVICE_AUTO_START;model_switches[0][i].before_size=50+i;memcpy(model_switches[0][i].before_sha256,model_source.sha256[i],32);
    }
}
int wmain(void){L4Journal journal={0};wcscpy_s(journal.directory,MAX_PATH,L"C:\\model\\17730000-0000-4000-8000-000000000031");SetupInstalledSource source={0};SetupRemotePreparation prepared={0};SetupRemoteConfigProposals configs={6,7,8,{9,10,11,12,13,14,15,16,17}};SetupRemoteWorkerPlan* p=NULL;
    reset();CHECK(setup_remote_worker_plan_prepare(&journal,&source,&prepared,&configs,1000,&model_cancel,&p));CHECK(planner_calls==1 && source_fetches==1 && source_verifies==3 && setup_remote_worker_plan_sequence(p)==30);
    CHECK(setup_remote_worker_plan_operation(p)==&model_operation && !wcscmp(setup_remote_worker_plan_uuid(p),L"17730000-0000-4000-8000-000000000031"));
    source_dead=true;memset(&source,0xff,sizeof(source));memset(&journal.layout,0xff,sizeof(journal.layout));memset(&model_source,0xff,sizeof(model_source));model_request.version[0]='X';
    CHECK(!wcscmp(setup_remote_worker_plan_source(p)->commands[0],L"Leo4Proxy") && !strcmp(setup_remote_worker_plan_request(p)->version,"latest") && !strcmp(setup_remote_worker_plan_arch(p),"x86"));setup_remote_worker_plan_free(p);p=NULL;
    reset();CHECK(setup_remote_worker_plan_prepare(&journal,&source,&prepared,&configs,1000,&model_cancel,&p));setup_remote_worker_plan_free(p);p=NULL;
    CHECK(setup_remote_worker_plan_prepare(&journal,&source,&prepared,&configs,1000,&model_cancel,&p));CHECK(source_fetches==1 && planner_calls==2);setup_remote_worker_plan_free(p);p=NULL;
    for(int i=1;i<=14;i++){reset();fault=i;CHECK(!setup_remote_worker_plan_prepare(&journal,&source,&prepared,&configs,1000,&model_cancel,&p) && !p);if(i<=10)CHECK(!planner_calls);if(i==11)CHECK(GetLastError()==ERROR_CANCELLED && saved==30);if(i==13)CHECK(GetLastError()==ERROR_TIMEOUT && saved==30);}
    for(int i=15;i<=19;i++){reset();fault=i;CHECK(!setup_remote_worker_plan_prepare(&journal,&source,&prepared,&configs,1000,&model_cancel,&p) && !p && !planner_calls && GetLastError()==ERROR_REVISION_MISMATCH);}
    reset();model_cancel=1;CHECK(!setup_remote_worker_plan_prepare(&journal,&source,&prepared,&configs,1000,&model_cancel,&p) && !source_verifies);
    reset();configs.acl=configs.broker;CHECK(!setup_remote_worker_plan_prepare(&journal,&source,&prepared,&configs,1000,&model_cancel,&p) && GetLastError()==ERROR_DUP_NAME && !planner_calls);configs.acl=8;
    reset();configs.launchers[0]=0;CHECK(!setup_remote_worker_plan_prepare(&journal,&source,&prepared,&configs,1000,&model_cancel,&p) && GetLastError()==ERROR_INVALID_PARAMETER && !planner_calls);configs.launchers[0]=9;
    reset();configs.launchers[8]=configs.broker;CHECK(!setup_remote_worker_plan_prepare(&journal,&source,&prepared,&configs,1000,&model_cancel,&p) && GetLastError()==ERROR_DUP_NAME && !planner_calls);configs.launchers[8]=17;
    reset();model_hops.count=0;CHECK(!setup_remote_worker_plan_prepare(&journal,&source,&prepared,&configs,1000,&model_cancel,&p) && GetLastError()==ERROR_NO_MORE_ITEMS && !planner_calls);
    CHECK(!setup_remote_worker_plan_prepare(&journal,&source,&prepared,&configs,99,&model_cancel,&p));CHECK(!setup_remote_worker_plan_prepare(&journal,&source,&prepared,&configs,600001,&model_cancel,&p));
    printf("Remote worker plan composition: %u checks, %u failures; snapshot lifetime/strict modeled bindings, no live worker/SCM/network\n",checks,failures);return failures?1:0;
}
