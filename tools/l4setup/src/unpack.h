#pragma once
#include <windows.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Check if the existing installation in dest_dir matches current_version
 * and all required executables are present and intact.
 */
bool unpack_is_idempotent(const wchar_t* dest_dir, const char* current_version);

/**
 * Unpack payload (either embedded resource or from payload_dir) into staging dir,
 * atomically swap into dest_dir, moving previous files to rollback/<prev_version>,
 * preserving user configurations and logs.
 * 
 * Returns true on success, false on error (exit code 23).
 */
bool unpack_payload(
    const wchar_t* dest_dir,
    const char* target_arch, // "x64" or "x86"
    const wchar_t* dev_payload_dir, // May be NULL
    const char* current_version
);

/**
 * Filter and transform relative file path based on target architecture ("x64" or "x86").
 */
bool transform_arch_path(const wchar_t* in_path, bool is_dir, const wchar_t* target_arch, wchar_t* out_path, size_t out_size);

#ifdef __cplusplus
}
#endif
