#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "orchestrator.h"
#include "cert_discovery.h"
#include "proxy_client.h"
#include "service_mgr.h"
#include "mosquitto_conf.h"

static bool probe_ok = true, restart_ok = true, persist_ok = true;
static int restarts, starts, writes, config_writes, failures;
static cert_state discovery = CERT_VALID;
static bool matching = true, service_running = true, active_config = true;
static cert_state fake_discover(const wchar_t* sn, cert_info* out) {
    (void)sn; memset(out, 0, sizeof(*out));
    strcpy_s(out->sn, sizeof(out->sn), "new-sn");
    strcpy_s(out->thumbprint_hex, sizeof(out->thumbprint_hex), matching
        ? "1111111111111111111111111111111111111111" : "2222222222222222222222222222222222222222");
    return discovery;
}
static bool fake_probe(const wchar_t* url, int timeout, Leo4ProxyInfo* info) {
    (void)url; (void)timeout; memset(info, 0, sizeof(*info));
    info->cert_ready = info->certificate_found = true;
    strcpy_s(info->sn, sizeof(info->sn), "new-sn");
    strcpy_s(info->thumbprint, sizeof(info->thumbprint), "1111111111111111111111111111111111111111");
    return probe_ok;
}
static bool fake_restart(const wchar_t* name) { (void)name; restarts++; return restart_ok; }
static bool fake_mqtt_restart(void) { restarts++; return restart_ok; }
static bool fake_start(const wchar_t* name) { (void)name; starts++; return true; }
static bool fake_running(const wchar_t* name) { (void)name; return service_running; }
static bool fake_exists(const wchar_t* name) { (void)name; return true; }
static bool fake_save(const wchar_t* path, const L4State* state) { (void)path; (void)state; writes++; return persist_ok; }
static void fake_update(const wchar_t* path, L4State* state) { (void)path; (void)state; }
static bool fake_conf(const wchar_t* path, int port, const char* sn, const wchar_t* template_path) {
    (void)path; (void)port; (void)sn; (void)template_path; config_writes++; return true;
}
static bool fake_active(const wchar_t* path, const char* sn) { (void)path; (void)sn; return active_config; }

#define cert_discover fake_discover
#define proxy_client_query_info fake_probe
#define svc_restart fake_restart
#define svc_restart_mqtt_stack fake_mqtt_restart
#define svc_start fake_start
#define svc_is_running fake_running
#define svc_exists fake_exists
#define state_save fake_save
#define state_update_services fake_update
#define mosquitto_conf_generate_active fake_conf
#define mosquitto_conf_is_active_with_sn fake_active
#include "../src/communication_watch.h"
#include "../../l4common/communication_plan.h"
static bool fake_plan_open(const L4Layout* r,const wchar_t* id,L4CommunicationPin** p){(void)r;(void)id;*p=NULL;SetLastError(ERROR_NOT_SUPPORTED);return false;}
static bool fake_plan_matches(const L4CommunicationPin* p,const L4UpdateState* s){(void)p;(void)s;return false;}
static void fake_plan_close(L4CommunicationPin* p){(void)p;}
#define l4_communication_plan_open fake_plan_open
#define l4_communication_plan_matches fake_plan_matches
#define l4_communication_plan_close fake_plan_close
static bool fake_watch_start(const L4UpdateConsumer* c,L4CommunicationWatch** w){(void)c;*w=NULL;SetLastError(ERROR_NOT_SUPPORTED);return false;}
static bool fake_watch_close(L4CommunicationWatch** w,DWORD t){(void)t;*w=NULL;return true;}
static DWORD fake_watch_query(L4CommunicationWatch* w,const L4UpdateState* s,DWORD t,HANDLE c){(void)w;(void)s;(void)t;(void)c;return ERROR_NOT_SUPPORTED;}
#define supervisor_communication_watch_start fake_watch_start
#define supervisor_communication_watch_close fake_watch_close
#define supervisor_communication_watch_query fake_watch_query
#include "../src/orchestrator.c"
#include "../../l4common/tests/update_state_fixture.h"

#define CHECK(x) do { if (!(x)) { printf("FAIL identity %d: %s\n", __LINE__, #x); failures++; } } while (0)
static DWORD WINAPI complete_health(void* context){Sleep(20);InterlockedExchange(&health_active,(LONG)(ULONG_PTR)context);InterlockedIncrement(&health_cycle);return 0;}
static void test_health(void){
    HANDLE cancel=CreateEventW(NULL,TRUE,FALSE,NULL);g_hForceTickEvent=CreateEventW(NULL,FALSE,FALSE,NULL);CHECK(cancel && g_hForceTickEvent);
    InterlockedExchange(&health_active,1);CHECK(health_probe(0,5,cancel,NULL)==ERROR_TIMEOUT); /* Old active flag is not evidence. */
    HANDLE worker=CreateThread(NULL,0,complete_health,(void*)(ULONG_PTR)1,0,NULL);CHECK(worker);CHECK(health_probe(0,500,cancel,NULL)==ERROR_SUCCESS);WaitForSingleObject(worker,1000);CloseHandle(worker);
    worker=CreateThread(NULL,0,complete_health,NULL,0,NULL);CHECK(worker);CHECK(health_probe(0,500,cancel,NULL)==ERROR_NOT_READY);WaitForSingleObject(worker,1000);CloseHandle(worker);
    SetEvent(cancel);CHECK(health_probe(0,500,cancel,NULL)==ERROR_CANCELLED);CHECK(health_probe(1,500,cancel,NULL)==ERROR_NOT_SUPPORTED);
    CloseHandle(cancel);CloseHandle(g_hForceTickEvent);g_hForceTickEvent=NULL;
}
static void test_drain(UpdateFixture* fixture){
    L4UpdateState expected;CHECK(update_fixture_live(fixture,&expected));
    HANDLE cancel=CreateEventW(NULL,TRUE,FALSE,NULL);CHECK(cancel);
    CHECK(drain_probe(&expected,1000,cancel,NULL)==ERROR_SUCCESS);
    AcquireSRWLockShared(&g_update_consumer.admission);
    CHECK(drain_probe(&expected,20,cancel,NULL)==ERROR_TIMEOUT);
    ReleaseSRWLockShared(&g_update_consumer.admission);
    g_pending_pin_process=CreateEventW(NULL,TRUE,FALSE,NULL);CHECK(g_pending_pin_process);
    CHECK(drain_probe(&expected,20,cancel,NULL)==ERROR_TIMEOUT);
    SetEvent(g_pending_pin_process);CHECK(drain_probe(&expected,1000,cancel,NULL)==ERROR_SUCCESS);CHECK(!g_pending_pin_process);
    expected.plan_sequence++;CHECK(drain_probe(&expected,1000,cancel,NULL)==ERROR_REVISION_MISMATCH);expected.plan_sequence--;
    SetEvent(cancel);CHECK(drain_probe(&expected,1000,cancel,NULL)==ERROR_CANCELLED);ResetEvent(cancel);
    CHECK(update_fixture_put(fixture,1));expected.deadline_utc=1;
    CHECK(drain_probe(&expected,1000,cancel,NULL)==ERROR_TIMEOUT);
    CloseHandle(cancel);
    puts("Supervisor drain: cycle fence, unconfirmed owned PIN, exact plan, expiry and cancellation PASS");
}
static void setup(L4SupervConfig* cfg, L4State* state) {
    config_init_defaults(cfg, NULL);
    cfg->auto_reset_on_clone = false;
    cfg->watchdog_enabled = false;
    cfg->auto_start_l4desk = false;
    state_init(state);
    strcpy_s(state->status, sizeof(state->status), "active");
    strcpy_s(state->sn, sizeof(state->sn), "old-sn");
    strcpy_s(state->hw_fingerprint, sizeof(state->hw_fingerprint), "fixture");
    probe_ok = restart_ok = persist_ok = matching = service_running = active_config = true;
    restarts = starts = writes = config_writes = 0;
    g_last_proxy_identity_restart = 0;
}
int main(void) {
    L4SupervConfig cfg;
    L4State state;
    bool action = false;
    setup(&cfg, &state);
    probe_ok = false;
    CHECK(orchestrator_step(&cfg, &state, &action));
    CHECK(!strcmp(state.sn, "old-sn")); CHECK(!restarts); CHECK(!config_writes);
    /* A broken probe must still allow SCM recovery of a stopped service. */
    cfg.watchdog_enabled = true;
    cfg.auto_start_mosquitto = cfg.auto_start_l4con = cfg.auto_start_l4desk = false;
    cfg.auto_start_leo4proxy = true;
    service_running = false;
    CHECK(orchestrator_step(&cfg, &state, &action)); CHECK(starts == 1);
    state_cleanup(&state);
    setup(&cfg, &state); restart_ok = false;
    CHECK(!orchestrator_step(&cfg, &state, &action)); CHECK(!strcmp(state.sn, "old-sn"));
    state_cleanup(&state);
    setup(&cfg, &state); persist_ok = false;
    CHECK(!orchestrator_step(&cfg, &state, &action)); CHECK(!strcmp(state.sn, "old-sn"));
    state_cleanup(&state);
    setup(&cfg, &state);
    CHECK(orchestrator_step(&cfg, &state, &action)); CHECK(!strcmp(state.sn, "new-sn")); CHECK(restarts == 1);
    state_cleanup(&state);
    setup(&cfg, &state); matching = false;
    CHECK(!orchestrator_step(&cfg, &state, &action)); CHECK(!strcmp(state.sn, "old-sn")); CHECK(restarts == 1);
    CHECK(!orchestrator_step(&cfg, &state, &action)); CHECK(restarts == 1);
    state_cleanup(&state);
    /* Actual fresh-start regression: authenticated pre-rendered link must keep
     * its broker/con epochs. SCM/proxy/config/persistence are modeled here. */
    setup(&cfg, &state);state.sn[0]=state.thumbprint[0]=0;
    strcpy_s(state.status,sizeof(state.status),"standby");
    CHECK(orchestrator_step(&cfg,&state,&action));CHECK(!restarts && !config_writes);
    CHECK(!strcmp(state.sn,"new-sn") && writes>=1);
    state_cleanup(&state);
    setup(&cfg,&state);state.sn[0]=state.thumbprint[0]=0;active_config=false;
    CHECK(orchestrator_step(&cfg,&state,&action));CHECK(restarts==1 && config_writes==1);
    state_cleanup(&state);
    setup(&cfg,&state);state.sn[0]=state.thumbprint[0]=0;service_running=false;
    CHECK(orchestrator_step(&cfg,&state,&action));CHECK(restarts==1 && config_writes==1);
    state_cleanup(&state);
    setup(&cfg,&state);state.sn[0]=state.thumbprint[0]=0;persist_ok=false;
    CHECK(!orchestrator_step(&cfg,&state,&action));CHECK(!restarts && !config_writes && !state.sn[0]);
    state_cleanup(&state);
    puts("Fresh prepared link: no restart/rewrite; invalid config/service and persistence failure checks PASS");
    test_health();
    UpdateFixture update_fixture;CHECK(update_fixture_init(&update_fixture));
    g_update_consumer.enabled=true;g_update_consumer.layout=update_fixture.layout;
    setup(&cfg,&state);cfg.watchdog_enabled=true;cfg.auto_reset_on_clone=true;
    CHECK(!orchestrator_step(&cfg,&state,&action)); /* Missing state: no repair. */
    CHECK(!restarts && !starts && !writes && !config_writes && !action);
    CHECK(update_fixture_put(&update_fixture,1));
    CHECK(!orchestrator_step(&cfg,&state,&action));
    CHECK(!restarts && !starts && !writes && !config_writes && !action);
    CHECK(update_fixture_put(&update_fixture,2));
    strcpy_s(state.sn,sizeof(state.sn),"new-sn");
    strcpy_s(state.thumbprint,sizeof(state.thumbprint),"1111111111111111111111111111111111111111");
    CHECK(orchestrator_step(&cfg,&state,&action));
    CHECK(!restarts && !starts && !writes && !config_writes && !action);
    service_running=false;CHECK(!orchestrator_step(&cfg,&state,&action));CHECK(!starts && !writes && !config_writes);
    service_running=true;probe_ok=false;CHECK(!orchestrator_step(&cfg,&state,&action));CHECK(!restarts && !starts);
    test_drain(&update_fixture);CHECK(update_fixture_dispose(&update_fixture));CHECK(!orchestrator_step(&cfg,&state,&action));
    state_cleanup(&state);g_update_consumer.enabled=false;
    puts("Persistent update supervisor: missing/window1 no writes or repair; window2 identity/SCM observation only PASS");
    printf("Identity transition failures: %d\n", failures);
    return failures ? 1 : 0;
}
