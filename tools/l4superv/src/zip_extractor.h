#pragma once
#include <windows.h>
#include <stdbool.h>

/**
 * Extract zip archive into dest_dir, selectively filtering by architecture:
 * - target_arch: "x64" or "x86".
 * - Entries matching opposite architecture segment (e.g. "\x86\" or "x86\") are skipped.
 * - Entries matching target architecture segment (e.g. "\x64\" or "x64\") have the segment stripped,
 *   placing the binary directly into the utility folder (e.g. leo4proxy\x64\leo4proxy.exe -> leo4proxy\leo4proxy.exe).
 * - Directory entries for architectures are skipped to prevent creating empty x86/x64 folders.
 * - Common files (mosquitto, scripts, configs, docs) are extracted as-is.
 */
bool zip_extract_all(const wchar_t* zip_path, const wchar_t* dest_dir, const wchar_t* target_arch, bool verbose);
