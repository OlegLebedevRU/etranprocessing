#ifndef L4D_MEDIA_BACKEND_H
#define L4D_MEDIA_BACKEND_H

#include <stdint.h>
#include <stdbool.h>
#include <windows.h>

typedef enum {
    L4D_BACKEND_FFMPEG   = 0,
    L4D_BACKEND_L4CAPTURE = 1
} l4d_backend_type_t;

typedef enum {
    L4D_STREAM_STOPPED   = 0,
    L4D_STREAM_STARTING  = 1,
    L4D_STREAM_RUNNING   = 2,
    L4D_STREAM_RESTARTING= 3,
    L4D_STREAM_FAILED    = 4
} l4d_stream_state_t;

typedef struct l4d_stream_params {
    const char *lease_id;
    const char *stream_id;
    const char *source_id;
    const char *profile;
    RECT source_rect;
    uint64_t geometry_gen;
    uint16_t rtp_port;
    uint16_t rtcp_port;
    uint64_t deadline_tick_ms;
} l4d_stream_params_t;

typedef struct l4d_backend_metrics {
    uint16_t fps;
    uint32_t bitrate_kbps;
    uint32_t raw_drops;
    uint32_t encoder_drops;
    uint32_t transport_drops;
    uint16_t encode_p95_ms;
    uint16_t queue_depth;
    uint32_t private_bytes_kb;
    uint32_t gdi_handles;
    uint16_t actual_width;
    uint16_t actual_height;
    uint16_t degradation_state;
} l4d_backend_metrics_t;

struct l4d_media_backend;

typedef struct l4d_media_backend_vtable {
    bool (*start)(struct l4d_media_backend *self, const l4d_stream_params_t *params);
    bool (*stop)(struct l4d_media_backend *self, const char *stream_id);
    bool (*renew_lease)(struct l4d_media_backend *self, const char *lease_id, uint64_t new_deadline_tick_ms);
    bool (*force_idr)(struct l4d_media_backend *self, const char *stream_id);
    void (*poll)(struct l4d_media_backend *self);
    bool (*get_metrics)(struct l4d_media_backend *self, l4d_backend_metrics_t *out_metrics);
    bool (*is_running)(struct l4d_media_backend *self);
    void (*destroy)(struct l4d_media_backend *self);
} l4d_media_backend_vtable_t;

typedef struct l4d_media_backend {
    const l4d_media_backend_vtable_t *vtable;
    l4d_backend_type_t type;
    void *impl_ctx;
} l4d_media_backend_t;

l4d_media_backend_t *l4d_media_backend_create(l4d_backend_type_t type, const char *bin_dir);

#endif /* L4D_MEDIA_BACKEND_H */
