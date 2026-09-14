#pragma once
#include <windows.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Compare two version strings numerically by components (major.minor.patch.build).
 * Returns:
 *   > 0 if v1 > v2
 *  == 0 if v1 == v2
 *   < 0 if v1 < v2
 */
int version_compare(const char* v1, const char* v2);

/**
 * Read installed_version from state.json in dest_dir.
 * Returns true if found and parsed, false otherwise.
 */
bool unpack_read_installed_version(const wchar_t* dest_dir, char* out_version, size_t out_size);

/**
 * Incomplete install marker management in state.json:
 * Records phase, old_version, and target_version prior to updating live files.
 */
bool unpack_has_incomplete_marker(const wchar_t* dest_dir, char* out_phase, size_t out_phase_size);
bool unpack_set_incomplete_marker(const wchar_t* dest_dir, const char* phase, const char* old_ver, const char* target_ver);
bool unpack_clear_incomplete_marker(const wchar_t* dest_dir);

/**
 * Recover from interrupted install/power loss if incomplete marker is found.
 */
bool unpack_recover_from_crash(const wchar_t* dest_dir);

/**
 * Verify that executables in dest_dir are not locked by other processes.
 * Waits up to wait_timeout_ms (e.g. 10000ms).
 */
bool unpack_check_files_locked(const wchar_t* dest_dir, DWORD wait_timeout_ms);

/**
 * Roll back previous version from dest\rollback\<prev_version>.
 */
bool unpack_rollback(const wchar_t* dest_dir, const char* prev_version);

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
