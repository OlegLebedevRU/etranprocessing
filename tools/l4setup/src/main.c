#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "version.h"
#include "cli.h"
#include "log.h"
#include "uac.h"
#include "preflight.h"
#include "drainage.h"
#include "unpack.h"
#include "services.h"
#include "cert_phase.h"
#include "smoke.h"
#include "summary.h"

static void init_console(void) {
    HANDLE hStdOut = GetStdHandle(STD_OUTPUT_HANDLE);
    BOOL is_stdout_redirected = (hStdOut != NULL && hStdOut != INVALID_HANDLE_VALUE && GetFileType(hStdOut) != FILE_TYPE_UNKNOWN);
    HANDLE hStdErr = GetStdHandle(STD_ERROR_HANDLE);
    BOOL is_stderr_redirected = (hStdErr != NULL && hStdErr != INVALID_HANDLE_VALUE && GetFileType(hStdErr) != FILE_TYPE_UNKNOWN);
    HANDLE hStdIn = GetStdHandle(STD_INPUT_HANDLE);
    BOOL is_stdin_redirected = (hStdIn != NULL && hStdIn != INVALID_HANDLE_VALUE && GetFileType(hStdIn) != FILE_TYPE_UNKNOWN);

    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        FILE* fp = NULL;
        if (!is_stdout_redirected) {
            freopen_s(&fp, "CONOUT$", "w", stdout);
        }
        if (!is_stderr_redirected) {
            freopen_s(&fp, "CONOUT$", "w", stderr);
        }
        if (!is_stdin_redirected) {
            freopen_s(&fp, "CONIN$", "r", stdin);
        }
        SetConsoleOutputCP(CP_UTF8);
    }
}

static void determine_default_dest(wchar_t* out_dest, size_t max_len) {
    if (!out_dest || max_len == 0) return;
    wcscpy_s(out_dest, max_len, L"C:\\l4tools");

    // Check if C:\l4tools\state.json exists and has installer_base_path
    FILE* fp = NULL;
    if (_wfopen_s(&fp, L"C:\\l4tools\\state.json", L"r") == 0 && fp) {
        char buf[4096] = { 0 };
        fread(buf, 1, sizeof(buf) - 1, fp);
        fclose(fp);

        const char* p = strstr(buf, "\"installer_base_path\": \"");
        if (!p) p = strstr(buf, "\"installer_base_path\":\"");
        if (p) {
            p = strchr(p, ':');
            if (p) p = strchr(p, '"');
            if (p) {
                p++;
                char path_utf8[MAX_PATH] = { 0 };
                size_t k = 0;
                while (*p && *p != '"' && k + 1 < sizeof(path_utf8)) {
                    if (*p == '\\' && *(p + 1) == '\\') p++; // Unescape JSON backslashes
                    path_utf8[k++] = *p++;
                }
                path_utf8[k] = '\0';
                if (k > 0) {
                    MultiByteToWideChar(CP_UTF8, 0, path_utf8, -1, out_dest, (int)max_len);
                }
            }
        }
    }
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow) {
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(pCmdLine);
    UNREFERENCED_PARAMETER(nCmdShow);

    // 1. Attach to parent console if running from cmd/PowerShell
    init_console();

    // 2. Parse command line arguments
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv || argc == 0) {
        return 1;
    }

    CliOptions cli_opts;
    char cli_err[256] = { 0 };
    if (!cli_parse(argc, argv, &cli_opts, cli_err, sizeof(cli_err))) {
        fprintf(stderr, "Error: %s\n\n", cli_err[0] ? cli_err : "Invalid arguments");
        cli_print_usage(argv[0]);
        LocalFree(argv);
        return 1;
    }

    // 3. Early flags: --version and --help require NO elevation
    if (cli_opts.show_version) {
        cli_print_version();
        LocalFree(argv);
        return 0;
    }

    if (cli_opts.show_help) {
        cli_print_usage(argv[0]);
        LocalFree(argv);
        return 0;
    }

    LocalFree(argv);

    // 4. UAC Elevation Manager
    if (!uac_is_elevated()) {
        bool user_cancelled = false;
        const wchar_t* args = uac_get_arguments(GetCommandLineW());
        DWORD code = uac_relaunch_elevated(args, &user_cancelled);
        if (user_cancelled) {
            fprintf(stderr, "[ERROR] Administrator privileges are required to run l4setup (UAC elevation declined, code 20).\n");
            return 20;
        }
        return (int)code;
    }

    // 5. Preflight Phase
    PreflightInfo preflight;
    preflight_check(&preflight);

    if (!preflight.is_supported_os) {
        fprintf(stderr, "[ERROR] Unsupported OS version: %s. Minimum required: Windows 7 SP1 (6.1+). Exit code 21.\n",
                preflight.os_display_name);
        return 21;
    }

    if (preflight.is_win7) {
        preflight_configure_win7_tls12();
    }

    // 6. Resolve Destination directory
    if (!cli_opts.dest_specified) {
        determine_default_dest(cli_opts.dest, MAX_PATH);
    }

    // Initialize logger
    log_init(cli_opts.dest);

    log_info("=================================================================");
    log_info("  Leo4 Zero-Touch Setup (l4setup) v%s", L4SETUP_VERSION_STRING);
    log_info("=================================================================");
    log_info("OS:          %s", preflight.os_display_name);
    log_info("Target Arch: %s", preflight.target_arch);
    log_info("Destination: %ls", cli_opts.dest);
    log_info("Silent:      %s", cli_opts.silent ? "yes" : "no");
    log_info("Repair:      %s", cli_opts.repair ? "yes" : "no");
    log_info("Smoke Only:  %s", cli_opts.smoke_only ? "yes" : "no");

    InstallSummaryData summary;
    memset(&summary, 0, sizeof(summary));
    strncpy_s(summary.installer_version, 32, L4SETUP_VERSION_STRING, _TRUNCATE);
    strncpy_s(summary.os, 128, preflight.os_display_name, _TRUNCATE);
    strncpy_s(summary.target_arch, 16, preflight.target_arch, _TRUNCATE);
    wcsncpy_s(summary.dest, MAX_PATH, cli_opts.dest, _TRUNCATE);

    if (!preflight.ucrtbase_present) {
        summary_add_warning(&summary, "ucrtbase_missing");
        log_warn("Universal C Runtime (ucrtbase.dll) is not installed in system directory.");
    }

    // 7. Handle --smoke-only mode
    if (cli_opts.smoke_only) {
        log_info("Executing in smoke-only mode...");
        cert_info ci;
        memset(&ci, 0, sizeof(ci));
        cert_state cst = cert_discover(NULL, &ci);
        summary.cert.state = cst;
        summary.cert.reused = true;
        strncpy_s(summary.cert.thumbprint, 64, ci.thumbprint_hex, _TRUNCATE);
        strncpy_s(summary.cert.sn, 64, ci.sn, _TRUNCATE);
        strncpy_s(summary.cert.not_after, 64, ci.not_after_utc, _TRUNCATE);

        bool is_active = (cst == CERT_VALID && ci.has_private_key);
        smoke_run_probes(cli_opts.dest, is_active, &summary.probes);

        summary.exit_code = summary.probes.calculated_exit_code;
        if (summary.exit_code == 0) {
            strcpy_s(summary.status, sizeof(summary.status), "ready");
        } else if (summary.exit_code == 12) {
            strcpy_s(summary.status, sizeof(summary.status), "ready_with_warnings");
        } else {
            strcpy_s(summary.status, sizeof(summary.status), "failed");
        }

        summary_write_json(&summary, cli_opts.dest);
        wchar_t summary_path[MAX_PATH];
        swprintf_s(summary_path, MAX_PATH, L"%ls\\install_summary.json", cli_opts.dest);
        state_patch_version(cli_opts.dest, L4SETUP_VERSION_STRING, summary_path);

        log_info("Smoke-only run complete. Exit code: %d, Status: %s", summary.exit_code, summary.status);
        log_close();
        return summary.exit_code;
    }

    // 8. Configure Firewall rules
    preflight_setup_firewall(cli_opts.dest);

    // 9. Idempotency Check
    bool is_idempotent = unpack_is_idempotent(cli_opts.dest, L4SETUP_VERSION_STRING);
    if (is_idempotent && !cli_opts.repair) {
        log_info("Version %s already installed and binaries verified intact.", L4SETUP_VERSION_STRING);
        log_info("Skipping drainage, unpacking, and service registration (idempotent path).");
    } else {
        // Phase 1.4: Drainage
        DrainageResult drainage_res;
        if (!drainage_execute(cli_opts.dest, cli_opts.silent, &drainage_res)) {
            if (drainage_res.active_stream_detected && cli_opts.silent) {
                summary_add_warning(&summary, "active_stream");
            }
            summary.drainage = drainage_res;
            summary.exit_code = 22;
            strcpy_s(summary.status, sizeof(summary.status), "failed");
            summary_write_json(&summary, cli_opts.dest);
            log_err("Drainage failed. Aborting installation with exit code 22.");
            log_close();
            cli_clean_pin(&cli_opts);
            return 22;
        }
        summary.drainage = drainage_res;

        // Phase 2.1: Unpack payload
        if (!unpack_payload(cli_opts.dest, preflight.target_arch,
                            cli_opts.payload_dir_specified ? cli_opts.payload_dir : NULL,
                            L4SETUP_VERSION_STRING)) {
            summary.exit_code = 23;
            strcpy_s(summary.status, sizeof(summary.status), "failed");
            summary_write_json(&summary, cli_opts.dest);
            log_err("Payload unpacking failed. Aborting with exit code 23.");
            log_close();
            cli_clean_pin(&cli_opts);
            return 23;
        }

        // Phase 2.2: Services and Environment
        services_configure_environment(cli_opts.dest);
        if (!services_ensure_all_registered(cli_opts.dest)) {
            summary.exit_code = 24;
            strcpy_s(summary.status, sizeof(summary.status), "failed");
            summary_write_json(&summary, cli_opts.dest);
            log_err("Service registration failed. Aborting with exit code 24.");
            log_close();
            cli_clean_pin(&cli_opts);
            return 24;
        }
    }

    // 10. Phase 3: Certificate Discovery & Provisioning
    CertPhaseResult cert_res;
    bool cert_phase_ok = cert_phase_execute(cli_opts.dest, &cli_opts, hInstance, &cert_res);
    summary.cert = cert_res;
    for (int w = 0; w < cert_res.warnings_count; w++) {
        summary_add_warning(&summary, cert_res.warnings[w]);
    }

    if (!cert_phase_ok) {
        // Fatal error during Phase 3 (code 25 or 26)
        summary.exit_code = cert_res.exit_code;
        strcpy_s(summary.status, sizeof(summary.status), cert_res.status);
        summary_write_json(&summary, cli_opts.dest);
        log_err("Phase 3 failed with exit code %d (%s). Aborting.", cert_res.exit_code, cert_res.status);
        log_close();
        cli_clean_pin(&cli_opts);
        return cert_res.exit_code;
    }

    // 11. Phase 4: Start services
    services_start_all_in_order();

    // 12. Phase 5: Smoke Tests
    bool is_active_status = (strcmp(cert_res.status, "ready") == 0);
    smoke_run_probes(cli_opts.dest, is_active_status, &summary.probes);

    // 13. Determine final exit code & status
    if (cert_res.exit_code == 10) {
        summary.exit_code = 10;
        strcpy_s(summary.status, sizeof(summary.status), "standby_waiting_pin");
    } else if (cert_res.exit_code == 11) {
        summary.exit_code = 11;
        strcpy_s(summary.status, sizeof(summary.status), "ready_for_online");
    } else if (summary.probes.critical_failed) {
        summary.exit_code = 27;
        strcpy_s(summary.status, sizeof(summary.status), "failed");
    } else if (summary.probes.has_warnings) {
        summary.exit_code = 12;
        strcpy_s(summary.status, sizeof(summary.status), "ready_with_warnings");
    } else {
        summary.exit_code = 0;
        strcpy_s(summary.status, sizeof(summary.status), "ready");
    }

    // 14. Write install_summary.json and patch state.json
    summary_write_json(&summary, cli_opts.dest);

    wchar_t summary_path[MAX_PATH];
    swprintf_s(summary_path, MAX_PATH, L"%ls\\install_summary.json", cli_opts.dest);
    state_patch_version(cli_opts.dest, L4SETUP_VERSION_STRING, summary_path);

    log_info("=================================================================");
    log_info("  l4setup finished with exit code %d (status: %s)", summary.exit_code, summary.status);
    log_info("=================================================================");

    cli_clean_pin(&cli_opts);
    log_close();

    return summary.exit_code;
}
