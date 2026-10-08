#include "remote_prepare.h"
#include "../../l4common/journal_internal.h"
#include "../../l4common/proxy_certificate.h"
#include "../../l4common/platform_profile.h"
#include <stdlib.h>
#include <string.h>
struct SetupRemotePreparation{SetupPreparedPlan* packages;ULONGLONG route_sequence,packages_sequence;wchar_t operation[37];};
static bool fail(DWORD error){SetLastError(error);return false;}
static bool checkpoint(ULONGLONG deadline,const volatile LONG* cancelled){
    if(cancelled && InterlockedCompareExchange((volatile LONG*)cancelled,0,0))return fail(ERROR_CANCELLED);
    return GetTickCount64()<deadline?true:fail(ERROR_TIMEOUT);
}
static DWORD budget(ULONGLONG deadline){ULONGLONG now=GetTickCount64();if(now>=deadline || deadline-now<100){SetLastError(ERROR_TIMEOUT);return 0;}return (DWORD)(deadline-now);}
static bool profile_valid(const char* profile){if(!profile || !*profile || strlen(profile)>127)return false;
    for(const char* c=profile;*c;c++)if(!((*c>='a' && *c<='z') || (*c>='A' && *c<='Z') || (*c>='0' && *c<='9') || *c=='-' || *c=='_' || *c=='.'))return false;return true;}
typedef struct{ULONGLONG route;bool request,ack;} History;
static bool history_visit(DWORD kind,ULONGLONG sequence,const void* bytes,DWORD size,void* context){
    (void)bytes;(void)size;History* h=context;
    if(kind==L4_RECORD_REMOTE_REQUEST){if(h->request || sequence!=1)return fail(ERROR_INVALID_DATA);h->request=true;}
    else if(kind==L4_RECORD_REMOTE_HOST_ACK){if(!h->request || h->ack || sequence!=2)return fail(ERROR_INVALID_DATA);h->ack=true;}
    else if(kind==L4_RECORD_ROUTE_PLAN){if(!h->ack || h->route)return fail(ERROR_INVALID_DATA);h->route=sequence;}
    else if(kind==L4_RECORD_PREPARED_PACKAGE || kind==L4_RECORD_PACKAGES_COMPLETE){if(!h->route)return fail(ERROR_INVALID_DATA);}
    else return fail(ERROR_INVALID_STATE); /* Never resume prep after apply planning. */
    return true;
}
static bool route_binding(const L4RoutePlan* route,SetupInstalledSource* installed,const L4RemoteRequest* request,const char* profile){
    const char* version=setup_installed_source_version(installed),*arch=setup_installed_source_arch(installed);
    const BYTE* hash=setup_root_identity(setup_installed_source_root(installed));L4CatalogRelease source;
    if(!version || !arch || !hash || !l4_route_is_owner_trusted(route) || !l4_route_source(route,&source) ||
        strcmp(source.version,version) || memcmp(source.manifest_sha256,hash,32) || strcmp(l4_route_requested(route),request->version) ||
        strcmp(l4_route_arch(route),arch) || strcmp(l4_route_profile(route),profile))return fail(ERROR_REVISION_MISMATCH);
    return true;
}
void setup_remote_preparation_free(SetupRemotePreparation* p){if(p){setup_prepared_free(p->packages);free(p);}}
const SetupPreparedPlan* setup_remote_preparation_packages(const SetupRemotePreparation* p){return p?p->packages:NULL;}
ULONGLONG setup_remote_preparation_route_sequence(const SetupRemotePreparation* p){return p?p->route_sequence:0;}
ULONGLONG setup_remote_preparation_packages_sequence(const SetupRemotePreparation* p){return p?p->packages_sequence:0;}
bool setup_remote_preparation_matches(L4Journal* j,const SetupRemotePreparation* p){
    if(!j || !p || !j->lock || j->lock==INVALID_HANDLE_VALUE || j->poisoned)return fail(ERROR_INVALID_STATE);
    const wchar_t* operation=wcsrchr(j->directory,L'\\');
    return operation && !wcscmp(operation+1,p->operation)?true:fail(ERROR_REVISION_MISMATCH);
}
#define REQUIRE(call) do{SetLastError(ERROR_SUCCESS);if(!(call)){error=GetLastError()?GetLastError():ERROR_INVALID_DATA;goto done;}}while(0)
bool setup_remote_prepare(L4Journal* j,SetupInstalledSource* source,DWORD timeout,const volatile LONG* cancelled,SetupRemotePreparation** result){
    if(!result)return fail(ERROR_INVALID_PARAMETER);*result=NULL;
    if(!j || !source || timeout<100 || timeout>600000)return fail(ERROR_INVALID_PARAMETER);
    ULONGLONG deadline=GetTickCount64()+timeout;DWORD error=ERROR_INVALID_DATA;L4RoutePlan* route=NULL;SetupRemotePreparation* p=NULL;
    L4RemoteRequest request={0};L4RemoteHostReceipt ack={0};History history={0};FILETIME birth,exit,kernel,user;
    char profile[L4_PLATFORM_PROFILE_SIZE];REQUIRE(checkpoint(deadline,cancelled));REQUIRE(l4_platform_current(profile));
    if(!profile_valid(profile)){error=ERROR_INVALID_DATA;goto done;}
    REQUIRE(setup_installed_source_verify(source));REQUIRE(checkpoint(deadline,cancelled));
    const char* version=setup_installed_source_version(source),*arch=setup_installed_source_arch(source);
    const BYTE* hash=setup_root_identity(setup_installed_source_root(source));const L4BootstrapPlan* plan=setup_installed_source_plan(source);
    if(!version || !arch || !hash || !plan){error=ERROR_INVALID_DATA;goto done;}
    REQUIRE(l4_remote_request_load(j,&request));if(request.target!=L4_REMOTE_SUITE){error=ERROR_NOT_SUPPORTED;goto done;}
    const wchar_t* operation=wcsrchr(j->directory,L'\\');if(!operation || !operation[1]){error=ERROR_INVALID_DATA;goto done;}++operation;
    REQUIRE(l4_remote_host_load(j,&ack));REQUIRE(l4_remote_host_self(&j->layout,operation,arch));
    REQUIRE(GetProcessTimes(GetCurrentProcess(),&birth,&exit,&kernel,&user));
    const SetupRootAsset* installer=setup_root_installer(setup_installed_source_updater_root(source));
    const char* updater=setup_installed_source_updater_version(source);
    if(ack.pid!=GetCurrentProcessId() || ack.birth!=(((ULONGLONG)birth.dwHighDateTime<<32)|birth.dwLowDateTime) ||
        ack.request.target!=request.target || strcmp(ack.request.version,request.version) || ack.request.accepted_utc!=request.accepted_utc ||
        ack.format_version!=2 || !updater || strcmp(ack.updater_version,updater) || strcmp(ack.source_version,version) || strcmp(ack.arch,arch) || !installer || ack.executable_size!=installer->size ||
        memcmp(ack.executable_sha256,installer->sha256,32)){error=ERROR_REVISION_MISMATCH;goto done;}
    REQUIRE(l4_journal_replay(j,history_visit,&history));if(!history.request || !history.ack){error=ERROR_INVALID_DATA;goto done;}
    WORD http=0,mqtt=0;char thumb[64];REQUIRE(l4_proxy_signal_source(plan->commands[0],&http,&mqtt,thumb));
    REQUIRE(checkpoint(deadline,cancelled));
    if(!history.route){DWORD left=budget(deadline);REQUIRE(left && setup_update_acquire_route(j,http,left,version,hash,request.version,arch,profile,&history.route));}
    REQUIRE(checkpoint(deadline,cancelled));REQUIRE(setup_installed_source_verify(source));REQUIRE(checkpoint(deadline,cancelled));
    REQUIRE(l4_route_load_trusted(j,history.route,&route));REQUIRE(route_binding(route,source,&request,profile));
    REQUIRE(checkpoint(deadline,cancelled));p=calloc(1,sizeof(*p));if(!p){error=ERROR_NOT_ENOUGH_MEMORY;goto done;}p->route_sequence=history.route;
    REQUIRE(setup_update_prepare_route_controlled(j,history.route,http,deadline,cancelled,&p->packages_sequence));
    REQUIRE(checkpoint(deadline,cancelled));REQUIRE(setup_installed_source_verify(source));REQUIRE(checkpoint(deadline,cancelled));
    REQUIRE(setup_update_load_prepared(j,p->packages_sequence,&p->packages));REQUIRE(route_binding(setup_prepared_route(p->packages),source,&request,profile));
    REQUIRE(checkpoint(deadline,cancelled));REQUIRE(setup_installed_source_verify(source));REQUIRE(l4_remote_host_self(&j->layout,operation,arch));
    REQUIRE(checkpoint(deadline,cancelled));if(wcslen(operation)!=36){error=ERROR_INVALID_DATA;goto done;}wcscpy_s(p->operation,37,operation);
    *result=p;p=NULL;error=ERROR_SUCCESS;
done:
    setup_remote_preparation_free(p);l4_route_free(route);return error?fail(error):true;
}
