#include "service_mgr.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <aclapi.h>
#include <sddl.h>

#pragma comment(lib, "advapi32.lib")

static void w_to_utf8(const wchar_t* wstr, char* out_buf, size_t out_size) {
    if (!out_buf || out_size == 0) return;
    out_buf[0] = '\0';
    if (!wstr) return;
    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, out_buf, (int)out_size, NULL, NULL);
}

bool svc_set_dir_permissions(const wchar_t* dir_path) {
    if (!dir_path || dir_path[0] == L'\0') return false;
    CreateDirectoryW(dir_path, NULL);

    PSECURITY_DESCRIPTOR pSD = NULL;
    if (ConvertStringSecurityDescriptorToSecurityDescriptorW(
            L"D:(A;OICI;GA;;;WD)(A;OICI;GA;;;BU)(A;OICI;GA;;;SY)(A;OICI;GA;;;BA)",
            SDDL_REVISION_1, &pSD, NULL)) {
        PACL pDacl = NULL;
        BOOL daclPresent = FALSE, daclDefaulted = FALSE;
        if (GetSecurityDescriptorDacl(pSD, &daclPresent, &pDacl, &daclDefaulted) && daclPresent && pDacl) {
            SetNamedSecurityInfoW(
                (LPWSTR)dir_path,
                SE_FILE_OBJECT,
                DACL_SECURITY_INFORMATION | UNPROTECTED_DACL_SECURITY_INFORMATION,
                NULL, NULL, pDacl, NULL
            );
        }
        LocalFree(pSD);
        return true;
    }
    return false;
}

bool svc_set_mosquitto_log_permissions(const wchar_t* dir_path) {
    if (!dir_path || dir_path[0] == L'\0') return false;
    DWORD attributes = GetFileAttributesW(dir_path);
    if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY)) return false;
    PSECURITY_DESCRIPTOR descriptor = NULL;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            L"D:P(A;OICI;GA;;;SY)(A;OICI;GA;;;BA)", SDDL_REVISION_1,
            &descriptor, NULL)) return false;
    PACL acl = NULL;
    BOOL present = FALSE, defaulted = FALSE;
    bool ok = false;
    if (GetSecurityDescriptorDacl(descriptor, &present, &acl, &defaulted) && present && acl) {
        DWORD result = SetNamedSecurityInfoW((LPWSTR)dir_path, SE_FILE_OBJECT,
            DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
            NULL, NULL, acl, NULL);
        ok = result == ERROR_SUCCESS;
        if (!ok) SetLastError(result);
    }
    LocalFree(descriptor);
    return ok;
}

bool svc_get_binary_path(const wchar_t* svc_name, wchar_t* out_bin_path, size_t out_size) {
    if (!svc_name || !out_bin_path || out_size == 0) return false;
    out_bin_path[0] = L'\0';

    SC_HANDLE hSCM = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT);
    if (!hSCM) return false;

    SC_HANDLE hSvc = OpenServiceW(hSCM, svc_name, SERVICE_QUERY_CONFIG);
    if (!hSvc) {
        CloseServiceHandle(hSCM);
        return false;
    }

    DWORD bytesNeeded = 0;
    QueryServiceConfigW(hSvc, NULL, 0, &bytesNeeded);
    if (GetLastError() == ERROR_INSUFFICIENT_BUFFER && bytesNeeded > 0) {
        LPQUERY_SERVICE_CONFIGW pConfig = (LPQUERY_SERVICE_CONFIGW)malloc(bytesNeeded);
        if (pConfig) {
            if (QueryServiceConfigW(hSvc, pConfig, bytesNeeded, &bytesNeeded)) {
                if (pConfig->lpBinaryPathName) {
                    wcscpy_s(out_bin_path, out_size, pConfig->lpBinaryPathName);
                }
            }
            free(pConfig);
        }
    }

    CloseServiceHandle(hSvc);
    CloseServiceHandle(hSCM);
    return (out_bin_path[0] != L'\0');
}

bool svc_get_process_image(DWORD pid, wchar_t* out_exe_path, size_t out_size) {
    if (!out_exe_path || out_size == 0) return false;
    out_exe_path[0] = L'\0';
    if (pid == 0) return false;

    HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hProc) {
        hProc = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pid);
    }
    if (!hProc) return false;

    DWORD size = (DWORD)out_size;
    BOOL ok = QueryFullProcessImageNameW(hProc, 0, out_exe_path, &size);
    CloseHandle(hProc);
    return (ok != 0);
}

bool svc_inspect(const wchar_t* svc_name,
                 const wchar_t* expected_base_path,
                 const wchar_t* relative_bin,
                 const wchar_t* relative_conf,
                 const wchar_t* relative_log,
                 L4ServiceState* out_state) {
    if (!svc_name || !expected_base_path || !out_state) return false;
    memset(out_state, 0, sizeof(*out_state));

    wchar_t expected_exe[MAX_PATH];
    swprintf_s(expected_exe, MAX_PATH, L"%ls\\%ls", expected_base_path, relative_bin ? relative_bin : L"");
    w_to_utf8(expected_exe, out_state->installed_path, sizeof(out_state->installed_path));

    if (relative_conf && relative_conf[0] != L'\0') {
        wchar_t conf_full[MAX_PATH];
        swprintf_s(conf_full, MAX_PATH, L"%ls\\%ls", expected_base_path, relative_conf);
        w_to_utf8(conf_full, out_state->config_path, sizeof(out_state->config_path));
    }

    if (relative_log && relative_log[0] != L'\0') {
        wchar_t log_full[MAX_PATH];
        swprintf_s(log_full, MAX_PATH, L"%ls\\%ls", expected_base_path, relative_log);
        w_to_utf8(log_full, out_state->log_path, sizeof(out_state->log_path));
    }

    DWORD state = 0, pid = 0;
    if (!svc_get_status(svc_name, &state, &pid)) {
        strcpy_s(out_state->status, sizeof(out_state->status), "NOT_INSTALLED");
        out_state->path_match = false;
        return true;
    }

    out_state->runtime_pid = pid;
    if (state == SERVICE_RUNNING) {
        strcpy_s(out_state->status, sizeof(out_state->status), "RUNNING");
    } else if (state == SERVICE_STOPPED) {
        strcpy_s(out_state->status, sizeof(out_state->status), "STOPPED");
    } else {
        strcpy_s(out_state->status, sizeof(out_state->status), "PENDING");
    }

    if (pid > 0) {
        wchar_t exe_image[MAX_PATH] = { 0 };
        if (svc_get_process_image(pid, exe_image, MAX_PATH)) {
            w_to_utf8(exe_image, out_state->runtime_exe, sizeof(out_state->runtime_exe));
            size_t base_len = wcslen(expected_base_path);
            if (_wcsnicmp(exe_image, expected_base_path, base_len) == 0) {
                out_state->path_match = true;
            } else {
                out_state->path_match = false;
                strcpy_s(out_state->status, sizeof(out_state->status), "MISMATCH");
            }
        } else {
            wchar_t scm_bin[MAX_PATH] = { 0 };
            if (svc_get_binary_path(svc_name, scm_bin, MAX_PATH)) {
                w_to_utf8(scm_bin, out_state->runtime_exe, sizeof(out_state->runtime_exe));
                const wchar_t* p = scm_bin;
                while (*p == L'\"' || *p == L' ') p++;
                size_t base_len = wcslen(expected_base_path);
                out_state->path_match = (_wcsnicmp(p, expected_base_path, base_len) == 0);
            }
        }
    } else {
        wchar_t scm_bin[MAX_PATH] = { 0 };
        if (svc_get_binary_path(svc_name, scm_bin, MAX_PATH)) {
            w_to_utf8(scm_bin, out_state->runtime_exe, sizeof(out_state->runtime_exe));
            const wchar_t* p = scm_bin;
            while (*p == L'\"' || *p == L' ') p++;
            size_t base_len = wcslen(expected_base_path);
            out_state->path_match = (_wcsnicmp(p, expected_base_path, base_len) == 0);
        }
    }

    return true;
}

bool svc_cleanup_foreign(const wchar_t* svc_name, const wchar_t* expected_base_path) {
    if (!svc_name || !expected_base_path) return false;

    // If checking mosquitto, also ensure legacy upper-case Mosquitto service is cleaned up
    if (_wcsicmp(svc_name, L"mosquitto") == 0) {
        DWORD leg_state = 0, leg_pid = 0;
        if (svc_get_status(L"Mosquitto", &leg_state, &leg_pid)) {
            svc_stop_and_kill(L"Mosquitto");
            svc_uninstall(L"Mosquitto");
        }
    }

    DWORD state = 0, pid = 0;
    if (!svc_get_status(svc_name, &state, &pid)) {
        return true; // Not installed
    }

    wchar_t scm_bin[MAX_PATH] = { 0 };
    svc_get_binary_path(svc_name, scm_bin, MAX_PATH);

    wchar_t proc_exe[MAX_PATH] = { 0 };
    if (pid > 0) {
        svc_get_process_image(pid, proc_exe, MAX_PATH);
    }

    const wchar_t* p_scm = scm_bin;
    while (*p_scm == L'\"' || *p_scm == L' ') p_scm++;

    size_t base_len = wcslen(expected_base_path);
    bool scm_matches = (scm_bin[0] != L'\0' && _wcsnicmp(p_scm, expected_base_path, base_len) == 0);
    bool proc_matches = (pid == 0 || (proc_exe[0] != L'\0' && _wcsnicmp(proc_exe, expected_base_path, base_len) == 0));

    if (!scm_matches || !proc_matches) {
        wprintf(L"  [MIGRATION] Foreign/out-of-tree service detected: %ls\n", svc_name);
        if (scm_bin[0] != L'\0') {
            wprintf(L"              SCM Path: %ls\n", scm_bin);
        }
        if (proc_exe[0] != L'\0') {
            wprintf(L"              Running Process (PID %lu): %ls\n", pid, proc_exe);
        }
        wprintf(L"              Stopping and reinstalling cleanly to: %ls\n", expected_base_path);

        svc_stop(svc_name);

        if (pid > 0) {
            HANDLE hProc = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
            if (hProc) {
                TerminateProcess(hProc, 1);
                CloseHandle(hProc);
            }
        }

        svc_uninstall(svc_name);
        Sleep(500);
    }

    return true;
}

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

    if (description) {
        SERVICE_DESCRIPTIONW sd;
        sd.lpDescription = (LPWSTR)description;
        ChangeServiceConfig2W(hSvc, SERVICE_CONFIG_DESCRIPTION, &sd);
    }

    // Configure Auto-Recovery (Restart service on failure after 5s, 10s, 30s)
    SC_ACTION actions[3];
    actions[0].Type = SC_ACTION_RESTART;
    actions[0].Delay = 5000;
    actions[1].Type = SC_ACTION_RESTART;
    actions[1].Delay = 10000;
    actions[2].Type = SC_ACTION_RESTART;
    actions[2].Delay = 30000;

    SERVICE_FAILURE_ACTIONSW sfa;
    memset(&sfa, 0, sizeof(sfa));
    sfa.dwResetPeriod = 86400;
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

bool svc_stop_and_kill(const wchar_t* svc_name) {
    if (!svc_name) return false;

    DWORD state = 0, pid = 0;
    if (svc_get_status(svc_name, &state, &pid)) {
        if (state != SERVICE_STOPPED) {
            svc_stop(svc_name);
        }
        if (pid > 0) {
            HANDLE hProc = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
            if (hProc) {
                TerminateProcess(hProc, 1);
                CloseHandle(hProc);
            }
        }
    }
    return true;
}

bool svc_restart(const wchar_t* svc_name) {
    svc_stop_and_kill(svc_name);
    Sleep(500);
    return svc_start(svc_name);
}

static bool file_exists(const wchar_t* path) {
    DWORD attr = GetFileAttributesW(path);
    return (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY));
}

bool svc_ensure_all_installed_and_running(const wchar_t* base_path) {
    if (!base_path) return false;

    wchar_t mosq_log_dir[MAX_PATH];
    swprintf_s(mosq_log_dir, MAX_PATH, L"%s\\mosquitto\\log", base_path);
    if (!CreateDirectoryW(mosq_log_dir, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) return false;
    if (!svc_set_mosquitto_log_permissions(mosq_log_dir)) return false;

    // Ensure MOSQUITTO_DIR system and process environment variable is set
    wchar_t mosq_dir[MAX_PATH];
    swprintf_s(mosq_dir, MAX_PATH, L"%s\\mosquitto", base_path);
    SetEnvironmentVariableW(L"MOSQUITTO_DIR", mosq_dir);

    HKEY hEnvKey;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment", 0, KEY_SET_VALUE, &hEnvKey) == ERROR_SUCCESS) {
        RegSetValueExW(hEnvKey, L"MOSQUITTO_DIR", 0, REG_SZ, (const BYTE*)mosq_dir, (DWORD)((wcslen(mosq_dir) + 1) * sizeof(wchar_t)));
        RegCloseKey(hEnvKey);
        DWORD_PTR dwResult = 0;
        SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0, (LPARAM)L"Environment", SMTO_ABORTIFHUNG, 2000, &dwResult);
    }

    wchar_t exe_path[MAX_PATH];
    wchar_t cmd_line[MAX_PATH * 2];

    // 1. Leo4Proxy
    svc_cleanup_foreign(SVC_NAME_LEO4PROXY, base_path);
    swprintf_s(exe_path, MAX_PATH, L"%s\\leo4proxy\\leo4proxy.exe", base_path);
    if (!file_exists(exe_path)) {
        swprintf_s(exe_path, MAX_PATH, L"%s\\leo4proxy\\bin\\leo4proxy.exe", base_path);
    }
    if (file_exists(exe_path)) {
        swprintf_s(cmd_line, sizeof(cmd_line)/sizeof(wchar_t), L"\"%s\" --service", exe_path);
        svc_install(SVC_NAME_LEO4PROXY,
                    L"Leo4 mTLS Proxy Service",
                    cmd_line,
                    SERVICE_AUTO_START,
                    L"Leo4 SChannel mTLS Proxy for MQTT and HTTP",
                    NULL);
    }

    // 2. Mosquitto
    svc_cleanup_foreign(SVC_NAME_MOSQUITTO, base_path);
    swprintf_s(exe_path, MAX_PATH, L"%s\\mosquitto\\mosquitto.exe", base_path);
    if (!file_exists(exe_path)) {
        swprintf_s(exe_path, MAX_PATH, L"%s\\mosquitto\\bin\\mosquitto.exe", base_path);
    }
    if (file_exists(exe_path)) {
        swprintf_s(cmd_line, sizeof(cmd_line)/sizeof(wchar_t), L"\"%s\" run", exe_path);
        svc_install(SVC_NAME_MOSQUITTO,
                    L"Mosquitto Broker",
                    cmd_line,
                    SERVICE_AUTO_START,
                    L"Mosquitto MQTT local broker and bridge to Leo4",
                    SVC_NAME_LEO4PROXY);
    }

    // 3. L4Con
    svc_cleanup_foreign(SVC_NAME_L4CON, base_path);
    swprintf_s(exe_path, MAX_PATH, L"%s\\l4con\\l4con.exe", base_path);
    if (!file_exists(exe_path)) {
        swprintf_s(exe_path, MAX_PATH, L"%s\\l4con\\bin\\l4con.exe", base_path);
    }
    if (file_exists(exe_path)) {
        swprintf_s(cmd_line, sizeof(cmd_line)/sizeof(wchar_t), L"\"%s\" --service", exe_path);
        svc_install(SVC_NAME_L4CON,
                    L"Leo4 Diagnostic Console Agent",
                    cmd_line,
                    SERVICE_AUTO_START,
                    L"Leo4 Diagnostic Console Agent (l4con)",
                    SVC_NAME_MOSQUITTO);
    }

    // 4. L4Superv
    svc_cleanup_foreign(SVC_NAME_L4SUPERV, base_path);
    swprintf_s(exe_path, MAX_PATH, L"%s\\l4superv\\l4superv.exe", base_path);
    if (!file_exists(exe_path)) {
        swprintf_s(exe_path, MAX_PATH, L"%s\\l4superv\\bin\\l4superv.exe", base_path);
    }
    if (file_exists(exe_path)) {
        swprintf_s(cmd_line, sizeof(cmd_line)/sizeof(wchar_t), L"\"%s\"", exe_path);
        svc_install(SVC_NAME_L4SUPERV,
                    L"Leo4 Supervisor & Watchdog",
                    cmd_line,
                    SERVICE_AUTO_START,
                    L"Leo4 Service Orchestrator and Watchdog",
                    NULL);
    }

    // Start all services in order
    svc_start(SVC_NAME_LEO4PROXY);
    svc_start(SVC_NAME_MOSQUITTO);
    svc_start(SVC_NAME_L4CON);
    svc_start(SVC_NAME_L4SUPERV);

    return true;
}
