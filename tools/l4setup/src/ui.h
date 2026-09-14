#pragma once
#include <windows.h>
#include <stdbool.h>
#include "cli.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Launch single-window Win32 interactive setup GUI.
 * Runs standard Win32 message loop with a background worker thread executing
 * the 7 setup phases: Check -> Prepare -> Stop -> Update -> Start -> Verify -> Finish.
 * Returns process exit code (0 for ready, or product error code).
 */
int ui_run_interactive_setup(HINSTANCE hInstance, CliOptions* opts);

#ifdef __cplusplus
}
#endif
