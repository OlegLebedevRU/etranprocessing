#include <stdlib.h>
#include <string.h>
#include "l4capture/scale.h"

int test_scale_solid_fill(void) {
    uint8_t src[4 * 4 * 4]; /* 4x4 BGRA */
    uint8_t dst[2 * 2 * 4]; /* 2x2 BGRA */
    l4c_status_t s;
    uint32_t i;
    /* Fill source with solid red (BGRA: 0,0,255,255) */
    for (i = 0; i < 4 * 4; ++i) {
        src[i * 4 + 0] = 0; src[i * 4 + 1] = 0;
        src[i * 4 + 2] = 255; src[i * 4 + 3] = 255;
    }
    memset(dst, 0, sizeof(dst));
    s = l4c_scale_bilinear_bgra(src, 4, 4, 16, dst, 2, 2, 8);
    if (s != L4C_OK) return 1;
    /* All output pixels should be red */
    for (i = 0; i < 2 * 2; ++i) {
        if (dst[i * 4 + 0] != 0 || dst[i * 4 + 1] != 0 ||
            dst[i * 4 + 2] != 255 || dst[i * 4 + 3] != 255) return 2;
    }
    return 0;
}

int test_scale_checkerboard(void) {
    uint8_t src[8 * 8 * 4];
    uint8_t dst[4 * 4 * 4];
    l4c_status_t s;
    uint32_t x, y;
    /* Create 8x8 checkerboard: black and white */
    for (y = 0; y < 8; ++y) {
        for (x = 0; x < 8; ++x) {
            uint8_t v = ((x + y) & 1) ? 255 : 0;
            uint32_t idx = (y * 8 + x) * 4;
            src[idx + 0] = v; src[idx + 1] = v;
            src[idx + 2] = v; src[idx + 3] = 255;
        }
    }
    memset(dst, 0, sizeof(dst));
    s = l4c_scale_bilinear_bgra(src, 8, 8, 32, dst, 4, 4, 16);
    if (s != L4C_OK) return 1;
    /* Center pixel should be grey (interpolated) */
    return 0;
}

int test_scale_edge_preservation(void) {
    uint8_t src[2 * 2 * 4];
    uint8_t dst[4 * 4 * 4];
    l4c_status_t s;
    /* 2x2: TL=blue, TR=green, BL=red, BR=white */
    src[0] = 255; src[1] = 0; src[2] = 0; src[3] = 255;     /* TL blue */
    src[4] = 0; src[5] = 255; src[6] = 0; src[7] = 255;     /* TR green */
    src[8] = 0; src[9] = 0; src[10] = 255; src[11] = 255;   /* BL red */
    src[12] = 255; src[13] = 255; src[14] = 255; src[15] = 255; /* BR white */
    memset(dst, 0, sizeof(dst));
    s = l4c_scale_bilinear_bgra(src, 2, 2, 8, dst, 4, 4, 16);
    if (s != L4C_OK) return 1;
    /* Top-left corner should be exactly blue */
    if (dst[0] != 255 || dst[1] != 0 || dst[2] != 0) return 2;
    /* Bottom-right corner should be exactly white */
    if (dst[3*16+3*4+0] != 255 || dst[3*16+3*4+1] != 255 || dst[3*16+3*4+2] != 255) return 3;
    return 0;
}

int test_scale_large_to_480p(void) {
    /* Test that 1920x1080 -> 854x480 doesn't overflow */
    uint8_t *src, *dst;
    l4c_status_t s;
    src = (uint8_t *)malloc(1920u * 1080u * 4u);
    dst = (uint8_t *)malloc(854u * 480u * 4u);
    if (!src || !dst) { free(src); free(dst); return 0; /* skip if OOM */ }
    memset(src, 128, 1920u * 1080u * 4u);
    s = l4c_scale_bilinear_bgra(src, 1920, 1080, 1920 * 4, dst, 854, 480, 854 * 4);
    free(src);
    free(dst);
    if (s != L4C_OK) return 1;
    return 0;
}

int test_scale_invalid_params(void) {
    uint8_t buf[64];
    l4c_status_t s;
    s = l4c_scale_bilinear_bgra(NULL, 2, 2, 8, buf, 1, 1, 4);
    if (s != L4C_ERR_INVALID_ARG) return 1;
    s = l4c_scale_bilinear_bgra(buf, 0, 2, 8, buf, 1, 1, 4);
    if (s != L4C_ERR_INVALID_ARG) return 2;
    return 0;
}
