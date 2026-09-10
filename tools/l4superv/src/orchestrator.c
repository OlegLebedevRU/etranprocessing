#include "orchestrator.h"
#include "hardware_fingerprint.h"
#include "service_mgr.h"
#include "mosquitto_conf.h"
#include "proxy_client.h"
#include "session_proc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <shlwapi.h>

#pragma comment(lib, "shlwapi.lib")

static PROCESS_INFORMATION g_l4desk_pi = { 0 };
static HANDLE               g_l4desk_job = NULL;
static DWORD                g_l4desk_session = 0;
static time_t              g_l4desk_last_start_attempt = 0;
static int                 g_l4desk_backoff_sec = 5;

static bool json_extract_str(const char* json, const char* key, char* out, size_t out_size) {
    if (!json || !key || !out || out_size == 0) return false;
    char search[128];
    sprintf_s(search, sizeof(search), "\"%s\"", key);
    const char* p = strstr(json, search);
    if (!p) return false;
    p += strlen(search);
    while (*p && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' || *p == ':')) p++;
    if (*p != '\"') return false;
    p++;
    size_t i = 0;
    while (*p && *p != '\"' && i < out_size - 1) {
        out[i++] = *p++;
    }
    out[i] = '\0';
    return true;
}

static bool json_extract_int(const char* json, const char* key, int* out_val) {
    if (!json || !key || !out_val) return false;
    char search[128];
    sprintf_s(search, sizeof(search), "\"%s\"", key);
    const char* p = strstr(json, search);
    if (!p) return false;
    p += strlen(search);
    while (*p && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' || *p == ':')) p++;
    *out_val = atoi(p);
    return true;
}

bool orchestrator_get_l4desk_status(DWORD* out_pid, DWORD* out_session) {
    if (out_pid) *out_pid = 0;
    if (out_session) *out_session = 0;

    if (sp_is_alive(g_l4desk_pi.hProcess)) {
        if (out_pid) *out_pid = g_l4desk_pi.dwProcessId;
        DWORD sid = 0;
        ProcessIdToSessionId(g_l4desk_pi.dwProcessId, &sid);
        if (out_session) *out_session = sid ? sid : g_l4desk_session;
        return true;
    } else if (g_l4desk_pi.hProcess || g_l4desk_job) {
        sp_stop(&g_l4desk_pi, &g_l4desk_job, NULL, 0);
        g_l4desk_session = 0;
    }
    return false;
}

bool orchestrator_get_ffmpeg_status(const wchar_t* base_path, FFmpegStatus* out_status) {
    if (!out_status) return false;
    memset(out_status, 0, sizeof(FFmpegStatus));

    wchar_t state_file[MAX_PATH];
    swprintf_s(state_file, MAX_PATH, L"%ls\\l4desk\\state\\ffmpeg_state.json",
               (base_path && base_path[0]) ? base_path : L"C:\\l4tools");

    if (!PathFileExistsW(state_file)) {
        return false;
    }

    FILE* f = NULL;
    if (_wfopen_s(&f, state_file, L"rb") != 0 || !f) {
        return false;
    }

    char buf[4096] = { 0 };
    size_t bytes_read = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    if (bytes_read == 0) {
        return false;
    }
    buf[bytes_read] = '\0';

    int pid = 0;
    if (json_extract_int(buf, "pid", &pid)) {
        out_status->pid = (DWORD)pid;
    }
    json_extract_str(buf, "state", out_status->state, sizeof(out_status->state));
    json_extract_str(buf, "stream_instance_id", out_status->stream_instance_id, sizeof(out_status->stream_instance_id));

    if (out_status->pid > 0) {
        HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, out_status->pid);
        if (!hProc) {
            hProc = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, out_status->pid);
        }
        if (hProc) {
            DWORD exitCode = 0;
            if (GetExitCodeProcess(hProc, &exitCode) && exitCode == STILL_ACTIVE) {
                out_status->is_active = true;
            }
            CloseHandle(hProc);
        }
    }

    return true;
}

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

            if (sp_is_alive(g_l4desk_pi.hProcess)) {
                sp_stop(&g_l4desk_pi, &g_l4desk_job, NULL, 8000);
                g_l4desk_session = 0;
            }

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

                // l4desk also caches SN on start; restart it
                if (sp_is_alive(g_l4desk_pi.hProcess)) {
                    wchar_t stop_evt[128];
                    swprintf_s(stop_evt, 128, L"Global\\L4Desk_Stop_%hs", state->sn);
                    sp_stop(&g_l4desk_pi, &g_l4desk_job, stop_evt, 8000);
                    g_l4desk_session = 0;
                }
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
        if (cfg->auto_start_l4desk) {
            // Only start l4desk when state is ACTIVE and SN is resolved
            if (strcmp(state->status, "active") == 0 && state->sn[0] != '\0') {
                DWORD active_session = sp_get_active_console_session();
                if (active_session == 0) {
                    if (sp_is_alive(g_l4desk_pi.hProcess)) {
                        log_info("[WATCHDOG] No active console session found. Stopping l4desk...");
                        wchar_t stop_evt[128];
                        swprintf_s(stop_evt, 128, L"Global\\L4Desk_Stop_%hs", state->sn);
                        sp_stop(&g_l4desk_pi, &g_l4desk_job, stop_evt, 8000);
                        g_l4desk_session = 0;
                    }
                } else {
                    if (sp_is_alive(g_l4desk_pi.hProcess)) {
                        DWORD proc_session = 0;
                        ProcessIdToSessionId(g_l4desk_pi.dwProcessId, &proc_session);
                        if (proc_session != active_session) {
                            log_info("[WATCHDOG] Active console session changed (%lu -> %lu). Re-launching l4desk...",
                                     proc_session, active_session);
                            wchar_t stop_evt[128];
                            swprintf_s(stop_evt, 128, L"Global\\L4Desk_Stop_%hs", state->sn);
                            sp_stop(&g_l4desk_pi, &g_l4desk_job, stop_evt, 8000);
                            g_l4desk_session = 0;
                        }
                    }

                    if (!sp_is_alive(g_l4desk_pi.hProcess)) {
                        time_t now = time(NULL);
                        if (now - g_l4desk_last_start_attempt >= g_l4desk_backoff_sec) {
                            g_l4desk_last_start_attempt = now;
                            wchar_t l4desk_exe[MAX_PATH];
                            swprintf_s(l4desk_exe, MAX_PATH, L"%ls\\l4desk\\l4desk.exe", cfg->base_path);
                            if (!PathFileExistsW(l4desk_exe)) {
                                swprintf_s(l4desk_exe, MAX_PATH, L"%ls\\l4desk\\x86\\l4desk.exe", cfg->base_path);
                                if (!PathFileExistsW(l4desk_exe)) {
                                    swprintf_s(l4desk_exe, MAX_PATH, L"%ls\\l4desk\\x64\\l4desk.exe", cfg->base_path);
                                }
                            }

                            if (PathFileExistsW(l4desk_exe)) {
                                wchar_t cmdline[1024];
                                swprintf_s(cmdline, 1024, L"\"%ls\" %ls", l4desk_exe,
                                           cfg->l4desk_args[0] ? cfg->l4desk_args : L"--run --presence-interval 30");
                                wchar_t workdir[MAX_PATH];
                                swprintf_s(workdir, MAX_PATH, L"%ls\\l4desk", cfg->base_path);

                                log_info("[WATCHDOG] Launching l4desk in active console session %lu...", active_session);
                                sp_enable_system_privileges();
                                if (sp_start_in_session(active_session, l4desk_exe, cmdline, workdir, &g_l4desk_pi, &g_l4desk_job)) {
                                    log_info("[WATCHDOG] l4desk started in session %lu (PID: %lu, Job: %p)",
                                             active_session, g_l4desk_pi.dwProcessId, g_l4desk_job);
                                    g_l4desk_session = active_session;
                                    g_l4desk_backoff_sec = 5;
                                    if (p_action_taken) *p_action_taken = true;
                                } else {
                                    log_info("[WARN] Failed to start l4desk in session %lu (err=%lu)", active_session, GetLastError());
                                    g_l4desk_backoff_sec = (g_l4desk_backoff_sec < 30) ? (g_l4desk_backoff_sec == 5 ? 10 : 30) : 30;
                                }
                            }
                        }
                    }
                }
            } else {
                if (sp_is_alive(g_l4desk_pi.hProcess)) {
                    log_info("[WATCHDOG] Terminal state not active. Stopping l4desk...");
                    sp_stop(&g_l4desk_pi, &g_l4desk_job, NULL, 8000);
                    g_l4desk_session = 0;
                }
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

    if (sp_is_alive(g_l4desk_pi.hProcess)) {
        wchar_t stop_evt[128];
        swprintf_s(stop_evt, 128, L"Global\\L4Desk_Stop_%hs", state.sn);
        sp_stop(&g_l4desk_pi, &g_l4desk_job, stop_evt, 8000);
        g_l4desk_session = 0;
    }

    log_info("l4superv supervisor loop stopped.");
}
