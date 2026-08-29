#pragma once
#include <windows.h>
#include <stdbool.h>

/**
 * Extract entire zip archive into dest_dir.
 * Automatically creates all required subdirectories.
 */
bool zip_extract_all(const wchar_t* zip_path, const wchar_t* dest_dir, bool verbose);
