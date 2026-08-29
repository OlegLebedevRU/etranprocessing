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

static void print_banner(void) {
    wprintf(L"===============================================================\n");
    wprintf(L"  Leo4 Tools Suite Installer (l4install) v%ls\n", L4_SUPERV_VERSION_STR);
    wprintf(L"  Automated Zero-Touch Deployment for Terminal Services\n");
    wprintf(L"===============================================================\n\n");
}

static void print_usage(void) {
    wprintf(L"Usage: l4install.exe [OPTIONS]\n\n");
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
        wprintf(L"[1/4] Destination directory: %ls\n", dest_dir);
    }

    // Locate ZIP package
    wchar_t zip_file[MAX_PATH] = { 0 };
    if (find_zip_package(exe_path, custom_zip, zip_file, MAX_PATH)) {
        if (!silent) {
            wprintf(L"[2/4] Found package archive: %ls\n", zip_file);
            wprintf(L"      Unpacking tools tree...\n");
        }
        if (!zip_extract_all(zip_file, dest_dir, !silent)) {
            if (!silent) {
                wprintf(L"[ERROR] Failed to unpack archive %ls into %ls\n", zip_file, dest_dir);
            }
            return 1;
        }
    } else {
        if (!silent) {
            wprintf(L"[2/4] [INFO] No zip package found next to installer. Checking existing directory...\n");
        }
    }

    // Ensure directory structure
    CreateDirectoryW(dest_dir, NULL);
    wchar_t sub_dir[MAX_PATH];
    swprintf_s(sub_dir, MAX_PATH, L"%ls\\leo4proxy", dest_dir); CreateDirectoryW(sub_dir, NULL);
    swprintf_s(sub_dir, MAX_PATH, L"%ls\\mosquitto", dest_dir); CreateDirectoryW(sub_dir, NULL);
    swprintf_s(sub_dir, MAX_PATH, L"%ls\\mosquitto\\log", dest_dir); CreateDirectoryW(sub_dir, NULL);
    swprintf_s(sub_dir, MAX_PATH, L"%ls\\l4con", dest_dir); CreateDirectoryW(sub_dir, NULL);
    swprintf_s(sub_dir, MAX_PATH, L"%ls\\l4pin", dest_dir); CreateDirectoryW(sub_dir, NULL);
    swprintf_s(sub_dir, MAX_PATH, L"%ls\\l4superv", dest_dir); CreateDirectoryW(sub_dir, NULL);

    // Initial config and state
    if (!silent) {
        wprintf(L"[3/4] Initializing default configurations and hardware bindings...\n");
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
    state_save(dest_dir, &state);

    // Services installation & start
    if (!skip_services) {
        if (!silent) {
            wprintf(L"[4/4] Installing and registering Windows Services (SCM)...\n");
        }

        bool ok = svc_ensure_all_installed_and_running(dest_dir);
        if (!ok) {
            if (!silent) {
                wprintf(L"[WARN] Some services could not be registered immediately (check administrative privileges).\n");
            }
        }
    }

    if (!silent) {
        wprintf(L"\n===============================================================\n");
        wprintf(L" [OK] Installation Completed Successfully!\n");
        wprintf(L" Target Location: %ls\n", dest_dir);
        wprintf(L" Installed Services:\n");
        wprintf(L"   - %-12ls (Status: %ls)\n", SVC_NAME_LEO4PROXY, svc_is_running(SVC_NAME_LEO4PROXY) ? L"RUNNING" : L"STOPPED");
        wprintf(L"   - %-12ls (Status: %ls)\n", SVC_NAME_MOSQUITTO, svc_is_running(SVC_NAME_MOSQUITTO) ? L"RUNNING" : L"STOPPED");
        wprintf(L"   - %-12ls (Status: %ls)\n", SVC_NAME_L4CON,     svc_is_running(SVC_NAME_L4CON)     ? L"RUNNING" : L"STOPPED");
        wprintf(L"   - %-12ls (Status: %ls)\n", SVC_NAME_L4SUPERV,  svc_is_running(SVC_NAME_L4SUPERV)  ? L"RUNNING" : L"STOPPED");
        wprintf(L"===============================================================\n\n");
    }

    return 0;
}
