#ifndef L4D_KIOSK_LIFECYCLE_H
#define L4D_KIOSK_LIFECYCLE_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    L4D_KIOSK_STATUS_STOPPED      = 0,
    L4D_KIOSK_STATUS_RUNNING      = 1,
    L4D_KIOSK_STATUS_STARTING     = 2,
    L4D_KIOSK_STATUS_STOPPING     = 3,
    L4D_KIOSK_STATUS_UNRESPONSIVE = 4
} l4d_kiosk_status_t;

typedef struct {
    l4d_kiosk_status_t status;
    DWORD pid;
    HWND hwnd;
    uint64_t uptime_sec;
    bool is_hung;
    wchar_t process_name[MAX_PATH];
} l4d_kiosk_info_t;

bool kiosk_lifecycle_init(const wchar_t *process_name, const wchar_t *launch_path, const wchar_t *cmd_args);
bool kiosk_lifecycle_start(DWORD *out_pid, DWORD *out_error);
bool kiosk_lifecycle_stop(uint32_t timeout_ms, bool force_terminate, DWORD *out_error);
bool kiosk_lifecycle_restart(uint32_t stop_timeout_ms, DWORD *out_pid, DWORD *out_error);
bool kiosk_lifecycle_get_status(l4d_kiosk_info_t *out_info);

#endif /* L4D_KIOSK_LIFECYCLE_H */
