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

bool zip_extract_all(const wchar_t* zip_path, const wchar_t* dest_dir, bool verbose) {
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
        wprintf(L"[ZIP] Extracting %u files/directories to %ls...\n", num_files, dest_dir);
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

        wchar_t full_dest[MAX_PATH] = { 0 };
        swprintf_s(full_dest, MAX_PATH, L"%ls\\%ls", dest_dir, rel_path);

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
                    wprintf(L"[WARN] Failed to extract file: %ls\n", rel_path);
                }
            }
        }
    }

    mz_zip_reader_end(&zip);
    if (verbose) {
        wprintf(L"[ZIP] Successfully extracted %d files.\n", extracted_count);
    }
    return (extracted_count > 0 || num_files == 0);
}
