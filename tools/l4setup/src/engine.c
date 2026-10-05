#include "engine.h"
#include "log.h"
#include "version.h"
#include "preflight.h"
#include "drainage.h"
#include "unpack.h"
#include "cert_phase.h"
#include "smoke.h"
#include "summary.h"
#include "proxy_probe.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char* engine_op_type_to_str(SetupOperationType op) {
    switch (op) {
        case OP_INSTALL: return "Install";
        case OP_UPGRADE: return "Upgrade";
        case OP_REPAIR:  return "Repair";
        case OP_VERIFY:  return "Verify";
        default:         return "Install";
    }
}

const char* engine_phase_to_str(SetupPhase phase) {
    switch (phase) {
        case SETUP_PHASE_CHECK:   return "Check";
        case SETUP_PHASE_PREPARE: return "Prepare";
        case SETUP_PHASE_STOP:    return "Stop";
        case SETUP_PHASE_UPDATE:  return "Update";
        case SETUP_PHASE_START:   return "Start";
        case SETUP_PHASE_VERIFY:  return "Verify";
        case SETUP_PHASE_FINISH:  return "Finish";
        default:                  return "Unknown";
    }
}

static void engine_write_summary(SetupContext* ctx) {
    ctx->summary.exit_code = ctx->final_exit_code;
    const wchar_t* names[] = { SVC_NAME_LEO4PROXY, SVC_NAME_MOSQUITTO, SVC_NAME_L4CON, SVC_NAME_L4SUPERV };
    char* values[] = { ctx->summary.service_leo4proxy, ctx->summary.service_mosquitto,
                      ctx->summary.service_l4con, ctx->summary.service_l4superv };
    for (int i = 0; i < 4; ++i) {
        DWORD state = services_query_status(names[i]);
        const char* value = state == SERVICE_RUNNING ? "running" :
            state == SERVICE_STOPPED ? "stopped" : state == SERVICE_START_PENDING ? "starting" :
            state == SERVICE_STOP_PENDING ? "stopping" : "unknown";
        strcpy_s(values[i], 32, value);
    }
    for (int i = 0; i < ctx->summary.cert.warnings_count; ++i)
        summary_add_warning(&ctx->summary, ctx->summary.cert.warnings[i]);
    summary_write_json(&ctx->summary, ctx->opts->dest);
}

static void rollback_failed_upgrade(SetupContext* ctx) {
    if(ctx->op_type!=OP_UPGRADE && ctx->op_type!=OP_REPAIR)return;
    if(!ctx->installed_version[0])return;
    strcpy_s(ctx->summary.rollback,sizeof(ctx->summary.rollback),"failed");
    /* Fence the new supervisor before restoring configuration and executables. */
    if(!services_stop_all_in_order(ctx->sn,NULL,NULL))return;
    if(!unpack_rollback(ctx->opts->dest,ctx->installed_version))return;
    if(!services_start_all_in_order(NULL,NULL))return;
    strcpy_s(ctx->summary.rollback,sizeof(ctx->summary.rollback),"restored");
}

static bool wait_proxy_after_start(bool require_ready) {
    ULONGLONG deadline = GetTickCount64() + 15000;
    do {
        ULONGLONG now = GetTickCount64();
        if (now >= deadline) break;
        int remaining = (int)(deadline - now);
        if (setup_proxy_probe(18443, require_ready, remaining < 1200 ? remaining : 1200)) return true;
        now=GetTickCount64();if(now<deadline)Sleep((DWORD)(deadline-now<200?deadline-now:200));
    } while (GetTickCount64() < deadline);
    return false;
}

static int service_name_to_idx(const wchar_t* svc_name) {
    if (_wcsicmp(svc_name, SVC_NAME_LEO4PROXY) == 0) return 0;
    if (_wcsicmp(svc_name, SVC_NAME_MOSQUITTO) == 0) return 1;
    if (_wcsicmp(svc_name, SVC_NAME_L4CON) == 0)     return 2;
    if (_wcsicmp(svc_name, SVC_NAME_L4SUPERV) == 0)  return 3;
    return 0;
}

static void engine_service_lifecycle_cb(
    const wchar_t* svc_name,
    ServiceLifecycleStatus status,
    DWORD elapsed_sec,
    const char* notice,
    void* user_data
) {
    SetupContext* ctx = (SetupContext*)user_data;
    if (!ctx) return;

    int idx = service_name_to_idx(svc_name);
    if (ctx->on_service_status) {
        ctx->on_service_status(idx, svc_name, status, elapsed_sec, notice, ctx->user_data);
    }
    if (notice && ctx->on_notice) {
        ctx->on_notice(notice, ctx->user_data);
    }

    // Update summary service status
    const char* status_str = "pending";
    switch (status) {
        case SVC_STATUS_RUNNING:  status_str = "running"; break;
        case SVC_STATUS_STOPPED:  status_str = "stopped"; break;
        case SVC_STATUS_STARTING: status_str = "starting"; break;
        case SVC_STATUS_STOPPING: status_str = "stopping"; break;
        case SVC_STATUS_FAILED:   status_str = "failed"; break;
        default: break;
    }

    if (idx == 0) strncpy_s(ctx->summary.service_leo4proxy, 32, status_str, _TRUNCATE);
    else if (idx == 1) strncpy_s(ctx->summary.service_mosquitto, 32, status_str, _TRUNCATE);
    else if (idx == 2) strncpy_s(ctx->summary.service_l4con, 32, status_str, _TRUNCATE);
    else if (idx == 3) strncpy_s(ctx->summary.service_l4superv, 32, status_str, _TRUNCATE);
}

bool engine_phase_check(SetupContext* ctx) {
    if (!ctx || !ctx->opts) return false;

    log_info("=== Phase 1: Check ===");
    if (ctx->on_phase_change) {
        ctx->on_phase_change(SETUP_PHASE_CHECK, "Check", "Checking system environment and prerequisites...", ctx->user_data);
    }

    // 1. Single-instance mutex
    ctx->hMutex = CreateMutexW(NULL, TRUE, L"Global\\L4Setup_Instance_Mutex");
    if (ctx->hMutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        log_err("Another instance of l4setup is already running. Setup is busy (code 28).");
        ctx->final_exit_code = 28;
        strcpy_s(ctx->summary.status, sizeof(ctx->summary.status), "failed");
        strncpy_s(ctx->summary.error_reason, sizeof(ctx->summary.error_reason), "setup_busy", _TRUNCATE);
        return false;
    }

    // 2. Preflight OS check
    PreflightInfo pinfo;
    memset(&pinfo, 0, sizeof(pinfo));
    if (!preflight_check(&pinfo)) {
        log_err("Preflight OS check failed. Unsupported OS version (code 21).");
        ctx->final_exit_code = 21;
        strcpy_s(ctx->summary.status, sizeof(ctx->summary.status), "failed");
        strncpy_s(ctx->summary.error_reason, sizeof(ctx->summary.error_reason), "unsupported_os", _TRUNCATE);
        return false;
    }
    strncpy_s(ctx->os_name, sizeof(ctx->os_name), pinfo.os_display_name, _TRUNCATE);
    strncpy_s(ctx->target_arch, sizeof(ctx->target_arch), pinfo.target_arch, _TRUNCATE);

    // 3. Crash recovery check
    if (!ctx->opts->smoke_only) unpack_recover_from_crash(ctx->opts->dest);

    // 4. Version detection
    unpack_read_installed_version(ctx->opts->dest, ctx->installed_version, sizeof(ctx->installed_version));
    strncpy_s(ctx->target_version, sizeof(ctx->target_version), L4SETUP_VERSION_STRING, _TRUNCATE);

    if (ctx->opts->smoke_only) {
        ctx->op_type = OP_VERIFY;
    } else if (ctx->installed_version[0] != '\0') {
        int cmp = version_compare(ctx->installed_version, ctx->target_version);
        if (cmp > 0) {
            log_err("Installed version (%s) is newer than package (%s). Downgrade blocked (code 29).",
                    ctx->installed_version, ctx->target_version);
            ctx->downgrade_blocked = true;
            ctx->final_exit_code = 29;
            strcpy_s(ctx->summary.status, sizeof(ctx->summary.status), "failed");
            strncpy_s(ctx->summary.error_reason, sizeof(ctx->summary.error_reason), "downgrade_blocked", _TRUNCATE);
            return false;
        } else if (cmp == 0) {
            if (ctx->opts->repair || ctx->opts->network_specified) {
                ctx->op_type = OP_REPAIR;
            } else if (unpack_is_idempotent(ctx->opts->dest, ctx->target_version)) {
                ctx->op_type = OP_VERIFY;
            } else {
                ctx->op_type = OP_REPAIR;
            }
        } else {
            ctx->op_type = OP_UPGRADE;
        }
    } else {
        ctx->op_type = OP_INSTALL;
    }

    log_info("Detected operation: %s (Installed: %s, Target: %s)",
             engine_op_type_to_str(ctx->op_type),
             ctx->installed_version[0] ? ctx->installed_version : "none",
             ctx->target_version);

    // 5. Disk space check (>= 100 MB headroom)
    ULARGE_INTEGER free_bytes, total_bytes, total_free;
    if (GetDiskFreeSpaceExW(ctx->opts->dest, &free_bytes, &total_bytes, &total_free) ||
        GetDiskFreeSpaceExW(L"C:\\", &free_bytes, &total_bytes, &total_free)) {
        if (!ctx->opts->smoke_only && free_bytes.QuadPart < 104857600ULL) { // 100 MB
            log_err("Insufficient disk space on target drive (headroom < 100 MB required).");
            ctx->disk_space_ok = false;
            ctx->final_exit_code = 30;
            strcpy_s(ctx->summary.status, sizeof(ctx->summary.status), "failed");
            strncpy_s(ctx->summary.error_reason, sizeof(ctx->summary.error_reason), "insufficient_disk_space", _TRUNCATE);
            return false;
        }
        ctx->disk_space_ok = true;
    }

    // 6. Check pending reboot
    HKEY hKey = NULL;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\Session Manager", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        DWORD dwType = 0;
        if (RegQueryValueExW(hKey, L"PendingFileRenameOperations", NULL, &dwType, NULL, NULL) == ERROR_SUCCESS) {
            ctx->pending_reboot = true;
            log_warn("PendingFileRenameOperations detected. System reboot is recommended.");
            summary_add_warning(&ctx->summary, "pending_reboot_detected");
        }
        RegCloseKey(hKey);
    }

    // 7. Install/verify trusted Root CA certificate
    if (!ctx->opts->smoke_only && install_root_ca_certificate_ex(ctx->opts->dest)) {
        ctx->summary.ca_root_installed = true;
    }

    // 8. Check certificate & SN
    cert_info ci;
    memset(&ci, 0, sizeof(ci));
    cert_state st = cert_discover(NULL, &ci);
    if (ci.sn[0]) {
        strncpy_s(ctx->sn, sizeof(ctx->sn), ci.sn, _TRUNCATE);
    }
    log_info("Initial certificate discovery state: %s, SN: %s", cert_state_to_str(st), ctx->sn[0] ? ctx->sn : "none");
    if (ctx->opts->smoke_only) {
        ctx->summary.cert.state = st;
        ctx->summary.cert.reused = st == CERT_VALID || st == CERT_EXPIRING;
        ctx->summary.cert.exit_code = st==CERT_STORE_ERROR ? 20 : ctx->summary.cert.reused ? 0 : 10;
        strcpy_s(ctx->summary.cert.status,sizeof(ctx->summary.cert.status),st==CERT_STORE_ERROR?"failed":ctx->summary.cert.reused?"ready":"activation_required");
        ctx->summary.cert.days_left = ci.days_left;
        strcpy_s(ctx->summary.cert.sn, sizeof(ctx->summary.cert.sn), ci.sn);
        strcpy_s(ctx->summary.cert.thumbprint, sizeof(ctx->summary.cert.thumbprint), ci.thumbprint_hex);
        strcpy_s(ctx->summary.cert.not_after, sizeof(ctx->summary.cert.not_after), ci.not_after_utc);
    }

    // Initialize summary data
    strncpy_s(ctx->summary.installer_version, sizeof(ctx->summary.installer_version), L4SETUP_VERSION_STRING, _TRUNCATE);
    strncpy_s(ctx->summary.installed_version, sizeof(ctx->summary.installed_version), ctx->installed_version, _TRUNCATE);
    strncpy_s(ctx->summary.target_version, sizeof(ctx->summary.target_version), ctx->target_version, _TRUNCATE);
    strncpy_s(ctx->summary.os, sizeof(ctx->summary.os), ctx->os_name, _TRUNCATE);
    strncpy_s(ctx->summary.target_arch, sizeof(ctx->summary.target_arch), ctx->target_arch, _TRUNCATE);
    wcscpy_s(ctx->summary.dest, MAX_PATH, ctx->opts->dest);
    log_get_path(ctx->summary.log_path, MAX_PATH);

    if (ctx->on_phase_change) {
        char status_msg[128];
        snprintf(status_msg, sizeof(status_msg), "Ready to %s %s.", engine_op_type_to_str(ctx->op_type), ctx->target_version);
        ctx->on_phase_change(SETUP_PHASE_CHECK, "Check", status_msg, ctx->user_data);
    }

    return true;
}

int engine_run_pipeline(SetupContext* ctx) {
    if (!ctx || !ctx->opts) return 1;

    // Check cancellation
    if (ctx->cancel_requested) {
        log_warn("Setup cancelled by user prior to execution.");
        ctx->final_exit_code = 31;
        strcpy_s(ctx->summary.status, sizeof(ctx->summary.status), "cancelled");
        engine_write_summary(ctx);
        return 31;
    }

    if (ctx->opts->smoke_only) goto verify_only;
    /* GUI network settings can be selected after Check has chosen Verify. */
    if(ctx->opts->network_specified && ctx->op_type==OP_VERIFY)ctx->op_type=OP_REPAIR;

    // ------------------------------------------------------------------------
    // Phase 2: Prepare
    // ------------------------------------------------------------------------
    log_info("=== Phase 2: Prepare ===");
    strncpy_s(ctx->summary.phase, sizeof(ctx->summary.phase), "prepare", _TRUNCATE);
    if (ctx->on_phase_change) {
        ctx->on_phase_change(SETUP_PHASE_PREPARE, "Prepare", "Configuring environment and staging payload...", ctx->user_data);
    }

    services_configure_environment(ctx->opts->dest);
    if (preflight_setup_firewall(ctx->opts->dest)) {
        ctx->summary.firewall_configured = true;
    }

    if (install_root_ca_certificate_ex(ctx->opts->dest)) {
        ctx->summary.ca_root_installed = true;
    }

    PreflightInfo pinfo;
    if (preflight_check(&pinfo) && pinfo.is_win7) {
        preflight_configure_win7_tls12();
    }

    if (ctx->cancel_requested) {
        ctx->final_exit_code = 31;
        strcpy_s(ctx->summary.status, sizeof(ctx->summary.status), "cancelled");
        engine_write_summary(ctx);
        return 31;
    }

    // ------------------------------------------------------------------------
    // Phase 3: Stop
    // ------------------------------------------------------------------------
    if (ctx->op_type != OP_VERIFY) {
        log_info("=== Phase 3: Stop ===");
        strncpy_s(ctx->summary.phase, sizeof(ctx->summary.phase), "stop", _TRUNCATE);
        if (ctx->on_phase_change) {
            ctx->on_phase_change(SETUP_PHASE_STOP, "Stop", "Stopping services and draining connections...", ctx->user_data);
        }

        if (!drainage_execute(ctx->opts->dest, ctx->sn[0] ? ctx->sn : NULL, ctx->opts->silent, &ctx->summary.drainage)) {
            log_err("Drainage phase failed (exit code 22).");
            ctx->final_exit_code = 22;
            strcpy_s(ctx->summary.status, sizeof(ctx->summary.status), "failed");
            strncpy_s(ctx->summary.error_reason, sizeof(ctx->summary.error_reason), "drainage_failed", _TRUNCATE);
            engine_write_summary(ctx);
            return 22;
        }
    }

    if (ctx->cancel_requested) {
        ctx->final_exit_code = 31;
        strcpy_s(ctx->summary.status, sizeof(ctx->summary.status), "cancelled");
        engine_write_summary(ctx);
        return 31;
    }

    // ------------------------------------------------------------------------
    // Phase 4: Update
    // ------------------------------------------------------------------------
    if (ctx->op_type != OP_VERIFY) {
        log_info("=== Phase 4: Update ===");
        strncpy_s(ctx->summary.phase, sizeof(ctx->summary.phase), "update", _TRUNCATE);
        if (ctx->on_phase_change) {
            ctx->on_phase_change(SETUP_PHASE_UPDATE, "Update", "Unpacking files and registering services...", ctx->user_data);
        }

        const wchar_t* dev_payload = ctx->opts->payload_dir_specified ? ctx->opts->payload_dir : NULL;
        if (!unpack_payload(ctx->opts->dest, ctx->target_arch, dev_payload, ctx->target_version)) {
            log_err("Payload extraction or file swap failed (exit code 23).");
            ctx->final_exit_code = 23;
            strcpy_s(ctx->summary.status, sizeof(ctx->summary.status), "failed");
            strncpy_s(ctx->summary.error_reason, sizeof(ctx->summary.error_reason), "payload_extraction_failed", _TRUNCATE);
            engine_write_summary(ctx);
            return 23;
        }

        services_set_network_options(ctx->opts);
        if (!services_ensure_all_registered(ctx->opts->dest)) {
            log_err("Service registration failed (exit code 24). Initiating rollback...");
            unpack_rollback(ctx->opts->dest, ctx->installed_version[0] ? ctx->installed_version : "prev");
            ctx->final_exit_code = 24;
            strcpy_s(ctx->summary.status, sizeof(ctx->summary.status), "failed");
            strncpy_s(ctx->summary.rollback, sizeof(ctx->summary.rollback), "restored", _TRUNCATE);
            strncpy_s(ctx->summary.error_reason, sizeof(ctx->summary.error_reason), "service_registration_failed", _TRUNCATE);
            engine_write_summary(ctx);
            return 24;
        }
    }

    if (ctx->cancel_requested) {
        ctx->final_exit_code = 31;
        strcpy_s(ctx->summary.status, sizeof(ctx->summary.status), "cancelled");
        engine_write_summary(ctx);
        return 31;
    }

    // ------------------------------------------------------------------------
    // Phase 5: Start
    // ------------------------------------------------------------------------
    log_info("=== Phase 5: Start ===");
    strncpy_s(ctx->summary.phase, sizeof(ctx->summary.phase), "start", _TRUNCATE);
    if (ctx->on_phase_change) {
        ctx->on_phase_change(SETUP_PHASE_START, "Start", "Enrolling certificate and starting services in order...", ctx->user_data);
    }

    // Certificate Phase
    bool cert_cont = cert_phase_execute(ctx->opts->dest, ctx->opts, ctx->hInstance, &ctx->summary.cert);
    if (!cert_cont) {
        if (ctx->summary.cert.exit_code == 31) {
            ctx->final_exit_code = 31;
            strcpy_s(ctx->summary.status, sizeof(ctx->summary.status), "cancelled");
            engine_write_summary(ctx);
            return 31;
        }
        log_err("Certificate provisioning failed (exit code %d).", ctx->summary.cert.exit_code);
        ctx->final_exit_code = ctx->summary.cert.exit_code;
        strcpy_s(ctx->summary.status, sizeof(ctx->summary.status), "failed");
        strncpy_s(ctx->summary.error_reason, sizeof(ctx->summary.error_reason), "certificate_enrollment_failed", _TRUNCATE);
        engine_write_summary(ctx);
        return ctx->final_exit_code;
    }

    if (ctx->summary.cert.sn[0]) {
        strncpy_s(ctx->sn, sizeof(ctx->sn), ctx->summary.cert.sn, _TRUNCATE);
    }

    // A fresh package intentionally contains no terminal-specific bridge.
    // Prepare local-only bootstrap through the supervisor's existing generator.
    if (!services_prepare_mosquitto(ctx->opts->dest)) {
        ctx->final_exit_code = 24;
        strcpy_s(ctx->summary.status, sizeof(ctx->summary.status), "failed");
        strcpy_s(ctx->summary.error_reason, sizeof(ctx->summary.error_reason), "mosquitto_configuration_failed");
        rollback_failed_upgrade(ctx);
        engine_write_summary(ctx);
        return 24;
    }

    // Start all 4 services in order
    bool svcs_started = services_start_all_in_order(engine_service_lifecycle_cb, ctx);
    if (!svcs_started) {
        log_err("Service startup timed out or failed (exit code 24).");
        ctx->final_exit_code = 24;
        strcpy_s(ctx->summary.status, sizeof(ctx->summary.status), "failed");
        strncpy_s(ctx->summary.error_reason, sizeof(ctx->summary.error_reason), "service_start_failed", _TRUNCATE);
        rollback_failed_upgrade(ctx);
        engine_write_summary(ctx);
        return 24;
    }

    // ------------------------------------------------------------------------
    // Phase 6: Verify
    // ------------------------------------------------------------------------
verify_only:
    ; /* A label must precede a statement in C. */
    ULONGLONG verify_deadline=GetTickCount64()+60000;
    log_info("=== Phase 6: Verify ===");
    strncpy_s(ctx->summary.phase, sizeof(ctx->summary.phase), "verify", _TRUNCATE);
    if (ctx->on_phase_change) {
        ctx->on_phase_change(SETUP_PHASE_VERIFY, "Verify", "Running local and remote health probes (budget <= 60s)...", ctx->user_data);
    }

    bool is_active_cert = (ctx->summary.cert.state == CERT_VALID || ctx->summary.cert.state == CERT_EXPIRING);
    if(ctx->opts->smoke_only) log_info("Checking Leo4Proxy (single probe up to 1.2s, certificate required: %s)...",is_active_cert?"yes":"no");
    else log_info("Waiting for Leo4Proxy after service startup (up to 15s, certificate required: %s)...",is_active_cert?"yes":"no");
    if (!(ctx->opts->smoke_only ? setup_proxy_probe(18443, is_active_cert, 1200) : wait_proxy_after_start(is_active_cert))) {
        ctx->final_exit_code = 27;
        strcpy_s(ctx->summary.status, sizeof(ctx->summary.status), "failed");
        strcpy_s(ctx->summary.error_reason, sizeof(ctx->summary.error_reason), "proxy_ready_timeout");
        strcpy_s(ctx->summary.probes.proxy_info, sizeof(ctx->summary.probes.proxy_info), "fail");
        engine_write_summary(ctx);
        return 27;
    }
    smoke_run_probes(ctx->opts->dest, is_active_cert, &ctx->summary.probes);
    if (is_active_cert && !ctx->summary.probes.critical_failed) {
        bool local_warnings=ctx->summary.probes.has_warnings;
        int local_code=ctx->summary.probes.calculated_exit_code;
        for (int attempt=0;attempt<3;attempt++) {
            if (attempt) {
                if (GetTickCount64()+15500>verify_deadline) break;
                log_info("Upstream transport not ready; retrying after 5s for policy/network convergence...");
                ULONGLONG pause_until=GetTickCount64()+5000;
                while(!ctx->cancel_requested) {
                    ULONGLONG now=GetTickCount64();if(now>=pause_until)break;
                    Sleep((DWORD)(pause_until-now<100?pause_until-now:100));
                }
            }
            if(ctx->cancel_requested) {
                ctx->final_exit_code=31;strcpy_s(ctx->summary.status,sizeof(ctx->summary.status),"cancelled");
                engine_write_summary(ctx);return 31;
            }
            ctx->summary.probes.has_warnings=local_warnings;
            ctx->summary.probes.calculated_exit_code=local_code;
            smoke_probe_upstream(ctx->opts->dest,&ctx->summary.probes);
            bool retry=false, permanent=false;
            for(int c=0;c<4;c++) {
                const char* verdict=ctx->summary.probes.upstream_tls[c];
                if(!strcmp(verdict,"probe_failed") || !strcmp(verdict,"timeout"))retry=true;
                if(!strcmp(verdict,"cert_invalid") || !strcmp(verdict,"no_certificate"))permanent=true;
            }
            if(!retry || permanent)break;
        }
    }
    const wchar_t* required_services[]={SVC_NAME_LEO4PROXY,SVC_NAME_MOSQUITTO,SVC_NAME_L4CON,SVC_NAME_L4SUPERV};
    for (int n=0;n<4;n++) if (services_query_status(required_services[n])!=SERVICE_RUNNING) {
        ctx->summary.probes.critical_failed=true;
        ctx->summary.probes.calculated_exit_code=27;
        summary_add_warning(&ctx->summary,"required_service_not_running");
    }

    // Determine exit code and status
    bool upstream_failed = false;
    for (int channel = 0; channel < 4; ++channel) {
        const char* verdict = ctx->summary.probes.upstream_tls[channel];
        if (!strcmp(verdict,"cert_invalid") || !strcmp(verdict,"probe_failed") || !strcmp(verdict,"timeout") || !strcmp(verdict,"no_certificate")) upstream_failed = true;
    }
    if (ctx->summary.probes.critical_failed) {
        ctx->final_exit_code = 27;
        strcpy_s(ctx->summary.status, sizeof(ctx->summary.status), "failed");
        strncpy_s(ctx->summary.error_reason, sizeof(ctx->summary.error_reason), "critical_smoke_probe_failed", _TRUNCATE);
        rollback_failed_upgrade(ctx);
    } else if (ctx->summary.cert.exit_code == 20) {
        ctx->final_exit_code=20;
        strcpy_s(ctx->summary.status,sizeof(ctx->summary.status),"failed");
        strcpy_s(ctx->summary.error_reason,sizeof(ctx->summary.error_reason),"certificate_discovery_failed");
    } else if (ctx->summary.cert.exit_code == 10) {
        ctx->final_exit_code = 10;
        strcpy_s(ctx->summary.status, sizeof(ctx->summary.status), "activation_required");
    } else if (ctx->summary.cert.exit_code == 11) {
        ctx->final_exit_code = 11;
        strcpy_s(ctx->summary.status, sizeof(ctx->summary.status), "ready_for_online");
    } else if (ctx->summary.probes.calculated_exit_code == 12) {
        ctx->final_exit_code = 12;
        strcpy_s(ctx->summary.status, sizeof(ctx->summary.status), "degraded");
        strcpy_s(ctx->summary.error_reason, sizeof(ctx->summary.error_reason),
                 strcmp(ctx->summary.probes.network, "unreachable") == 0 ? "network_unreachable" :
                 upstream_failed ? "upstream_tls_failed" : "l4desk_or_upstream_not_ready");
    } else {
        ctx->final_exit_code = 0;
        strcpy_s(ctx->summary.status, sizeof(ctx->summary.status), "ready");
    }

    // ------------------------------------------------------------------------
    // Phase 7: Finish
    // ------------------------------------------------------------------------
    log_info("=== Phase 7: Finish ===");
    strncpy_s(ctx->summary.phase, sizeof(ctx->summary.phase), "finish", _TRUNCATE);
    ctx->summary.exit_code = ctx->final_exit_code;

    if (!ctx->opts->smoke_only && (ctx->final_exit_code == 0 || ctx->final_exit_code == 10 || ctx->final_exit_code == 11 || ctx->final_exit_code == 12)) {
        // Clear crash marker
        unpack_clear_incomplete_marker(ctx->opts->dest);

        // Update state.json with installed_version
        wchar_t sum_path[MAX_PATH];
        swprintf_s(sum_path, MAX_PATH, L"%ls\\install_summary.json", ctx->opts->dest);
        char saved_version[64] = { 0 };
        if (!state_patch_version(ctx->opts->dest, ctx->target_version, sum_path) ||
            !unpack_read_installed_version(ctx->opts->dest, saved_version, sizeof(saved_version)) ||
            strcmp(saved_version, ctx->target_version) != 0) {
            log_err("Failed to persist and verify installed_version in state.json.");
            ctx->final_exit_code = 31;
            ctx->summary.exit_code = 31;
            strcpy_s(ctx->summary.status, sizeof(ctx->summary.status), "failed");
            strcpy_s(ctx->summary.error_reason, sizeof(ctx->summary.error_reason),
                     "installed_version_persist_failed");
        } else {
            strcpy_s(ctx->summary.installed_version, sizeof(ctx->summary.installed_version), saved_version);
        }
    }

    engine_write_summary(ctx);

    log_info("Setup finished with status: %s (exit code %d).", ctx->summary.status, ctx->final_exit_code);

    if (ctx->on_phase_change) {
        char finish_msg[128];
        snprintf(finish_msg, sizeof(finish_msg), "Finished: %s (code %d)", ctx->summary.status, ctx->final_exit_code);
        ctx->on_phase_change(SETUP_PHASE_FINISH, "Finish", finish_msg, ctx->user_data);
    }

    if (ctx->on_pipeline_finish) {
        ctx->on_pipeline_finish(ctx->final_exit_code, ctx->summary.status, ctx->user_data);
    }

    return ctx->final_exit_code;
}
