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
        // Service already exists: check if binary path matches
        DWORD bytesNeeded = 0;
        QueryServiceConfigW(hSvc, NULL, 0, &bytesNeeded);
        if (bytesNeeded > 0) {
            LPQUERY_SERVICE_CONFIGW pConfig = (LPQUERY_SERVICE_CONFIGW)malloc(bytesNeeded);
            if (pConfig) {
                if (QueryServiceConfigW(hSvc, pConfig, bytesNeeded, &bytesNeeded)) {
                    if (_wcsicmp(pConfig->lpBinaryPathName, cmd_line) == 0) {
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
            dependencies,
            NULL,
            NULL,
            display_name
        );
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
            SVC_NAME_LEO4PROXY)) {
        CloseServiceHandle(hSCM);
        return false;
    }

    // 3. L4Con
    swprintf_s(cmd, sizeof(cmd)/sizeof(wchar_t), L"\"%ls\\l4con\\l4con.exe\" --service", dest_dir);
    if (!register_or_update_service(
            hSCM,
            SVC_NAME_L4CON,
            L"Leo4 Diagnostic Console Agent",
            cmd,
            L"Leo4 Diagnostic Console Agent (l4con)",
            SVC_NAME_MOSQUITTO)) {
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

static bool start_single_service(SC_HANDLE hSCM, const wchar_t* svc_name) {
    SC_HANDLE hSvc = OpenServiceW(hSCM, svc_name, SERVICE_START | SERVICE_QUERY_STATUS);
    if (!hSvc) {
        log_err("Cannot open service %ls to start (error %lu)", svc_name, GetLastError());
        return false;
    }

    SERVICE_STATUS_PROCESS ssp;
    DWORD bytesNeeded = 0;
    if (QueryServiceStatusEx(hSvc, SC_STATUS_PROCESS_INFO, (LPBYTE)&ssp, sizeof(ssp), &bytesNeeded)) {
        if (ssp.dwCurrentState == SERVICE_RUNNING) {
            CloseServiceHandle(hSvc);
            return true;
        }
    }

    log_info("Starting service %ls...", svc_name);
    if (!StartServiceW(hSvc, 0, NULL)) {
        DWORD err = GetLastError();
        if (err != ERROR_SERVICE_ALREADY_RUNNING) {
            log_err("Failed to start service %ls (error %lu)", svc_name, err);
            CloseServiceHandle(hSvc);
            return false;
        }
    }

    // Wait up to 10 seconds for service to reach running state
    for (int i = 0; i < 40; i++) {
        Sleep(250);
        if (QueryServiceStatusEx(hSvc, SC_STATUS_PROCESS_INFO, (LPBYTE)&ssp, sizeof(ssp), &bytesNeeded)) {
            if (ssp.dwCurrentState == SERVICE_RUNNING) {
                log_info("Service %ls is RUNNING (PID: %lu).", svc_name, ssp.dwProcessId);
                CloseServiceHandle(hSvc);
                return true;
            }
        }
    }

    log_warn("Service %ls did not reach RUNNING state within 10 seconds.", svc_name);
    CloseServiceHandle(hSvc);
    return false;
}

bool services_start_all_in_order(void) {
    SC_HANDLE hSCM = OpenSCManagerW(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!hSCM) return false;

    log_info("Starting services in order: Leo4Proxy -> mosquitto -> L4Con -> L4Superv...");
    bool ok = true;
    ok = ok && start_single_service(hSCM, SVC_NAME_LEO4PROXY);
    ok = ok && start_single_service(hSCM, SVC_NAME_MOSQUITTO);
    ok = ok && start_single_service(hSCM, SVC_NAME_L4CON);
    ok = ok && start_single_service(hSCM, SVC_NAME_L4SUPERV);

    CloseServiceHandle(hSCM);
    return ok;
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
