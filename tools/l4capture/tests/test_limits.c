#include "l4capture/limits.h"

int test_limits_checked_mul(void) {
    size_t result;
    l4c_status_t s;
    /* Normal multiplication. */
    s = l4c_checked_mul(100, 200, &result);
    if (s != L4C_OK || result != 20000) return 1;
    /* Overflow detection. */
    s = l4c_checked_mul(SIZE_MAX, 2, &result);
    if (s != L4C_ERR_OVERFLOW) return 2;
    /* Zero. */
    s = l4c_checked_mul(0, SIZE_MAX, &result);
    if (s != L4C_OK || result != 0) return 3;
    /* NULL out. */
    s = l4c_checked_mul(1, 2, NULL);
    if (s != L4C_ERR_INVALID_ARG) return 4;
    return 0;
}

int test_limits_checked_add(void) {
    size_t result;
    l4c_status_t s;
    s = l4c_checked_add(SIZE_MAX - 1, 1, &result);
    if (s != L4C_OK || result != SIZE_MAX) return 1;
    s = l4c_checked_add(SIZE_MAX, 1, &result);
    if (s != L4C_ERR_OVERFLOW) return 2;
    s = l4c_checked_add(0, 0, &result);
    if (s != L4C_OK || result != 0) return 3;
    return 0;
}

int test_limits_bgra_layout(void) {
    l4c_raster_layout_t layout;
    l4c_status_t s;
    /* 1920x1080 BGRA: stride = 1920*4 = 7680, already 16-aligned. */
    s = l4c_bgra_layout(1920, 1080, &layout);
    if (s != L4C_OK) return 1;
    if (layout.stride != 7680) return 2;
    if (layout.bytes != (size_t)7680 * 1080) return 3;
    /* Zero dimension rejected. */
    s = l4c_bgra_layout(0, 1080, &layout);
    if (s != L4C_ERR_INVALID_ARG) return 4;
    return 0;
}

int test_limits_4k_cap(void) {
    l4c_raster_layout_t layout;
    l4c_status_t s;
    /* 3840x2160 = 8294400 pixels — exactly at limit. */
    s = l4c_bgra_layout(3840, 2160, &layout);
    if (s != L4C_OK) return 1;
    /* 3841x2160 = 8296560 pixels — exceeds limit. */
    s = l4c_bgra_layout(3841, 2160, &layout);
    if (s != L4C_ERR_OVERFLOW) return 2;
    /* 3840x2161 — exceeds limit. */
    s = l4c_bgra_layout(3840, 2161, &layout);
    if (s != L4C_ERR_OVERFLOW) return 3;
    /* 1x1 — minimum valid. */
    s = l4c_bgra_layout(1, 1, &layout);
    if (s != L4C_OK || layout.stride != 16 || layout.bytes != 16) return 4;
    return 0;
}

int test_limits_stride_alignment(void) {
    l4c_raster_layout_t layout;
    l4c_status_t s;
    /* 1x1: width*4 = 4 bytes, aligned to 16 => stride = 16. */
    s = l4c_bgra_layout(1, 1, &layout);
    if (s != L4C_OK || layout.stride != 16) return 1;
    /* 3x1: width*4 = 12, aligned to 16 => stride = 16. */
    s = l4c_bgra_layout(3, 1, &layout);
    if (s != L4C_OK || layout.stride != 16) return 2;
    /* 4x1: width*4 = 16, aligned to 16 => stride = 16. */
    s = l4c_bgra_layout(4, 1, &layout);
    if (s != L4C_OK || layout.stride != 16) return 3;
    /* 5x1: width*4 = 20, aligned to 16 => stride = 32. */
    s = l4c_bgra_layout(5, 1, &layout);
    if (s != L4C_OK || layout.stride != 32) return 4;
    return 0;
}

int test_limits_memory_reserve(void) {
    size_t used = 0;
    l4c_status_t s;
    /* Reserve within limit. */
    s = l4c_memory_reserve(&used, L4C_ADMISSION_MEMORY_LIMIT - 1);
    if (s != L4C_OK || used != L4C_ADMISSION_MEMORY_LIMIT - 1) return 1;
    /* Overflow. */
    s = l4c_memory_reserve(&used, 2);
    if (s != L4C_ERR_OUT_OF_MEMORY) return 2;
    /* Release and re-reserve. */
    l4c_memory_release(&used, L4C_ADMISSION_MEMORY_LIMIT - 1);
    if (used != 0) return 3;
    s = l4c_memory_reserve(&used, L4C_ADMISSION_MEMORY_LIMIT);
    if (s != L4C_OK) return 4;
    return 0;
}
