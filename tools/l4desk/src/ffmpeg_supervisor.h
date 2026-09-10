#ifndef L4DESK_FFMPEG_SUPERVISOR_H
#define L4DESK_FFMPEG_SUPERVISOR_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <stdbool.h>
#include <stdint.h>
#include "display_inventory.h"

typedef struct {
    char stream_instance_id[64];
    char lease_id[64];
    char mode[32];        /* "desktop", "usb-camera", "stopped" */
    char source_id[64];   /* "disp:...", "cam:..." */
    char profile[32];
    RECT desktop_rect;
    DWORD session_id;
    char state[32];       /* "stopped", "starting", "running", "stopping", "restarting", "failed", "source_unavailable", "session_unavailable" */
    char reason[64];
    DWORD ffmpeg_pid;
    uint64_t ffmpeg_start_time;
    uint64_t started_at;
    int restart_count;
} StreamStateInfo;

typedef void (*FFmpegEventCallback)(const char* stream_instance_id,
                                    const char* state,
                                    const char* reason,
                                    void* user_data);

void ffmpeg_supervisor_init(const char* base_path, const char* sn);
void ffmpeg_supervisor_cleanup(void);
void ffmpeg_supervisor_set_event_callback(FFmpegEventCallback cb, void* user_data);

void ffmpeg_supervisor_set_custom_binary(const wchar_t* path);

void ffmpeg_supervisor_reconcile(void);

bool ffmpeg_supervisor_start(const char* stream_instance_id,
                             const char* lease_id,
                             const char* mode,
                             const char* source_id,
                             const char* profile,
                             const SystemInventory* inv,
                             char* out_result, size_t max_result,
                             char* out_err_code, size_t max_err_code,
                             char* out_err_msg, size_t max_err_msg);

bool ffmpeg_supervisor_stop(const char* stream_instance_id,
                            const char* lease_id,
                            char* out_result, size_t max_result,
                            char* out_err_code, size_t max_err_code,
                            char* out_err_msg, size_t max_err_msg);

bool ffmpeg_supervisor_tick(const SystemInventory* inv,
                            bool* p_state_changed,
                            char* out_new_state, size_t max_state_len,
                            char* out_reason, size_t max_reason_len);

void ffmpeg_supervisor_get_info(StreamStateInfo* out_info);

#endif /* L4DESK_FFMPEG_SUPERVISOR_H */
