#pragma once
#include <windows.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char proxy_info[16];           // "ok", "fail"
    char mosquitto_port[16];       // "ok", "fail"
    int user_session_id;
    bool l4desk_running;
    char ffmpeg_smoke_capture[16]; // "ok", "skipped", "fail"
    bool desktop_locked;
    char network[16];              // "reachable", "unreachable"
    char remote_input[32];         // "available", "session_unavailable", "desktop_locked", etc.

    bool critical_failed;
    bool has_warnings;
    int calculated_exit_code;      // 0, 12, or 27
} SmokeProbesResult;

/**
 * Run Phase 5 Smoke Tests:
 * 1. GET http://127.0.0.1:18443/_leo4/info (WinHTTP, 5s timeout)
 * 2. connect 127.0.0.1:1883 (TCP, 3s timeout)
 * 3. WTSGetActiveConsoleSessionId() and l4desk.exe running
 * 4. ffmpeg gdigrab frame 1 test (10s timeout, session 0 -> skipped)
 * 5. desktop_locked check via OpenInputDesktop
 */
bool smoke_run_probes(
    const wchar_t* dest_dir,
    bool is_active_status,
    SmokeProbesResult* out_result
);

#ifdef __cplusplus
}
#endif
