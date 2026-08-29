#include "orchestrator.h"
#include "hardware_fingerprint.h"
#include "service_mgr.h"
#include "mosquitto_conf.h"
#include "proxy_client.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void log_info(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    char buf[1024];
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    SYSTEMTIME st;
    GetLocalTime(&st);
    printf("[%04d-%02d-%02d %02d:%02d:%02d] %s\n",
           st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, buf);
    fflush(stdout);
}

bool orchestrator_step(const L4SupervConfig* cfg, L4State* state, bool* p_action_taken) {
    if (!cfg || !state) return false;
    if (p_action_taken) *p_action_taken = false;

    // 1. Hardware Fingerprint & Clone Detection
    char current_fp[128] = { 0 };
    hw_get_fingerprint(current_fp, sizeof(current_fp));

    if (state->hw_fingerprint[0] != '\0' && current_fp[0] != '\0' &&
        strcmp(state->hw_fingerprint, current_fp) != 0) {
        
        log_info("[WARN] Hardware fingerprint mismatch detected! (Old: %s, Current: %s)",
                 state->hw_fingerprint, current_fp);

        if (cfg->auto_reset_on_clone) {
            log_info("[WARN] Auto-reset on clone is enabled. Cleaning certificates and resetting to Standby...");
            hw_clean_terminal_certificates();
            
            state_init(state);
            strcpy_s(state->hw_fingerprint, sizeof(state->hw_fingerprint), current_fp);
            state_update_services(cfg->base_path, state);
            state_save(cfg->base_path, state);

            mosquitto_conf_generate_standby(cfg->base_path, cfg->mosquitto_port);
            svc_restart(SVC_NAME_MOSQUITTO);
            svc_restart(SVC_NAME_LEO4PROXY);
            svc_restart(SVC_NAME_L4CON);

            if (p_action_taken) *p_action_taken = true;
            return true;
        }
    } else if (state->hw_fingerprint[0] == '\0' && current_fp[0] != '\0') {
        strcpy_s(state->hw_fingerprint, sizeof(state->hw_fingerprint), current_fp);
        state_update_services(cfg->base_path, state);
        state_save(cfg->base_path, state);
    }

    // 2. Query Leo4Proxy
    Leo4ProxyInfo proxy_info;
    bool query_ok = proxy_client_query_info(cfg->proxy_url, 3000, &proxy_info);
    state->last_check = time(NULL);

    // 3. Handle Certificate Status Transitions
    if (query_ok && proxy_info.cert_ready && proxy_info.sn[0] != '\0') {
        // --- Certificate is active & valid ---
        bool sn_changed = (strcmp(state->sn, proxy_info.sn) != 0);
        bool thumbprint_changed = (strcmp(state->thumbprint, proxy_info.thumbprint) != 0);
        bool was_standby = (strcmp(state->status, "active") != 0);

        if (was_standby || sn_changed) {
            log_info("[STATE] Transitioning to ACTIVE (SN: %s, Thumbprint: %.8s...)",
                     proxy_info.sn, proxy_info.thumbprint);

            mosquitto_conf_generate_active(cfg->base_path, cfg->mosquitto_port, proxy_info.sn, cfg->mosquitto_template_path);
            svc_restart(SVC_NAME_MOSQUITTO);
            
            if (sn_changed) {
                // l4con caches SN on start; restart it to fetch new SN
                svc_restart(SVC_NAME_L4CON);
            }

            strcpy_s(state->status, sizeof(state->status), "active");
            strcpy_s(state->sn, sizeof(state->sn), proxy_info.sn);
            strcpy_s(state->thumbprint, sizeof(state->thumbprint), proxy_info.thumbprint);
            strcpy_s(state->not_after, sizeof(state->not_after), proxy_info.not_after);
            state->updated_at = time(NULL);
            state_update_services(cfg->base_path, state);
            state_save(cfg->base_path, state);

            if (p_action_taken) *p_action_taken = true;

        } else if (thumbprint_changed) {
            // Certificate renewed with the same SN
            log_info("[STATE] Certificate renewed with same SN: %s (Thumbprint: %.8s...)",
                     proxy_info.sn, proxy_info.thumbprint);

            // Restart mosquitto to reconnect TLS bridge to updated proxy
            svc_restart(SVC_NAME_MOSQUITTO);

            strcpy_s(state->thumbprint, sizeof(state->thumbprint), proxy_info.thumbprint);
            strcpy_s(state->not_after, sizeof(state->not_after), proxy_info.not_after);
            state->updated_at = time(NULL);
            state_update_services(cfg->base_path, state);
            state_save(cfg->base_path, state);

            if (p_action_taken) *p_action_taken = true;
        } else {
            // Check if active mosquitto.conf was accidentally corrupted or overwritten
            if (!mosquitto_conf_is_active_with_sn(cfg->base_path, proxy_info.sn)) {
                log_info("[REPAIR] Active mosquitto.conf was missing/corrupted. Regenerating for SN: %s...", proxy_info.sn);
                mosquitto_conf_generate_active(cfg->base_path, cfg->mosquitto_port, proxy_info.sn, cfg->mosquitto_template_path);
                svc_restart(SVC_NAME_MOSQUITTO);
                if (p_action_taken) *p_action_taken = true;
            }
        }
    } else {
        // --- Certificate is missing or proxy in standby ---
        bool was_active = (strcmp(state->status, "active") == 0);
        bool conf_in_standby = mosquitto_conf_is_standby(cfg->base_path);

        if (was_active || !conf_in_standby) {
            log_info("[STATE] Standby mode: no certificate loaded in leo4proxy. Configuring local Mosquitto...");
            
            mosquitto_conf_generate_standby(cfg->base_path, cfg->mosquitto_port);
            svc_restart(SVC_NAME_MOSQUITTO);

            strcpy_s(state->status, sizeof(state->status), "standby");
            state->sn[0] = '\0';
            state->thumbprint[0] = '\0';
            state->not_after[0] = '\0';
            state->updated_at = time(NULL);
            state_update_services(cfg->base_path, state);
            state_save(cfg->base_path, state);

            if (p_action_taken) *p_action_taken = true;
        }
    }

    // 4. Watchdog Process & Path Enforcement
    if (cfg->watchdog_enabled) {
        if (cfg->auto_start_leo4proxy) {
            if (!svc_exists(SVC_NAME_LEO4PROXY) || !svc_is_running(SVC_NAME_LEO4PROXY)) {
                log_info("[WATCHDOG] %ls is stopped or missing. Starting...", SVC_NAME_LEO4PROXY);
                svc_start(SVC_NAME_LEO4PROXY);
                if (p_action_taken) *p_action_taken = true;
            }
        }
        if (cfg->auto_start_mosquitto) {
            if (!svc_exists(SVC_NAME_MOSQUITTO) || !svc_is_running(SVC_NAME_MOSQUITTO)) {
                log_info("[WATCHDOG] %ls is stopped or missing. Starting...", SVC_NAME_MOSQUITTO);
                svc_start(SVC_NAME_MOSQUITTO);
                if (p_action_taken) *p_action_taken = true;
            }
        }
        if (cfg->auto_start_l4con) {
            if (!svc_exists(SVC_NAME_L4CON) || !svc_is_running(SVC_NAME_L4CON)) {
                log_info("[WATCHDOG] %ls is stopped or missing. Starting...", SVC_NAME_L4CON);
                svc_start(SVC_NAME_L4CON);
                if (p_action_taken) *p_action_taken = true;
            }
        }
    }

    // Always keep services state updated in state.json
    state_update_services(cfg->base_path, state);
    state_save(cfg->base_path, state);

    return true;
}

void orchestrator_run_loop(const L4SupervConfig* cfg, volatile bool* p_stop_flag) {
    if (!cfg) return;

    log_info("=======================================================");
    log_info(" l4superv (Leo4 Supervisor & Watchdog) Started");
    log_info(" Base Directory: %ls", cfg->base_path);
    log_info(" Poll Interval:  %d sec", cfg->poll_interval_sec);
    log_info(" Watchdog:       %s", cfg->watchdog_enabled ? "Enabled" : "Disabled");
    log_info("=======================================================");

    L4State state;
    state_load(cfg->base_path, &state);

    int poll_countdown = 0;

    while (!p_stop_flag || !(*p_stop_flag)) {
        if (poll_countdown <= 0) {
            bool action = false;
            orchestrator_step(cfg, &state, &action);
            poll_countdown = (cfg->poll_interval_sec > 0) ? cfg->poll_interval_sec : 15;
        }

        Sleep(1000);
        poll_countdown--;
    }

    log_info("l4superv supervisor loop stopped.");
}
