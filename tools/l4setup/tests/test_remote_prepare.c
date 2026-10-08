/* Composition fixture: source/SCM/signature/transport are explicit models,
 * not SYSTEM/Registry acceptance evidence. Real controller process epoch is used. */
#include "../src/remote_prepare.h"
#include "../../l4common/journal_internal.h"
#include "../../l4common/platform_profile.h"
#include <stdio.h>
#include <string.h>
struct SetupInstalledSource{int unused;};struct L4RoutePlan{int unused;};struct SetupPreparedPlan{int unused;};
static L4RoutePlan modeled_route;static SetupPreparedPlan modeled_packages;static SetupRootAsset modeled_installer;
static L4BootstrapPlan source_plan;static L4RemoteRequest modeled_request;static L4RemoteHostReceipt receipt;static BYTE digest[32];
static const char* modeled_profile="windows-nt-10.0.19045-x64-client";static ULONGLONG clock_now,saved_route;static volatile LONG cancellation;
static unsigned checks,failures,acquires,prepares,verifies,loads;static int mode;static DWORD transport_budget;static ULONGLONG prepare_deadline;
#define CHECK(x) do{++checks;if(!(x)){++failures;printf("FAIL %u %s error=%lu\n",__LINE__,#x,GetLastError());}}while(0)
static ULONGLONG modeled_clock(void){return clock_now;}
bool l4_platform_current(char out[L4_PLATFORM_PROFILE_SIZE]){strcpy_s(out,L4_PLATFORM_PROFILE_SIZE,modeled_profile);return true;}
bool setup_installed_source_verify(SetupInstalledSource* s){(void)s;++verifies;clock_now+=10;if(mode==1 && verifies==2){SetLastError(ERROR_CRC);return false;}return true;}
const char* setup_installed_source_version(const SetupInstalledSource* s){(void)s;return "1.13.6";}
const char* setup_installed_source_updater_version(const SetupInstalledSource* s){(void)s;return "1.13.5";}
const SetupRootManifest* setup_installed_source_updater_root(const SetupInstalledSource* s){return (const SetupRootManifest*)s;}
const char* setup_installed_source_arch(const SetupInstalledSource* s){(void)s;return "x86";}
const SetupRootManifest* setup_installed_source_root(const SetupInstalledSource* s){return (const SetupRootManifest*)s;}
const L4BootstrapPlan* setup_installed_source_plan(const SetupInstalledSource* s){(void)s;return &source_plan;}
const BYTE* setup_root_identity(const SetupRootManifest* r){(void)r;return digest;}
const SetupRootAsset* setup_root_installer(const SetupRootManifest* r){(void)r;return &modeled_installer;}
bool l4_remote_request_load(L4Journal* j,L4RemoteRequest* out){(void)j;*out=modeled_request;return true;}
bool l4_remote_host_load(L4Journal* j,L4RemoteHostReceipt* out){(void)j;*out=receipt;return true;}
bool l4_remote_host_self(const L4Layout* l,const wchar_t* uuid,const char* arch){(void)l;CHECK(!wcscmp(uuid,L"17730000-0000-4000-8000-000000000031"));CHECK(!strcmp(arch,"x86"));return true;}
bool l4_journal_replay(L4Journal* j,L4JournalVisitor visit,void* context){(void)j;
    if(!visit(92,1,NULL,0,context) || !visit(93,2,NULL,0,context))return false;
    if(mode==8)return visit(64,3,NULL,0,context);
    if(saved_route && !visit(60,saved_route,NULL,0,context))return false;
    if(mode==7)return visit(60,saved_route+1,NULL,0,context);
    return true;
}
bool l4_proxy_signal_source(const wchar_t* cmd,WORD* http,WORD* mqtt,char thumb[64]){CHECK(!wcscmp(cmd,L"signed source SCM"));*http=17731;*mqtt=17732;thumb[0]=0;return true;}
bool setup_update_acquire_route(L4Journal* j,WORD port,DWORD timeout,const char* current,const BYTE hash[32],const char* target,const char* arch,const char* selected,ULONGLONG* seq){
    (void)j;++acquires;CHECK(port==17731 && !strcmp(current,"1.13.6") && !memcmp(hash,digest,32));CHECK(!strcmp(target,modeled_request.version) && !strcmp(arch,"x86") && !strcmp(selected,modeled_profile));
    transport_budget=timeout;clock_now+=20;*seq=saved_route=3;if(mode==2)cancellation=1;if(mode==3)clock_now+=600000;return true;
}
bool l4_route_load_trusted(L4Journal* j,ULONGLONG seq,L4RoutePlan** out){(void)j;CHECK(seq==3);*out=&modeled_route;return true;}
bool l4_route_is_owner_trusted(const L4RoutePlan* p){(void)p;return mode!=9;}
bool l4_route_source(const L4RoutePlan* p,L4CatalogRelease* out){(void)p;memset(out,0,sizeof(*out));strcpy_s(out->version,64,mode==4?"1.13.5":"1.13.6");memcpy(out->manifest_sha256,digest,32);if(mode==5)out->manifest_sha256[0]^=1;return true;}
const char* l4_route_requested(const L4RoutePlan* p){(void)p;return mode==6?"1.13.7":modeled_request.version;}
const char* l4_route_arch(const L4RoutePlan* p){(void)p;return mode==10?"x64":"x86";}
const char* l4_route_profile(const L4RoutePlan* p){(void)p;return mode==11?"windows-7-x86":modeled_profile;}
void l4_route_free(L4RoutePlan* p){CHECK(!p || p==&modeled_route);}
bool setup_update_prepare_route_controlled(L4Journal* j,ULONGLONG seq,WORD port,ULONGLONG deadline,const volatile LONG* c,ULONGLONG* complete){
    (void)j;++prepares;CHECK(seq==3 && port==17731 && c==&cancellation);prepare_deadline=deadline;clock_now+=20;*complete=5;if(mode==12)cancellation=1;return true;
}
bool setup_update_load_prepared(L4Journal* j,ULONGLONG seq,SetupPreparedPlan** out){(void)j;++loads;CHECK(seq==5);clock_now+=10;*out=&modeled_packages;if(mode==13)clock_now+=600000;return true;}
void setup_prepared_free(SetupPreparedPlan* p){CHECK(!p || p==&modeled_packages);}
const L4RoutePlan* setup_prepared_route(const SetupPreparedPlan* p){CHECK(p==&modeled_packages);return &modeled_route;}
#define GetTickCount64 modeled_clock
#include "../src/remote_prepare.c"
#undef GetTickCount64
static void reset(void){mode=0;clock_now=1000;saved_route=0;cancellation=0;acquires=prepares=verifies=loads=0;transport_budget=0;prepare_deadline=0;
    memset(&receipt,0,sizeof(receipt));memset(&modeled_request,0,sizeof(modeled_request));modeled_request.target=L4_REMOTE_SUITE;strcpy_s(modeled_request.version,32,"latest");modeled_request.accepted_utc=123;
    receipt.request=modeled_request;receipt.pid=GetCurrentProcessId();FILETIME birth,e,k,u;CHECK(GetProcessTimes(GetCurrentProcess(),&birth,&e,&k,&u));receipt.birth=((ULONGLONG)birth.dwHighDateTime<<32)|birth.dwLowDateTime;
    receipt.format_version=2;strcpy_s(receipt.updater_version,32,"1.13.5");strcpy_s(receipt.arch,8,"x86");strcpy_s(receipt.source_version,32,"1.13.6");modeled_installer.size=42;memset(modeled_installer.sha256,1,32);receipt.executable_size=42;memcpy(receipt.executable_sha256,modeled_installer.sha256,32);memset(digest,2,32);
    wcscpy_s(source_plan.commands[0],2048,L"signed source SCM");
}
int wmain(void){L4Journal journal={0};wcscpy_s(journal.directory,MAX_PATH,L"C:\\model\\17730000-0000-4000-8000-000000000031");SetupInstalledSource source={0};SetupRemotePreparation* p=NULL;
    reset();CHECK(setup_remote_prepare(&journal,&source,1000,&cancellation,&p));CHECK(acquires==1 && prepares==1 && loads==1 && verifies==4 && transport_budget==990 && prepare_deadline==2000);
    CHECK(!setup_remote_preparation_matches(&journal,p) && GetLastError()==ERROR_INVALID_STATE);journal.lock=(HANDLE)(ULONG_PTR)123;
    CHECK(setup_remote_preparation_matches(&journal,p));journal.directory[wcslen(journal.directory)-1]=L'2';
    CHECK(!setup_remote_preparation_matches(&journal,p) && GetLastError()==ERROR_REVISION_MISMATCH);journal.directory[wcslen(journal.directory)-1]=L'1';
    CHECK(setup_remote_preparation_route_sequence(p)==3 && setup_remote_preparation_packages_sequence(p)==5 && setup_remote_preparation_packages(p)==&modeled_packages);setup_remote_preparation_free(p);p=NULL;
    CHECK(setup_remote_prepare(&journal,&source,1000,&cancellation,&p));CHECK(acquires==1 && prepares==2);setup_remote_preparation_free(p);p=NULL;
    for(int failure=1;failure<=13;failure++){reset();mode=failure;saved_route=(failure>=4 && failure<=11)?3:0;
        CHECK(!setup_remote_prepare(&journal,&source,1000,&cancellation,&p) && p==NULL);
        if(failure>=4 && failure<=11)CHECK(acquires==0 && prepares==0);
        if(failure==2 || failure==12)CHECK(GetLastError()==ERROR_CANCELLED);
        if(failure==3 || failure==13)CHECK(GetLastError()==ERROR_TIMEOUT);
    }
    reset();cancellation=1;CHECK(!setup_remote_prepare(&journal,&source,1000,&cancellation,&p) && GetLastError()==ERROR_CANCELLED && verifies==0);
    reset();receipt.pid++;CHECK(!setup_remote_prepare(&journal,&source,1000,&cancellation,&p) && acquires==0);
    reset();receipt.birth++;CHECK(!setup_remote_prepare(&journal,&source,1000,&cancellation,&p) && acquires==0);
    reset();receipt.request.accepted_utc++;CHECK(!setup_remote_prepare(&journal,&source,1000,&cancellation,&p) && acquires==0);
    reset();receipt.executable_sha256[0]^=1;CHECK(!setup_remote_prepare(&journal,&source,1000,&cancellation,&p) && acquires==0);
    reset();modeled_request.target=L4_REMOTE_UPDATER;CHECK(!setup_remote_prepare(&journal,&source,1000,&cancellation,&p) && GetLastError()==ERROR_NOT_SUPPORTED && acquires==0);
    reset();CHECK(!setup_remote_prepare(&journal,&source,99,&cancellation,&p));CHECK(!setup_remote_prepare(&journal,&source,600001,&cancellation,&p));
    printf("Remote preparation composition: %u checks, %u failures; source/SCM/transport modeled, no live acceptance\n",checks,failures);return failures?1:0;
}
