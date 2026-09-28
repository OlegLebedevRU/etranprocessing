#include <limits.h>
#include "l4capture/limits.h"

l4c_status_t l4c_checked_mul(size_t a, size_t b, size_t *out) {
    if (!out) return L4C_ERR_INVALID_ARG;
    if (b && a > SIZE_MAX / b) return L4C_ERR_OVERFLOW;
    *out = a * b;
    return L4C_OK;
}

l4c_status_t l4c_checked_add(size_t a, size_t b, size_t *out) {
    if (!out) return L4C_ERR_INVALID_ARG;
    if (a > SIZE_MAX - b) return L4C_ERR_OVERFLOW;
    *out = a + b;
    return L4C_OK;
}

l4c_status_t l4c_bgra_layout(uint32_t width, uint32_t height, l4c_raster_layout_t *out) {
    uint64_t stride;
    size_t bytes;
    if (!out || !width || !height) return L4C_ERR_INVALID_ARG;
    if ((uint64_t)width * height > L4C_MAX_PIXELS_AREA) return L4C_ERR_OVERFLOW;
    stride = ((uint64_t)width * 4u + 15u) & ~UINT64_C(15);
    if (stride > INT32_MAX || l4c_checked_mul((size_t)stride, height, &bytes) != L4C_OK)
        return L4C_ERR_OVERFLOW;
    if (bytes > L4C_ADMISSION_MEMORY_LIMIT) return L4C_ERR_OVERFLOW;
    out->stride = (uint32_t)stride;
    out->bytes = bytes;
    return L4C_OK;
}

l4c_status_t l4c_rect_layout(const l4c_rect_t *rect, l4c_raster_layout_t *out) {
    int64_t width, height;
    if (!rect || !out) return L4C_ERR_INVALID_ARG;
    width = (int64_t)rect->right - rect->left;
    height = (int64_t)rect->bottom - rect->top;
    if (width <= 0 || height <= 0) return L4C_ERR_INVALID_ARG;
    if (width > UINT32_MAX || height > UINT32_MAX) return L4C_ERR_OVERFLOW;
    return l4c_bgra_layout((uint32_t)width, (uint32_t)height, out);
}

l4c_status_t l4c_memory_reserve(size_t *used, size_t bytes) {
    size_t total;
    if (!used) return L4C_ERR_INVALID_ARG;
    if (l4c_checked_add(*used, bytes, &total) != L4C_OK || total > L4C_ADMISSION_MEMORY_LIMIT)
        return L4C_ERR_OUT_OF_MEMORY;
    *used = total;
    return L4C_OK;
}

void l4c_memory_release(size_t *used, size_t bytes) {
    if (used && bytes <= *used) *used -= bytes;
}