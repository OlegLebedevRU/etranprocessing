#include "unpack.h"
#include "log.h"
#include "../../l4superv/src/miniz.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <shlwapi.h>
#include <shellapi.h>

#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "shell32.lib")

static bool file_exists(const wchar_t* path) {
    DWORD attr = GetFileAttributesW(path);
    return (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY));
}

static bool dir_exists(const wchar_t* path) {
    DWORD attr = GetFileAttributesW(path);
    return (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY));
}

static void recursive_delete(const wchar_t* path) {
    if (!path || !dir_exists(path)) return;
    wchar_t double_null_path[MAX_PATH + 2] = { 0 };
    wcsncpy_s(double_null_path, MAX_PATH, path, _TRUNCATE);
    double_null_path[wcslen(path) + 1] = L'\0';

    SHFILEOPSTRUCTW sfo;
    memset(&sfo, 0, sizeof(sfo));
    sfo.wFunc = FO_DELETE;
    sfo.pFrom = double_null_path;
    sfo.fFlags = FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT;
    SHFileOperationW(&sfo);
}

static void create_parent_directories(const wchar_t* file_path) {
    wchar_t dir_path[MAX_PATH];
    wcscpy_s(dir_path, MAX_PATH, file_path);
    PathRemoveFileSpecW(dir_path);

    wchar_t tmp[MAX_PATH] = { 0 };
    for (size_t i = 0; dir_path[i]; i++) {
        tmp[i] = dir_path[i];
        if (dir_path[i] == L'\\' || dir_path[i] == L'/') {
            tmp[i] = L'\0';
            CreateDirectoryW(tmp, NULL);
            tmp[i] = dir_path[i];
        }
    }
    CreateDirectoryW(dir_path, NULL);
}

static bool is_arch_segment(const wchar_t* path, size_t pos, const wchar_t* arch, size_t arch_len) {
    if (_wcsnicmp(path + pos, arch, arch_len) != 0) return false;
    if (pos > 0 && path[pos - 1] != L'\\') return false;
    wchar_t after = path[pos + arch_len];
    if (after != L'\0' && after != L'\\') return false;
    return true;
}

bool transform_arch_path(const wchar_t* in_path, bool is_dir, const wchar_t* target_arch, wchar_t* out_path, size_t out_size) {
    if (!in_path || !out_path || out_size == 0) return false;

    if (!target_arch || target_arch[0] == L'\0') {
        wcscpy_s(out_path, out_size, in_path);
        return true;
    }

    const wchar_t* opposite_arch = (_wcsicmp(target_arch, L"x64") == 0) ? L"x86" : L"x64";
    size_t target_len = wcslen(target_arch);
    size_t opposite_len = wcslen(opposite_arch);
    size_t in_len = wcslen(in_path);

    // 1. Skip opposite architecture
    for (size_t i = 0; i <= in_len; i++) {
        if (is_arch_segment(in_path, i, opposite_arch, opposite_len)) {
            return false;
        }
    }

    // 2. Skip creating architecture folder directly
    if (is_dir) {
        for (size_t i = 0; i <= in_len; i++) {
            if (is_arch_segment(in_path, i, target_arch, target_len)) {
                return false;
            }
        }
    }

    // 3. Strip target_arch segment
    for (size_t i = 0; i <= in_len; i++) {
        if (is_arch_segment(in_path, i, target_arch, target_len)) {
            if (i == 0) {
                if (in_path[target_len] == L'\\') {
                    wcscpy_s(out_path, out_size, in_path + target_len + 1);
                } else {
                    wcscpy_s(out_path, out_size, in_path + target_len);
                }
            } else {
                wcsncpy_s(out_path, out_size, in_path, i - 1);
                out_path[i - 1] = L'\0';
                if (in_path[i + target_len] == L'\\') {
                    wcscat_s(out_path, out_size, L"\\");
                    wcscat_s(out_path, out_size, in_path + i + target_len + 1);
                }
            }
            return (out_path[0] != L'\0');
        }
    }

    // 4. Common file
    wcscpy_s(out_path, out_size, in_path);
    return true;
}

static bool extract_archive(mz_zip_archive* zip, const wchar_t* dest_dir, const wchar_t* target_arch) {
    mz_uint32 num_files = mz_zip_reader_get_num_files(zip);
    CreateDirectoryW(dest_dir, NULL);

    int extracted_count = 0;
    for (mz_uint32 i = 0; i < num_files; i++) {
        mz_zip_archive_file_stat file_stat;
        if (!mz_zip_reader_file_stat(zip, i, &file_stat)) {
            continue;
        }

        wchar_t rel_path[MAX_PATH] = { 0 };
        MultiByteToWideChar(CP_UTF8, 0, file_stat.m_filename, -1, rel_path, MAX_PATH);

        for (size_t k = 0; rel_path[k]; k++) {
            if (rel_path[k] == L'/') rel_path[k] = L'\\';
        }

        size_t rlen = wcslen(rel_path);
        if (rlen > 0 && rel_path[rlen - 1] == L'\\') {
            rel_path[rlen - 1] = L'\0';
        }

        wchar_t target_rel_path[MAX_PATH] = { 0 };
        if (!transform_arch_path(rel_path, file_stat.m_is_directory != 0, target_arch, target_rel_path, MAX_PATH)) {
            continue;
        }

        if (target_rel_path[0] == L'\0') {
            continue;
        }

        wchar_t full_dest[MAX_PATH] = { 0 };
        swprintf_s(full_dest, MAX_PATH, L"%ls\\%ls", dest_dir, target_rel_path);

        if (file_stat.m_is_directory) {
            create_parent_directories(full_dest);
            CreateDirectoryW(full_dest, NULL);
        } else {
            create_parent_directories(full_dest);
            char full_dest_utf8[MAX_PATH * 3] = { 0 };
            WideCharToMultiByte(CP_UTF8, 0, full_dest, -1, full_dest_utf8, sizeof(full_dest_utf8), NULL, NULL);

            if (mz_zip_reader_extract_to_file(zip, i, full_dest_utf8, 0)) {
                extracted_count++;
            }
        }
    }

    return (extracted_count > 0 || num_files == 0);
}

static bool extract_zip_file(const wchar_t* zip_path, const wchar_t* dest_dir, const wchar_t* target_arch) {
    if (!zip_path || !file_exists(zip_path)) return false;

    char path_utf8[MAX_PATH * 3] = { 0 };
    WideCharToMultiByte(CP_UTF8, 0, zip_path, -1, path_utf8, sizeof(path_utf8), NULL, NULL);

    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));
    if (!mz_zip_reader_init_file(&zip, path_utf8, 0)) {
        log_err("Failed to initialize zip reader for file: %ls", zip_path);
        return false;
    }

    bool ok = extract_archive(&zip, dest_dir, target_arch);
    mz_zip_reader_end(&zip);
    return ok;
}

static bool extract_zip_resource(const wchar_t* res_name, const wchar_t* dest_dir, const wchar_t* target_arch) {
    HRSRC hRes = FindResourceW(NULL, res_name, RT_RCDATA);
    if (!hRes) return false;

    HGLOBAL hGlob = LoadResource(NULL, hRes);
    if (!hGlob) return false;

    const void* pData = LockResource(hGlob);
    DWORD dwSize = SizeofResource(NULL, hRes);
    if (!pData || dwSize == 0) return false;

    wchar_t tmp_zip[MAX_PATH];
    swprintf_s(tmp_zip, MAX_PATH, L"%ls\\payload_%ls.tmp.zip", dest_dir, res_name);

    FILE* fp = NULL;
    if (_wfopen_s(&fp, tmp_zip, L"wb") != 0 || !fp) return false;
    fwrite(pData, 1, dwSize, fp);
    fclose(fp);

    bool ok = extract_zip_file(tmp_zip, dest_dir, target_arch);
    DeleteFileW(tmp_zip);
    return ok;
}

bool unpack_is_idempotent(const wchar_t* dest_dir, const char* current_version) {
    if (!dest_dir || !current_version) return false;

    char installed_ver[64] = { 0 };
    if (!unpack_read_installed_version(dest_dir, installed_ver, sizeof(installed_ver))) {
        return false;
    }

    if (version_compare(installed_ver, current_version) != 0) {
        return false;
    }

    // Verify key executables are present
    const wchar_t* bins[] = {
        L"leo4proxy\\leo4proxy.exe",
        L"mosquitto\\mosquitto.exe",
        L"l4con\\l4con.exe",
        L"l4superv\\l4superv.exe",
        L"l4pin\\l4pin.exe",
        L"l4desk\\l4desk.exe",
        L"l4capture\\bin\\l4capture.exe",
        L"ffmpeg\\ffmpeg.exe"
    };

    for (int i = 0; i < (int)(sizeof(bins)/sizeof(bins[0])); i++) {
        wchar_t exe[MAX_PATH];
        swprintf_s(exe, MAX_PATH, L"%ls\\%ls", dest_dir, bins[i]);
        if (!file_exists(exe)) return false;
    }

    return true;
}

int version_compare(const char* v1, const char* v2) {
    if (!v1 && !v2) return 0;
    if (!v1) return -1;
    if (!v2) return 1;

    int p1[4] = { 0, 0, 0, 0 };
    int p2[4] = { 0, 0, 0, 0 };

    sscanf_s(v1, "%d.%d.%d.%d", &p1[0], &p1[1], &p1[2], &p1[3]);
    sscanf_s(v2, "%d.%d.%d.%d", &p2[0], &p2[1], &p2[2], &p2[3]);

    for (int i = 0; i < 4; i++) {
        if (p1[i] < p2[i]) return -1;
        if (p1[i] > p2[i]) return 1;
    }
    return 0;
}

bool unpack_read_installed_version(const wchar_t* dest_dir, char* out_version, size_t out_size) {
    if (!dest_dir || !out_version || out_size == 0) return false;
    out_version[0] = '\0';

    wchar_t state_file[MAX_PATH];
    swprintf_s(state_file, MAX_PATH, L"%ls\\state.json", dest_dir);
    if (!file_exists(state_file)) return false;

    FILE* fp = NULL;
    if (_wfopen_s(&fp, state_file, L"rb") != 0 || !fp) return false;

    char buf[4096] = { 0 };
    size_t n = fread(buf, 1, sizeof(buf) - 1, fp);
    fclose(fp);
    if (n == 0) return false;

    const char* key = "\"installed_version\":";
    const char* p = strstr(buf, key);
    if (!p) {
        key = "\"installed_version\" :";
        p = strstr(buf, key);
    }
    if (!p) return false;

    p = strchr(p, ':');
    if (!p) return false;
    p = strchr(p, '"');
    if (!p) return false;
    p++; // Skip opening quote

    size_t idx = 0;
    while (*p && *p != '"' && idx + 1 < out_size) {
        out_version[idx++] = *p++;
    }
    out_version[idx] = '\0';
    return (idx > 0);
}

bool unpack_has_incomplete_marker(const wchar_t* dest_dir, char* out_phase, size_t out_phase_size) {
    if (!dest_dir) return false;
    if (out_phase && out_phase_size > 0) out_phase[0] = '\0';

    wchar_t marker_path[MAX_PATH];
    swprintf_s(marker_path, MAX_PATH, L"%ls\\.install_in_progress.json", dest_dir);
    if (!file_exists(marker_path)) return false;

    if (out_phase && out_phase_size > 0) {
        FILE* fp = NULL;
        if (_wfopen_s(&fp, marker_path, L"r") == 0 && fp) {
            char buf[512] = { 0 };
            fread(buf, 1, sizeof(buf) - 1, fp);
            fclose(fp);
            const char* p = strstr(buf, "\"phase\":");
            if (p) {
                p = strchr(p, ':');
                if (p) p = strchr(p, '"');
                if (p) {
                    p++;
                    size_t idx = 0;
                    while (*p && *p != '"' && idx + 1 < out_phase_size) {
                        out_phase[idx++] = *p++;
                    }
                    out_phase[idx] = '\0';
                }
            }
        }
    }
    return true;
}

bool unpack_set_incomplete_marker(const wchar_t* dest_dir, const char* phase, const char* old_ver, const char* target_ver) {
    if (!dest_dir) return false;

    wchar_t marker_path[MAX_PATH];
    swprintf_s(marker_path, MAX_PATH, L"%ls\\.install_in_progress.json", dest_dir);

    FILE* fp = NULL;
    if (_wfopen_s(&fp, marker_path, L"wb") != 0 || !fp) return false;

    fprintf(fp, "{\n  \"phase\": \"%s\",\n  \"old_version\": \"%s\",\n  \"target_version\": \"%s\"\n}\n",
            phase ? phase : "update",
            old_ver ? old_ver : "none",
            target_ver ? target_ver : "unknown");
    fflush(fp);
    fclose(fp);
    return true;
}

bool unpack_clear_incomplete_marker(const wchar_t* dest_dir) {
    if (!dest_dir) return false;
    wchar_t marker_path[MAX_PATH];
    swprintf_s(marker_path, MAX_PATH, L"%ls\\.install_in_progress.json", dest_dir);
    DWORD attributes = GetFileAttributesW(marker_path);
    if (attributes == INVALID_FILE_ATTRIBUTES) return GetLastError() == ERROR_FILE_NOT_FOUND;
    if (!SetFileAttributesW(marker_path, FILE_ATTRIBUTE_NORMAL)) return false;
    return DeleteFileW(marker_path) != FALSE;
}

bool unpack_check_files_locked(const wchar_t* dest_dir, DWORD wait_timeout_ms) {
    if (!dest_dir) return true;

    const wchar_t* bins[] = {
        L"leo4proxy\\leo4proxy.exe",
        L"mosquitto\\mosquitto.exe",
        L"l4con\\l4con.exe",
        L"l4superv\\l4superv.exe",
        L"l4pin\\l4pin.exe",
        L"l4desk\\l4desk.exe",
        L"ffmpeg\\ffmpeg.exe"
    };

    ULONGLONG start = GetTickCount64();
    while (true) {
        bool any_locked = false;
        for (int i = 0; i < (int)(sizeof(bins)/sizeof(bins[0])); i++) {
            wchar_t path[MAX_PATH];
            swprintf_s(path, MAX_PATH, L"%ls\\%ls", dest_dir, bins[i]);
            if (!file_exists(path)) continue;

            HANDLE hFile = CreateFileW(path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
            if (hFile == INVALID_HANDLE_VALUE) {
                DWORD err = GetLastError();
                if (err == ERROR_SHARING_VIOLATION || err == ERROR_LOCK_VIOLATION || err == ERROR_ACCESS_DENIED) {
                    any_locked = true;
                    break;
                }
            } else {
                CloseHandle(hFile);
            }
        }

        if (!any_locked) return true;

        if ((GetTickCount64() - start) >= wait_timeout_ms) {
            log_err("Target executable files in %ls are locked by running processes.", dest_dir);
            return false;
        }

        Sleep(500);
    }
}

bool unpack_rollback(const wchar_t* dest_dir, const char* prev_version) {
    if (!dest_dir || !prev_version || prev_version[0] == '\0') return false;

    wchar_t rollback_dir[MAX_PATH];
    swprintf_s(rollback_dir, MAX_PATH, L"%ls\\rollback\\%S", dest_dir, prev_version);
    if (!dir_exists(rollback_dir)) {
        log_warn("Rollback directory %ls does not exist; cannot restore.", rollback_dir);
        return false;
    }

    log_info("Rolling back files from %ls to %ls...", rollback_dir, dest_dir);

    const wchar_t* subdirs[] = {
        L"leo4proxy",
        L"mosquitto",
        L"l4con",
        L"l4superv",
        L"l4pin",
        L"l4desk",
        L"l4capture",
        L"ffmpeg",
        L"l4sql"
    };

    for (int i = 0; i < (int)(sizeof(subdirs)/sizeof(subdirs[0])); i++) {
        wchar_t roll_sub[MAX_PATH];
        wchar_t cur_sub[MAX_PATH];
        swprintf_s(roll_sub, MAX_PATH, L"%ls\\%ls", rollback_dir, subdirs[i]);
        swprintf_s(cur_sub, MAX_PATH, L"%ls\\%ls", dest_dir, subdirs[i]);

        if (dir_exists(roll_sub)) {
            wchar_t replaced_sub[MAX_PATH];
            swprintf_s(replaced_sub, MAX_PATH, L"%ls\\.rollback-replaced-%lu-%d",
                       dest_dir, GetCurrentProcessId(), i);
            if (GetFileAttributesW(replaced_sub) != INVALID_FILE_ATTRIBUTES) {
                log_err("Rollback staging path %ls already exists; refusing to overwrite it.", replaced_sub);
                return false;
            }
            bool had_current = dir_exists(cur_sub);
            if (had_current &&
                !MoveFileExW(cur_sub, replaced_sub, MOVEFILE_WRITE_THROUGH)) {
                log_err("Rollback cannot move current %ls (error %lu); previous payload remains in %ls",
                        cur_sub, GetLastError(), roll_sub);
                return false;
            }
            if (!MoveFileExW(roll_sub, cur_sub, MOVEFILE_WRITE_THROUGH)) {
                DWORD error = GetLastError();
                log_err("Rollback cannot restore %ls to %ls (error %lu)", roll_sub, cur_sub, error);
                if (had_current &&
                    !MoveFileExW(replaced_sub, cur_sub, MOVEFILE_WRITE_THROUGH)) {
                    log_err("Cannot restore current payload from %ls (error %lu); manual recovery required.",
                            replaced_sub, GetLastError());
                }
                return false;
            }
            if (had_current) recursive_delete(replaced_sub);
        }
    }

    log_info("Rollback completed.");
    return true;
}

bool unpack_recover_from_crash(const wchar_t* dest_dir) {
    if (!dest_dir) return false;

    char phase[64] = { 0 };
    if (!unpack_has_incomplete_marker(dest_dir, phase, sizeof(phase))) {
        return false; // No crash marker
    }

    log_warn("Detected interrupted previous installation (phase: %s)! Initiating crash recovery...", phase);

    // Read old_version from marker
    wchar_t marker_path[MAX_PATH];
    swprintf_s(marker_path, MAX_PATH, L"%ls\\.install_in_progress.json", dest_dir);

    char old_ver[64] = "prev";
    FILE* fp = NULL;
    if (_wfopen_s(&fp, marker_path, L"r") == 0 && fp) {
        char buf[512] = { 0 };
        fread(buf, 1, sizeof(buf) - 1, fp);
        fclose(fp);
        const char* p = strstr(buf, "\"old_version\":");
        if (p) {
            p = strchr(p, ':');
            if (p) p = strchr(p, '"');
            if (p) {
                p++;
                size_t idx = 0;
                while (*p && *p != '"' && idx + 1 < sizeof(old_ver)) {
                    old_ver[idx++] = *p++;
                }
                old_ver[idx] = '\0';
            }
        }
    }

    // Keep the marker on failure so recovery can be retried safely.
    if (!unpack_rollback(dest_dir, old_ver)) {
        log_err("Crash recovery failed; incomplete marker was retained.");
        return false;
    }
    if (!unpack_clear_incomplete_marker(dest_dir)) {
        log_err("Rollback completed but incomplete marker could not be cleared.");
        return false;
    }

    log_info("Crash recovery completed successfully.");
    return true;
}

bool unpack_payload(
    const wchar_t* dest_dir,
    const char* target_arch,
    const wchar_t* dev_payload_dir,
    const char* current_version
) {
    if (!dest_dir || !target_arch) return false;

    wchar_t w_target_arch[16];
    MultiByteToWideChar(CP_UTF8, 0, target_arch, -1, w_target_arch, 16);

    // 1. Create staging directory: dest\.staging-<pid>
    DWORD pid = GetCurrentProcessId();
    wchar_t staging_dir[MAX_PATH];
    swprintf_s(staging_dir, MAX_PATH, L"%ls\\.staging-%lu", dest_dir, pid);

    recursive_delete(staging_dir);
    create_parent_directories(staging_dir);
    CreateDirectoryW(dest_dir, NULL);
    CreateDirectoryW(staging_dir, NULL);

    log_info("Unpacking payload into staging directory %ls...", staging_dir);
    bool extracted = false;

    // 2. Try dev payload directory if provided
    if (dev_payload_dir && dev_payload_dir[0] != L'\0') {
        wchar_t tools_zip[MAX_PATH];
        wchar_t ffmpeg_zip[MAX_PATH];
        swprintf_s(tools_zip, MAX_PATH, L"%ls\\tools.zip", dev_payload_dir);
        swprintf_s(ffmpeg_zip, MAX_PATH, L"%ls\\ffmpeg.zip", dev_payload_dir);

        if (file_exists(tools_zip)) {
            log_info("Extracting %ls (target arch: %ls)...", tools_zip, w_target_arch);
            if (extract_zip_file(tools_zip, staging_dir, w_target_arch)) {
                extracted = true;
            }
        }
        if (file_exists(ffmpeg_zip)) {
            log_info("Extracting %ls (target arch: %ls)...", ffmpeg_zip, w_target_arch);
            wchar_t ffmpeg_staging[MAX_PATH];
            swprintf_s(ffmpeg_staging, MAX_PATH, L"%ls\\ffmpeg", staging_dir);
            extract_zip_file(ffmpeg_zip, ffmpeg_staging, w_target_arch);
        }
    }

    // 3. Try embedded resource PAYLOAD_X64 or PAYLOAD_X86
    if (!extracted) {
        const wchar_t* res_name = (_stricmp(target_arch, "x64") == 0) ? L"PAYLOAD_X64" : L"PAYLOAD_X86";
        log_info("Attempting to unpack embedded resource %ls...", res_name);
        if (extract_zip_resource(res_name, staging_dir, w_target_arch)) {
            extracted = true;
        }
    }

    // 4. Try fallback adjacent tools.zip / ffmpeg.zip
    if (!extracted) {
        wchar_t exe_path[MAX_PATH];
        GetModuleFileNameW(NULL, exe_path, MAX_PATH);
        PathRemoveFileSpecW(exe_path);

        wchar_t tools_zip[MAX_PATH];
        wchar_t ffmpeg_zip[MAX_PATH];
        swprintf_s(tools_zip, MAX_PATH, L"%ls\\tools.zip", exe_path);
        swprintf_s(ffmpeg_zip, MAX_PATH, L"%ls\\ffmpeg.zip", exe_path);

        if (!file_exists(tools_zip)) {
            // Also try relative ..\dist or tools\dist
            swprintf_s(tools_zip, MAX_PATH, L"%ls\\..\\dist\\tools.zip", exe_path);
            swprintf_s(ffmpeg_zip, MAX_PATH, L"%ls\\..\\dist\\ffmpeg.zip", exe_path);
        }

        if (file_exists(tools_zip)) {
            log_info("Extracting fallback %ls (target arch: %ls)...", tools_zip, w_target_arch);
            if (extract_zip_file(tools_zip, staging_dir, w_target_arch)) {
                extracted = true;
            }
            if (file_exists(ffmpeg_zip)) {
                wchar_t ffmpeg_staging[MAX_PATH];
                swprintf_s(ffmpeg_staging, MAX_PATH, L"%ls\\ffmpeg", staging_dir);
                extract_zip_file(ffmpeg_zip, ffmpeg_staging, w_target_arch);
            }
        }
    }

    if (!extracted) {
        log_err("Failed to extract payload: no valid archive found (neither resource nor file).");
        recursive_delete(staging_dir);
        return false;
    }

    // 5. Setup Rollback directory: dest\rollback\<prev_version>
    // Read previous version from state.json if exists
    char prev_version[64] = "prev";
    wchar_t state_file[MAX_PATH];
    swprintf_s(state_file, MAX_PATH, L"%ls\\state.json", dest_dir);
    if (file_exists(state_file)) {
        FILE* fp = NULL;
        if (_wfopen_s(&fp, state_file, L"r") == 0 && fp) {
            char buf[4096] = { 0 };
            fread(buf, 1, sizeof(buf) - 1, fp);
            fclose(fp);
            const char* p = strstr(buf, "\"installed_version\": \"");
            if (!p) p = strstr(buf, "\"installed_version\":\"");
            if (p) {
                p = strchr(p, ':');
                if (p) p = strchr(p, '"');
                if (p) {
                    p++;
                    size_t v_idx = 0;
                    while (*p && *p != '"' && v_idx + 1 < sizeof(prev_version)) {
                        prev_version[v_idx++] = *p++;
                    }
                    prev_version[v_idx] = '\0';
                }
            }
        }
    }

    // Check live files before touching the previous rollback directory.
    if (!unpack_check_files_locked(dest_dir, 10000)) {
        log_err("Files in %ls are still locked by another process. Existing rollback was preserved.", dest_dir);
        recursive_delete(staging_dir);
        return false;
    }

    // Remove existing rollback folder (keep only one previous version)
    wchar_t rollback_base[MAX_PATH];
    swprintf_s(rollback_base, MAX_PATH, L"%ls\\rollback", dest_dir);
    recursive_delete(rollback_base);

    wchar_t rollback_dir[MAX_PATH];
    swprintf_s(rollback_dir, MAX_PATH, L"%ls\\rollback\\%S", dest_dir, prev_version);
    create_parent_directories(rollback_dir);
    CreateDirectoryW(rollback_dir, NULL);

    log_info("Prepared rollback directory: %ls", rollback_dir);

    // Save existing user configuration files from mosquitto
    wchar_t user_mosq_conf[MAX_PATH];
    wchar_t user_mosq_conf_saved[MAX_PATH];
    swprintf_s(user_mosq_conf, MAX_PATH, L"%ls\\mosquitto\\mosquitto.conf", dest_dir);
    swprintf_s(user_mosq_conf_saved, MAX_PATH, L"%ls\\mosquitto.conf.user", staging_dir);
    bool has_user_mosq_conf = false;
    if (file_exists(user_mosq_conf)) {
        CopyFileW(user_mosq_conf, user_mosq_conf_saved, FALSE);
        has_user_mosq_conf = true;
    }

    // Set incomplete installation marker before modifying live files
    unpack_set_incomplete_marker(dest_dir, "update", prev_version, current_version);

    // 6. Atomically swap subdirectories
    const wchar_t* subdirs[] = {
        L"leo4proxy",
        L"mosquitto",
        L"l4con",
        L"l4superv",
        L"l4pin",
        L"l4desk",
        L"l4capture",
        L"ffmpeg",
        L"l4sql"
    };

    bool swap_failed = false;
    for (int i = 0; i < (int)(sizeof(subdirs)/sizeof(subdirs[0])); i++) {
        wchar_t cur_sub[MAX_PATH];
        wchar_t roll_sub[MAX_PATH];
        wchar_t stg_sub[MAX_PATH];

        swprintf_s(cur_sub, MAX_PATH, L"%ls\\%ls", dest_dir, subdirs[i]);
        swprintf_s(roll_sub, MAX_PATH, L"%ls\\%ls", rollback_dir, subdirs[i]);
        swprintf_s(stg_sub, MAX_PATH, L"%ls\\%ls", staging_dir, subdirs[i]);

        if (dir_exists(cur_sub)) {
            if (!MoveFileExW(cur_sub, roll_sub, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
                log_err("Failed to move %ls to rollback %ls (error %lu)", cur_sub, roll_sub, GetLastError());
                swap_failed = true;
                break;
            }
        }

        if (dir_exists(stg_sub)) {
            if (!MoveFileExW(stg_sub, cur_sub, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
                log_err("Failed to move %ls to dest %ls (error %lu)", stg_sub, cur_sub, GetLastError());
                swap_failed = true;
                break;
            }
        }
    }

    if (swap_failed) {
        log_err("File swap failed. Initiating automatic rollback...");
        if (!unpack_rollback(dest_dir, prev_version)) {
            log_err("Automatic rollback failed; preserving staging and incomplete marker for recovery.");
            return false;
        }
        if (!unpack_clear_incomplete_marker(dest_dir))
            log_err("Rollback succeeded, but the incomplete marker remains; manual recovery may be required.");
        recursive_delete(staging_dir);
        return false;
    }

    // Restore user mosquitto.conf if previously existed
    if (has_user_mosq_conf && file_exists(user_mosq_conf_saved)) {
        CopyFileW(user_mosq_conf_saved, user_mosq_conf, FALSE);
    }

    // Ensure log directories exist
    wchar_t log_dir[MAX_PATH];
    swprintf_s(log_dir, MAX_PATH, L"%ls\\mosquitto\\log", dest_dir);
    CreateDirectoryW(log_dir, NULL);
    swprintf_s(log_dir, MAX_PATH, L"%ls\\l4desk\\log", dest_dir);
    CreateDirectoryW(log_dir, NULL);
    swprintf_s(log_dir, MAX_PATH, L"%ls\\ffmpeg\\log", dest_dir);
    CreateDirectoryW(log_dir, NULL);

    // 7. Cleanup staging directory
    recursive_delete(staging_dir);

    log_info("Payload unpacked and swapped into %ls successfully.", dest_dir);
    return true;
}
