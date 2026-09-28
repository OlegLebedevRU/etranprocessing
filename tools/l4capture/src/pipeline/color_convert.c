#include <stdlib.h>
#include <string.h>
#include "l4capture/color_convert.h"
#include "l4capture/limits.h"

struct l4c_color_converter {
    uint32_t width, height;
    int32_t stride_y, stride_u, stride_v, stride_uv;
    uint8_t *plane_y;
    uint8_t *plane_u;
    uint8_t *plane_v;
    uint8_t *plane_uv;
    uint8_t *buffer; /* Single allocation for all planes */
    size_t plane_y_size, plane_u_size, plane_v_size, plane_uv_size;
};

static int32_t align16(int32_t v) { return (v + 15) & ~15; }

static uint8_t clamp_u8(int v) {
    if (v < 0) return 0;
    if (v > 255) return 255;
    return (uint8_t)v;
}

l4c_status_t l4c_color_converter_create(uint32_t width, uint32_t height, l4c_color_converter_t **out_converter) {
    l4c_color_converter_t *cvt;
    uint32_t half_w, half_h;
    size_t total;
    if (!out_converter || !width || !height) return L4C_ERR_INVALID_ARG;
    if ((uint64_t)width * height > L4C_MAX_PIXELS_AREA) return L4C_ERR_OVERFLOW;
    cvt = (l4c_color_converter_t *)calloc(1, sizeof(l4c_color_converter_t));
    if (!cvt) return L4C_ERR_OUT_OF_MEMORY;
    cvt->width = width;
    cvt->height = height;
    cvt->stride_y = align16((int32_t)width);
    half_w = (width + 1u) / 2u;
    half_h = (height + 1u) / 2u;
    cvt->stride_u = align16((int32_t)half_w);
    cvt->stride_v = align16((int32_t)half_w);
    cvt->stride_uv = cvt->stride_y;
    cvt->plane_y_size = (size_t)cvt->stride_y * (size_t)height;
    cvt->plane_u_size = (size_t)cvt->stride_u * (size_t)half_h;
    cvt->plane_v_size = (size_t)cvt->stride_v * (size_t)half_h;
    cvt->plane_uv_size = (size_t)cvt->stride_uv * (size_t)half_h;
    total = cvt->plane_y_size + cvt->plane_u_size + cvt->plane_v_size + cvt->plane_uv_size;
    if (total > L4C_ADMISSION_MEMORY_LIMIT) { free(cvt); return L4C_ERR_OUT_OF_MEMORY; }
    cvt->buffer = (uint8_t *)malloc(total);
    if (!cvt->buffer) { free(cvt); return L4C_ERR_OUT_OF_MEMORY; }
    cvt->plane_y = cvt->buffer;
    cvt->plane_u = cvt->plane_y + cvt->plane_y_size;
    cvt->plane_v = cvt->plane_u + cvt->plane_u_size;
    cvt->plane_uv = cvt->plane_v + cvt->plane_v_size;
    *out_converter = cvt;
    return L4C_OK;
}

l4c_status_t l4c_color_convert_bgra_to_i420(
    l4c_color_converter_t *converter,
    const uint8_t *bgra, int32_t bgra_stride,
    uint64_t pts_ms, l4c_raw_frame_t *out_raw)
{
    uint32_t w, h, y, x;
    int32_t sy, su, sv;
    if (!converter || !bgra || !out_raw) return L4C_ERR_INVALID_ARG;
    if (bgra_stride <= 0) return L4C_ERR_INVALID_ARG;
    w = converter->width;
    h = converter->height;
    sy = converter->stride_y;
    su = converter->stride_u;
    sv = converter->stride_v;
    /* Y plane: BT.601 limited range */
    for (y = 0; y < h; ++y) {
        const uint8_t *row = bgra + (size_t)y * (size_t)bgra_stride;
        uint8_t *y_row = converter->plane_y + (size_t)y * (size_t)sy;
        for (x = 0; x < w; ++x) {
            int b = row[x * 4 + 0];
            int g = row[x * 4 + 1];
            int r = row[x * 4 + 2];
            int yy = ((66 * r + 129 * g + 25 * b + 128) >> 8) + 16;
            if (yy < 16) yy = 16;
            if (yy > 235) yy = 235;
            y_row[x] = (uint8_t)yy;
        }
    }
    /* U/V planes: 2x2 box average chroma subsampling, BT.601 */
    {
        uint32_t half_h = (h + 1u) / 2u;
        uint32_t half_w = (w + 1u) / 2u;
        for (y = 0; y < half_h; ++y) {
            uint32_t sy0 = y * 2;
            uint32_t sy1 = sy0 + 1;
            if (sy1 >= h) sy1 = h - 1;
            const uint8_t *row0 = bgra + (size_t)sy0 * (size_t)bgra_stride;
            const uint8_t *row1 = bgra + (size_t)sy1 * (size_t)bgra_stride;
            uint8_t *u_row = converter->plane_u + (size_t)y * (size_t)su;
            uint8_t *v_row = converter->plane_v + (size_t)y * (size_t)sv;
            for (x = 0; x < half_w; ++x) {
                uint32_t sx0 = x * 2;
                uint32_t sx1 = sx0 + 1;
                int b_sum, g_sum, r_sum, b, g, r, uu, vv;
                if (sx1 >= w) sx1 = w - 1;
                /* Average 2x2 block */
                b = row0[sx0*4+0] + row0[sx1*4+0] + row1[sx0*4+0] + row1[sx1*4+0];
                g = row0[sx0*4+1] + row0[sx1*4+1] + row1[sx0*4+1] + row1[sx1*4+1];
                r = row0[sx0*4+2] + row0[sx1*4+2] + row1[sx0*4+2] + row1[sx1*4+2];
                b_sum = (b + 2) >> 2;
                g_sum = (g + 2) >> 2;
                r_sum = (r + 2) >> 2;
                uu = ((-38 * r_sum - 74 * g_sum + 112 * b_sum + 128) >> 8) + 128;
                vv = ((112 * r_sum - 94 * g_sum - 18 * b_sum + 128) >> 8) + 128;
                if (uu < 16) uu = 16; if (uu > 240) uu = 240;
                if (vv < 16) vv = 16; if (vv > 240) vv = 240;
                u_row[x] = (uint8_t)uu;
                v_row[x] = (uint8_t)vv;
            }
        }
    }
    /* Fill output raw frame */
    memset(out_raw, 0, sizeof(*out_raw));
    out_raw->planes[0] = converter->plane_y;
    out_raw->planes[1] = converter->plane_u;
    out_raw->planes[2] = converter->plane_v;
    out_raw->strides[0] = (uint32_t)sy;
    out_raw->strides[1] = (uint32_t)su;
    out_raw->strides[2] = (uint32_t)sv;
    out_raw->width = w;
    out_raw->height = h;
    out_raw->format = L4C_PIX_FMT_I420;
    out_raw->pts_ms = pts_ms;
    out_raw->plane_sizes[0] = converter->plane_y_size;
    out_raw->plane_sizes[1] = converter->plane_u_size;
    out_raw->plane_sizes[2] = converter->plane_v_size;
    return L4C_OK;
}

l4c_status_t l4c_color_convert_bgra_to_nv12(
    l4c_color_converter_t *converter,
    const uint8_t *src_bgra,
    int32_t src_stride,
    uint8_t *dst_y,
    int32_t dst_stride_y,
    uint8_t *dst_uv,
    int32_t dst_stride_uv,
    uint32_t width,
    uint32_t height
) {
    uint32_t w, h, y, x;
    uint32_t half_h, half_w;

    if (!src_bgra || src_stride <= 0) return L4C_ERR_INVALID_ARG;

    w = width;
    h = height;
    if (w == 0 || h == 0) {
        if (!converter) return L4C_ERR_INVALID_ARG;
        w = converter->width;
        h = converter->height;
    }
    if ((uint64_t)w * h > L4C_MAX_PIXELS_AREA) return L4C_ERR_OVERFLOW;

    if (!dst_y) {
        if (!converter) return L4C_ERR_INVALID_ARG;
        dst_y = converter->plane_y;
        dst_stride_y = converter->stride_y;
    }
    if (!dst_uv) {
        if (!converter) return L4C_ERR_INVALID_ARG;
        dst_uv = converter->plane_uv;
        dst_stride_uv = converter->stride_uv;
    }
    if (dst_stride_y < (int32_t)w || dst_stride_uv < (int32_t)w) return L4C_ERR_INVALID_ARG;

    /* Y plane: ITU-R BT.601 limited range — scalar */
    for (y = 0; y < h; ++y) {
        const uint8_t *row = src_bgra + (size_t)y * (size_t)src_stride;
        uint8_t *y_row = dst_y + (size_t)y * (size_t)dst_stride_y;
        for (x = 0; x < w; ++x) {
            int b = row[x * 4 + 0];
            int g = row[x * 4 + 1];
            int r = row[x * 4 + 2];
            int yy = ((66 * r + 129 * g + 25 * b + 128) >> 8) + 16;
            if (yy < 16) yy = 16;
            if (yy > 235) yy = 235;
            y_row[x] = (uint8_t)yy;
        }
    }

    /* UV interleaved plane: 2x2 box average chroma subsampling, BT.601 limited range */
    half_h = (h + 1u) / 2u;
    half_w = (w + 1u) / 2u;
    for (y = 0; y < half_h; ++y) {
        uint32_t sy0 = y * 2;
        uint32_t sy1 = sy0 + 1;
        if (sy1 >= h) sy1 = h - 1;
        const uint8_t *row0 = src_bgra + (size_t)sy0 * (size_t)src_stride;
        const uint8_t *row1 = src_bgra + (size_t)sy1 * (size_t)src_stride;
        uint8_t *uv_row = dst_uv + (size_t)y * (size_t)dst_stride_uv;
        for (x = 0; x < half_w; ++x) {
            uint32_t sx0 = x * 2;
            uint32_t sx1 = sx0 + 1;
            int b_sum, g_sum, r_sum, b, g, r, uu, vv;
            if (sx1 >= w) sx1 = w - 1;
            /* Average 2x2 block */
            b = row0[sx0 * 4 + 0] + row0[sx1 * 4 + 0] + row1[sx0 * 4 + 0] + row1[sx1 * 4 + 0];
            g = row0[sx0 * 4 + 1] + row0[sx1 * 4 + 1] + row1[sx0 * 4 + 1] + row1[sx1 * 4 + 1];
            r = row0[sx0 * 4 + 2] + row0[sx1 * 4 + 2] + row1[sx0 * 4 + 2] + row1[sx1 * 4 + 2];
            b_sum = (b + 2) >> 2;
            g_sum = (g + 2) >> 2;
            r_sum = (r + 2) >> 2;
            uu = ((-38 * r_sum - 74 * g_sum + 112 * b_sum + 128) >> 8) + 128;
            vv = ((112 * r_sum - 94 * g_sum - 18 * b_sum + 128) >> 8) + 128;
            if (uu < 16) uu = 16; if (uu > 240) uu = 240;
            if (vv < 16) vv = 16; if (vv > 240) vv = 240;
            uv_row[x * 2 + 0] = (uint8_t)uu;
            uv_row[x * 2 + 1] = (uint8_t)vv;
        }
    }

    return L4C_OK;
}

l4c_status_t l4c_color_convert_bgra_to_nv12_frame(
    l4c_color_converter_t *converter,
    const uint8_t *bgra, int32_t bgra_stride,
    uint64_t pts_ms, l4c_raw_frame_t *out_raw)
{
    l4c_status_t status;
    if (!converter || !bgra || !out_raw) return L4C_ERR_INVALID_ARG;
    status = l4c_color_convert_bgra_to_nv12(
        converter, bgra, bgra_stride,
        converter->plane_y, converter->stride_y,
        converter->plane_uv, converter->stride_uv,
        converter->width, converter->height
    );
    if (status != L4C_OK) return status;

    memset(out_raw, 0, sizeof(*out_raw));
    out_raw->planes[0] = converter->plane_y;
    out_raw->planes[1] = converter->plane_uv;
    out_raw->planes[2] = NULL;
    out_raw->strides[0] = (uint32_t)converter->stride_y;
    out_raw->strides[1] = (uint32_t)converter->stride_uv;
    out_raw->strides[2] = 0;
    out_raw->width = converter->width;
    out_raw->height = converter->height;
    out_raw->format = L4C_PIX_FMT_NV12;
    out_raw->pts_ms = pts_ms;
    out_raw->plane_sizes[0] = converter->plane_y_size;
    out_raw->plane_sizes[1] = converter->plane_uv_size;
    out_raw->plane_sizes[2] = 0;
    return L4C_OK;
}

void l4c_color_converter_destroy(l4c_color_converter_t *converter) {
    if (!converter) return;
    free(converter->buffer);
    free(converter);
}
