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
#include <wincrypt.h>
#include <shellapi.h>

#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "shell32.lib")

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

    wchar_t tools_path[MAX_PATH * 8];
    swprintf_s(tools_path, sizeof(tools_path)/sizeof(wchar_t),
               L"%ls;%ls\\l4sql;%ls\\l4pin;%ls\\l4con;%ls\\l4superv;%ls\\l4desk;%ls\\ffmpeg",
               base_dir, base_dir, base_dir, base_dir, base_dir, base_dir, base_dir);

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
    wprintf(L"  --dest <dir>         Target installation directory (default: C:\\l4tools)\n");
    wprintf(L"  --zip <path>         Path to tools.zip package (default: auto-detected next to exe)\n");
    wprintf(L"  --ffmpeg-zip <path>  Path to ffmpeg.zip package (default: auto-detected next to exe)\n");
    wprintf(L"  --skip-ffmpeg        Skip FFmpeg media engine installation\n");
    wprintf(L"  --silent, /S         Run silently without interactive console output\n");
    wprintf(L"  --no-services        Extract files only without registering Windows services\n");
    wprintf(L"  --help, -h           Show this help message\n\n");
}

static bool remove_directory_recursive(const wchar_t* dir_path) {
    if (!dir_path || !PathFileExistsW(dir_path)) return true;
    wchar_t double_null_path[MAX_PATH + 2] = { 0 };
    wcscpy_s(double_null_path, MAX_PATH, dir_path);
    double_null_path[wcslen(dir_path) + 1] = L'\0';

    SHFILEOPSTRUCTW sfo;
    memset(&sfo, 0, sizeof(sfo));
    sfo.wFunc = FO_DELETE;
    sfo.pFrom = double_null_path;
    sfo.fFlags = FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT;
    return (SHFileOperationW(&sfo) == 0);
}

static bool calc_sha256(const wchar_t* file_path, char* out_hex, size_t out_hex_size) {
    if (!file_path || !out_hex || out_hex_size < 65) return false;
    out_hex[0] = '\0';

    HANDLE hFile = CreateFileW(file_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return false;

    HCRYPTPROV hProv = 0;
    HCRYPTHASH hHash = 0;
    bool success = false;

    if (CryptAcquireContextW(&hProv, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) {
        if (CryptCreateHash(hProv, CALG_SHA_256, 0, 0, &hHash)) {
            BYTE buffer[65536];
            DWORD bytes_read = 0;
            BOOL read_ok = TRUE;

            while (read_ok && ReadFile(hFile, buffer, sizeof(buffer), &bytes_read, NULL) && bytes_read > 0) {
                if (!CryptHashData(hHash, buffer, bytes_read, 0)) {
                    read_ok = FALSE;
                    break;
                }
            }

            if (read_ok) {
                BYTE hash_val[32];
                DWORD hash_len = sizeof(hash_val);
                if (CryptGetHashParam(hHash, HP_HASHVAL, hash_val, &hash_len, 0)) {
                    for (DWORD i = 0; i < hash_len; i++) {
                        sprintf_s(out_hex + (i * 2), out_hex_size - (i * 2), "%02x", hash_val[i]);
                    }
                    out_hex[64] = '\0';
                    success = true;
                }
            }
            CryptDestroyHash(hHash);
        }
        CryptReleaseContext(hProv, 0);
    }

    CloseHandle(hFile);
    return success;
}

static bool verify_ffmpeg_manifest(const wchar_t* dir_path, const wchar_t* target_arch, bool verbose, int* out_checked_count) {
    if (out_checked_count) *out_checked_count = 0;
    wchar_t manifest_path[MAX_PATH];
    swprintf_s(manifest_path, MAX_PATH, L"%ls\\ffmpeg.sha256", dir_path);
    if (!PathFileExistsW(manifest_path)) {
        if (verbose) wprintf(L"[WARN] ffmpeg.sha256 manifest not found in %ls\n", dir_path);
        return false;
    }

    FILE* f = NULL;
    if (_wfopen_s(&f, manifest_path, L"r") != 0 || !f) {
        return false;
    }

    const wchar_t* opposite_arch_prefix = (_wcsicmp(target_arch, L"x64") == 0) ? L"x86/" : L"x64/";
    const wchar_t* target_arch_prefix = (_wcsicmp(target_arch, L"x64") == 0) ? L"x64/" : L"x86/";
    size_t target_prefix_len = wcslen(target_arch_prefix);

    char line[512];
    int checked = 0;
    bool all_ok = true;

    while (fgets(line, sizeof(line), f)) {
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == '\n' || line[len - 1] == ' ')) {
            line[--len] = '\0';
        }
        if (len < 66) continue;

        char expected_hash[65] = { 0 };
        memcpy(expected_hash, line, 64);
        expected_hash[64] = '\0';

        char* rel_file = line + 64;
        while (*rel_file == ' ' || *rel_file == '*') rel_file++;
        if (*rel_file == '\0') continue;

        wchar_t rel_w[MAX_PATH] = { 0 };
        MultiByteToWideChar(CP_UTF8, 0, rel_file, -1, rel_w, MAX_PATH);

        for (size_t i = 0; rel_w[i]; i++) {
            if (rel_w[i] == L'\\') rel_w[i] = L'/';
        }

        if (_wcsnicmp(rel_w, opposite_arch_prefix, wcslen(opposite_arch_prefix)) == 0) {
            continue;
        }

        const wchar_t* sub_file = rel_w;
        if (_wcsnicmp(rel_w, target_arch_prefix, target_prefix_len) == 0) {
            sub_file = rel_w + target_prefix_len;
        }

        wchar_t sub_file_win[MAX_PATH];
        wcscpy_s(sub_file_win, MAX_PATH, sub_file);
        for (size_t i = 0; sub_file_win[i]; i++) {
            if (sub_file_win[i] == L'/') sub_file_win[i] = L'\\';
        }

        wchar_t full_file[MAX_PATH];
        swprintf_s(full_file, MAX_PATH, L"%ls\\%ls", dir_path, sub_file_win);

        if (!PathFileExistsW(full_file)) {
            if (verbose) {
                wprintf(L"[ERROR] Missing file required by manifest: %ls\n", sub_file_win);
            }
            all_ok = false;
            break;
        }

        char actual_hash[65] = { 0 };
        if (!calc_sha256(full_file, actual_hash, sizeof(actual_hash))) {
            if (verbose) {
                wprintf(L"[ERROR] Failed to calculate hash for: %ls\n", sub_file_win);
            }
            all_ok = false;
            break;
        }

        if (_stricmp(expected_hash, actual_hash) != 0) {
            if (verbose) {
                wprintf(L"[ERROR] Checksum mismatch for %ls (expected %hs, got %hs)\n",
                        sub_file_win, expected_hash, actual_hash);
            }
            all_ok = false;
            break;
        }

        checked++;
    }

    fclose(f);
    if (out_checked_count) *out_checked_count = checked;
    return (all_ok && checked > 0);
}

static bool check_active_ffmpeg_stream(const wchar_t* base_dir, DWORD* out_active_pid) {
    if (out_active_pid) *out_active_pid = 0;
    wchar_t state_path[MAX_PATH];
    swprintf_s(state_path, MAX_PATH, L"%ls\\l4desk\\state\\ffmpeg_state.json", base_dir);
    if (!PathFileExistsW(state_path)) return false;

    FILE* f = NULL;
    if (_wfopen_s(&f, state_path, L"rb") != 0 || !f) return false;

    char buf[4096] = { 0 };
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    if (n == 0) return false;
    buf[n] = '\0';

    int pid = 0;
    const char* p = strstr(buf, "\"pid\"");
    if (!p) return false;
    p += 5;
    while (*p && (*p == ' ' || *p == '\t' || *p == ':' || *p == '\r' || *p == '\n')) p++;
    pid = atoi(p);

    if (pid > 0) {
        HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, (DWORD)pid);
        if (!hProc) {
            hProc = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, (DWORD)pid);
        }
        if (hProc) {
            DWORD exitCode = 0;
            if (GetExitCodeProcess(hProc, &exitCode) && exitCode == STILL_ACTIVE) {
                if (out_active_pid) *out_active_pid = (DWORD)pid;
                CloseHandle(hProc);
                return true;
            }
            CloseHandle(hProc);
        }
    }
    return false;
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

static bool find_ffmpeg_package(const wchar_t* exe_path, const wchar_t* custom_zip, wchar_t* out_zip, size_t out_size) {
    if (custom_zip && custom_zip[0] != L'\0' && PathFileExistsW(custom_zip)) {
        wcscpy_s(out_zip, out_size, custom_zip);
        return true;
    }

    wchar_t exe_dir[MAX_PATH];
    wcscpy_s(exe_dir, MAX_PATH, exe_path);
    PathRemoveFileSpecW(exe_dir);

    const wchar_t* candidates[] = {
        L"ffmpeg.zip",
        L"ffmpeg_package.zip",
        L"l4ffmpeg.zip"
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

static bool install_ffmpeg_package(const wchar_t* ffmpeg_zip, const wchar_t* dest_dir, const wchar_t* target_arch, bool verbose) {
    if (!ffmpeg_zip || !dest_dir || !target_arch) return false;

    wchar_t target_ffmpeg[MAX_PATH];
    swprintf_s(target_ffmpeg, MAX_PATH, L"%ls\\ffmpeg", dest_dir);

    wchar_t temp_staging[MAX_PATH];
    swprintf_s(temp_staging, MAX_PATH, L"%ls\\__ffmpeg_staging", dest_dir);

    wchar_t ffmpeg_new[MAX_PATH];
    swprintf_s(ffmpeg_new, MAX_PATH, L"%ls\\ffmpeg.new", dest_dir);

    wchar_t ffmpeg_old[MAX_PATH];
    swprintf_s(ffmpeg_old, MAX_PATH, L"%ls\\ffmpeg.old", dest_dir);

    // Clean up any stale temp directories from previous runs
    remove_directory_recursive(temp_staging);
    remove_directory_recursive(ffmpeg_new);
    remove_directory_recursive(ffmpeg_old);

    // 1. Unpack into temp staging directory
    CreateDirectoryW(temp_staging, NULL);
    if (!zip_extract_all(ffmpeg_zip, temp_staging, target_arch, verbose)) {
        if (verbose) wprintf(L"[ERROR] Failed to extract FFmpeg archive: %ls\n", ffmpeg_zip);
        remove_directory_recursive(temp_staging);
        return false;
    }

    // Extracted files are in <temp_staging>\ffmpeg
    wchar_t extracted_sub[MAX_PATH];
    swprintf_s(extracted_sub, MAX_PATH, L"%ls\\ffmpeg", temp_staging);
    if (!PathFileExistsW(extracted_sub)) {
        wcscpy_s(extracted_sub, MAX_PATH, temp_staging);
    }

    // Move to ffmpeg.new
    if (!MoveFileExW(extracted_sub, ffmpeg_new, MOVEFILE_COPY_ALLOWED | MOVEFILE_REPLACE_EXISTING)) {
        if (verbose) wprintf(L"[ERROR] Failed to stage ffmpeg.new (error %lu)\n", GetLastError());
        remove_directory_recursive(temp_staging);
        return false;
    }
    remove_directory_recursive(temp_staging);

    // 2. Verify SHA-256 manifest
    int checked_count = 0;
    if (verbose) {
        wprintf(L"      Validating SHA-256 integrity manifest for FFmpeg...\n");
    }
    if (!verify_ffmpeg_manifest(ffmpeg_new, target_arch, verbose, &checked_count)) {
        if (verbose) {
            wprintf(L"[ERROR] FFmpeg integrity check FAILED! Staged files corrupted or incomplete.\n");
        }
        remove_directory_recursive(ffmpeg_new);
        return false;
    }
    if (verbose) {
        wprintf(L"      [OK] Integrity verified: %d files matched SHA-256 manifest.\n", checked_count);
    }

    // 3. Atomic replacement
    if (PathFileExistsW(target_ffmpeg)) {
        // Move current to .old
        if (!MoveFileExW(target_ffmpeg, ffmpeg_old, MOVEFILE_REPLACE_EXISTING)) {
            DWORD err = GetLastError();
            if (verbose) {
                if (err == ERROR_SHARING_VIOLATION || err == ERROR_ACCESS_DENIED) {
                    wprintf(L"[ERROR] Failed to replace %ls: file is locked by another process (ERROR_SHARING_VIOLATION).\n", target_ffmpeg);
                } else {
                    wprintf(L"[ERROR] Failed to backup current FFmpeg to %ls (error %lu)\n", ffmpeg_old, err);
                }
            }
            remove_directory_recursive(ffmpeg_new);
            return false;
        }

        // Move .new to current
        if (!MoveFileExW(ffmpeg_new, target_ffmpeg, MOVEFILE_REPLACE_EXISTING)) {
            DWORD err = GetLastError();
            if (verbose) {
                wprintf(L"[ERROR] Failed to activate new FFmpeg directory (error %lu). Rolling back...\n", err);
            }
            // Rollback
            MoveFileExW(ffmpeg_old, target_ffmpeg, MOVEFILE_REPLACE_EXISTING);
            remove_directory_recursive(ffmpeg_new);
            return false;
        }

        // Successfully replaced: clean up .old
        remove_directory_recursive(ffmpeg_old);
    } else {
        if (!MoveFileExW(ffmpeg_new, target_ffmpeg, MOVEFILE_REPLACE_EXISTING)) {
            DWORD err = GetLastError();
            if (verbose) wprintf(L"[ERROR] Failed to install FFmpeg directory (error %lu)\n", err);
            remove_directory_recursive(ffmpeg_new);
            return false;
        }
    }

    // 4. Create log directory and configure permissions
    wchar_t ffmpeg_log[MAX_PATH];
    swprintf_s(ffmpeg_log, MAX_PATH, L"%ls\\ffmpeg\\log", dest_dir);
    CreateDirectoryW(ffmpeg_log, NULL);
    svc_set_dir_permissions(ffmpeg_log);

    return true;
}

int wmain(int argc, wchar_t* argv[]) {
    wchar_t dest_dir[MAX_PATH] = { 0 };
    wchar_t custom_zip[MAX_PATH] = { 0 };
    wchar_t custom_ffmpeg_zip[MAX_PATH] = { 0 };
    bool silent = false;
    bool skip_services = false;
    bool skip_ffmpeg = false;

    wchar_t exe_path[MAX_PATH];
    GetModuleFileNameW(NULL, exe_path, MAX_PATH);

    for (int i = 1; i < argc; i++) {
        if (_wcsicmp(argv[i], L"--dest") == 0 && i + 1 < argc) {
            wcscpy_s(dest_dir, MAX_PATH, argv[++i]);
        } else if (_wcsicmp(argv[i], L"--zip") == 0 && i + 1 < argc) {
            wcscpy_s(custom_zip, MAX_PATH, argv[++i]);
        } else if (_wcsicmp(argv[i], L"--ffmpeg-zip") == 0 && i + 1 < argc) {
            wcscpy_s(custom_ffmpeg_zip, MAX_PATH, argv[++i]);
        } else if (_wcsicmp(argv[i], L"--skip-ffmpeg") == 0) {
            skip_ffmpeg = true;
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

    // Check if FFmpeg is actively streaming before altering binaries or stopping services
    DWORD active_stream_pid = 0;
    if (check_active_ffmpeg_stream(dest_dir, &active_stream_pid)) {
        wprintf(L"\n[ERROR] Active FFmpeg streaming session detected (PID: %lu)!\n", active_stream_pid);
        wprintf(L"        Installation/update cannot proceed while video streaming is active.\n");
        wprintf(L"        Please stop the supervisor service or end the session (e.g. l4superv_stop.cmd) and retry.\n\n");
        return 1;
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

    // Locate and install FFmpeg package
    if (!skip_ffmpeg) {
        wchar_t ffmpeg_zip_file[MAX_PATH] = { 0 };
        if (find_ffmpeg_package(exe_path, custom_ffmpeg_zip, ffmpeg_zip_file, MAX_PATH)) {
            if (!silent) {
                wprintf(L"[2b/5] Found FFmpeg package archive: %ls\n", ffmpeg_zip_file);
                wprintf(L"       Unpacking and verifying FFmpeg media engine...\n");
            }
            if (!install_ffmpeg_package(ffmpeg_zip_file, dest_dir, CURRENT_INSTALLER_ARCH, !silent)) {
                if (!silent) {
                    wprintf(L"[ERROR] Failed to install FFmpeg package %ls into %ls\n", ffmpeg_zip_file, dest_dir);
                }
                return 1;
            }
        } else {
            if (!silent) {
                wprintf(L"[2b/5] [INFO] ffmpeg.zip package not found next to installer. Skipping FFmpeg installation.\n");
            }
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
    swprintf_s(sub_dir, MAX_PATH, L"%ls\\l4desk", dest_dir); CreateDirectoryW(sub_dir, NULL);
    swprintf_s(sub_dir, MAX_PATH, L"%ls\\l4desk\\log", dest_dir); CreateDirectoryW(sub_dir, NULL);
    svc_set_dir_permissions(sub_dir);

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
        wprintf(L"   - Remote Input:   %ls\\l4desk\\l4desk.exe\n", dest_dir);
        wprintf(L"   - PIN Tool:       %ls\\l4pin\\l4pin.exe\n", dest_dir);
        wprintf(L"   - SQL Client:     %ls\\l4sql\\l4sql.exe\n", dest_dir);

        wchar_t ffmpeg_exe[MAX_PATH];
        swprintf_s(ffmpeg_exe, MAX_PATH, L"%ls\\ffmpeg\\ffmpeg.exe", dest_dir);
        if (PathFileExistsW(ffmpeg_exe)) {
            wprintf(L"   - Media Engine:   %ls (FFmpeg, Arch: %ls, Integrity: VERIFIED)\n", ffmpeg_exe, CURRENT_INSTALLER_ARCH);
            wprintf(L"   - FFmpeg Logs:    %ls\\ffmpeg\\log\n", dest_dir);
        } else {
            wprintf(L"   - Media Engine:   NOT INSTALLED (skipped or not found)\n");
        }
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

        wprintf(L"   - %-12ls : CONFIGURED (user session process)\n", L"l4desk");

        wprintf(L"---------------------------------------------------------------\n");
        wprintf(L" Orchestrator State:\n");
        wprintf(L"   Status:            %hs\n", state.status);
        wprintf(L"   Device SN:         %hs\n", state.sn[0] ? state.sn : "(none)");
        wprintf(L"   HW Fingerprint:    %hs\n", state.hw_fingerprint);
        wprintf(L"===============================================================\n\n");
    }

    return 0;
}
