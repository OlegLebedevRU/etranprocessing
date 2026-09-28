#include <string.h>
#include "l4capture/scale.h"
#include "l4capture/limits.h"

/* Catmull-Rom (a = -0.5): w0..w3 для t in [0,1] между соседями p1 и p2. */
static void cubic_weights(float t, float w[4]) {
    float t2 = t * t;
    float t3 = t2 * t;
    w[0] = -0.5f * t + t2 - 0.5f * t3;
    w[1] = 1.0f - 2.5f * t2 + 1.5f * t3;
    w[2] = 0.5f * t + 2.0f * t2 - 1.5f * t3;
    w[3] = -0.5f * t2 + 0.5f * t3;
}

static int32_t clamp_i(int32_t v, int32_t lo, int32_t hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static uint8_t clamp_u8(float v) {
    if (v < 0.0f) return 0;
    if (v > 255.0f) return 255;
    return (uint8_t)(v + 0.5f);
}

static const uint8_t *src_row(const uint8_t *src, uint32_t src_h, int32_t src_stride, int32_t y) {
    y = clamp_i(y, 0, (int32_t)src_h - 1);
    return src + (size_t)y * (size_t)src_stride;
}

l4c_status_t l4c_scale_bicubic_bgra(
    const uint8_t *src,
    uint32_t src_w,
    uint32_t src_h,
    int32_t src_stride,
    uint8_t *dst,
    uint32_t dst_w,
    uint32_t dst_h,
    int32_t dst_stride)
{
    uint32_t dy, dx, c;
    uint64_t x_ratio, y_ratio;
    if (!src || !dst || !src_w || !src_h || !dst_w || !dst_h) return L4C_ERR_INVALID_ARG;
    if (src_stride <= 0 || dst_stride <= 0) return L4C_ERR_INVALID_ARG;

    if (src_w == dst_w && src_h == dst_h && src_stride == dst_stride) {
        memcpy(dst, src, (size_t)src_h * (size_t)src_stride);
        return L4C_OK;
    }

    if (dst_w == 1 || dst_h == 1) {
        uint32_t dy2, dx2;
        for (dy2 = 0; dy2 < dst_h; ++dy2) {
            uint8_t *out_row = dst + (size_t)dy2 * (size_t)dst_stride;
            for (dx2 = 0; dx2 < dst_w; ++dx2) {
                for (c = 0; c < 4; ++c) out_row[dx2 * 4 + c] = src[c];
            }
        }
        return L4C_OK;
    }

    /* Та же mapping, что и bilinear: (n-1)/(m-1) — углы 1:1 с исходником. */
    x_ratio = ((uint64_t)(src_w - 1) << 16) / (uint64_t)(dst_w - 1);
    y_ratio = ((uint64_t)(src_h - 1) << 16) / (uint64_t)(dst_h - 1);

    for (dy = 0; dy < dst_h; ++dy) {
        uint64_t sy_fp = (uint64_t)dy * y_ratio;
        int32_t sy = (int32_t)(sy_fp >> 16);
        float fy = (float)(sy_fp & 0xFFFF) / 65536.0f;
        float wy[4];
        const uint8_t *rows[4];
        uint8_t *out_row = dst + (size_t)dy * (size_t)dst_stride;
        int k;

        cubic_weights(fy, wy);
        for (k = 0; k < 4; ++k) {
            rows[k] = src_row(src, src_h, src_stride, sy - 1 + k);
        }

        for (dx = 0; dx < dst_w; ++dx) {
            uint64_t sx_fp = (uint64_t)dx * x_ratio;
            int32_t sx = (int32_t)(sx_fp >> 16);
            float fx = (float)(sx_fp & 0xFFFF) / 65536.0f;
            float wx[4];
            int32_t xs[4];
            int i, j;

            cubic_weights(fx, wx);
            for (i = 0; i < 4; ++i) {
                xs[i] = clamp_i(sx - 1 + i, 0, (int32_t)src_w - 1);
            }

            for (c = 0; c < 4; ++c) {
                float sum = 0.0f;
                for (j = 0; j < 4; ++j) {
                    float col = 0.0f;
                    for (i = 0; i < 4; ++i) {
                        col += wy[i] * (float)rows[j][xs[i] * 4 + c];
                    }
                    sum += wx[j] * col;
                }
                out_row[dx * 4 + c] = clamp_u8(sum);
            }
        }
    }
    return L4C_OK;
}
