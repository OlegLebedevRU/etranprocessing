#include "db_discovery.h"
#include <windows.h>
#include <shlwapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma comment(lib, "shlwapi.lib")

static bool is_matching_dir_name(const wchar_t* name) {
    if (!name) return false;
    if (PathMatchSpecW(name, L"*platerra*") ||
        PathMatchSpecW(name, L"*postomat*") ||
        PathMatchSpecW(name, L"*postamat*")) {
        return true;
    }
    return false;
}

static bool get_latest_log_time(const wchar_t* dir_path, FILETIME* out_ft, wchar_t* out_log_file, size_t out_log_max) {
    if (!dir_path || !out_ft) return false;
    ZeroMemory(out_ft, sizeof(FILETIME));
    if (out_log_file && out_log_max > 0) out_log_file[0] = L'\0';

    wchar_t search_pattern[MAX_PATH];
    _snwprintf(search_pattern, MAX_PATH, L"%ls\\log\\PlaterraTerminal*.log", dir_path);

    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW(search_pattern, &fd);
    if (hFind == INVALID_HANDLE_VALUE) {
        // Fallback to any .log file in log folder
        _snwprintf(search_pattern, MAX_PATH, L"%ls\\log\\*.log", dir_path);
        hFind = FindFirstFileW(search_pattern, &fd);
        if (hFind == INVALID_HANDLE_VALUE) {
            return false;
        }
    }

    bool found = false;
    FILETIME best_ft = {0, 0};
    wchar_t best_name[MAX_PATH] = {0};

    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            if (!found || CompareFileTime(&fd.ftLastWriteTime, &best_ft) > 0) {
                best_ft = fd.ftLastWriteTime;
                wcsncpy(best_name, fd.cFileName, MAX_PATH - 1);
                found = true;
            }
        }
    } while (FindNextFileW(hFind, &fd));

    FindClose(hFind);

    if (found) {
        *out_ft = best_ft;
        if (out_log_file && out_log_max > 0) {
            _snwprintf(out_log_file, out_log_max, L"%ls\\log\\%ls", dir_path, best_name);
        }
    }
    return found;
}

bool db_discovery_find_terminal_config(DBConfig* out_cfg, bool verbose) {
    if (!out_cfg) return false;
    db_config_init(out_cfg);

    wchar_t drives_buf[512] = {0};
    DWORD len = GetLogicalDriveStringsW(sizeof(drives_buf)/sizeof(wchar_t) - 1, drives_buf);
    if (len == 0) return false;

    wchar_t best_terminal_dir[MAX_PATH] = {0};
    wchar_t best_xml_path[MAX_PATH] = {0};
    wchar_t best_log_path[MAX_PATH] = {0};
    FILETIME best_ft = {0, 0};
    bool found_active = false;

    wchar_t* drive = drives_buf;
    while (*drive) {
        UINT drive_type = GetDriveTypeW(drive);
        if (drive_type == DRIVE_FIXED || drive_type == DRIVE_REMOVABLE) {
            if (verbose) {
                wprintf(L"[discovery] Scanning root of drive %ls...\n", drive);
            }

            wchar_t search_pattern[MAX_PATH];
            _snwprintf(search_pattern, MAX_PATH, L"%ls*", drive);

            WIN32_FIND_DATAW fd;
            HANDLE hFind = FindFirstFileW(search_pattern, &fd);
            if (hFind != INVALID_HANDLE_VALUE) {
                do {
                    if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
                        wcscmp(fd.cFileName, L".") != 0 &&
                        wcscmp(fd.cFileName, L"..") != 0) {

                        if (is_matching_dir_name(fd.cFileName)) {
                            wchar_t candidate_dir[MAX_PATH];
                            _snwprintf(candidate_dir, MAX_PATH, L"%ls%ls", drive, fd.cFileName);

                            wchar_t xml_path[MAX_PATH];
                            _snwprintf(xml_path, MAX_PATH, L"%ls\\DBConfig.xml", candidate_dir);

                            bool has_xml = (GetFileAttributesW(xml_path) != INVALID_FILE_ATTRIBUTES);
                            FILETIME log_ft = {0, 0};
                            wchar_t log_file[MAX_PATH] = {0};
                            bool has_log = get_latest_log_time(candidate_dir, &log_ft, log_file, MAX_PATH);

                            if (verbose) {
                                wprintf(L"[discovery] Found directory: %ls (DBConfig.xml: %ls, Log: %ls)\n",
                                        candidate_dir,
                                        has_xml ? L"YES" : L"NO",
                                        has_log ? log_file : L"NONE");
                            }

                            if (has_xml) {
                                if (!found_active || (has_log && CompareFileTime(&log_ft, &best_ft) > 0)) {
                                    wcsncpy(best_terminal_dir, candidate_dir, MAX_PATH - 1);
                                    wcsncpy(best_xml_path, xml_path, MAX_PATH - 1);
                                    if (has_log) {
                                        best_ft = log_ft;
                                        wcsncpy(best_log_path, log_file, MAX_PATH - 1);
                                    }
                                    found_active = true;
                                }
                            }
                        }
                    }
                } while (FindNextFileW(hFind, &fd));
                FindClose(hFind);
            }
        }
        drive += wcslen(drive) + 1;
    }

    if (found_active) {
        if (verbose) {
            wprintf(L"[discovery] Selected active terminal directory: %ls\n", best_terminal_dir);
            if (best_log_path[0]) {
                wprintf(L"[discovery] Most recent log file: %ls\n", best_log_path);
            }
            wprintf(L"[discovery] Loading DB configuration from %ls\n", best_xml_path);
        }

        bool parsed = db_config_parse_file(best_xml_path, out_cfg);
        if (parsed) {
            WideCharToMultiByte(CP_UTF8, 0, best_terminal_dir, -1, out_cfg->terminal_dir, MAX_PATH, NULL, NULL);
            return true;
        }
    }

    if (verbose) {
        wprintf(L"[discovery] No DBConfig.xml discovered. Using default (local)\\sqlexpress, DB=Terminal\n");
    }
    return false;
}
