#ifndef L4C_ENCODER_BACKEND_H
#define L4C_ENCODER_BACKEND_H
#include "types.h"
typedef struct {
    const uint8_t *data; /* Без Annex B / AVCC префикса. */
    uint32_t length;
    uint8_t nal_type;
} l4c_nal_desc_t;

/* Действителен до release_au; размер не больше L4C_MAX_AU_SIZE. */
typedef struct {
    l4c_nal_desc_t *nals;
    uint32_t nal_count;
    uint64_t pts_ms;
    bool is_idr;
    size_t total_bytes;
} l4c_access_unit_t;

typedef struct {
    uint32_t width, height;
    uint32_t target_fps;
    uint32_t target_bitrate_kbps, max_bitrate_kbps;
    l4c_pixel_format_t input_format;
} l4c_encoder_config_t;

typedef struct {
    const uint8_t *planes[3];
    uint32_t strides[3];
    uint32_t width, height;
    l4c_pixel_format_t format;
    uint64_t pts_ms;
    bool force_idr;
    size_t plane_sizes[3]; /* Явные границы каждого borrowed буфера. */
} l4c_raw_frame_t;

struct l4c_encoder_backend;
typedef struct l4c_encoder_backend_vtable {
    l4c_status_t (*init)(struct l4c_encoder_backend *self, const l4c_encoder_config_t *config);
    l4c_status_t (*encode)(struct l4c_encoder_backend *self, const l4c_raw_frame_t *raw, l4c_access_unit_t *out_au);
    l4c_status_t (*force_idr)(struct l4c_encoder_backend *self);
    void (*release_au)(struct l4c_encoder_backend *self, l4c_access_unit_t *au);
    void (*destroy)(struct l4c_encoder_backend *self);
} l4c_encoder_backend_vtable_t;

typedef struct l4c_encoder_backend {
    const l4c_encoder_backend_vtable_t *vtable;
    void *impl_ctx;
} l4c_encoder_backend_t;
#endif