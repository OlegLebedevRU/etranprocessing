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
static bool matching = true, service_running = true;
static cert_state fake_discover(const wchar_t* sn, cert_info* out) {
    (void)sn; memset(out, 0, sizeof(*out));
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
static bool fake_active(const wchar_t* path, const char* sn) { (void)path; (void)sn; return true; }

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
#include "../src/orchestrator.c"

#define CHECK(x) do { if (!(x)) { printf("FAIL identity %d: %s\n", __LINE__, #x); failures++; } } while (0)
static void setup(L4SupervConfig* cfg, L4State* state) {
    config_init_defaults(cfg, NULL);
    cfg->auto_reset_on_clone = false;
    cfg->watchdog_enabled = false;
    cfg->auto_start_l4desk = false;
    state_init(state);
    strcpy_s(state->status, sizeof(state->status), "active");
    strcpy_s(state->sn, sizeof(state->sn), "old-sn");
    strcpy_s(state->hw_fingerprint, sizeof(state->hw_fingerprint), "fixture");
    probe_ok = restart_ok = persist_ok = matching = service_running = true;
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
    printf("Identity transition failures: %d\n", failures);
    return failures ? 1 : 0;
}
