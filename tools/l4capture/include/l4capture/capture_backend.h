#ifndef L4C_CAPTURE_BACKEND_H
#define L4C_CAPTURE_BACKEND_H
#include "types.h"

/* Буфер принадлежит backend до release_frame; очередь хранит собственную копию. */
typedef struct {
    const uint8_t *data;
    uint32_t width;
    uint32_t height;
    int32_t stride;
    size_t buffer_size;
    l4c_rect_t physical_rect;
    uint64_t pts_ms;
    uint64_t geometry_generation;
} l4c_frame_view_t;

typedef struct {
    l4c_rect_t target_rect;
    bool capture_cursor;
} l4c_capture_config_t;

struct l4c_capture_backend;
typedef struct l4c_capture_backend_vtable {
    l4c_status_t (*init)(struct l4c_capture_backend *self, const l4c_capture_config_t *config);
    l4c_status_t (*acquire_frame)(struct l4c_capture_backend *self, l4c_frame_view_t *out_frame, uint32_t timeout_ms);
    void (*release_frame)(struct l4c_capture_backend *self, l4c_frame_view_t *frame);
    void (*destroy)(struct l4c_capture_backend *self);
} l4c_capture_backend_vtable_t;

typedef struct l4c_capture_backend {
    const l4c_capture_backend_vtable_t *vtable;
    void *impl_ctx;
} l4c_capture_backend_t;
#endif