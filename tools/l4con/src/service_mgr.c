#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include "service_mgr.h"
#include "mqtt_client.h"

#pragma comment(lib, "advapi32.lib")

static SERVICE_STATUS g_serviceStatus = { 0 };
static SERVICE_STATUS_HANDLE g_statusHandle = NULL;
static HANDLE g_stopEvent = NULL;
static AppConfig g_serviceConfig;

static void report_service_status(DWORD currentState, DWORD win32ExitCode, DWORD waitHint) {
    static DWORD checkPoint = 1;

    g_serviceStatus.dwCurrentState = currentState;
    g_serviceStatus.dwWin32ExitCode = win32ExitCode;
    g_serviceStatus.dwWaitHint = waitHint;

    if (currentState == SERVICE_START_PENDING) {
        g_serviceStatus.dwControlsAccepted = 0;
    } else {
        g_serviceStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;
    }

    if (currentState == SERVICE_RUNNING || currentState == SERVICE_STOPPED) {
        g_serviceStatus.dwCheckPoint = 0;
    } else {
        g_serviceStatus.dwCheckPoint = checkPoint++;
    }

    SetServiceStatus(g_statusHandle, &g_serviceStatus);
}

static DWORD WINAPI service_ctrl_handler_ex(DWORD dwControl, DWORD dwEventType, LPVOID lpEventData, LPVOID lpContext) {
    (void)dwEventType;
    (void)lpEventData;
    (void)lpContext;

    switch (dwControl) {
        case SERVICE_CONTROL_STOP:
        case SERVICE_CONTROL_SHUTDOWN:
            report_service_status(SERVICE_STOP_PENDING, NO_ERROR, 5000);
            if (g_stopEvent) {
                SetEvent(g_stopEvent);
            }
            return NO_ERROR;

        case SERVICE_CONTROL_INTERROGATE:
            return NO_ERROR;

        default:
            break;
    }

    return ERROR_CALL_NOT_IMPLEMENTED;
}

static void WINAPI service_main(DWORD argc, LPWSTR* argv) {
    (void)argc;
    (void)argv;

    g_statusHandle = RegisterServiceCtrlHandlerExW(L4CON_SERVICE_NAME, service_ctrl_handler_ex, NULL);
    if (!g_statusHandle) {
        return;
    }

    g_serviceStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    g_serviceStatus.dwServiceSpecificExitCode = 0;
    report_service_status(SERVICE_START_PENDING, NO_ERROR, 5000);

    g_stopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!g_stopEvent) {
        report_service_status(SERVICE_STOPPED, GetLastError(), 0);
        return;
    }

    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        report_service_status(SERVICE_STOPPED, WSAGetLastError(), 0);
        CloseHandle(g_stopEvent);
        g_stopEvent = NULL;
        return;
    }

    report_service_status(SERVICE_RUNNING, NO_ERROR, 0);

    // Windows Service mode: enforce is_service flag and discard any SN argument.
    // Leo4Proxy is the strict source of truth for SN when running as a Windows Service!
    g_serviceConfig.is_service = true;
    g_serviceConfig.sn_explicitly_set = false;
    g_serviceConfig.device_sn[0] = '\0';

    // Run main MQTT loop (blocks until stopEvent is signaled)
    mqtt_client_run(&g_serviceConfig, g_stopEvent);

    report_service_status(SERVICE_STOP_PENDING, NO_ERROR, 3000);

    WSACleanup();

    if (g_stopEvent) {
        CloseHandle(g_stopEvent);
        g_stopEvent = NULL;
    }

    report_service_status(SERVICE_STOPPED, NO_ERROR, 0);
}

bool service_run_dispatcher(const AppConfig* config) {
    if (config) {
        g_serviceConfig = *config;
        g_serviceConfig.is_service = true;
        g_serviceConfig.sn_explicitly_set = false;
        g_serviceConfig.device_sn[0] = '\0';
    }

    SERVICE_TABLE_ENTRYW dispatchTable[] = {
        { (LPWSTR)L4CON_SERVICE_NAME, (LPSERVICE_MAIN_FUNCTIONW)service_main },
        { NULL, NULL }
    };

    return StartServiceCtrlDispatcherW(dispatchTable) != 0;
}

bool service_install_or_update(const AppConfig* config, int argc, char* argv[]) {
    (void)config;
    wchar_t exePath[MAX_PATH];
    if (!GetModuleFileNameW(NULL, exePath, MAX_PATH)) {
        fprintf(stderr, "[ERROR] GetModuleFileName failed: %lu\n", GetLastError());
        return false;
    }

    // Build binary path with service parameter and forwarded options
    wchar_t binaryPathWithArgs[2048];
    _snwprintf(binaryPathWithArgs, sizeof(binaryPathWithArgs)/sizeof(wchar_t), L"\"%ls\" --service", exePath);

    for (int i = 1; i < argc; i++) {
        if (_stricmp(argv[i], "--install") == 0 ||
            _stricmp(argv[i], "--uninstall") == 0 ||
            _stricmp(argv[i], "--start") == 0 ||
            _stricmp(argv[i], "--stop") == 0 ||
            _stricmp(argv[i], "--restart") == 0 ||
            _stricmp(argv[i], "--status") == 0 ||
            _stricmp(argv[i], "--console") == 0 ||
            _stricmp(argv[i], "-f") == 0) {
            continue;
        }

        wchar_t wArg[256];
        MultiByteToWideChar(CP_UTF8, 0, argv[i], -1, wArg, 256);
        size_t curLen = wcslen(binaryPathWithArgs);
        _snwprintf(binaryPathWithArgs + curLen, (sizeof(binaryPathWithArgs)/sizeof(wchar_t)) - curLen, L" \"%ls\"", wArg);
    }

    SC_HANDLE schSCManager = OpenSCManagerW(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!schSCManager) {
        fprintf(stderr, "[ERROR] OpenSCManager failed (Are you Administrator?): %lu\n", GetLastError());
        return false;
    }

    SC_HANDLE schService = OpenServiceW(schSCManager, L4CON_SERVICE_NAME, SERVICE_ALL_ACCESS);
    if (schService) {
        printf("[INFO] Service %ls exists. Updating configuration...\n", L4CON_SERVICE_NAME);
        ChangeServiceConfigW(schService,
                             SERVICE_WIN32_OWN_PROCESS,
                             SERVICE_AUTO_START,
                             SERVICE_ERROR_NORMAL,
                             binaryPathWithArgs,
                             NULL, NULL, NULL, NULL, NULL,
                             L4CON_DISPLAY_NAME);
    } else {
        printf("[INFO] Creating service %ls in Windows SCM...\n", L4CON_SERVICE_NAME);
        schService = CreateServiceW(
            schSCManager,
            L4CON_SERVICE_NAME,
            L4CON_DISPLAY_NAME,
            SERVICE_ALL_ACCESS,
            SERVICE_WIN32_OWN_PROCESS,
            SERVICE_AUTO_START,
            SERVICE_ERROR_NORMAL,
            binaryPathWithArgs,
            NULL, NULL, NULL, NULL, NULL
        );
        if (!schService) {
            fprintf(stderr, "[ERROR] CreateService failed: %lu\n", GetLastError());
            CloseServiceHandle(schSCManager);
            return false;
        }
    }

    // Set Service Description
    SERVICE_DESCRIPTIONW sd;
    sd.lpDescription = (LPWSTR)L4CON_DESC;
    ChangeServiceConfig2W(schService, SERVICE_CONFIG_DESCRIPTION, &sd);

    // Set Auto-Recovery on failure: restart after 5s, 10s, 30s
    SC_ACTION actions[3];
    actions[0].Type = SC_ACTION_RESTART;
    actions[0].Delay = 5000;
    actions[1].Type = SC_ACTION_RESTART;
    actions[1].Delay = 10000;
    actions[2].Type = SC_ACTION_RESTART;
    actions[2].Delay = 30000;

    SERVICE_FAILURE_ACTIONSW sfa;
    ZeroMemory(&sfa, sizeof(sfa));
    sfa.dwResetPeriod = 86400;
    sfa.cActions = 3;
    sfa.lpsaActions = actions;
    ChangeServiceConfig2W(schService, SERVICE_CONFIG_FAILURE_ACTIONS, &sfa);

    printf("[OK] Service %ls installed successfully.\n", L4CON_SERVICE_NAME);
    printf("     Path: %ls\n", binaryPathWithArgs);

    CloseServiceHandle(schService);
    CloseServiceHandle(schSCManager);
    return true;
}

bool service_uninstall(void) {
    SC_HANDLE schSCManager = OpenSCManagerW(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!schSCManager) {
        fprintf(stderr, "[ERROR] OpenSCManager failed (Are you Administrator?): %lu\n", GetLastError());
        return false;
    }

    SC_HANDLE schService = OpenServiceW(schSCManager, L4CON_SERVICE_NAME, SERVICE_ALL_ACCESS);
    if (!schService) {
        fprintf(stderr, "[WARN] Service %ls not found.\n", L4CON_SERVICE_NAME);
        CloseServiceHandle(schSCManager);
        return true;
    }

    SERVICE_STATUS status;
    ControlService(schService, SERVICE_CONTROL_STOP, &status);
    Sleep(1000);

    if (!DeleteService(schService)) {
        fprintf(stderr, "[ERROR] DeleteService failed: %lu\n", GetLastError());
        CloseServiceHandle(schService);
        CloseServiceHandle(schSCManager);
        return false;
    }

    printf("[OK] Service %ls deleted successfully.\n", L4CON_SERVICE_NAME);
    CloseServiceHandle(schService);
    CloseServiceHandle(schSCManager);
    return true;
}

bool service_start(void) {
    SC_HANDLE schSCManager = OpenSCManagerW(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!schSCManager) {
        fprintf(stderr, "[ERROR] OpenSCManager failed: %lu\n", GetLastError());
        return false;
    }

    SC_HANDLE schService = OpenServiceW(schSCManager, L4CON_SERVICE_NAME, SERVICE_START | SERVICE_QUERY_STATUS);
    if (!schService) {
        fprintf(stderr, "[ERROR] OpenService failed: %lu\n", GetLastError());
        CloseServiceHandle(schSCManager);
        return false;
    }

    SERVICE_STATUS_PROCESS ssStatus;
    DWORD dwBytesNeeded;
    if (QueryServiceStatusEx(schService, SC_STATUS_PROCESS_INFO, (LPBYTE)&ssStatus, sizeof(SERVICE_STATUS_PROCESS), &dwBytesNeeded)) {
        if (ssStatus.dwCurrentState == SERVICE_RUNNING) {
            printf("[INFO] Service %ls is already RUNNING (PID: %lu).\n", L4CON_SERVICE_NAME, ssStatus.dwProcessId);
            CloseServiceHandle(schService);
            CloseServiceHandle(schSCManager);
            return true;
        }
    }

    if (!StartServiceW(schService, 0, NULL)) {
        fprintf(stderr, "[ERROR] StartService failed: %lu\n", GetLastError());
        CloseServiceHandle(schService);
        CloseServiceHandle(schSCManager);
        return false;
    }

    printf("[OK] Service %ls start signal sent.\n", L4CON_SERVICE_NAME);
    CloseServiceHandle(schService);
    CloseServiceHandle(schSCManager);
    return true;
}

bool service_stop(void) {
    SC_HANDLE schSCManager = OpenSCManagerW(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!schSCManager) {
        fprintf(stderr, "[ERROR] OpenSCManager failed: %lu\n", GetLastError());
        return false;
    }

    SC_HANDLE schService = OpenServiceW(schSCManager, L4CON_SERVICE_NAME, SERVICE_STOP | SERVICE_QUERY_STATUS);
    if (!schService) {
        fprintf(stderr, "[ERROR] OpenService failed: %lu\n", GetLastError());
        CloseServiceHandle(schSCManager);
        return false;
    }

    SERVICE_STATUS_PROCESS ssStatus;
    if (!ControlService(schService, SERVICE_CONTROL_STOP, (LPSERVICE_STATUS)&ssStatus)) {
        DWORD err = GetLastError();
        if (err == ERROR_SERVICE_NOT_ACTIVE) {
            printf("[INFO] Service %ls is already STOPPED.\n", L4CON_SERVICE_NAME);
            CloseServiceHandle(schService);
            CloseServiceHandle(schSCManager);
            return true;
        }
        fprintf(stderr, "[ERROR] ControlService(STOP) failed: %lu\n", err);
        CloseServiceHandle(schService);
        CloseServiceHandle(schSCManager);
        return false;
    }

    printf("[OK] Service %ls stop signal sent.\n", L4CON_SERVICE_NAME);
    CloseServiceHandle(schService);
    CloseServiceHandle(schSCManager);
    return true;
}

bool service_restart(void) {
    printf("[INFO] Restarting service %ls...\n", L4CON_SERVICE_NAME);
    service_stop();
    Sleep(2000);
    return service_start();
}

void service_query_status(void) {
    SC_HANDLE schSCManager = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT);
    if (!schSCManager) {
        fprintf(stderr, "[ERROR] OpenSCManager failed: %lu\n", GetLastError());
        return;
    }

    SC_HANDLE schService = OpenServiceW(schSCManager, L4CON_SERVICE_NAME, SERVICE_QUERY_STATUS);
    if (!schService) {
        fprintf(stderr, "[WARN] Service %ls is NOT installed in Windows SCM.\n", L4CON_SERVICE_NAME);
        CloseServiceHandle(schSCManager);
        return;
    }

    SERVICE_STATUS_PROCESS ssStatus;
    DWORD dwBytesNeeded;
    if (QueryServiceStatusEx(schService, SC_STATUS_PROCESS_INFO, (LPBYTE)&ssStatus, sizeof(SERVICE_STATUS_PROCESS), &dwBytesNeeded)) {
        const char* stateStr = "UNKNOWN";
        switch (ssStatus.dwCurrentState) {
            case SERVICE_STOPPED:          stateStr = "STOPPED"; break;
            case SERVICE_START_PENDING:    stateStr = "START_PENDING"; break;
            case SERVICE_STOP_PENDING:     stateStr = "STOP_PENDING"; break;
            case SERVICE_RUNNING:          stateStr = "RUNNING"; break;
            case SERVICE_CONTINUE_PENDING: stateStr = "CONTINUE_PENDING"; break;
            case SERVICE_PAUSE_PENDING:    stateStr = "PAUSE_PENDING"; break;
            case SERVICE_PAUSED:           stateStr = "PAUSED"; break;
        }
        printf("Service: %ls\n", L4CON_SERVICE_NAME);
        printf("Status:  %s\n", stateStr);
        if (ssStatus.dwCurrentState == SERVICE_RUNNING) {
            printf("PID:     %lu\n", ssStatus.dwProcessId);
        }
    }

    CloseServiceHandle(schService);
    CloseServiceHandle(schSCManager);
}
