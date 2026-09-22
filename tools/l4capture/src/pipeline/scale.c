#include <string.h>
#include "l4capture/scale.h"
#include "l4capture/limits.h"

l4c_status_t l4c_scale_bilinear_bgra(
    const uint8_t *src, uint32_t src_w, uint32_t src_h, int32_t src_stride,
    uint8_t *dst, uint32_t dst_w, uint32_t dst_h, int32_t dst_stride)
{
    uint32_t dy, dx;
    uint64_t x_ratio, y_ratio;
    if (!src || !dst || !src_w || !src_h || !dst_w || !dst_h) return L4C_ERR_INVALID_ARG;
    if (src_stride <= 0 || dst_stride <= 0) return L4C_ERR_INVALID_ARG;
    if (dst_w == 1 || dst_h == 1) {
        /* Degenerate: copy top-left pixel to entire output */
        uint32_t c;
        for (dy = 0; dy < dst_h; ++dy) {
            uint8_t *out_row = dst + (size_t)dy * (size_t)dst_stride;
            for (dx = 0; dx < dst_w; ++dx) {
                for (c = 0; c < 4; ++c) out_row[dx * 4 + c] = src[c];
            }
        }
        return L4C_OK;
    }
    x_ratio = ((uint64_t)(src_w - 1) << 16) / (uint64_t)(dst_w - 1);
    y_ratio = ((uint64_t)(src_h - 1) << 16) / (uint64_t)(dst_h - 1);
    for (dy = 0; dy < dst_h; ++dy) {
        uint64_t sy_fp = (uint64_t)dy * y_ratio;
        uint32_t sy = (uint32_t)(sy_fp >> 16);
        uint32_t fy = (uint32_t)(sy_fp & 0xFFFF);
        uint32_t sy1 = sy + 1;
        if (sy1 >= src_h) sy1 = src_h - 1;
        const uint8_t *row0 = src + (size_t)sy * (size_t)src_stride;
        const uint8_t *row1 = src + (size_t)sy1 * (size_t)src_stride;
        uint8_t *out_row = dst + (size_t)dy * (size_t)dst_stride;
        for (dx = 0; dx < dst_w; ++dx) {
            uint64_t sx_fp = (uint64_t)dx * x_ratio;
            uint32_t sx = (uint32_t)(sx_fp >> 16);
            uint32_t fx = (uint32_t)(sx_fp & 0xFFFF);
            uint32_t sx1 = sx + 1;
            uint32_t c, ch;
            const uint8_t *p00, *p10, *p01, *p11;
            uint64_t w00, w10, w01, w11;
            if (sx1 >= src_w) sx1 = src_w - 1;
            p00 = row0 + (size_t)sx * 4;
            p10 = row0 + (size_t)sx1 * 4;
            p01 = row1 + (size_t)sx * 4;
            p11 = row1 + (size_t)sx1 * 4;
            w00 = (uint64_t)(65536u - fx) * (65536u - fy);
            w10 = (uint64_t)fx * (65536u - fy);
            w01 = (uint64_t)(65536u - fx) * fy;
            w11 = (uint64_t)fx * fy;
            for (c = 0; c < 4; ++c) {
                uint64_t sum = (uint64_t)p00[c] * w00 + (uint64_t)p10[c] * w10 +
                               (uint64_t)p01[c] * w01 + (uint64_t)p11[c] * w11;
                ch = (uint32_t)((sum + UINT64_C(0x80000000)) >> 32);
                if (ch > 255) ch = 255;
                out_row[dx * 4 + c] = (uint8_t)ch;
            }
        }
    }
    return L4C_OK;
}
