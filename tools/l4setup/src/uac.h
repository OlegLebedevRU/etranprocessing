#pragma once
#include <windows.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

bool uac_is_elevated(void);

/**
 * Re-launch current process with elevated privileges ("runas").
 * Waits for child process to exit and returns its exit code.
 * If user declined UAC prompt (or error occurred), sets *user_cancelled = true and returns 20.
 */
DWORD uac_relaunch_elevated(const wchar_t* cmd_line_args, bool* user_cancelled);

/**
 * Extract argument string from full GetCommandLineW() string, skipping executable path.
 */
const wchar_t* uac_get_arguments(const wchar_t* full_cmd_line);

#ifdef __cplusplus
}
#endif
