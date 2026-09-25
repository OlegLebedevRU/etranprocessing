#include "engine.h"
#include "log.h"
#include "version.h"
#include "preflight.h"
#include "drainage.h"
#include "unpack.h"
#include "cert_phase.h"
#include "smoke.h"
#include "summary.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool prepare_mosquitto_config(const wchar_t* dest) {
    wchar_t command[MAX_PATH * 3];
    swprintf_s(command, sizeof(command) / sizeof(command[0]),
        L"\"%ls\\l4superv\\l4superv.exe\" --prepare-mosquitto \"%ls\"", dest, dest);
    STARTUPINFOW startup = { 0 };
    PROCESS_INFORMATION process = { 0 };
    startup.cb = sizeof(startup);
    if (!CreateProcessW(NULL, command, NULL, NULL, FALSE, CREATE_NO_WINDOW,
                        NULL, dest, &startup, &process)) {
        log_err("Cannot run l4superv Mosquitto config preparation (error %lu)", GetLastError());
        return false;
    }
    DWORD wait_result = WaitForSingleObject(process.hProcess, 30000);
    if (wait_result == WAIT_TIMEOUT) {
        TerminateProcess(process.hProcess, 1);
        WaitForSingleObject(process.hProcess, INFINITE);
    }
    DWORD exit_code = 1;
    GetExitCodeProcess(process.hProcess, &exit_code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    if (wait_result != WAIT_OBJECT_0 || exit_code != 0) {
        log_err("l4superv Mosquitto config preparation failed (exit=%lu, wait=%lu)", exit_code, wait_result);
        return false;
    }
    return true;
}

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

static bool engine_continue_after_service_failure(const wchar_t* svc_name, void* user_data) {
    SetupContext* ctx = (SetupContext*)user_data;
    if (!ctx || !ctx->is_interactive) return false;
    wchar_t message[512];
    swprintf_s(message, sizeof(message) / sizeof(message[0]),
        L"%ls did not reach RUNNING after the initial attempt and two retries.\n\n"
        L"Continue with other services where dependencies allow? Installed files and service registrations will remain. "
        L"A Windows restart may help start them later, but it will not fix a bad config, ACL or occupied port.\n\n"
        L"Yes: continue with safe independent services. No: stop further startup.",
        svc_name);
    return MessageBoxW((HWND)ctx->user_data, message, L"Service startup failed",
                       MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) == IDYES;
}

static bool engine_write_summary(SetupContext* ctx) {
    ctx->summary.exit_code = ctx->final_exit_code;
    return summary_write_json(&ctx->summary, ctx->opts->dest);
}

static void engine_note_rollback(SetupContext* ctx, bool restored) {
    strncpy_s(ctx->summary.rollback, sizeof(ctx->summary.rollback),
              restored ? "restored" : "failed", _TRUNCATE);
    ctx->summary.requires_intervention = true;
    if (restored) {
        if (!unpack_clear_incomplete_marker(ctx->opts->dest))
            log_err("Rollback restored payload but could not clear the incomplete marker.");
        ctx->summary.payload_deployed = false;
        strncpy_s(ctx->summary.installed_version, sizeof(ctx->summary.installed_version),
                  ctx->installed_version[0] ? ctx->installed_version : "none", _TRUNCATE);
    }
}

bool engine_phase_check(SetupContext* ctx) {
    if (!ctx || !ctx->opts) return false;

    log_info("=== Phase 1: Check ===");
    if (ctx->on_phase_change) {
        ctx->on_phase_change(SETUP_PHASE_CHECK, "Check", "Checking system environment and prerequisites...", ctx->user_data);
    }

    // 1. Single-instance mutex
    bool mutex_conflict = false;
    if (!ctx->hMutex) {
        ctx->hMutex = CreateMutexW(NULL, TRUE, L"Global\\L4Setup_Instance_Mutex");
        mutex_conflict = ctx->hMutex && GetLastError() == ERROR_ALREADY_EXISTS;
    }
    if (!ctx->hMutex || mutex_conflict) {
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

    // 3. Recover only after explicit start, and stop if recovery cannot complete.
    if (unpack_has_incomplete_marker(ctx->opts->dest, NULL, 0) &&
        !unpack_recover_from_crash(ctx->opts->dest)) {
        ctx->final_exit_code = 23;
        strcpy_s(ctx->summary.status, sizeof(ctx->summary.status), "failed");
        strncpy_s(ctx->summary.error_reason, sizeof(ctx->summary.error_reason),
                  "crash_recovery_failed", _TRUNCATE);
        return false;
    }

    // 4. Version detection
    unpack_read_installed_version(ctx->opts->dest, ctx->installed_version, sizeof(ctx->installed_version));
    strncpy_s(ctx->target_version, sizeof(ctx->target_version), L4SETUP_VERSION_STRING, _TRUNCATE);

    if (ctx->installed_version[0] != '\0') {
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
            if (ctx->opts->repair) {
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
        if (free_bytes.QuadPart < 104857600ULL) { // 100 MB
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
    if (install_root_ca_certificate_ex(ctx->opts->dest)) {
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

    // Initialize summary data
    strncpy_s(ctx->summary.installer_version, sizeof(ctx->summary.installer_version), L4SETUP_VERSION_STRING, _TRUNCATE);
    strncpy_s(ctx->summary.installed_version, sizeof(ctx->summary.installed_version),
              ctx->installed_version[0] ? ctx->installed_version : "none", _TRUNCATE);
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
        return 31;
    }

    // ------------------------------------------------------------------------
    // Phase 2: Prepare
    // ------------------------------------------------------------------------
    log_info("=== Phase 2: Prepare ===");
    strncpy_s(ctx->summary.phase, sizeof(ctx->summary.phase), "prepare", _TRUNCATE);
    if (ctx->on_phase_change) {
        ctx->on_phase_change(SETUP_PHASE_PREPARE, "Prepare", "Configuring environment and staging payload...", ctx->user_data);
    }

    if (!services_configure_environment(ctx->opts->dest)) {
        log_err("Machine environment preparation failed.");
        ctx->final_exit_code = 24;
        strcpy_s(ctx->summary.status, sizeof(ctx->summary.status), "failed");
        strncpy_s(ctx->summary.error_reason, sizeof(ctx->summary.error_reason),
                  "machine_environment_failed", _TRUNCATE);
        engine_write_summary(ctx);
        return 24;
    }
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

        ctx->summary.payload_deployed = true;
        strncpy_s(ctx->summary.installed_version, sizeof(ctx->summary.installed_version),
                  ctx->target_version, _TRUNCATE);
        if (!services_prepare_mosquitto(ctx->opts->dest) ||
            !prepare_mosquitto_config(ctx->opts->dest)) {
            log_err("Mosquitto directory/config preparation failed; restoring previous payload.");
            engine_note_rollback(ctx, unpack_rollback(ctx->opts->dest,
                ctx->installed_version[0] ? ctx->installed_version : "prev"));
            ctx->final_exit_code = 24;
            strcpy_s(ctx->summary.status, sizeof(ctx->summary.status), "failed");
            strncpy_s(ctx->summary.error_reason, sizeof(ctx->summary.error_reason), "mosquitto_prepare_failed", _TRUNCATE);
            engine_write_summary(ctx);
            return 24;
        }

        if (!services_ensure_all_registered(ctx->opts->dest)) {
            log_err("Service registration failed (exit code 24). Initiating rollback...");
            engine_note_rollback(ctx, unpack_rollback(ctx->opts->dest,
                ctx->installed_version[0] ? ctx->installed_version : "prev"));
            ctx->final_exit_code = 24;
            strcpy_s(ctx->summary.status, sizeof(ctx->summary.status), "failed");
            strncpy_s(ctx->summary.error_reason, sizeof(ctx->summary.error_reason), "service_registration_failed", _TRUNCATE);
            engine_write_summary(ctx);
            return 24;
        }
        ctx->summary.services_registered = true;
    }

    if (ctx->cancel_requested) {
        ctx->final_exit_code = 31;
        strcpy_s(ctx->summary.status, sizeof(ctx->summary.status), "cancelled");
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

    // Start all 4 services in order
    if (ctx->op_type == OP_VERIFY) ctx->summary.payload_deployed =
        unpack_is_idempotent(ctx->opts->dest, ctx->target_version);
    if (ctx->op_type == OP_VERIFY &&
        (!services_prepare_mosquitto(ctx->opts->dest) ||
         !prepare_mosquitto_config(ctx->opts->dest))) {
        ctx->final_exit_code = 24;
        strcpy_s(ctx->summary.status, sizeof(ctx->summary.status), "failed");
        strncpy_s(ctx->summary.error_reason, sizeof(ctx->summary.error_reason),
                  "mosquitto_prepare_failed", _TRUNCATE);
        engine_write_summary(ctx);
        return 24;
    }
    bool partial_requested = false;
    bool svcs_started = services_start_all_in_order(
        engine_service_lifecycle_cb, engine_continue_after_service_failure, ctx,
        &partial_requested, ctx->summary.service_start_attempts);
    if (!svcs_started) {
        log_err("One or more services did not reach RUNNING (exit code 24).");
        ctx->final_exit_code = 24;
        strcpy_s(ctx->summary.status, sizeof(ctx->summary.status),
                 partial_requested ? "partial" : "failed");
        strncpy_s(ctx->summary.error_reason, sizeof(ctx->summary.error_reason),
                  partial_requested ? "service_start_partial" : "service_start_failed", _TRUNCATE);
        ctx->summary.reboot_recommended = true;
        ctx->summary.requires_intervention = true;
        summary_add_warning(&ctx->summary, "service_start_reboot_may_help");
        engine_write_summary(ctx);
        return 24;
    }

    // ------------------------------------------------------------------------
    // Phase 6: Verify
    // ------------------------------------------------------------------------
    log_info("=== Phase 6: Verify ===");
    strncpy_s(ctx->summary.phase, sizeof(ctx->summary.phase), "verify", _TRUNCATE);
    if (ctx->on_phase_change) {
        ctx->on_phase_change(SETUP_PHASE_VERIFY, "Verify", "Running local and remote health probes (budget <= 60s)...", ctx->user_data);
    }

    bool is_active_cert = (ctx->summary.cert.state == CERT_VALID);
    smoke_run_probes(ctx->opts->dest, is_active_cert, &ctx->summary.probes);

    // Determine exit code and status
    if (ctx->summary.probes.critical_failed) {
        ctx->final_exit_code = 27;
        strcpy_s(ctx->summary.status, sizeof(ctx->summary.status), "failed");
        strncpy_s(ctx->summary.error_reason, sizeof(ctx->summary.error_reason), "critical_smoke_probe_failed", _TRUNCATE);
    } else if (ctx->summary.cert.exit_code == 10) {
        ctx->final_exit_code = 10;
        strcpy_s(ctx->summary.status, sizeof(ctx->summary.status), "activation_required");
    } else if (ctx->summary.cert.exit_code == 11) {
        ctx->final_exit_code = 11;
        strcpy_s(ctx->summary.status, sizeof(ctx->summary.status), "ready_for_online");
    } else if (ctx->summary.probes.calculated_exit_code == 12) {
        ctx->final_exit_code = 12;
        strcpy_s(ctx->summary.status, sizeof(ctx->summary.status), "degraded");
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

    if (ctx->final_exit_code == 0 || ctx->final_exit_code == 10 || ctx->final_exit_code == 11 || ctx->final_exit_code == 12) {
        // Clear crash marker
        unpack_clear_incomplete_marker(ctx->opts->dest);

        // Update state.json with installed_version
        wchar_t sum_path[MAX_PATH];
        swprintf_s(sum_path, MAX_PATH, L"%ls\\install_summary.json", ctx->opts->dest);
        state_patch_version(ctx->opts->dest, ctx->target_version, sum_path);
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
