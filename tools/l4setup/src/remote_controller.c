#include "remote_controller.h"
#include "remote_policy.h"
#include "remote_config.h"
#include "remote_result.h"
#include "../../l4common/platform_profile.h"
#include "../../l4common/proxy_certificate.h"
#include "../../l4common/journal_internal.h"
#include <string.h>

static bool fail(DWORD error){SetLastError(error);return false;}
static ULONGLONG utc(void){FILETIME t;GetSystemTimeAsFileTime(&t);return ((ULONGLONG)t.dwHighDateTime<<32)|t.dwLowDateTime;}
static bool preflight(L4Journal* j,SetupInstalledSource* source,const L4RemoteRequest* request){
    const SetupWorkerEngine* engine=setup_worker_compiled_engine();
    if(!engine || !engine->preflight || !engine->execute)return fail(ERROR_CALL_NOT_IMPLEMENTED);
    if(!j || !source || !request || request->target!=L4_REMOTE_SUITE)return fail(ERROR_NOT_SUPPORTED);
    L4UpdateState state;char profile[L4_PLATFORM_PROFILE_SIZE];
    if(!setup_installed_source_owned_by(source,j) || !setup_installed_source_verify(source) ||
       !l4_platform_current(profile) || !l4_update_state_read(&j->layout,&state))return false;
    if(state.window)return fail(ERROR_BUSY);
    /* No IoT/catalog wait before ACK93. Existing frozen helper admission is
     * strict and read-only here; this controller never installs/replaces it. */
    SetupRecoveryReceipt* helper=NULL;
    bool ok=setup_recovery_receipt_load(j,setup_installed_source_arch(source),&helper);
    setup_recovery_receipt_free(helper);return ok;
}
#define REQUIRE(call) do{SetLastError(0);if(!(call)){error=GetLastError()?GetLastError():ERROR_NOT_READY;goto done;}}while(0)
static DWORD execute(L4Journal** owner,SetupInstalledSource* source,const L4RemoteRequest* request,const volatile LONG* cancelled){
    if(!owner || !*owner || !source || !request)return ERROR_INVALID_PARAMETER;
    SetupRemotePreparation* preparation=NULL;SetupRemoteWorkerPlan* plan=NULL;SetupRemoteWorkerLaunch* launch=NULL;
    SetupRemoteWorkerLaunchReport report={0};SetupRemoteConfigProposals configs={0};SetupRemoteWorkerPolicy policy;
    L4Readiness readiness;L4BootstrapChecks checks;DWORD error=ERROR_NOT_READY,child=STILL_ACTIVE;
    L4Journal* j=*owner;REQUIRE(preflight(j,source,request));
    REQUIRE(setup_remote_prepare(j,source,SETUP_REMOTE_PREPARE_MS,cancelled,&preparation));
    const SetupPreparedPlan* packages=setup_remote_preparation_packages(preparation);
    const L4CatalogRoute* route=l4_route_steps(setup_prepared_route(packages));
    /* Per-hop durable recovery is not composed yet. Refuse before any config
     * application/window/worker, rather than applying a prefix of the route. */
    if(!route || route->count!=1){error=ERROR_NOT_SUPPORTED;goto done;}
    const SetupManifest* target=setup_prepared_manifest(packages,0);
    REQUIRE(target && setup_remote_config_prepare(j,source,target,&configs));
    REQUIRE(setup_remote_worker_plan_prepare(j,source,preparation,&configs,SETUP_REMOTE_PREPARE_MS,cancelled,&plan));
    WORD http=0,mqtt=0;char thumbprint[64];const L4BootstrapPlan* original=setup_installed_source_plan(source);
    REQUIRE(original && l4_proxy_signal_source(original->commands[0],&http,&mqtt,thumbprint));
    if(mqtt!=18883){error=ERROR_REVISION_MISMATCH;goto done;}
    readiness.proxy_port=http;readiness.broker_port=1883;
    const DWORD service_ms[4]={60000,300000,60000,60000};
    REQUIRE(setup_readiness_checks(&readiness,service_ms,60000,&checks));
    REQUIRE(setup_remote_policy_at(utc(),&policy));
    REQUIRE(setup_remote_worker_launch_prepare(j,source,plan,&policy,cancelled,&launch));
    REQUIRE(setup_remote_worker_launch_start(owner,source,launch,&checks,cancelled,&report));
    /* source borrows the journal which has now transferred. Never dereference
     * it again. Keep original Job, image, helper and template pins alive. */
    j=NULL;
    REQUIRE(setup_remote_worker_launch_wait(launch,SETUP_REMOTE_CONTROLLER_WAIT_MS,&child));
    /* Diagnostic child code only. Durable102/103 are the result authority;
     * this function neither publishes success nor clears an active marker. */
    error=child;
done:
    if(owner && *owner && error && !report.recovery_reported && !report.startup.plan_published && !report.startup.task_attempted){
        L4RemoteResult result;DWORD saved=error;
        if(!setup_remote_preparation_finish(*owner,saved,&result)){
            /* Preserve the original failure; absence of a durable result is
             * observable through7032 and must never become successful update. */
            OutputDebugStringW(L"L4 update controller: preparation result could not be persisted.\n");
        }
        error=saved;
    }
    if(!setup_remote_worker_launch_close(&launch,60000) && !error)error=GetLastError()?GetLastError():ERROR_NOT_READY;
    setup_remote_worker_plan_free(plan);setup_remote_preparation_free(preparation);return error;
}
const SetupRemoteEngine* setup_remote_controller_engine(void){
    static const SetupRemoteEngine engine={preflight,execute};return &engine;
}
