#include "services.h"
#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <aclapi.h>
#include <sddl.h>

#pragma comment(lib, "advapi32.lib")

static bool set_dir_permissions(const wchar_t* dir_path) {
    if (!dir_path) return false;
    CreateDirectoryW(dir_path, NULL);

    PSECURITY_DESCRIPTOR pSD = NULL;
    // Grant full control to SYSTEM, Administrators and Everyone
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            L"D:(A;OICI;GA;;;SY)(A;OICI;GA;;;BA)(A;OICI;GA;;;WD)",
            SDDL_REVISION_1,
            &pSD,
            NULL)) {
        return false;
    }

    PACL pDacl = NULL;
    BOOL bDaclPresent = FALSE, bDaclDefaulted = FALSE;
    GetSecurityDescriptorDacl(pSD, &bDaclPresent, &pDacl, &bDaclDefaulted);

    DWORD res = SetNamedSecurityInfoW(
        (LPWSTR)dir_path,
        SE_FILE_OBJECT,
        DACL_SECURITY_INFORMATION,
        NULL,
        NULL,
        pDacl,
        NULL
    );

    LocalFree(pSD);
    return (res == ERROR_SUCCESS);
}

bool services_configure_environment(const wchar_t* dest_dir) {
    if (!dest_dir) return false;

    log_info("Configuring system environment variables and PATH...");

    // 1. Ensure MOSQUITTO_DIR
    wchar_t mosq_dir[MAX_PATH];
    swprintf_s(mosq_dir, MAX_PATH, L"%ls\\mosquitto", dest_dir);
    SetEnvironmentVariableW(L"MOSQUITTO_DIR", mosq_dir);

    wchar_t mosq_log[MAX_PATH];
    swprintf_s(mosq_log, MAX_PATH, L"%ls\\mosquitto\\log", dest_dir);
    set_dir_permissions(mosq_log);

    // 2. Registry Environment
    HKEY hKey = NULL;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment", 0, KEY_READ | KEY_WRITE, &hKey) == ERROR_SUCCESS) {
        RegSetValueExW(hKey, L"MOSQUITTO_DIR", 0, REG_SZ, (const BYTE*)mosq_dir, (DWORD)((wcslen(mosq_dir) + 1) * sizeof(wchar_t)));

        // Read Path
        DWORD dwType = 0;
        DWORD dwSize = 0;
        if (RegQueryValueExW(hKey, L"Path", NULL, &dwType, NULL, &dwSize) == ERROR_SUCCESS && dwSize > 0) {
            DWORD buf_size = dwSize + 4096;
            wchar_t* pPath = (wchar_t*)malloc(buf_size);
            if (pPath) {
                if (RegQueryValueExW(hKey, L"Path", NULL, &dwType, (LPBYTE)pPath, &dwSize) == ERROR_SUCCESS) {
                    const wchar_t* subdirs[] = {
                        L"l4sql",
                        L"l4pin",
                        L"l4con",
                        L"l4superv",
                        L"l4desk",
                        L"l4capture\\bin",
                        L"ffmpeg"
                    };

                    bool modified = false;
                    for (int i = 0; i < (int)(sizeof(subdirs)/sizeof(subdirs[0])); i++) {
                        wchar_t target[MAX_PATH];
                        swprintf_s(target, MAX_PATH, L"%ls\\%ls", dest_dir, subdirs[i]);

                        if (wcsstr(pPath, target) == NULL) {
                            size_t cur_len = wcslen(pPath);
                            if (cur_len > 0 && pPath[cur_len - 1] != L';') {
                                wcscat_s(pPath, buf_size / sizeof(wchar_t), L";");
                            }
                            wcscat_s(pPath, buf_size / sizeof(wchar_t), target);
                            modified = true;
                        }
                    }

                    if (modified) {
                        RegSetValueExW(hKey, L"Path", 0, dwType, (const BYTE*)pPath, (DWORD)((wcslen(pPath) + 1) * sizeof(wchar_t)));
                        SetEnvironmentVariableW(L"Path", pPath);
                    }
                }
                free(pPath);
            }
        }
        RegCloseKey(hKey);

        // Notify environment change
        DWORD_PTR dwResult = 0;
        SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0, (LPARAM)L"Environment", SMTO_ABORTIFHUNG, 2000, &dwResult);
    }

    return true;
}

static bool register_or_update_service(
    SC_HANDLE hSCM,
    const wchar_t* svc_name,
    const wchar_t* display_name,
    const wchar_t* cmd_line,
    const wchar_t* description,
    const wchar_t* dependencies
) {
    if (!hSCM || !svc_name || !cmd_line) return false;

    SC_HANDLE hSvc = OpenServiceW(hSCM, svc_name, SERVICE_QUERY_CONFIG | SERVICE_CHANGE_CONFIG);
    if (hSvc) {
        // Service already exists: check if binary path and dependencies match
        DWORD bytesNeeded = 0;
        QueryServiceConfigW(hSvc, NULL, 0, &bytesNeeded);
        if (bytesNeeded > 0) {
            LPQUERY_SERVICE_CONFIGW pConfig = (LPQUERY_SERVICE_CONFIGW)malloc(bytesNeeded);
            if (pConfig) {
                if (QueryServiceConfigW(hSvc, pConfig, bytesNeeded, &bytesNeeded)) {
                    bool bin_matches = (_wcsicmp(pConfig->lpBinaryPathName, cmd_line) == 0);
                    bool deps_match = false;
                    if (!dependencies || dependencies[0] == L'\0') {
                        deps_match = (!pConfig->lpDependencies || pConfig->lpDependencies[0] == L'\0');
                    } else if (pConfig->lpDependencies) {
                        size_t dep_len = wcslen(dependencies);
                        deps_match = (wcscmp(pConfig->lpDependencies, dependencies) == 0 &&
                                      pConfig->lpDependencies[dep_len + 1] == L'\0');
                    }

                    if (bin_matches && deps_match) {
                        log_info("Service %ls already configured with correct path (%ls).", svc_name, cmd_line);
                        free(pConfig);
                        CloseServiceHandle(hSvc);
                        return true;
                    }
                }
                free(pConfig);
            }
        }

        log_info("Updating service configuration for %ls...", svc_name);
        BOOL changed = ChangeServiceConfigW(
            hSvc,
            SERVICE_WIN32_OWN_PROCESS,
            SERVICE_AUTO_START,
            SERVICE_ERROR_NORMAL,
            cmd_line,
            NULL,
            NULL,
            dependencies ? dependencies : L"",
            NULL,
            NULL,
            display_name
        );
        if (!changed) {
            DWORD err = GetLastError();
            log_err("Failed to update service config for %ls (error %lu)", svc_name, err);
        }
        CloseServiceHandle(hSvc);
        return (changed != FALSE);
    }

    // Service does not exist: create it
    log_info("Creating service %ls (%ls)...", svc_name, cmd_line);
    hSvc = CreateServiceW(
        hSCM,
        svc_name,
        display_name,
        SERVICE_ALL_ACCESS,
        SERVICE_WIN32_OWN_PROCESS,
        SERVICE_AUTO_START,
        SERVICE_ERROR_NORMAL,
        cmd_line,
        NULL,
        NULL,
        dependencies,
        NULL,
        NULL
    );

    if (!hSvc) {
        DWORD err = GetLastError();
        log_err("Failed to create service %ls (error %lu)", svc_name, err);
        return false;
    }

    if (description) {
        SERVICE_DESCRIPTIONW sd;
        sd.lpDescription = (LPWSTR)description;
        ChangeServiceConfig2W(hSvc, SERVICE_CONFIG_DESCRIPTION, &sd);
    }

    CloseServiceHandle(hSvc);
    return true;
}

bool services_ensure_all_registered(const wchar_t* dest_dir) {
    if (!dest_dir) return false;

    SC_HANDLE hSCM = OpenSCManagerW(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!hSCM) {
        log_err("Failed to open Service Control Manager (error %lu)", GetLastError());
        return false;
    }

    wchar_t cmd[MAX_PATH * 2];

    // 1. Leo4Proxy
    swprintf_s(cmd, sizeof(cmd)/sizeof(wchar_t), L"\"%ls\\leo4proxy\\leo4proxy.exe\" --service --rtp-tunnel", dest_dir);
    if (!register_or_update_service(
            hSCM,
            SVC_NAME_LEO4PROXY,
            L"Leo4 mTLS Proxy Service",
            cmd,
            L"Leo4 SChannel mTLS Proxy for MQTT and HTTP",
            NULL)) {
        CloseServiceHandle(hSCM);
        return false;
    }

    // 2. Mosquitto
    swprintf_s(cmd, sizeof(cmd)/sizeof(wchar_t), L"\"%ls\\mosquitto\\mosquitto.exe\" run", dest_dir);
    if (!register_or_update_service(
            hSCM,
            SVC_NAME_MOSQUITTO,
            L"Mosquitto Broker",
            cmd,
            L"Mosquitto MQTT local broker and bridge to Leo4",
            NULL)) {
        CloseServiceHandle(hSCM);
        return false;
    }

    // 3. L4Con (strictly depends on mosquitto, double null-terminated: L"mosquitto\0\0")
    static const wchar_t l4con_deps[] = L"mosquitto\0";
    swprintf_s(cmd, sizeof(cmd)/sizeof(wchar_t), L"\"%ls\\l4con\\l4con.exe\" --service", dest_dir);
    if (!register_or_update_service(
            hSCM,
            SVC_NAME_L4CON,
            L"Leo4 Diagnostic Console Agent",
            cmd,
            L"Leo4 Diagnostic Console Agent (l4con)",
            l4con_deps)) {
        CloseServiceHandle(hSCM);
        return false;
    }

    // 4. L4Superv
    swprintf_s(cmd, sizeof(cmd)/sizeof(wchar_t), L"\"%ls\\l4superv\\l4superv.exe\"", dest_dir);
    if (!register_or_update_service(
            hSCM,
            SVC_NAME_L4SUPERV,
            L"Leo4 Supervisor & Watchdog",
            cmd,
            L"Leo4 Service Orchestrator and Watchdog",
            NULL)) {
        CloseServiceHandle(hSCM);
        return false;
    }

    CloseServiceHandle(hSCM);
    log_info("All 4 services registered and verified successfully.");
    return true;
}

DWORD services_query_status(const wchar_t* svc_name) {
    if (!svc_name) return 0;
    SC_HANDLE hSCM = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT);
    if (!hSCM) return 0;

    SC_HANDLE hSvc = OpenServiceW(hSCM, svc_name, SERVICE_QUERY_STATUS);
    if (!hSvc) {
        CloseServiceHandle(hSCM);
        return 0;
    }

    SERVICE_STATUS_PROCESS ssp;
    DWORD bytesNeeded = 0;
    DWORD state = 0;
    if (QueryServiceStatusEx(hSvc, SC_STATUS_PROCESS_INFO, (LPBYTE)&ssp, sizeof(ssp), &bytesNeeded)) {
        state = ssp.dwCurrentState;
    }

    CloseServiceHandle(hSvc);
    CloseServiceHandle(hSCM);
    return state;
}

bool services_start_single_service(
    SC_HANDLE hSCM,
    const wchar_t* svc_name,
    DWORD timeout_sec,
    ServiceLifecycleCallback cb,
    void* user_data
) {
    if (!hSCM || !svc_name) return false;
    if (timeout_sec == 0) timeout_sec = 120;

    SC_HANDLE hSvc = OpenServiceW(hSCM, svc_name, SERVICE_START | SERVICE_QUERY_STATUS);
    if (!hSvc) {
        log_err("Cannot open service %ls to start (error %lu)", svc_name, GetLastError());
        if (cb) cb(svc_name, SVC_STATUS_FAILED, 0, "Cannot open service in SCM", user_data);
        return false;
    }

    SERVICE_STATUS_PROCESS ssp;
    DWORD bytesNeeded = 0;
    if (QueryServiceStatusEx(hSvc, SC_STATUS_PROCESS_INFO, (LPBYTE)&ssp, sizeof(ssp), &bytesNeeded)) {
        if (ssp.dwCurrentState == SERVICE_RUNNING) {
            log_info("Service %ls is already RUNNING (PID: %lu). Verifying without restart.", svc_name, ssp.dwProcessId);
            if (cb) cb(svc_name, SVC_STATUS_RUNNING, 0, "Already running", user_data);
            CloseServiceHandle(hSvc);
            return true;
        }
    }

    if (cb) cb(svc_name, SVC_STATUS_STARTING, 0, NULL, user_data);
    log_info("Starting service %ls...", svc_name);

    if (ssp.dwCurrentState != SERVICE_START_PENDING) {
        if (!StartServiceW(hSvc, 0, NULL)) {
            DWORD err = GetLastError();
            if (err == ERROR_SERVICE_ALREADY_RUNNING) {
                // Re-query state
                if (QueryServiceStatusEx(hSvc, SC_STATUS_PROCESS_INFO, (LPBYTE)&ssp, sizeof(ssp), &bytesNeeded)) {
                    if (ssp.dwCurrentState == SERVICE_RUNNING) {
                        log_info("Service %ls is RUNNING.", svc_name);
                        if (cb) cb(svc_name, SVC_STATUS_RUNNING, 0, NULL, user_data);
                        CloseServiceHandle(hSvc);
                        return true;
                    }
                }
            } else {
                log_err("StartServiceW failed for %ls (error %lu)", svc_name, err);
                if (cb) cb(svc_name, SVC_STATUS_FAILED, 0, "StartServiceW call failed", user_data);
                CloseServiceHandle(hSvc);
                return false;
            }
        }
    }

    ULONGLONG start_tick = GetTickCount64();
    bool notice_30s_given = false;

    while (true) {
        DWORD elapsed_sec = (DWORD)((GetTickCount64() - start_tick) / 1000);

        if (!notice_30s_given && elapsed_sec >= 30) {
            notice_30s_given = true;
            log_warn("Service %ls taking longer than usual; still waiting...", svc_name);
            if (cb) cb(svc_name, SVC_STATUS_STARTING, elapsed_sec, "Taking longer than usual; still waiting...", user_data);
        }

        if (elapsed_sec >= timeout_sec) {
            log_err("Service %ls did not reach RUNNING within %lu seconds timeout.", svc_name, timeout_sec);
            if (cb) cb(svc_name, SVC_STATUS_FAILED, elapsed_sec, "Startup timed out", user_data);
            CloseServiceHandle(hSvc);
            return false;
        }

        if (!QueryServiceStatusEx(hSvc, SC_STATUS_PROCESS_INFO, (LPBYTE)&ssp, sizeof(ssp), &bytesNeeded)) {
            log_err("QueryServiceStatusEx failed for %ls (error %lu)", svc_name, GetLastError());
            CloseServiceHandle(hSvc);
            return false;
        }

        if (ssp.dwCurrentState == SERVICE_RUNNING) {
            log_info("Service %ls reached RUNNING (PID: %lu) in %lu seconds.", svc_name, ssp.dwProcessId, elapsed_sec);
            if (cb) cb(svc_name, SVC_STATUS_RUNNING, elapsed_sec, NULL, user_data);
            CloseServiceHandle(hSvc);
            return true;
        }

        // Check for immediate failure when service stopped with an exit code
        if (ssp.dwCurrentState == SERVICE_STOPPED) {
            if (ssp.dwWin32ExitCode != NO_ERROR || ssp.dwServiceSpecificExitCode != 0) {
                log_err("Service %ls stopped immediately with error code: win32=%lu, specific=%lu",
                        svc_name, ssp.dwWin32ExitCode, ssp.dwServiceSpecificExitCode);
                if (cb) cb(svc_name, SVC_STATUS_FAILED, elapsed_sec, "Service stopped immediately with error", user_data);
                CloseServiceHandle(hSvc);
                return false;
            }
        }

        // Wait based on service wait hint (clamped between 500ms and 2000ms)
        DWORD wait_time = ssp.dwWaitHint / 10;
        if (wait_time < 500) wait_time = 500;
        if (wait_time > 2000) wait_time = 2000;

        Sleep(wait_time);
        if (cb) cb(svc_name, SVC_STATUS_STARTING, (DWORD)((GetTickCount64() - start_tick) / 1000), NULL, user_data);
    }
}

bool services_stop_single_service(
    SC_HANDLE hSCM,
    const wchar_t* svc_name,
    const char* sn,
    DWORD timeout_sec,
    ServiceLifecycleCallback cb,
    void* user_data
) {
    if (!hSCM || !svc_name) return false;
    if (timeout_sec == 0) timeout_sec = 120;

    SC_HANDLE hSvc = OpenServiceW(hSCM, svc_name, SERVICE_STOP | SERVICE_QUERY_STATUS);
    if (!hSvc) {
        // If service does not exist or cannot be opened, it's not running
        DWORD err = GetLastError();
        if (err == ERROR_SERVICE_DOES_NOT_EXIST) {
            return true;
        }
        log_warn("Cannot open service %ls to stop (error %lu)", svc_name, err);
        return true;
    }

    SERVICE_STATUS_PROCESS ssp;
    DWORD bytesNeeded = 0;
    if (QueryServiceStatusEx(hSvc, SC_STATUS_PROCESS_INFO, (LPBYTE)&ssp, sizeof(ssp), &bytesNeeded)) {
        if (ssp.dwCurrentState == SERVICE_STOPPED) {
            CloseServiceHandle(hSvc);
            if (cb) cb(svc_name, SVC_STATUS_STOPPED, 0, NULL, user_data);
            return true;
        }
    }

    if (cb) cb(svc_name, SVC_STATUS_STOPPING, 0, NULL, user_data);
    log_info("Stopping service %ls...", svc_name);

    // If stopping L4Superv: signal Global\L4Desk_Stop_<SN> event before sending SCM STOP
    if (_wcsicmp(svc_name, SVC_NAME_L4SUPERV) == 0 && sn && sn[0]) {
        wchar_t stop_evt[128];
        swprintf_s(stop_evt, 128, L"Global\\L4Desk_Stop_%hs", sn);
        HANDLE hEvt = OpenEventW(EVENT_MODIFY_STATE, FALSE, stop_evt);
        if (!hEvt) {
            hEvt = CreateEventW(NULL, TRUE, FALSE, stop_evt);
        }
        if (hEvt) {
            log_info("Signaling event %ls for graceful l4desk termination...", stop_evt);
            SetEvent(hEvt);
            CloseHandle(hEvt);
        }
    }

    if (ssp.dwCurrentState != SERVICE_STOP_PENDING) {
        SERVICE_STATUS ss;
        if (!ControlService(hSvc, SERVICE_CONTROL_STOP, &ss)) {
            DWORD err = GetLastError();
            if (err != ERROR_SERVICE_NOT_ACTIVE) {
                log_warn("ControlService(STOP) for %ls returned %lu", svc_name, err);
            }
        }
    }

    ULONGLONG start_tick = GetTickCount64();
    bool notice_30s_given = false;

    while (true) {
        DWORD elapsed_sec = (DWORD)((GetTickCount64() - start_tick) / 1000);

        if (!notice_30s_given && elapsed_sec >= 30) {
            notice_30s_given = true;
            log_warn("Stopping service %ls is taking longer than usual; still waiting...", svc_name);
            if (cb) cb(svc_name, SVC_STATUS_STOPPING, elapsed_sec, "Taking longer than usual; still waiting...", user_data);
        }

        if (elapsed_sec >= timeout_sec) {
            log_err("Service %ls did not reach STOPPED within %lu seconds timeout.", svc_name, timeout_sec);
            if (cb) cb(svc_name, SVC_STATUS_FAILED, elapsed_sec, "Stop timed out", user_data);
            CloseServiceHandle(hSvc);
            return false;
        }

        if (!QueryServiceStatusEx(hSvc, SC_STATUS_PROCESS_INFO, (LPBYTE)&ssp, sizeof(ssp), &bytesNeeded)) {
            CloseServiceHandle(hSvc);
            return true;
        }

        if (ssp.dwCurrentState == SERVICE_STOPPED) {
            log_info("Service %ls stopped in %lu seconds.", svc_name, elapsed_sec);
            if (cb) cb(svc_name, SVC_STATUS_STOPPED, elapsed_sec, NULL, user_data);
            CloseServiceHandle(hSvc);
            return true;
        }

        DWORD wait_time = ssp.dwWaitHint / 10;
        if (wait_time < 500) wait_time = 500;
        if (wait_time > 2000) wait_time = 2000;

        Sleep(wait_time);
        if (cb) cb(svc_name, SVC_STATUS_STOPPING, (DWORD)((GetTickCount64() - start_tick) / 1000), NULL, user_data);
    }
}

bool services_start_all_in_order(
    ServiceLifecycleCallback cb,
    void* user_data
) {
    SC_HANDLE hSCM = OpenSCManagerW(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!hSCM) {
        log_err("Failed to open SCM with ALL_ACCESS for starting services (error %lu)", GetLastError());
        return false;
    }

    log_info("Starting services in strict order: Leo4Proxy -> mosquitto -> L4Con -> L4Superv...");
    ULONGLONG overall_start = GetTickCount64();
    const DWORD max_overall_sec = 480;

    const wchar_t* svcs[] = {
        SVC_NAME_LEO4PROXY,
        SVC_NAME_MOSQUITTO,
        SVC_NAME_L4CON,
        SVC_NAME_L4SUPERV
    };

    bool all_ok = true;
    for (int i = 0; i < 4; i++) {
        DWORD elapsed = (DWORD)((GetTickCount64() - overall_start) / 1000);
        if (elapsed >= max_overall_sec) {
            log_err("Overall service start deadline (480s) exceeded.");
            all_ok = false;
            break;
        }

        DWORD remaining = max_overall_sec - elapsed;
        DWORD timeout = (remaining < 120) ? remaining : 120;

        if (!services_start_single_service(hSCM, svcs[i], timeout, cb, user_data)) {
            log_err("Failed to start service %ls", svcs[i]);
            all_ok = false;
            break;
        }
    }

    CloseServiceHandle(hSCM);
    return all_ok;
}

bool services_stop_all_in_order(
    const char* sn,
    ServiceLifecycleCallback cb,
    void* user_data
) {
    SC_HANDLE hSCM = OpenSCManagerW(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!hSCM) {
        log_err("Failed to open SCM with ALL_ACCESS for stopping services (error %lu)", GetLastError());
        return false;
    }

    log_info("Stopping services in reverse order: L4Superv -> L4Con -> mosquitto -> Leo4Proxy...");

    const wchar_t* svcs[] = {
        SVC_NAME_L4SUPERV,
        SVC_NAME_L4CON,
        SVC_NAME_MOSQUITTO,
        SVC_NAME_LEO4PROXY
    };

    bool all_ok = true;
    for (int i = 0; i < 4; i++) {
        if (!services_stop_single_service(hSCM, svcs[i], sn, 120, cb, user_data)) {
            log_warn("Failed to stop service %ls cleanly within timeout", svcs[i]);
            all_ok = false;
        }
    }

    CloseServiceHandle(hSCM);
    return all_ok;
}

bool services_control_l4superv(DWORD control_code) {
    SC_HANDLE hSCM = OpenSCManagerW(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!hSCM) return false;

    SC_HANDLE hSvc = OpenServiceW(hSCM, SVC_NAME_L4SUPERV, SERVICE_USER_DEFINED_CONTROL);
    if (!hSvc) {
        CloseServiceHandle(hSCM);
        return false;
    }

    SERVICE_STATUS ss;
    BOOL res = ControlService(hSvc, control_code, &ss);
    CloseServiceHandle(hSvc);
    CloseServiceHandle(hSCM);

    return (res != FALSE);
}
