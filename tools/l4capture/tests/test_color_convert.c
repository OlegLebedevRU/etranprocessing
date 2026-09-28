#include <stdlib.h>
#include <string.h>
#include "l4capture/color_convert.h"

/* Helper: fill a BGRA pixel */
static void set_pixel(uint8_t *buf, uint8_t b, uint8_t g, uint8_t r, uint8_t a) {
    buf[0] = b; buf[1] = g; buf[2] = r; buf[3] = a;
}

int test_color_white(void) {
    l4c_color_converter_t *cvt = NULL;
    l4c_raw_frame_t raw;
    l4c_status_t s;
    uint8_t bgra[2 * 2 * 4];
    /* White: R=255, G=255, B=255 */
    memset(bgra, 0, sizeof(bgra));
    set_pixel(bgra + 0, 255, 255, 255, 255);
    set_pixel(bgra + 4, 255, 255, 255, 255);
    set_pixel(bgra + 8, 255, 255, 255, 255);
    set_pixel(bgra + 12, 255, 255, 255, 255);
    s = l4c_color_converter_create(2, 2, &cvt);
    if (s != L4C_OK) return 1;
    s = l4c_color_convert_bgra_to_i420(cvt, bgra, 8, 1000, &raw);
    if (s != L4C_OK) { l4c_color_converter_destroy(cvt); return 2; }
    /* BT.601 white: Y=235, U=128, V=128 */
    if (raw.planes[0][0] != 235) { l4c_color_converter_destroy(cvt); return 3; }
    if (raw.planes[1][0] < 127 || raw.planes[1][0] > 129) { l4c_color_converter_destroy(cvt); return 4; }
    if (raw.planes[2][0] < 127 || raw.planes[2][0] > 129) { l4c_color_converter_destroy(cvt); return 5; }
    l4c_color_converter_destroy(cvt);
    return 0;
}

int test_color_black(void) {
    l4c_color_converter_t *cvt = NULL;
    l4c_raw_frame_t raw;
    l4c_status_t s;
    uint8_t bgra[2 * 2 * 4];
    /* Black: R=0, G=0, B=0 */
    memset(bgra, 0, sizeof(bgra));
    s = l4c_color_converter_create(2, 2, &cvt);
    if (s != L4C_OK) return 1;
    s = l4c_color_convert_bgra_to_i420(cvt, bgra, 8, 1000, &raw);
    if (s != L4C_OK) { l4c_color_converter_destroy(cvt); return 2; }
    /* BT.601 black: Y=16, U=128, V=128 */
    if (raw.planes[0][0] != 16) { l4c_color_converter_destroy(cvt); return 3; }
    if (raw.planes[1][0] < 127 || raw.planes[1][0] > 129) { l4c_color_converter_destroy(cvt); return 4; }
    if (raw.planes[2][0] < 127 || raw.planes[2][0] > 129) { l4c_color_converter_destroy(cvt); return 5; }
    l4c_color_converter_destroy(cvt);
    return 0;
}

int test_color_range_clamp(void) {
    l4c_color_converter_t *cvt = NULL;
    l4c_raw_frame_t raw;
    l4c_status_t s;
    uint32_t x, y;
    uint8_t bgra[16 * 16 * 4];
    /* Fill with various colors and check no Y value out of [16,235] or U/V out of [16,240] */
    for (y = 0; y < 16; ++y) {
        for (x = 0; x < 16; ++x) {
            uint32_t idx = (y * 16 + x) * 4;
            bgra[idx + 0] = (uint8_t)(x * 17);     /* B */
            bgra[idx + 1] = (uint8_t)(y * 17);     /* G */
            bgra[idx + 2] = (uint8_t)((x + y) * 8); /* R */
            bgra[idx + 3] = 255;
        }
    }
    s = l4c_color_converter_create(16, 16, &cvt);
    if (s != L4C_OK) return 1;
    s = l4c_color_convert_bgra_to_i420(cvt, bgra, 16 * 4, 1000, &raw);
    if (s != L4C_OK) { l4c_color_converter_destroy(cvt); return 2; }
    /* Check Y range */
    for (y = 0; y < 16; ++y) {
        for (x = 0; x < 16; ++x) {
            uint8_t yy = raw.planes[0][y * raw.strides[0] + x];
            if (yy < 16 || yy > 235) { l4c_color_converter_destroy(cvt); return 3; }
        }
    }
    /* Check U/V range */
    for (y = 0; y < 8; ++y) {
        for (x = 0; x < 8; ++x) {
            uint8_t uu = raw.planes[1][y * raw.strides[1] + x];
            uint8_t vv = raw.planes[2][y * raw.strides[2] + x];
            if (uu < 16 || uu > 240) { l4c_color_converter_destroy(cvt); return 4; }
            if (vv < 16 || vv > 240) { l4c_color_converter_destroy(cvt); return 5; }
        }
    }
    l4c_color_converter_destroy(cvt);
    return 0;
}

int test_color_stride_alignment(void) {
    l4c_color_converter_t *cvt = NULL;
    l4c_raw_frame_t raw;
    l4c_status_t s;
    uint8_t *bgra = (uint8_t *)malloc(854u * 480u * 4u);
    if (!bgra) return 0;
    s = l4c_color_converter_create(854, 480, &cvt);
    if (s != L4C_OK) { free(bgra); return 1; }
    memset(bgra, 128, 854u * 480u * 4u);
    s = l4c_color_convert_bgra_to_i420(cvt, bgra, 854 * 4, 1000, &raw);
    free(bgra);
    if (s != L4C_OK) { l4c_color_converter_destroy(cvt); return 2; }
    /* Y stride should be 864 (854 aligned to 16) */
    if (raw.strides[0] != 864) { l4c_color_converter_destroy(cvt); return 3; }
    /* U/V stride should be 432 (427 aligned to 16) */
    if (raw.strides[1] != 432) { l4c_color_converter_destroy(cvt); return 4; }
    if (raw.strides[2] != 432) { l4c_color_converter_destroy(cvt); return 5; }
    l4c_color_converter_destroy(cvt);
    return 0;
}

int test_color_create_destroy(void) {
    l4c_color_converter_t *cvt = NULL;
    l4c_status_t s;
    s = l4c_color_converter_create(640, 480, &cvt);
    if (s != L4C_OK) return 1;
    if (!cvt) return 2;
    l4c_color_converter_destroy(cvt);
    /* NULL destroy is safe */
    l4c_color_converter_destroy(NULL);
    return 0;
}
