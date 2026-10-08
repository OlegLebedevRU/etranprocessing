#include "remote_worker_plan.h"
#include "../../l4common/journal_internal.h"
#include "../../l4common/platform_profile.h"
#include "../../l4common/proxy_certificate.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
struct SetupRemoteWorkerPlan{SetupOperationPlan* operation;L4BootstrapPlan source;L4RemoteRequest request;wchar_t uuid[37];char arch[8];ULONGLONG sequence;};
static bool fail(DWORD code){SetLastError(code);return false;}
static bool checkpoint(ULONGLONG end,const volatile LONG* cancel){
    if(cancel && InterlockedCompareExchange((volatile LONG*)cancel,0,0))return fail(ERROR_CANCELLED);
    return GetTickCount64()<end?true:fail(ERROR_TIMEOUT);
}
typedef struct{bool request,ack,route,complete;ULONGLONG route_sequence,packages_sequence;} History;
static bool history_visit(DWORD kind,ULONGLONG sequence,const void* bytes,DWORD size,void* context){
    (void)bytes;(void)size;History* h=context;
    if(kind==92){if(h->request || sequence!=1)return fail(ERROR_INVALID_DATA);h->request=true;}
    else if(kind==93){if(!h->request || h->ack || sequence!=2)return fail(ERROR_INVALID_DATA);h->ack=true;}
    else if(kind==60){if(!h->ack || h->route || sequence!=h->route_sequence)return fail(ERROR_REVISION_MISMATCH);h->route=true;}
    else if(kind==61){if(!h->route || h->complete)return fail(ERROR_INVALID_DATA);}
    else if(kind==62){if(!h->route || h->complete || sequence!=h->packages_sequence)return fail(ERROR_REVISION_MISMATCH);h->complete=true;}
    else if(kind==20 || kind==63 || kind==10 || kind==64){if(!h->complete)return fail(ERROR_INVALID_STATE);}
    else return fail(ERROR_INVALID_STATE); /* Terminal94/worker/apply cannot plan. */
    return true;
}
static bool config_ref(L4Journal* j,ULONGLONG seq,const char* path){
    if(!seq)return fail(ERROR_INVALID_PARAMETER);if(!l4_config_verify(j,seq,false))return false;
    BYTE* bytes=NULL;DWORD size=0;if(!l4_store_find_record(j,20,seq,&bytes,&size))return false;
    size_t expected=strlen(path);bool ok=size>=24 && l4_store_get32(bytes+8)==expected && expected<=size-24 && !memcmp(bytes+24,path,expected);
    free(bytes);return ok?true:fail(ERROR_REVISION_MISMATCH);
}
static bool route_binding(const L4RoutePlan* route,SetupInstalledSource* source,const L4RemoteRequest* request,const char* profile){
    const BYTE* identity=setup_root_identity(setup_installed_source_root(source));L4CatalogRelease current;
    return identity && l4_route_is_owner_trusted(route) && l4_route_source(route,&current) &&
        !strcmp(current.version,setup_installed_source_version(source)) && !memcmp(current.manifest_sha256,identity,32) &&
        !strcmp(l4_route_requested(route),request->version) && !strcmp(l4_route_arch(route),setup_installed_source_arch(source)) && !strcmp(l4_route_profile(route),profile)?true:fail(ERROR_REVISION_MISMATCH);
}
static bool switches_binding(const SetupRemoteWorkerPlan* p,unsigned hops){
    if(!hops)return fail(ERROR_NO_MORE_ITEMS);
    for(unsigned i=0;i<4;i++){const L4ServiceSwitch* s=setup_operation_switch(p->operation,0,i);
        if(!s || wcscmp(s->service,p->source.services[i]) || wcscmp(s->before.image_path,p->source.commands[i]) ||
            _wcsicmp(s->before.account,L"LocalSystem") || s->before.start_type!=p->source.start_types[i] || s->before_size!=p->source.sizes[i] ||
            memcmp(s->before_sha256,p->source.sha256[i],32))return fail(ERROR_REVISION_MISMATCH);
    }
    for(unsigned h=0;h<hops;h++)for(unsigned s=0;s<4;s++)if(!setup_operation_switch(p->operation,h,s))return fail(ERROR_INVALID_DATA);
    return !setup_operation_switch(p->operation,hops,0)?true:fail(ERROR_REVISION_MISMATCH);
}
void setup_remote_worker_plan_free(SetupRemoteWorkerPlan* p){if(p){setup_operation_free(p->operation);free(p);}}
ULONGLONG setup_remote_worker_plan_sequence(const SetupRemoteWorkerPlan* p){return p?p->sequence:0;}
const SetupOperationPlan* setup_remote_worker_plan_operation(const SetupRemoteWorkerPlan* p){return p?p->operation:NULL;}
const L4BootstrapPlan* setup_remote_worker_plan_source(const SetupRemoteWorkerPlan* p){return p?&p->source:NULL;}
const L4RemoteRequest* setup_remote_worker_plan_request(const SetupRemoteWorkerPlan* p){return p?&p->request:NULL;}
const wchar_t* setup_remote_worker_plan_uuid(const SetupRemoteWorkerPlan* p){return p?p->uuid:NULL;}
const char* setup_remote_worker_plan_arch(const SetupRemoteWorkerPlan* p){return p?p->arch:NULL;}
#define REQUIRE(call) do{SetLastError(ERROR_SUCCESS);if(!(call)){error=GetLastError()?GetLastError():ERROR_INVALID_DATA;goto done;}}while(0)
bool setup_remote_worker_plan_prepare(L4Journal* j,SetupInstalledSource* source,const SetupRemotePreparation* prepared,const SetupRemoteConfigProposals* configs,
    DWORD timeout,const volatile LONG* cancel,SetupRemoteWorkerPlan** result){
    if(!result)return fail(ERROR_INVALID_PARAMETER);*result=NULL;
    if(!j || !source || !prepared || !configs || timeout<100 || timeout>600000)return fail(ERROR_INVALID_PARAMETER);
    ULONGLONG end=GetTickCount64()+timeout;DWORD error=ERROR_INVALID_DATA;SetupRemoteWorkerPlan* p=NULL;SetupPreparedPlan* packages=NULL;
    REQUIRE(checkpoint(end,cancel));REQUIRE(setup_remote_preparation_matches(j,prepared));REQUIRE(setup_installed_source_verify(source));
    const char* arch=setup_installed_source_arch(source),*version=setup_installed_source_version(source);const L4BootstrapPlan* original=setup_installed_source_plan(source);
    if(!arch || (strcmp(arch,"x86") && strcmp(arch,"x64")) || !version || !original){error=ERROR_INVALID_DATA;goto done;}
    const wchar_t* uuid=wcsrchr(j->directory,L'\\');if(!uuid || wcslen(uuid+1)!=36){error=ERROR_INVALID_DATA;goto done;}++uuid;
    p=calloc(1,sizeof(*p));if(!p){error=ERROR_NOT_ENOUGH_MEMORY;goto done;}p->source=*original;wcscpy_s(p->uuid,37,uuid);strcpy_s(p->arch,8,arch);
    REQUIRE(l4_remote_request_load(j,&p->request));if(p->request.target!=L4_REMOTE_SUITE){error=ERROR_NOT_SUPPORTED;goto done;}
    L4RemoteHostReceipt ack;FILETIME birth,exit,kernel,user;REQUIRE(l4_remote_host_load(j,&ack));REQUIRE(l4_remote_host_self(&j->layout,uuid,arch));
    REQUIRE(GetProcessTimes(GetCurrentProcess(),&birth,&exit,&kernel,&user));const SetupRootManifest* updater_root=setup_installed_source_updater_root(source);
    const SetupRootAsset* installer=updater_root?setup_root_installer(updater_root):NULL;const char* updater_version=setup_installed_source_updater_version(source);
    if(ack.pid!=GetCurrentProcessId() || ack.birth!=(((ULONGLONG)birth.dwHighDateTime<<32)|birth.dwLowDateTime) ||
        ack.request.target!=p->request.target || strcmp(ack.request.version,p->request.version) || ack.request.accepted_utc!=p->request.accepted_utc ||
        ack.format_version!=2 || !updater_version || strcmp(ack.updater_version,updater_version) || strcmp(ack.arch,arch) || strcmp(ack.source_version,version) || !installer || ack.executable_size!=installer->size || memcmp(ack.executable_sha256,installer->sha256,32)){
        error=ERROR_REVISION_MISMATCH;goto done;}
    History history={0};history.route_sequence=setup_remote_preparation_route_sequence(prepared);history.packages_sequence=setup_remote_preparation_packages_sequence(prepared);
    REQUIRE(l4_journal_replay(j,history_visit,&history));if(!history.complete){error=ERROR_INVALID_STATE;goto done;}
    char profile[L4_PLATFORM_PROFILE_SIZE];REQUIRE(l4_platform_current(profile));REQUIRE(checkpoint(end,cancel));
    REQUIRE(setup_update_load_prepared(j,history.packages_sequence,&packages));REQUIRE(route_binding(setup_prepared_route(packages),source,&p->request,profile));
    const L4CatalogRoute* hops=l4_route_steps(setup_prepared_route(packages));if(!hops || !hops->count){error=ERROR_NO_MORE_ITEMS;goto done;}unsigned count=hops->count;
    ULONGLONG refs[3+SETUP_REMOTE_LAUNCHERS]={configs->supervisor,configs->broker,configs->acl};
    for(unsigned i=0;i<SETUP_REMOTE_LAUNCHERS;i++)refs[3+i]=configs->launchers[i];
    for(unsigned i=0;i<_countof(refs);i++){if(!refs[i]){error=ERROR_INVALID_PARAMETER;goto done;}
        for(unsigned k=0;k<i;k++)if(refs[k]==refs[i]){error=ERROR_DUP_NAME;goto done;}}
    REQUIRE(config_ref(j,refs[0],"l4superv.json"));REQUIRE(config_ref(j,refs[1],"mosquitto\\mosquitto.conf"));REQUIRE(config_ref(j,refs[2],"mosquitto\\acl.conf"));
    const char* tools[]={"leo4proxy","mosquitto","l4con","l4superv","l4desk","l4sql","l4pin","l4capture","ffmpeg"};
    for(unsigned i=0;i<SETUP_REMOTE_LAUNCHERS;i++){char path[64];sprintf_s(path,64,"launchers\\%s.target",tools[i]);REQUIRE(config_ref(j,refs[3+i],path));}
    WORD http,mqtt;char thumb[64];REQUIRE(l4_proxy_signal_source(p->source.commands[0],&http,&mqtt,thumb));
    REQUIRE(checkpoint(end,cancel));ULONGLONG now=GetTickCount64();if(now>=end || end-now<100){error=ERROR_TIMEOUT;goto done;}
    REQUIRE(setup_update_plan_operation(j,history.packages_sequence,refs,_countof(refs),http,(DWORD)(end-now),&p->sequence));
    REQUIRE(checkpoint(end,cancel));REQUIRE(setup_installed_source_verify(source));REQUIRE(checkpoint(end,cancel));
    REQUIRE(setup_update_load_operation(j,p->sequence,&p->operation));REQUIRE(switches_binding(p,count));
    REQUIRE(checkpoint(end,cancel));REQUIRE(setup_installed_source_verify(source));REQUIRE(l4_remote_host_self(&j->layout,uuid,arch));
    setup_prepared_free(packages);packages=NULL;REQUIRE(checkpoint(end,cancel));*result=p;p=NULL;error=ERROR_SUCCESS;
done:
    setup_prepared_free(packages);setup_remote_worker_plan_free(p);return error?fail(error):true;
}
