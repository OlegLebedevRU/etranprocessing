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
#include "engine.h"
#include "ui.h"

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

static void unattended_on_phase_change(SetupPhase phase, const char* phase_name, const char* status_text, void* user_data) {
    UNREFERENCED_PARAMETER(phase);
    UNREFERENCED_PARAMETER(user_data);
    printf("[PHASE] %s: %s\n", phase_name, status_text);
    fflush(stdout);
}

static void unattended_on_service_status(
    int service_idx,
    const wchar_t* svc_name,
    ServiceLifecycleStatus status,
    DWORD elapsed_sec,
    const char* notice,
    void* user_data
) {
    UNREFERENCED_PARAMETER(service_idx);
    UNREFERENCED_PARAMETER(user_data);

    const char* st_str = "PENDING";
    switch (status) {
        case SVC_STATUS_STARTING: st_str = "STARTING"; break;
        case SVC_STATUS_RUNNING:  st_str = "RUNNING";  break;
        case SVC_STATUS_STOPPING: st_str = "STOPPING"; break;
        case SVC_STATUS_STOPPED:  st_str = "STOPPED";  break;
        case SVC_STATUS_CHECKING: st_str = "CHECKING"; break;
        case SVC_STATUS_READY:    st_str = "READY";    break;
        case SVC_STATUS_FAILED:   st_str = "FAILED";   break;
        default: break;
    }

    if (notice && notice[0]) {
        wprintf(L"[SERVICE] %ls: %hs (%lus) - %hs\n", svc_name, st_str, elapsed_sec, notice);
    } else {
        wprintf(L"[SERVICE] %ls: %hs (%lus)\n", svc_name, st_str, elapsed_sec);
    }
    fflush(stdout);
}

static void unattended_on_notice(const char* notice_text, void* user_data) {
    UNREFERENCED_PARAMETER(user_data);
    printf("[NOTICE] %s\n", notice_text);
    fflush(stdout);
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

    // 4. Elevation Check: Product Exit Code 20 (Win32 ERROR_ACCESS_DENIED is 5)
    // Never self-elevate or call runas
    if (!uac_is_elevated()) {
        fprintf(stderr, "Administrator privileges are required. No changes were made.\n");
        return 20;
    }

    // 5. Resolve Destination directory
    if (!cli_opts.dest_specified) {
        determine_default_dest(cli_opts.dest, MAX_PATH);
    }

    // 6. Initialize Logger
    log_init(cli_opts.dest);

    log_info("=================================================================");
    log_info("  Leo4 Zero-Touch Setup (l4setup) v%s", L4SETUP_VERSION_STRING);
    log_info("=================================================================");
    log_info("Destination: %ls", cli_opts.dest);
    log_info("Mode:        %s", cli_opts.interactive ? "interactive" : (cli_opts.silent ? "silent" : "unattended"));
    log_info("Repair:      %s", cli_opts.repair ? "yes" : "no");
    log_info("Smoke Only:  %s", cli_opts.smoke_only ? "yes" : "no");

    // 7. Interactive vs Unattended execution
    if (cli_opts.interactive) {
        DWORD sess_id = 0;
        ProcessIdToSessionId(GetCurrentProcessId(), &sess_id);
        if (sess_id == 0) {
            log_err("Interactive mode is not available in Session 0. Aborting.");
            fprintf(stderr, "Error: Interactive mode is not available in Session 0 or non-interactive desktop.\n");
            log_close();
            cli_clean_pin(&cli_opts);
            return 1;
        }

        int exit_code = ui_run_interactive_setup(hInstance, &cli_opts);
        cli_clean_pin(&cli_opts);
        log_close();
        return exit_code;
    }

    // 8. Unattended execution pipeline
    SetupContext ctx;
    memset(&ctx, 0, sizeof(SetupContext));
    ctx.opts = &cli_opts;
    ctx.hInstance = hInstance;
    ctx.is_interactive = false;

    ctx.on_phase_change = unattended_on_phase_change;
    ctx.on_service_status = unattended_on_service_status;
    ctx.on_notice = unattended_on_notice;

    if (!engine_phase_check(&ctx)) {
        int code = ctx.final_exit_code;
        if (ctx.hMutex) {
            CloseHandle(ctx.hMutex);
            ctx.hMutex = NULL;
        }
        cli_clean_pin(&cli_opts);
        log_close();
        return code;
    }

    int exit_code = engine_run_pipeline(&ctx);

    if (ctx.hMutex) {
        CloseHandle(ctx.hMutex);
        ctx.hMutex = NULL;
    }

    cli_clean_pin(&cli_opts);
    log_close();
    return exit_code;
}
