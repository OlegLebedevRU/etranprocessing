#include "services.h"
static const CliOptions* network_options;
void services_set_network_options(const CliOptions* options) { network_options=options; }
#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <aclapi.h>
#include <sddl.h>
#include <shellapi.h>
#include "mosquitto_log_acl.h"

#pragma comment(lib, "advapi32.lib")

bool services_prepare_mosquitto(const wchar_t* dest_dir) {
    wchar_t exe[MAX_PATH], command[MAX_PATH * 3];
    if (!dest_dir || swprintf_s(exe, MAX_PATH, L"%ls\\l4superv\\l4superv.exe", dest_dir) < 0 ||
        swprintf_s(command, _countof(command), L"\"%ls\" --prepare-mosquitto --dest \"%ls\"", exe, dest_dir) < 0)
        return false;
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = { 0 };
    log_info("Preparing missing Mosquitto configuration through l4superv (local-only bootstrap)...");
    if (!CreateProcessW(exe, command, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, dest_dir, &si, &pi)) {
        log_err("Cannot run l4superv configuration preparation (error %lu)", GetLastError());
        return false;
    }
    DWORD wait = WaitForSingleObject(pi.hProcess, 30000), code = 1;
    bool ok = wait == WAIT_OBJECT_0 && GetExitCodeProcess(pi.hProcess, &code) && code == 0;
    if (wait != WAIT_OBJECT_0) {
        TerminateProcess(pi.hProcess, 1); /* Only the child created above. */
        WaitForSingleObject(pi.hProcess, 5000);
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    if (!ok) log_err("Mosquitto configuration preparation failed (wait=%lu, code=%lu)", wait, code);
    if (ok) {
        wchar_t log_dir[MAX_PATH];
        swprintf_s(log_dir, MAX_PATH, L"%ls\\mosquitto\\log", dest_dir);
        if (!l4_mosquitto_log_acl(log_dir))
            log_warn("Mosquitto log read permissions could not be updated (error %lu); supervisor will retry at startup", GetLastError());
    }
    return ok;
}

bool services_configure_environment(const wchar_t* dest_dir) {
    if (!dest_dir) return false;

    log_info("Configuring system environment variables and PATH...");

    // 1. Ensure MOSQUITTO_DIR
    wchar_t mosq_dir[MAX_PATH];
    swprintf_s(mosq_dir, MAX_PATH, L"%ls\\mosquitto", dest_dir);
    SetEnvironmentVariableW(L"MOSQUITTO_DIR", mosq_dir);

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

static bool append_quoted_argument(wchar_t* command,size_t capacity,const wchar_t* argument) {
    size_t used=wcslen(command),slashes=0;
    if (used+3>=capacity) return false;
    if (used) command[used++]=L' ';
    command[used++]=L'"';
    for (const wchar_t* p=argument;;p++) {
        if (*p==L'\\') { ++slashes; continue; }
        size_t copies=(*p==L'"' || !*p)?slashes*2:slashes;
        if (used+copies+3>=capacity) return false;
        while (copies--) command[used++]=L'\\';
        slashes=0;
        if (!*p) break;
        if (*p==L'"') command[used++]=L'\\';
        command[used++]=*p;
    }
    command[used++]=L'"'; command[used]=0; return true;
}
bool services_read_proxy_arguments(const wchar_t* dest,wchar_t* out,size_t capacity) {
    if(!dest || !out || !capacity)return false;out[0]=0;
    SC_HANDLE scm=OpenSCManagerW(NULL,NULL,SC_MANAGER_CONNECT);if(!scm)return false;
    SC_HANDLE service=OpenServiceW(scm,SVC_NAME_LEO4PROXY,SERVICE_QUERY_CONFIG);bool ok=false;
    if(service) {
        DWORD bytes=0;QueryServiceConfigW(service,NULL,0,&bytes);
        LPQUERY_SERVICE_CONFIGW config=bytes && bytes<=65536?(LPQUERY_SERVICE_CONFIGW)malloc(bytes):NULL;
        if(config && QueryServiceConfigW(service,config,bytes,&bytes)) {
            int count=0;LPWSTR* args=CommandLineToArgvW(config->lpBinaryPathName,&count);
            wchar_t expected[MAX_PATH],expected_full[MAX_PATH],actual_full[MAX_PATH];
            DWORD expected_length=0,actual_length=0;
            if(swprintf_s(expected,MAX_PATH,L"%ls\\leo4proxy\\leo4proxy.exe",dest)>=0 && args && count) {
                expected_length=GetFullPathNameW(expected,MAX_PATH,expected_full,NULL);
                actual_length=GetFullPathNameW(args[0],MAX_PATH,actual_full,NULL);
            }
            if(expected_length && expected_length<MAX_PATH && actual_length && actual_length<MAX_PATH && !_wcsicmp(expected_full,actual_full)) {
                ok=true;for(int n=1;n<count && ok;n++)ok=append_quoted_argument(out,capacity,args[n]);
            }
            if(args)LocalFree(args);
        }
        free(config);CloseServiceHandle(service);
    }
    CloseServiceHandle(scm);if(!ok)out[0]=0;return ok;
}
bool services_read_network_options(const wchar_t* dest,CliOptions* options) {
    wchar_t command[2048];if(!options || !services_read_proxy_arguments(dest,command,2048))return false;
    int count=0;LPWSTR* args=CommandLineToArgvW(command,&count);if(!args)return false;
    const wchar_t* flags[]={L"--mqtt-remote",L"--http-remote",L"--stream-remote",L"--rtp-remote",L"--policy-bootstrap-ip"};
    wchar_t* values[5]={0};bool no_srv=false,complete=true;
    for(int n=0;n<count;n++) {
        if(!_wcsicmp(args[n],L"--no-srv"))no_srv=true;
        for(int c=0;c<5;c++)if(!_wcsicmp(args[n],flags[c])) {
            if(n+1>=count)complete=false;else values[c]=args[++n];break;
        }
    }
    wchar_t* subset[16]={L"l4setup"};int subset_count=1;
    for(int c=0;c<5;c++)if(values[c]) {subset[subset_count++]=(wchar_t*)flags[c];subset[subset_count++]=values[c];}
    if(no_srv)subset[subset_count++]=L"--no-srv";
    CliOptions parsed={0};bool ok=complete && cli_parse(subset_count,subset,&parsed,NULL,0);
    if(ok) {
        memcpy(options->remote_endpoints,parsed.remote_endpoints,sizeof(parsed.remote_endpoints));
        wcscpy_s(options->policy_bootstrap_ip,16,values[4]?parsed.policy_bootstrap_ip:L"");
        options->no_srv=parsed.no_srv;
    }
    LocalFree(args);return ok;
}
static bool retain_non_network_arguments(wchar_t* command,size_t capacity,const wchar_t* previous) {
    int count=0; LPWSTR* args=CommandLineToArgvW(previous,&count);
    if (!args || !count) { if (args) LocalFree(args); return false; }
    command[0]=0; bool ok=true;
    for (int n=0;n<count && ok;n++) {
        if (n && (!_wcsicmp(args[n],L"--mqtt-remote") || !_wcsicmp(args[n],L"--http-remote") ||
            !_wcsicmp(args[n],L"--stream-remote") || !_wcsicmp(args[n],L"--rtp-remote") ||
            !_wcsicmp(args[n],L"--policy-bootstrap-ip") || !_wcsicmp(args[n],L"--bootstrap-port"))) { if (n+1<count) ++n; continue; }
        if (n && !_wcsicmp(args[n],L"--no-srv")) continue;
        ok=append_quoted_argument(command,capacity,args[n]);
    }
    LocalFree(args); return ok;
}

bool services_ensure_all_registered(const wchar_t* dest_dir) {
    if (!dest_dir) return false;

    SC_HANDLE hSCM = OpenSCManagerW(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!hSCM) {
        log_err("Failed to open Service Control Manager (error %lu)", GetLastError());
        return false;
    }

    wchar_t cmd[2048];

    // 1. Leo4Proxy
    swprintf_s(cmd, sizeof(cmd)/sizeof(wchar_t), L"\"%ls\\leo4proxy\\leo4proxy.exe\" --service --rtp-tunnel", dest_dir);
    /* Preserve SCM arguments on an upgrade/repair unless network options were selected. */
    bool preserved=false;
    {
        SC_HANDLE old=OpenServiceW(hSCM,SVC_NAME_LEO4PROXY,SERVICE_QUERY_CONFIG);
        DWORD bytes=0;
        if (old) {
            QueryServiceConfigW(old,NULL,0,&bytes);
            QUERY_SERVICE_CONFIGW* config=(QUERY_SERVICE_CONFIGW*)malloc(bytes);
            if (config && QueryServiceConfigW(old,config,bytes,&bytes)) {
                wchar_t expected[MAX_PATH]; swprintf_s(expected,MAX_PATH,L"\"%ls\\leo4proxy\\leo4proxy.exe\"",dest_dir);
                if (!_wcsnicmp(config->lpBinaryPathName,expected,wcslen(expected))) {
                    if (network_options && network_options->network_specified) {
                        if (!retain_non_network_arguments(cmd,sizeof(cmd)/sizeof(wchar_t),config->lpBinaryPathName)) {
                            free(config); CloseServiceHandle(old); CloseServiceHandle(hSCM); return false;
                        }
                    } else { wcscpy_s(cmd,sizeof(cmd)/sizeof(wchar_t),config->lpBinaryPathName); preserved=true; }
                }
            }
            free(config); CloseServiceHandle(old);
        }
    }
    if (!preserved && network_options) {
        if (network_options->policy_bootstrap_ip[0]) {
            wcscat_s(cmd,sizeof(cmd)/sizeof(wchar_t),L" --policy-bootstrap-ip ");
            wcscat_s(cmd,sizeof(cmd)/sizeof(wchar_t),network_options->policy_bootstrap_ip);
        }
        if (network_options->no_srv) wcscat_s(cmd,sizeof(cmd)/sizeof(wchar_t),L" --no-srv");
        const wchar_t* flags[]={L" --mqtt-remote ",L" --http-remote ",L" --stream-remote ",L" --rtp-remote "};
        for (int c=0;c<4;c++) if (network_options->remote_endpoints[c][0]) {
            wcscat_s(cmd,sizeof(cmd)/sizeof(wchar_t),flags[c]); wcscat_s(cmd,sizeof(cmd)/sizeof(wchar_t),network_options->remote_endpoints[c]);
        }
    }
    if (preserved && network_options && network_options->policy_bootstrap_ip[0] && !wcsstr(cmd,L"--policy-bootstrap-ip")) {
        wcscat_s(cmd,sizeof(cmd)/sizeof(wchar_t),L" --policy-bootstrap-ip ");
        wcscat_s(cmd,sizeof(cmd)/sizeof(wchar_t),network_options->policy_bootstrap_ip);
    }
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

    /* Mirror the successfully registered SCM arguments atomically for watchdog. */
    wchar_t args_path[MAX_PATH],args_temp[MAX_PATH];
    swprintf_s(args_path,MAX_PATH,L"%ls\\leo4proxy\\service-args.txt",dest_dir);
    swprintf_s(args_temp,MAX_PATH,L"%ls.tmp",args_path);
    const wchar_t* args=wcschr(cmd+1,L'"');
    if (args) ++args;
    while (args && *args==L' ') ++args;
    char utf8[8192]={0};
    int bytes=args?WideCharToMultiByte(CP_UTF8,0,args,-1,utf8,sizeof(utf8),NULL,NULL):0;
    FILE* args_file=NULL; bool mirrored=false;
    if (bytes>0 && _wfopen_s(&args_file,args_temp,L"wb")==0 && args_file) {
        mirrored=fwrite(utf8,1,(size_t)bytes-1,args_file)==(size_t)bytes-1 && fflush(args_file)==0;
        if (fclose(args_file)!=0) mirrored=false;
        if (mirrored) mirrored=MoveFileExW(args_temp,args_path,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
    }
    if (!mirrored) {
        DeleteFileW(args_temp); log_err("Cannot persist Leo4Proxy SCM arguments for watchdog.");
        CloseServiceHandle(hSCM); return false;
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

        // A service that returned to STOPPED did not start, even if it exited
        // with zero (Mosquitto can do this when configuration is missing).
        if (ssp.dwCurrentState == SERVICE_STOPPED) {
            log_err("Service %ls stopped during startup: win32=%lu, specific=%lu",
                    svc_name, ssp.dwWin32ExitCode, ssp.dwServiceSpecificExitCode);
            if (cb) cb(svc_name, SVC_STATUS_FAILED, elapsed_sec, "Service stopped during startup", user_data);
            CloseServiceHandle(hSvc);
            return false;
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
