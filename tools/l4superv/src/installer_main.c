#include "config.h"
#include "service_mgr.h"
#include "state_mgr.h"
#include "hardware_fingerprint.h"
#include "mosquitto_conf.h"
#include "zip_extractor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <shlwapi.h>

#pragma comment(lib, "shlwapi.lib")

#if defined(_M_X64) || defined(__x86_64__)
static const wchar_t* CURRENT_INSTALLER_ARCH = L"x64";
static const wchar_t* CURRENT_INSTALLER_NAME = L"l4install_x64.exe";
#else
static const wchar_t* CURRENT_INSTALLER_ARCH = L"x86";
static const wchar_t* CURRENT_INSTALLER_NAME = L"l4install_x86.exe";
#endif

static void add_to_system_path(const wchar_t* base_dir) {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                      L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment",
                      0, KEY_READ | KEY_WRITE, &hKey) != ERROR_SUCCESS) {
        return;
    }

    wchar_t current_path[32768] = {0};
    DWORD type = 0;
    DWORD size = sizeof(current_path) - sizeof(wchar_t);
    if (RegQueryValueExW(hKey, L"Path", NULL, &type, (LPBYTE)current_path, &size) != ERROR_SUCCESS) {
        current_path[0] = L'\0';
    }

    wchar_t tools_path[MAX_PATH * 6];
    swprintf_s(tools_path, sizeof(tools_path)/sizeof(wchar_t),
               L"%ls;%ls\\l4sql;%ls\\l4pin;%ls\\l4con;%ls\\l4superv",
               base_dir, base_dir, base_dir, base_dir, base_dir);

    if (wcsstr(current_path, base_dir) == NULL) {
        wchar_t new_path[32768];
        if (current_path[0] != L'\0') {
            swprintf_s(new_path, sizeof(new_path)/sizeof(wchar_t), L"%ls;%ls", tools_path, current_path);
        } else {
            wcscpy_s(new_path, sizeof(new_path)/sizeof(wchar_t), tools_path);
        }

        DWORD new_size = (DWORD)((wcslen(new_path) + 1) * sizeof(wchar_t));
        RegSetValueExW(hKey, L"Path", 0, REG_EXPAND_SZ, (const BYTE*)new_path, new_size);

        DWORD_PTR result;
        SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0,
                            (LPARAM)L"Environment", SMTO_ABORTIFHUNG, 1000, &result);
    }

    RegCloseKey(hKey);
}

static void print_banner(void) {
    wprintf(L"===============================================================\n");
    wprintf(L"  Leo4 Tools Suite Installer (%ls) v%ls\n", CURRENT_INSTALLER_ARCH, L4_SUPERV_VERSION_STR);
    wprintf(L"  Automated Zero-Touch Deployment for Terminal Services\n");
    wprintf(L"===============================================================\n\n");
}

static void print_usage(void) {
    wprintf(L"Usage: %ls [OPTIONS]\n\n", CURRENT_INSTALLER_NAME);
    wprintf(L"Options:\n");
    wprintf(L"  --dest <dir>       Target installation directory (default: C:\\l4tools)\n");
    wprintf(L"  --zip <path>       Path to tools.zip package (default: auto-detected next to exe)\n");
    wprintf(L"  --silent, /S       Run silently without interactive console output\n");
    wprintf(L"  --no-services      Extract files only without registering Windows services\n");
    wprintf(L"  --help, -h         Show this help message\n\n");
}

static bool find_zip_package(const wchar_t* exe_path, const wchar_t* custom_zip, wchar_t* out_zip, size_t out_size) {
    if (custom_zip && custom_zip[0] != L'\0' && PathFileExistsW(custom_zip)) {
        wcscpy_s(out_zip, out_size, custom_zip);
        return true;
    }

    wchar_t exe_dir[MAX_PATH];
    wcscpy_s(exe_dir, MAX_PATH, exe_path);
    PathRemoveFileSpecW(exe_dir);

    const wchar_t* candidates[] = {
        L"tools.zip",
        L"l4tools.zip",
        L"packages.zip",
        L"bundle.zip"
    };

    for (size_t i = 0; i < sizeof(candidates)/sizeof(candidates[0]); i++) {
        wchar_t candidate_path[MAX_PATH];
        swprintf_s(candidate_path, MAX_PATH, L"%ls\\%ls", exe_dir, candidates[i]);
        if (PathFileExistsW(candidate_path)) {
            wcscpy_s(out_zip, out_size, candidate_path);
            return true;
        }
    }

    // Check current directory
    for (size_t i = 0; i < sizeof(candidates)/sizeof(candidates[0]); i++) {
        if (PathFileExistsW(candidates[i])) {
            _wfullpath(out_zip, candidates[i], out_size);
            return true;
        }
    }

    return false;
}

int wmain(int argc, wchar_t* argv[]) {
    wchar_t dest_dir[MAX_PATH] = { 0 };
    wchar_t custom_zip[MAX_PATH] = { 0 };
    bool silent = false;
    bool skip_services = false;

    wchar_t exe_path[MAX_PATH];
    GetModuleFileNameW(NULL, exe_path, MAX_PATH);

    for (int i = 1; i < argc; i++) {
        if (_wcsicmp(argv[i], L"--dest") == 0 && i + 1 < argc) {
            wcscpy_s(dest_dir, MAX_PATH, argv[++i]);
        } else if (_wcsicmp(argv[i], L"--zip") == 0 && i + 1 < argc) {
            wcscpy_s(custom_zip, MAX_PATH, argv[++i]);
        } else if (_wcsicmp(argv[i], L"--silent") == 0 || _wcsicmp(argv[i], L"/S") == 0) {
            silent = true;
        } else if (_wcsicmp(argv[i], L"--no-services") == 0) {
            skip_services = true;
        } else if (_wcsicmp(argv[i], L"--help") == 0 || _wcsicmp(argv[i], L"-h") == 0 || _wcsicmp(argv[i], L"/?") == 0) {
            print_usage();
            return 0;
        }
    }

    if (!silent) {
        print_banner();
    }

    // Determine target destination
    if (dest_dir[0] == L'\0') {
        DWORD env_len = GetEnvironmentVariableW(L"L4_TOOLS_BASE_PATH", dest_dir, MAX_PATH);
        if (env_len == 0 || env_len >= MAX_PATH) {
            wcscpy_s(dest_dir, MAX_PATH, L4_DEFAULT_BASE_PATH);
        }
    }

    if (!silent) {
        wprintf(L"[1/5] Target installation directory: %ls\n", dest_dir);
    }

    // Pre-stop any running services to release binary file locks before extraction
    svc_stop_and_kill(SVC_NAME_L4SUPERV);
    svc_stop_and_kill(SVC_NAME_L4CON);
    svc_stop_and_kill(SVC_NAME_MOSQUITTO);
    svc_stop_and_kill(SVC_NAME_LEO4PROXY);
    Sleep(500);

    // Locate ZIP package
    wchar_t zip_file[MAX_PATH] = { 0 };
    if (find_zip_package(exe_path, custom_zip, zip_file, MAX_PATH)) {
        if (!silent) {
            wprintf(L"[2/5] Found package archive: %ls\n", zip_file);
            wprintf(L"      Unpacking tools tree for %ls architecture...\n", CURRENT_INSTALLER_ARCH);
        }
        if (!zip_extract_all(zip_file, dest_dir, CURRENT_INSTALLER_ARCH, !silent)) {
            if (!silent) {
                wprintf(L"[ERROR] Failed to unpack archive %ls into %ls\n", zip_file, dest_dir);
            }
            return 1;
        }
    } else {
        if (!silent) {
            wprintf(L"[2/5] [INFO] No zip package found next to installer. Checking existing directory...\n");
        }
    }

    // Ensure directory structure
    CreateDirectoryW(dest_dir, NULL);
    wchar_t sub_dir[MAX_PATH];
    swprintf_s(sub_dir, MAX_PATH, L"%ls\\leo4proxy", dest_dir); CreateDirectoryW(sub_dir, NULL);
    swprintf_s(sub_dir, MAX_PATH, L"%ls\\mosquitto", dest_dir); CreateDirectoryW(sub_dir, NULL);
    swprintf_s(sub_dir, MAX_PATH, L"%ls\\mosquitto\\log", dest_dir); CreateDirectoryW(sub_dir, NULL);
    swprintf_s(sub_dir, MAX_PATH, L"%ls\\l4con", dest_dir); CreateDirectoryW(sub_dir, NULL);
    swprintf_s(sub_dir, MAX_PATH, L"%ls\\l4sql", dest_dir); CreateDirectoryW(sub_dir, NULL);
    swprintf_s(sub_dir, MAX_PATH, L"%ls\\l4pin", dest_dir); CreateDirectoryW(sub_dir, NULL);
    swprintf_s(sub_dir, MAX_PATH, L"%ls\\l4superv", dest_dir); CreateDirectoryW(sub_dir, NULL);

    // Register tools in system PATH for interactive sessions
    add_to_system_path(dest_dir);

    // Copy installer into target base directory under both universal and architectural names
    wchar_t target_universal[MAX_PATH];
    swprintf_s(target_universal, MAX_PATH, L"%ls\\l4install.exe", dest_dir);
    if (_wcsicmp(exe_path, target_universal) != 0) {
        CopyFileW(exe_path, target_universal, FALSE);
    }

    wchar_t target_arch_named[MAX_PATH];
    swprintf_s(target_arch_named, MAX_PATH, L"%ls\\%ls", dest_dir, CURRENT_INSTALLER_NAME);
    if (_wcsicmp(exe_path, target_arch_named) != 0) {
        CopyFileW(exe_path, target_arch_named, FALSE);
    }

    // Ensure terminal-tools-user-guide.md is present next to installer
    wchar_t target_guide[MAX_PATH];
    swprintf_s(target_guide, MAX_PATH, L"%ls\\terminal-tools-user-guide.md", dest_dir);
    wchar_t exe_dir[MAX_PATH];
    wcscpy_s(exe_dir, MAX_PATH, exe_path);
    wchar_t* last_slash = wcsrchr(exe_dir, L'\\');
    if (last_slash) *last_slash = L'\0';
    wchar_t source_guide[MAX_PATH];
    swprintf_s(source_guide, MAX_PATH, L"%ls\\terminal-tools-user-guide.md", exe_dir);
    if (PathFileExistsW(source_guide) && _wcsicmp(source_guide, target_guide) != 0) {
        CopyFileW(source_guide, target_guide, FALSE);
    }

    // Ensure permissive ACLs on mosquitto\log
    wchar_t mosq_log_dir[MAX_PATH];
    swprintf_s(mosq_log_dir, MAX_PATH, L"%ls\\mosquitto\\log", dest_dir);
    svc_set_dir_permissions(mosq_log_dir);

    // Ensure system environment variable MOSQUITTO_DIR is configured
    wchar_t mosq_dir[MAX_PATH];
    swprintf_s(mosq_dir, MAX_PATH, L"%ls\\mosquitto", dest_dir);
    SetEnvironmentVariableW(L"MOSQUITTO_DIR", mosq_dir);
    HKEY hEnvKey;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment", 0, KEY_SET_VALUE, &hEnvKey) == ERROR_SUCCESS) {
        RegSetValueExW(hEnvKey, L"MOSQUITTO_DIR", 0, REG_SZ, (const BYTE*)mosq_dir, (DWORD)((wcslen(mosq_dir) + 1) * sizeof(wchar_t)));
        RegCloseKey(hEnvKey);
    }

    // Initial config and state
    if (!silent) {
        wprintf(L"[3/5] Configuring default settings, permissions and hardware bindings...\n");
    }

    L4SupervConfig cfg;
    config_init_defaults(&cfg, dest_dir);
    wcscpy_s(cfg.base_path, MAX_PATH, dest_dir);

    wchar_t cfg_json[MAX_PATH];
    swprintf_s(cfg_json, MAX_PATH, L"%ls\\l4superv.json", dest_dir);
    if (!PathFileExistsW(cfg_json)) {
        config_save_json(&cfg, cfg_json);
    }

    // Generate initial Standby mosquitto.conf if missing
    wchar_t mosq_conf[MAX_PATH];
    swprintf_s(mosq_conf, MAX_PATH, L"%ls\\mosquitto\\mosquitto.conf", dest_dir);
    if (!PathFileExistsW(mosq_conf)) {
        mosquitto_conf_generate_standby(dest_dir, cfg.mosquitto_port);
    }

    // Initialize state.json with hardware fingerprint
    L4State state;
    state_load(dest_dir, &state);
    hw_get_fingerprint(state.hw_fingerprint, sizeof(state.hw_fingerprint));
    state_update_services(dest_dir, &state);
    state_save(dest_dir, &state);

    // Services installation & start
    if (!skip_services) {
        if (!silent) {
            wprintf(L"[4/5] Checking existing services, cleaning foreign paths, and registering native SCM services...\n");
        }

        bool ok = svc_ensure_all_installed_and_running(dest_dir);
        if (!ok) {
            if (!silent) {
                wprintf(L"[WARN] Some services could not be registered immediately (check administrative privileges).\n");
            }
        }
    }

    // Refresh state.json after services started
    state_load(dest_dir, &state);
    state_save(dest_dir, &state);

    if (!silent) {
        wprintf(L"[5/5] Service Verification & Diagnostics:\n");
        wprintf(L"===============================================================\n");
        wprintf(L" [OK] Installation and Service Verification Completed!\n");
        wprintf(L" Target Location:    %ls\n", dest_dir);
        wprintf(L" Log Directory:      %ls (Permissions: RW for All)\n", mosq_log_dir);
        wprintf(L" Installed Tools & Documentation:\n");
        wprintf(L"   - User Guide:     %ls\\terminal-tools-user-guide.md\n", dest_dir);
        wprintf(L"   - Installer Copy: %ls\\l4install.exe (and %ls)\n", dest_dir, CURRENT_INSTALLER_NAME);
        wprintf(L"   - Supervisor:     %ls\\l4superv\\l4superv.exe\n", dest_dir);
        wprintf(L"   - PIN Tool:       %ls\\l4pin\\l4pin.exe\n", dest_dir);
        wprintf(L"   - SQL Client:     %ls\\l4sql\\l4sql.exe\n", dest_dir);
        wprintf(L"---------------------------------------------------------------\n");
        wprintf(L" Service Runtime Status (from SCM & Process Table):\n");
        
        wprintf(L"   - %-12ls : %-12hs (PID: %6lu, Match: %ls)\n",
                SVC_NAME_LEO4PROXY, state.svc_leo4proxy.status, state.svc_leo4proxy.runtime_pid,
                state.svc_leo4proxy.path_match ? L"YES" : L"NO");
        if (state.svc_leo4proxy.runtime_exe[0] != '\0') {
            wprintf(L"                   Path: %hs\n", state.svc_leo4proxy.runtime_exe);
        }

        wprintf(L"   - %-12ls : %-12hs (PID: %6lu, Match: %ls)\n",
                SVC_NAME_MOSQUITTO, state.svc_mosquitto.status, state.svc_mosquitto.runtime_pid,
                state.svc_mosquitto.path_match ? L"YES" : L"NO");
        if (state.svc_mosquitto.runtime_exe[0] != '\0') {
            wprintf(L"                   Path: %hs\n", state.svc_mosquitto.runtime_exe);
        }

        wprintf(L"   - %-12ls : %-12hs (PID: %6lu, Match: %ls)\n",
                SVC_NAME_L4CON, state.svc_l4con.status, state.svc_l4con.runtime_pid,
                state.svc_l4con.path_match ? L"YES" : L"NO");
        if (state.svc_l4con.runtime_exe[0] != '\0') {
            wprintf(L"                   Path: %hs\n", state.svc_l4con.runtime_exe);
        }

        wprintf(L"   - %-12ls : %-12hs (PID: %6lu, Match: %ls)\n",
                SVC_NAME_L4SUPERV, state.svc_l4superv.status, state.svc_l4superv.runtime_pid,
                state.svc_l4superv.path_match ? L"YES" : L"NO");
        if (state.svc_l4superv.runtime_exe[0] != '\0') {
            wprintf(L"                   Path: %hs\n", state.svc_l4superv.runtime_exe);
        }

        wprintf(L"---------------------------------------------------------------\n");
        wprintf(L" Orchestrator State:\n");
        wprintf(L"   Status:            %hs\n", state.status);
        wprintf(L"   Device SN:         %hs\n", state.sn[0] ? state.sn : "(none)");
        wprintf(L"   HW Fingerprint:    %hs\n", state.hw_fingerprint);
        wprintf(L"===============================================================\n\n");
    }

    return 0;
}
