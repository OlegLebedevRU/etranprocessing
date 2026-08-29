#include "service_mgr.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma comment(lib, "advapi32.lib")

bool svc_install(const wchar_t* svc_name,
                 const wchar_t* display_name,
                 const wchar_t* bin_path,
                 DWORD start_type,
                 const wchar_t* description,
                 const wchar_t* dependencies) {
    if (!svc_name || !bin_path) return false;

    SC_HANDLE hSCM = OpenSCManagerW(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!hSCM) {
        return false;
    }

    SC_HANDLE hSvc = OpenServiceW(hSCM, svc_name, SERVICE_ALL_ACCESS);
    if (hSvc) {
        // Service already exists: update its config
        ChangeServiceConfigW(
            hSvc,
            SERVICE_WIN32_OWN_PROCESS,
            start_type,
            SERVICE_NO_CHANGE,
            bin_path,
            NULL,
            NULL,
            dependencies,
            NULL,
            NULL,
            display_name
        );
    } else {
        // Create new service
        hSvc = CreateServiceW(
            hSCM,
            svc_name,
            display_name ? display_name : svc_name,
            SERVICE_ALL_ACCESS,
            SERVICE_WIN32_OWN_PROCESS,
            start_type,
            SERVICE_ERROR_NORMAL,
            bin_path,
            NULL,
            NULL,
            dependencies,
            NULL,
            NULL
        );
    }

    if (!hSvc) {
        CloseServiceHandle(hSCM);
        return false;
    }

    // Set description if provided
    if (description) {
        SERVICE_DESCRIPTIONW sd;
        sd.lpDescription = (LPWSTR)description;
        ChangeServiceConfig2W(hSvc, SERVICE_CONFIG_DESCRIPTION, &sd);
    }

    // Configure Auto-Recovery (Restart service on failure after 5s, 10s, 30s)
    SC_ACTION actions[3];
    actions[0].Type = SC_ACTION_RESTART;
    actions[0].Delay = 5000;  // 5 sec
    actions[1].Type = SC_ACTION_RESTART;
    actions[1].Delay = 10000; // 10 sec
    actions[2].Type = SC_ACTION_RESTART;
    actions[2].Delay = 30000; // 30 sec

    SERVICE_FAILURE_ACTIONSW sfa;
    memset(&sfa, 0, sizeof(sfa));
    sfa.dwResetPeriod = 86400; // 1 day
    sfa.cActions = 3;
    sfa.lpsaActions = actions;
    ChangeServiceConfig2W(hSvc, SERVICE_CONFIG_FAILURE_ACTIONS, &sfa);

    CloseServiceHandle(hSvc);
    CloseServiceHandle(hSCM);
    return true;
}

bool svc_uninstall(const wchar_t* svc_name) {
    if (!svc_name) return false;

    svc_stop(svc_name);

    SC_HANDLE hSCM = OpenSCManagerW(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!hSCM) return false;

    SC_HANDLE hSvc = OpenServiceW(hSCM, svc_name, SERVICE_STOP | DELETE);
    if (!hSvc) {
        CloseServiceHandle(hSCM);
        return false;
    }

    BOOL res = DeleteService(hSvc);
    CloseServiceHandle(hSvc);
    CloseServiceHandle(hSCM);
    return (res != 0);
}

bool svc_get_status(const wchar_t* svc_name, DWORD* out_state, DWORD* out_pid) {
    if (!svc_name) return false;
    if (out_state) *out_state = 0;
    if (out_pid) *out_pid = 0;

    SC_HANDLE hSCM = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT);
    if (!hSCM) return false;

    SC_HANDLE hSvc = OpenServiceW(hSCM, svc_name, SERVICE_QUERY_STATUS);
    if (!hSvc) {
        CloseServiceHandle(hSCM);
        return false;
    }

    SERVICE_STATUS_PROCESS ssp;
    DWORD bytesNeeded = 0;
    BOOL ok = QueryServiceStatusEx(hSvc, SC_STATUS_PROCESS_INFO, (LPBYTE)&ssp, sizeof(ssp), &bytesNeeded);

    if (ok) {
        if (out_state) *out_state = ssp.dwCurrentState;
        if (out_pid) *out_pid = ssp.dwProcessId;
    }

    CloseServiceHandle(hSvc);
    CloseServiceHandle(hSCM);
    return (ok != 0);
}

bool svc_is_running(const wchar_t* svc_name) {
    DWORD state = 0;
    if (svc_get_status(svc_name, &state, NULL)) {
        return (state == SERVICE_RUNNING);
    }
    return false;
}

bool svc_exists(const wchar_t* svc_name) {
    DWORD state = 0;
    return svc_get_status(svc_name, &state, NULL);
}

bool svc_start(const wchar_t* svc_name) {
    if (!svc_name) return false;

    SC_HANDLE hSCM = OpenSCManagerW(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!hSCM) return false;

    SC_HANDLE hSvc = OpenServiceW(hSCM, svc_name, SERVICE_START | SERVICE_QUERY_STATUS);
    if (!hSvc) {
        CloseServiceHandle(hSCM);
        return false;
    }

    SERVICE_STATUS_PROCESS ssp;
    DWORD bytesNeeded = 0;
    if (QueryServiceStatusEx(hSvc, SC_STATUS_PROCESS_INFO, (LPBYTE)&ssp, sizeof(ssp), &bytesNeeded)) {
        if (ssp.dwCurrentState == SERVICE_RUNNING) {
            CloseServiceHandle(hSvc);
            CloseServiceHandle(hSCM);
            return true;
        }
    }

    if (!StartServiceW(hSvc, 0, NULL)) {
        DWORD err = GetLastError();
        if (err != ERROR_SERVICE_ALREADY_RUNNING) {
            CloseServiceHandle(hSvc);
            CloseServiceHandle(hSCM);
            return false;
        }
    }

    // Wait up to 5 seconds for SERVICE_RUNNING
    for (int i = 0; i < 20; i++) {
        Sleep(250);
        if (QueryServiceStatusEx(hSvc, SC_STATUS_PROCESS_INFO, (LPBYTE)&ssp, sizeof(ssp), &bytesNeeded)) {
            if (ssp.dwCurrentState == SERVICE_RUNNING) {
                CloseServiceHandle(hSvc);
                CloseServiceHandle(hSCM);
                return true;
            }
        }
    }

    CloseServiceHandle(hSvc);
    CloseServiceHandle(hSCM);
    return true;
}

bool svc_stop(const wchar_t* svc_name) {
    if (!svc_name) return false;

    SC_HANDLE hSCM = OpenSCManagerW(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!hSCM) return false;

    SC_HANDLE hSvc = OpenServiceW(hSCM, svc_name, SERVICE_STOP | SERVICE_QUERY_STATUS);
    if (!hSvc) {
        CloseServiceHandle(hSCM);
        return false;
    }

    SERVICE_STATUS_PROCESS ssp;
    DWORD bytesNeeded = 0;
    if (QueryServiceStatusEx(hSvc, SC_STATUS_PROCESS_INFO, (LPBYTE)&ssp, sizeof(ssp), &bytesNeeded)) {
        if (ssp.dwCurrentState == SERVICE_STOPPED) {
            CloseServiceHandle(hSvc);
            CloseServiceHandle(hSCM);
            return true;
        }
    }

    SERVICE_STATUS ss;
    ControlService(hSvc, SERVICE_CONTROL_STOP, &ss);

    // Wait up to 5 seconds for SERVICE_STOPPED
    for (int i = 0; i < 20; i++) {
        Sleep(250);
        if (QueryServiceStatusEx(hSvc, SC_STATUS_PROCESS_INFO, (LPBYTE)&ssp, sizeof(ssp), &bytesNeeded)) {
            if (ssp.dwCurrentState == SERVICE_STOPPED) {
                CloseServiceHandle(hSvc);
                CloseServiceHandle(hSCM);
                return true;
            }
        }
    }

    CloseServiceHandle(hSvc);
    CloseServiceHandle(hSCM);
    return true;
}

bool svc_restart(const wchar_t* svc_name) {
    svc_stop(svc_name);
    Sleep(500);
    return svc_start(svc_name);
}

static bool file_exists(const wchar_t* path) {
    DWORD attr = GetFileAttributesW(path);
    return (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY));
}

bool svc_ensure_all_installed_and_running(const wchar_t* base_path) {
    if (!base_path) return false;

    wchar_t exe_path[MAX_PATH];
    wchar_t cmd_line[MAX_PATH * 2];

    // 1. Leo4Proxy
    swprintf_s(exe_path, MAX_PATH, L"%s\\leo4proxy\\bin\\leo4proxy.exe", base_path);
    if (!file_exists(exe_path)) {
        swprintf_s(exe_path, MAX_PATH, L"%s\\leo4proxy\\leo4proxy.exe", base_path);
    }
    if (file_exists(exe_path)) {
        swprintf_s(cmd_line, sizeof(cmd_line)/sizeof(wchar_t), L"\"%s\"", exe_path);
        svc_install(SVC_NAME_LEO4PROXY,
                    L"Leo4 mTLS Proxy Service",
                    cmd_line,
                    SERVICE_AUTO_START,
                    L"Leo4 SChannel mTLS Proxy for MQTT and HTTP",
                    NULL);
    }

    // 2. Mosquitto
    swprintf_s(exe_path, MAX_PATH, L"%s\\mosquitto\\mosquitto.exe", base_path);
    if (!file_exists(exe_path)) {
        swprintf_s(exe_path, MAX_PATH, L"%s\\mosquitto\\bin\\mosquitto.exe", base_path);
    }
    if (file_exists(exe_path)) {
        wchar_t conf_path[MAX_PATH];
        swprintf_s(conf_path, MAX_PATH, L"%s\\mosquitto\\mosquitto.conf", base_path);
        swprintf_s(cmd_line, sizeof(cmd_line)/sizeof(wchar_t), L"\"%s\" run -c \"%s\"", exe_path, conf_path);
        svc_install(SVC_NAME_MOSQUITTO,
                    L"Mosquitto Broker",
                    cmd_line,
                    SERVICE_AUTO_START,
                    L"Mosquitto MQTT local broker and bridge to Leo4",
                    SVC_NAME_LEO4PROXY);
    }

    // 3. L4Con
    swprintf_s(exe_path, MAX_PATH, L"%s\\l4con\\bin\\l4con.exe", base_path);
    if (!file_exists(exe_path)) {
        swprintf_s(exe_path, MAX_PATH, L"%s\\l4con\\l4con.exe", base_path);
    }
    if (file_exists(exe_path)) {
        swprintf_s(cmd_line, sizeof(cmd_line)/sizeof(wchar_t), L"\"%s\"", exe_path);
        svc_install(SVC_NAME_L4CON,
                    L"Leo4 Diagnostic Console Agent",
                    cmd_line,
                    SERVICE_AUTO_START,
                    L"Leo4 Diagnostic Console Agent (l4con)",
                    SVC_NAME_MOSQUITTO);
    }

    // 4. L4Superv
    swprintf_s(exe_path, MAX_PATH, L"%s\\l4superv\\bin\\l4superv.exe", base_path);
    if (!file_exists(exe_path)) {
        swprintf_s(exe_path, MAX_PATH, L"%s\\l4superv\\l4superv.exe", base_path);
    }
    if (file_exists(exe_path)) {
        swprintf_s(cmd_line, sizeof(cmd_line)/sizeof(wchar_t), L"\"%s\"", exe_path);
        svc_install(SVC_NAME_L4SUPERV,
                    L"Leo4 Supervisor & Watchdog",
                    cmd_line,
                    SERVICE_AUTO_START,
                    L"Leo4 Services Supervisor, State Orchestrator and Watchdog",
                    NULL);
    }

    // Start services in dependency order
    if (svc_exists(SVC_NAME_LEO4PROXY) && !svc_is_running(SVC_NAME_LEO4PROXY)) {
        svc_start(SVC_NAME_LEO4PROXY);
    }
    if (svc_exists(SVC_NAME_MOSQUITTO) && !svc_is_running(SVC_NAME_MOSQUITTO)) {
        svc_start(SVC_NAME_MOSQUITTO);
    }
    if (svc_exists(SVC_NAME_L4CON) && !svc_is_running(SVC_NAME_L4CON)) {
        svc_start(SVC_NAME_L4CON);
    }

    return true;
}
