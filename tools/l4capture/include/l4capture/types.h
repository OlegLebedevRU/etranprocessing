#ifndef L4C_TYPES_H
#define L4C_TYPES_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef enum {
    L4C_OK = 0,
    L4C_ERR_NO_FRAME = 1,
    L4C_ERR_DEVICE_LOST = 2,
    L4C_ERR_SESSION_UNAVAILABLE = 3,
    L4C_ERR_OUT_OF_MEMORY = 4,
    L4C_ERR_INVALID_ARG = 5,
    L4C_ERR_OVERFLOW = 6,
    L4C_ERR_PROTOCOL = 7,
    L4C_ERR_DEADLINE_EXPIRED = 8,
    L4C_ERR_PIPE_BROKEN = 9,
    L4C_ERR_NETWORK = 10,
    L4C_ERR_FATAL = 99
} l4c_status_t;

typedef enum {
    L4C_PIX_FMT_BGRA = 1,
    L4C_PIX_FMT_I420 = 2,
    L4C_PIX_FMT_NV12 = 3
} l4c_pixel_format_t;

typedef struct {
    int32_t left, top, right, bottom;
} l4c_rect_t;
#endif