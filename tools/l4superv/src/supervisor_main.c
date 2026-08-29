#include "config.h"
#include "service_mgr.h"
#include "state_mgr.h"
#include "hardware_fingerprint.h"
#include "mosquitto_conf.h"
#include "orchestrator.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <shlwapi.h>

#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "advapi32.lib")

static SERVICE_STATUS        g_svcStatus;
static SERVICE_STATUS_HANDLE g_svcStatusHandle;
static HANDLE                g_svcStopEvent = NULL;
static volatile bool         g_stopFlag = false;

static void WINAPI ServiceCtrlHandler(DWORD dwCtrl) {
    switch (dwCtrl) {
        case SERVICE_CONTROL_STOP:
        case SERVICE_CONTROL_SHUTDOWN:
            g_svcStatus.dwCurrentState = SERVICE_STOP_PENDING;
            SetServiceStatus(g_svcStatusHandle, &g_svcStatus);
            g_stopFlag = true;
            if (g_svcStopEvent) {
                SetEvent(g_svcStopEvent);
            }
            break;
        default:
            break;
    }
}

static void WINAPI ServiceMain(DWORD dwArgc, LPWSTR *lpszArgv) {
    (void)dwArgc;
    (void)lpszArgv;

    g_svcStatusHandle = RegisterServiceCtrlHandlerW(SVC_NAME_L4SUPERV, ServiceCtrlHandler);
    if (!g_svcStatusHandle) {
        return;
    }

    g_svcStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    g_svcStatus.dwServiceSpecificExitCode = 0;
    g_svcStatus.dwCurrentState = SERVICE_START_PENDING;
    g_svcStatus.dwControlsAccepted = 0;
    g_svcStatus.dwWin32ExitCode = 0;
    g_svcStatus.dwCheckPoint = 0;
    g_svcStatus.dwWaitHint = 3000;
    SetServiceStatus(g_svcStatusHandle, &g_svcStatus);

    g_svcStopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!g_svcStopEvent) {
        g_svcStatus.dwCurrentState = SERVICE_STOPPED;
        g_svcStatus.dwWin32ExitCode = GetLastError();
        SetServiceStatus(g_svcStatusHandle, &g_svcStatus);
        return;
    }

    wchar_t exe_path[MAX_PATH];
    GetModuleFileNameW(NULL, exe_path, MAX_PATH);

    L4SupervConfig cfg;
    config_init_defaults(&cfg, exe_path);
    config_load_json(&cfg, cfg.config_file);

    g_svcStatus.dwCurrentState = SERVICE_RUNNING;
    g_svcStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;
    SetServiceStatus(g_svcStatusHandle, &g_svcStatus);

    // Run main orchestrator loop
    orchestrator_run_loop(&cfg, &g_stopFlag);

    g_svcStatus.dwCurrentState = SERVICE_STOPPED;
    g_svcStatus.dwControlsAccepted = 0;
    SetServiceStatus(g_svcStatusHandle, &g_svcStatus);

    if (g_svcStopEvent) {
        CloseHandle(g_svcStopEvent);
        g_svcStopEvent = NULL;
    }
}

static BOOL WINAPI ConsoleCtrlHandler(DWORD dwCtrlType) {
    (void)dwCtrlType;
    g_stopFlag = true;
    return TRUE;
}

static void print_status(const L4SupervConfig* cfg) {
    L4State st;
    state_load(cfg->base_path, &st);

    wprintf(L"===============================================================\n");
    wprintf(L" Leo4 Services Status Overview\n");
    wprintf(L" Base Path: %ls\n", cfg->base_path);
    wprintf(L"===============================================================\n");
    wprintf(L"  %-12ls : %-12hs (PID: %6lu, Match: %ls)\n",
            SVC_NAME_LEO4PROXY, st.svc_leo4proxy.status, st.svc_leo4proxy.runtime_pid,
            st.svc_leo4proxy.path_match ? L"YES" : L"NO");
    if (st.svc_leo4proxy.runtime_exe[0] != '\0') {
        wprintf(L"                  Path: %hs\n", st.svc_leo4proxy.runtime_exe);
    }

    wprintf(L"  %-12ls : %-12hs (PID: %6lu, Match: %ls)\n",
            SVC_NAME_MOSQUITTO, st.svc_mosquitto.status, st.svc_mosquitto.runtime_pid,
            st.svc_mosquitto.path_match ? L"YES" : L"NO");
    if (st.svc_mosquitto.runtime_exe[0] != '\0') {
        wprintf(L"                  Path: %hs\n", st.svc_mosquitto.runtime_exe);
    }

    wprintf(L"  %-12ls : %-12hs (PID: %6lu, Match: %ls)\n",
            SVC_NAME_L4CON, st.svc_l4con.status, st.svc_l4con.runtime_pid,
            st.svc_l4con.path_match ? L"YES" : L"NO");
    if (st.svc_l4con.runtime_exe[0] != '\0') {
        wprintf(L"                  Path: %hs\n", st.svc_l4con.runtime_exe);
    }

    wprintf(L"  %-12ls : %-12hs (PID: %6lu, Match: %ls)\n",
            SVC_NAME_L4SUPERV, st.svc_l4superv.status, st.svc_l4superv.runtime_pid,
            st.svc_l4superv.path_match ? L"YES" : L"NO");
    if (st.svc_l4superv.runtime_exe[0] != '\0') {
        wprintf(L"                  Path: %hs\n", st.svc_l4superv.runtime_exe);
    }

    wprintf(L"---------------------------------------------------------------\n");
    wprintf(L" Orchestrator State:\n");
    wprintf(L"   Status:            %hs\n", st.status);
    wprintf(L"   Device SN:         %hs\n", st.sn[0] ? st.sn : "(none)");
    wprintf(L"   Thumbprint:        %hs\n", st.thumbprint[0] ? st.thumbprint : "(none)");
    wprintf(L"   Valid To:          %hs\n", st.not_after[0] ? st.not_after : "(none)");
    wprintf(L"   HW Fingerprint:    %hs\n", st.hw_fingerprint[0] ? st.hw_fingerprint : "(none)");
    wprintf(L"===============================================================\n\n");
}

static void print_usage(void) {
    wprintf(L"Usage: l4superv.exe [OPTIONS]\n\n");
    wprintf(L"Options:\n");
    wprintf(L"  --console, -f      Run supervisor interactively in foreground console\n");
    wprintf(L"  --install, -i      Install/register L4Superv service in Windows SCM\n");
    wprintf(L"  --uninstall, -u    Uninstall/remove L4Superv service from Windows SCM\n");
    wprintf(L"  --start            Start L4Superv service\n");
    wprintf(L"  --stop             Stop L4Superv service\n");
    wprintf(L"  --restart          Restart L4Superv service\n");
    wprintf(L"  --status           Display status of all Leo4 services and orchestrator\n");
    wprintf(L"  --check            Execute a single orchestration cycle and exit\n");
    wprintf(L"  --help, -h         Show this help message\n\n");
}

int wmain(int argc, wchar_t* argv[]) {
    wchar_t exe_path[MAX_PATH];
    GetModuleFileNameW(NULL, exe_path, MAX_PATH);

    L4SupervConfig cfg;
    config_init_defaults(&cfg, exe_path);
    config_load_json(&cfg, cfg.config_file);

    if (argc > 1) {
        if (_wcsicmp(argv[1], L"--console") == 0 || _wcsicmp(argv[1], L"-f") == 0) {
            SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);
            orchestrator_run_loop(&cfg, &g_stopFlag);
            return 0;
        } else if (_wcsicmp(argv[1], L"--check") == 0) {
            L4State state;
            state_load(cfg.base_path, &state);
            bool action = false;
            orchestrator_step(&cfg, &state, &action);
            wprintf(L"[CHECK] Orchestrator check completed. Action taken: %ls\n", action ? L"YES" : L"NO");
            print_status(&cfg);
            return 0;
        } else if (_wcsicmp(argv[1], L"--status") == 0) {
            print_status(&cfg);
            return 0;
        } else if (_wcsicmp(argv[1], L"--install") == 0 || _wcsicmp(argv[1], L"-i") == 0) {
            wchar_t cmd[MAX_PATH * 2];
            swprintf_s(cmd, sizeof(cmd)/sizeof(wchar_t), L"\"%s\"", exe_path);
            bool ok = svc_install(SVC_NAME_L4SUPERV,
                                  L"Leo4 Supervisor & Watchdog",
                                  cmd,
                                  SERVICE_AUTO_START,
                                  L"Leo4 Services Supervisor, State Orchestrator and Watchdog",
                                  NULL);
            wprintf(L"Service %ls installation: %ls\n", SVC_NAME_L4SUPERV, ok ? L"SUCCESS" : L"FAILED");
            return ok ? 0 : 1;
        } else if (_wcsicmp(argv[1], L"--uninstall") == 0 || _wcsicmp(argv[1], L"-u") == 0) {
            bool ok = svc_uninstall(SVC_NAME_L4SUPERV);
            wprintf(L"Service %ls uninstallation: %ls\n", SVC_NAME_L4SUPERV, ok ? L"SUCCESS" : L"FAILED");
            return ok ? 0 : 1;
        } else if (_wcsicmp(argv[1], L"--start") == 0) {
            bool ok = svc_start(SVC_NAME_L4SUPERV);
            wprintf(L"Service %ls start: %ls\n", SVC_NAME_L4SUPERV, ok ? L"SUCCESS" : L"FAILED");
            return ok ? 0 : 1;
        } else if (_wcsicmp(argv[1], L"--stop") == 0) {
            bool ok = svc_stop(SVC_NAME_L4SUPERV);
            wprintf(L"Service %ls stop: %ls\n", SVC_NAME_L4SUPERV, ok ? L"SUCCESS" : L"FAILED");
            return ok ? 0 : 1;
        } else if (_wcsicmp(argv[1], L"--restart") == 0) {
            bool ok = svc_restart(SVC_NAME_L4SUPERV);
            wprintf(L"Service %ls restart: %ls\n", SVC_NAME_L4SUPERV, ok ? L"SUCCESS" : L"FAILED");
            return ok ? 0 : 1;
        } else if (_wcsicmp(argv[1], L"--help") == 0 || _wcsicmp(argv[1], L"-h") == 0 || _wcsicmp(argv[1], L"/?") == 0) {
            print_usage();
            return 0;
        }
    }

    // Try starting as Windows Service
    SERVICE_TABLE_ENTRYW dispatchTable[] = {
        { (LPWSTR)SVC_NAME_L4SUPERV, (LPSERVICE_MAIN_FUNCTIONW)ServiceMain },
        { NULL, NULL }
    };

    if (StartServiceCtrlDispatcherW(dispatchTable)) {
        return 0;
    }

    DWORD err = GetLastError();
    if (err == ERROR_FAILED_SERVICE_CONTROLLER_CONNECT) {
        // Launched interactively without arguments: perform idempotent setup/repair and start
        wprintf(L"===============================================================\n");
        wprintf(L"  l4superv (Interactive First Run / Repair Mode)\n");
        wprintf(L"===============================================================\n");
        wprintf(L"Registering and ensuring all services in %ls...\n", cfg.base_path);

        svc_ensure_all_installed_and_running(cfg.base_path);
        print_status(&cfg);

        wprintf(L"\nTo run supervisor in console loop, use: l4superv.exe --console\n");
        wprintf(L"To check status, use:                   l4superv.exe --status\n\n");
        return 0;
    }

    wprintf(L"[ERROR] StartServiceCtrlDispatcher failed with error %lu\n", err);
    return 1;
}
