#pragma once
#include <windows.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DRAINAGE_MAX_ITEMS 16

typedef struct {
    char services_stopped[DRAINAGE_MAX_ITEMS][64];
    int services_stopped_count;

    char processes_killed[DRAINAGE_MAX_ITEMS][128];
    int processes_killed_count;

    int ports_freed[DRAINAGE_MAX_ITEMS];
    int ports_freed_count;

    bool active_stream_detected;
} DrainageResult;

/**
 * Perform system drainage prior to update/reinstall:
 * - Check for active ffmpeg stream (prompts user if not silent; aborts with code 22 if silent).
 * - Stop services in reverse dependency order: L4Superv -> L4Con -> mosquitto -> Leo4Proxy
 *   (signals Global\L4Desk_Stop_<SN> when stopping L4Superv).
 * - Terminate orphaned ffmpeg.exe and l4desk.exe ONLY if residing in dest_dir and lingering after graceful stop.
 * - Free loopback ports 1883, 18443, 18883 ONLY if owner process is in dest_dir.
 * 
 * Returns true if drainage succeeded.
 * Returns false if drainage failed (foreign process on port, active stream denied, etc.) -> exit code 22.
 */
bool drainage_execute(const wchar_t* dest_dir, const char* sn, bool silent, DrainageResult* out_result);

#ifdef __cplusplus
}
#endif
