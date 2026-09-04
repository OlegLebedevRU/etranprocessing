#include "zip_extractor.h"
#include "miniz.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <shlwapi.h>

#pragma comment(lib, "shlwapi.lib")

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

static bool transform_arch_path(const wchar_t* in_path, bool is_dir, const wchar_t* target_arch, wchar_t* out_path, size_t out_size) {
    if (!in_path || !out_path || out_size == 0) return false;

    if (!target_arch || target_arch[0] == L'\0') {
        wcscpy_s(out_path, out_size, in_path);
        return true;
    }

    const wchar_t* opposite_arch = (_wcsicmp(target_arch, L"x64") == 0) ? L"x86" : L"x64";
    size_t target_len = wcslen(target_arch);
    size_t opposite_len = wcslen(opposite_arch);
    size_t in_len = wcslen(in_path);

    // 1. Check if path belongs to opposite architecture -> Skip completely
    for (size_t i = 0; i <= in_len; i++) {
        if (is_arch_segment(in_path, i, opposite_arch, opposite_len)) {
            return false;
        }
    }

    // 2. If this is a directory representing architecture folder -> Skip creating folder
    if (is_dir) {
        for (size_t i = 0; i <= in_len; i++) {
            if (is_arch_segment(in_path, i, target_arch, target_len)) {
                return false;
            }
        }
    }

    // 3. If path contains target_arch segment -> Strip it to place directly in tool folder
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

    // 4. Common file/directory without architecture marker -> Keep as-is
    wcscpy_s(out_path, out_size, in_path);
    return true;
}

bool zip_extract_all(const wchar_t* zip_path, const wchar_t* dest_dir, const wchar_t* target_arch, bool verbose) {
    if (!zip_path || !dest_dir) return false;

    char zip_path_utf8[MAX_PATH * 3] = { 0 };
    WideCharToMultiByte(CP_UTF8, 0, zip_path, -1, zip_path_utf8, sizeof(zip_path_utf8), NULL, NULL);

    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));

    if (!mz_zip_reader_init_file(&zip, zip_path_utf8, 0)) {
        if (verbose) {
            wprintf(L"[ERROR] Failed to open zip archive: %ls\n", zip_path);
        }
        return false;
    }

    mz_uint32 num_files = mz_zip_reader_get_num_files(&zip);
    if (verbose) {
        wprintf(L"[ZIP] Processing %u entries for architecture %ls into %ls...\n",
                num_files, target_arch ? target_arch : L"any", dest_dir);
    }

    CreateDirectoryW(dest_dir, NULL);

    int extracted_count = 0;
    for (mz_uint32 i = 0; i < num_files; i++) {
        mz_zip_archive_file_stat file_stat;
        if (!mz_zip_reader_file_stat(&zip, i, &file_stat)) {
            continue;
        }

        wchar_t rel_path[MAX_PATH] = { 0 };
        MultiByteToWideChar(CP_UTF8, 0, file_stat.m_filename, -1, rel_path, MAX_PATH);

        // Normalize forward slashes to backslashes
        for (size_t k = 0; rel_path[k]; k++) {
            if (rel_path[k] == L'/') rel_path[k] = L'\\';
        }

        // Strip trailing backslash if present
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

            if (mz_zip_reader_extract_to_file(&zip, i, full_dest_utf8, 0)) {
                extracted_count++;
            } else {
                if (verbose) {
                    wprintf(L"[WARN] Failed to extract file: %ls\n", target_rel_path);
                }
            }
        }
    }

    mz_zip_reader_end(&zip);
    if (verbose) {
        wprintf(L"[ZIP] Successfully extracted %d files for architecture %ls.\n",
                extracted_count, target_arch ? target_arch : L"any");
    }
    return (extracted_count > 0 || num_files == 0);
}
