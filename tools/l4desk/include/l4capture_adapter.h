#ifndef L4D_L4CAPTURE_ADAPTER_H
#define L4D_L4CAPTURE_ADAPTER_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <stdbool.h>
#include <stdint.h>
#include "media_backend.h"

typedef struct l4d_l4capture_adapter_cfg {
    wchar_t exe_path[MAX_PATH];
    uint32_t connect_timeout_ms;
    uint32_t ready_timeout_ms;
    uint32_t event_poll_interval_ms;
} l4d_l4capture_adapter_cfg_t;

typedef enum {
    L4D_ADAPTER_IDLE       = 0,
    L4D_ADAPTER_STARTING   = 1,
    L4D_ADAPTER_RUNNING    = 2,
    L4D_ADAPTER_STOPPING   = 3,
    L4D_ADAPTER_RESTARTING = 4,
    L4D_ADAPTER_FAILED     = 5
} l4d_adapter_state_t;

typedef struct l4d_adapter_status {
    l4d_adapter_state_t state;
    DWORD child_pid;
    uint64_t deadline_tick_ms;
    char lease_id[64];
    char stream_id[64];
    l4d_backend_metrics_t metrics;
    bool metrics_valid;
    uint64_t last_event_tick;
    uint32_t recovery_count;
} l4d_adapter_status_t;

/* Adapter lifecycle */
bool l4d_adapter_init(l4d_media_backend_t *backend, const l4d_l4capture_adapter_cfg_t *cfg);
void l4d_adapter_destroy(l4d_media_backend_t *backend);

/* Backend vtable implementation (exposed for testing) */
bool l4d_adapter_start(l4d_media_backend_t *self, const l4d_stream_params_t *params);
bool l4d_adapter_stop(l4d_media_backend_t *self, const char *stream_id);
bool l4d_adapter_renew_lease(l4d_media_backend_t *self, const char *lease_id, uint64_t new_deadline_tick_ms);
bool l4d_adapter_force_idr(l4d_media_backend_t *self, const char *stream_id);
void l4d_adapter_poll(l4d_media_backend_t *self);
bool l4d_adapter_get_metrics(l4d_media_backend_t *self, l4d_backend_metrics_t *out_metrics);
bool l4d_adapter_is_running(l4d_media_backend_t *self);

/* Status query */
void l4d_adapter_get_status(const l4d_media_backend_t *self, l4d_adapter_status_t *out_status);

#endif /* L4D_L4CAPTURE_ADAPTER_H */
