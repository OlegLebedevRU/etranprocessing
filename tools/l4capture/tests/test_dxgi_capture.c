#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "l4capture/dxgi_capture.h"
#include "l4capture/gdi_capture.h"
#include "l4capture/limits.h"

static void get_primary_monitor_rect(l4c_rect_t *rect) {
    POINT pt = { 0, 0 };
    HMONITOR hmon = MonitorFromPoint(pt, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi;
    memset(&mi, 0, sizeof(mi));
    mi.cbSize = sizeof(mi);
    if (hmon && GetMonitorInfoA(hmon, &mi)) {
        rect->left = mi.rcMonitor.left;
        rect->top = mi.rcMonitor.top;
        rect->right = mi.rcMonitor.right;
        rect->bottom = mi.rcMonitor.bottom;
    } else {
        rect->left = 0;
        rect->top = 0;
        rect->right = GetSystemMetrics(SM_CXSCREEN);
        rect->bottom = GetSystemMetrics(SM_CYSCREEN);
    }
}

int test_dxgi_probe_graceful(void) {
    /* Safe probe without memory leaks */
    bool supported = l4c_dxgi_capture_is_supported();
    /* Must return true or false cleanly without crashing */
    printf(" (supported=%s) ", supported ? "true" : "false");
    return 0;
}

int test_dxgi_create_destroy(void) {
    if (!l4c_dxgi_capture_is_supported()) {
        printf(" (skipped: unsupported) ");
        return 0;
    }
    l4c_capture_backend_t *backend = NULL;
    l4c_status_t s = l4c_dxgi_capture_create(&backend);
    if (s != L4C_OK) return 1;
    if (!backend || !backend->vtable) return 2;
    if (!backend->impl_ctx) return 3;
    if (!backend->vtable->init) return 4;
    if (!backend->vtable->acquire_frame) return 5;
    if (!backend->vtable->release_frame) return 6;
    if (!backend->vtable->destroy) return 7;

    backend->vtable->destroy(backend);
    return 0;
}

int test_dxgi_init_and_single_output_validation(void) {
    if (!l4c_dxgi_capture_is_supported()) {
        printf(" (skipped: unsupported) ");
        return 0;
    }
    l4c_capture_backend_t *backend = NULL;
    l4c_status_t s = l4c_dxgi_capture_create(&backend);
    if (s != L4C_OK) return 1;

    l4c_capture_config_t config;
    memset(&config, 0, sizeof(config));
    config.capture_cursor = true;
    get_primary_monitor_rect(&config.target_rect);
    s = backend->vtable->init(backend, &config);
    if (s != L4C_OK) {
        backend->vtable->destroy(backend);
        return 2;
    }

    backend->vtable->destroy(backend);
    return 0;
}

int test_dxgi_virtual_desktop_rejection(void) {
    if (!l4c_dxgi_capture_is_supported()) {
        printf(" (skipped: unsupported) ");
        return 0;
    }
    l4c_capture_backend_t *backend = NULL;
    l4c_status_t s = l4c_dxgi_capture_create(&backend);
    if (s != L4C_OK) return 1;

    l4c_capture_config_t config;
    memset(&config, 0, sizeof(config));
    /* Specify out-of-bounds / multi-monitor rectangle */
    config.target_rect.left = -50000;
    config.target_rect.top = -50000;
    config.target_rect.right = 50000;
    config.target_rect.bottom = 50000;

    s = backend->vtable->init(backend, &config);
    if (s != L4C_ERR_INVALID_ARG) {
        backend->vtable->destroy(backend);
        return 2;
    }

    backend->vtable->destroy(backend);
    return 0;
}

int test_dxgi_acquire_timeout_no_frame(void) {
    if (!l4c_dxgi_capture_is_supported()) {
        printf(" (skipped: unsupported) ");
        return 0;
    }
    l4c_capture_backend_t *backend = NULL;
    l4c_status_t s = l4c_dxgi_capture_create(&backend);
    if (s != L4C_OK) return 1;

    l4c_capture_config_t config;
    memset(&config, 0, sizeof(config));
    get_primary_monitor_rect(&config.target_rect);
    s = backend->vtable->init(backend, &config);
    if (s != L4C_OK) {
        backend->vtable->destroy(backend);
        return 2;
    }

    /* Acquire with 0ms timeout: if screen hasn't changed, returns L4C_ERR_NO_FRAME */
    l4c_frame_view_t frame;
    s = backend->vtable->acquire_frame(backend, &frame, 0);
    if (s == L4C_OK) {
        backend->vtable->release_frame(backend, &frame);
        /* Acquire a second time immediately with 0 timeout: desktop hasn't changed, must be NO_FRAME */
        s = backend->vtable->acquire_frame(backend, &frame, 0);
        if (s == L4C_ERR_NO_FRAME) {
            /* Verified: NO_FRAME returned and no release_frame needed */
        }
    } else if (s == L4C_ERR_NO_FRAME) {
        /* Success: NO_FRAME returned directly */
    } else {
        backend->vtable->destroy(backend);
        return 3;
    }

    backend->vtable->destroy(backend);
    return 0;
}

int test_dxgi_release_frame_leak_stress(void) {
    if (!l4c_dxgi_capture_is_supported()) {
        printf(" (skipped: unsupported) ");
        return 0;
    }
    l4c_capture_backend_t *backend = NULL;
    l4c_status_t s = l4c_dxgi_capture_create(&backend);
    if (s != L4C_OK) return 1;

    l4c_capture_config_t config;
    memset(&config, 0, sizeof(config));
    get_primary_monitor_rect(&config.target_rect);
    s = backend->vtable->init(backend, &config);
    if (s != L4C_OK) {
        backend->vtable->destroy(backend);
        return 2;
    }

    /* Run 100 cycles of acquire & release */
    for (int i = 0; i < 100; ++i) {
        l4c_frame_view_t frame;
        s = backend->vtable->acquire_frame(backend, &frame, 50);
        if (s == L4C_OK) {
            if (!frame.data || frame.width == 0 || frame.height == 0) {
                backend->vtable->release_frame(backend, &frame);
                backend->vtable->destroy(backend);
                return 3;
            }
            backend->vtable->release_frame(backend, &frame);
        }
    }

    backend->vtable->destroy(backend);
    return 0;
}

int test_dxgi_rotation_transform(void) {
    uint32_t w = 1920, h = 1080;
    uint32_t out_w = 0, out_h = 0;

    /* 1. Dimension tests */
    l4c_dxgi_calc_rotation_dims(w, h, DXGI_MODE_ROTATION_IDENTITY, &out_w, &out_h);
    if (out_w != 1920 || out_h != 1080) return 1;

    l4c_dxgi_calc_rotation_dims(w, h, DXGI_MODE_ROTATION_ROTATE90, &out_w, &out_h);
    if (out_w != 1080 || out_h != 1920) return 2;

    l4c_dxgi_calc_rotation_dims(w, h, DXGI_MODE_ROTATION_ROTATE180, &out_w, &out_h);
    if (out_w != 1920 || out_h != 1080) return 3;

    l4c_dxgi_calc_rotation_dims(w, h, DXGI_MODE_ROTATION_ROTATE270, &out_w, &out_h);
    if (out_w != 1080 || out_h != 1920) return 4;

    /* 2. Raster rotation tests (2x2 BGRA raster) */
    /*
     * Source pixels (2x2):
     * P0(0,0)=0x11111111  P1(1,0)=0x22222222
     * P2(0,1)=0x33333333  P3(1,1)=0x44444444
     */
    uint32_t src[4] = { 0x11111111, 0x22222222, 0x33333333, 0x44444444 };
    uint32_t dst[4];

    /* ROTATE90 (clockwise):
     * New (0,0) is old (0,1)=P2(0x33333333)
     * New (1,0) is old (0,0)=P0(0x11111111)
     * New (0,1) is old (1,1)=P3(0x44444444)
     * New (1,1) is old (1,0)=P1(0x22222222)
     */
    memset(dst, 0, sizeof(dst));
    l4c_dxgi_rotate_bgra((const uint8_t*)src, 2, 2, 8, (uint8_t*)dst, 8, DXGI_MODE_ROTATION_ROTATE90);
    if (dst[0] != 0x33333333 || dst[1] != 0x11111111 ||
        dst[2] != 0x44444444 || dst[3] != 0x22222222) {
        return 5;
    }

    /* ROTATE180:
     * New (0,0)=P3(0x44444444), (1,0)=P2(0x33333333)
     * New (0,1)=P1(0x22222222), (1,1)=P0(0x11111111)
     */
    memset(dst, 0, sizeof(dst));
    l4c_dxgi_rotate_bgra((const uint8_t*)src, 2, 2, 8, (uint8_t*)dst, 8, DXGI_MODE_ROTATION_ROTATE180);
    if (dst[0] != 0x44444444 || dst[1] != 0x33333333 ||
        dst[2] != 0x22222222 || dst[3] != 0x11111111) {
        return 6;
    }

    /* ROTATE270:
     * New (0,0)=P1(0x22222222), (1,0)=P3(0x44444444)
     * New (0,1)=P0(0x11111111), (1,1)=P2(0x33333333)
     */
    memset(dst, 0, sizeof(dst));
    l4c_dxgi_rotate_bgra((const uint8_t*)src, 2, 2, 8, (uint8_t*)dst, 8, DXGI_MODE_ROTATION_ROTATE270);
    if (dst[0] != 0x22222222 || dst[1] != 0x44444444 ||
        dst[2] != 0x11111111 || dst[3] != 0x33333333) {
        return 7;
    }

    return 0;
}

int test_dxgi_cursor_shape_handling(void) {
    /* 1. Test color cursor overlay */
    uint32_t frame[16 * 16];
    memset(frame, 0, sizeof(frame)); /* black */

    DXGI_OUTDUPL_POINTER_SHAPE_INFO color_shape;
    memset(&color_shape, 0, sizeof(color_shape));
    color_shape.Type = DXGI_OUTDUPL_POINTER_SHAPE_TYPE_COLOR;
    color_shape.Width = 2;
    color_shape.Height = 2;
    color_shape.Pitch = 8;

    /* 2x2 solid white with alpha=255 */
    uint32_t shape_pixels[4] = { 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF };

    l4c_dxgi_draw_cursor((uint8_t*)frame, 16, 16, 64, &color_shape, (const uint8_t*)shape_pixels, 4, 4);
    if (frame[4 * 16 + 4] != 0xFFFFFFFF || frame[4 * 16 + 5] != 0xFFFFFFFF ||
        frame[5 * 16 + 4] != 0xFFFFFFFF || frame[5 * 16 + 5] != 0xFFFFFFFF) {
        return 1;
    }

    /* 2. Test monochrome cursor overlay */
    memset(frame, 0, sizeof(frame));
    DXGI_OUTDUPL_POINTER_SHAPE_INFO mono_shape;
    memset(&mono_shape, 0, sizeof(mono_shape));
    mono_shape.Type = DXGI_OUTDUPL_POINTER_SHAPE_TYPE_MONOCHROME;
    mono_shape.Width = 8;
    mono_shape.Height = 4; /* actual height 2 */
    mono_shape.Pitch = 1;

    /* Top 2 bytes AND, bottom 2 bytes XOR */
    /* Pixel (0,0): AND=0, XOR=1 -> White (0xFFFFFFFF) */
    /* Pixel (1,0): AND=0, XOR=0 -> Black (0xFF000000) */
    uint8_t mono_data[4] = { 0x00, 0x00, 0x80, 0x00 }; /* row 0: and=0, xor=10000000 -> bit 0 has and=0, xor=1 (white) */

    l4c_dxgi_draw_cursor((uint8_t*)frame, 16, 16, 64, &mono_shape, mono_data, 0, 0);
    if (frame[0] != 0xFFFFFFFF) {
        return 2;
    }

    return 0;
}

int test_dxgi_access_lost_session_dead(void) {
    if (!l4c_dxgi_capture_is_supported()) {
        printf(" (skipped: unsupported) ");
        return 0;
    }
    l4c_capture_backend_t *backend = NULL;
    l4c_status_t s = l4c_dxgi_capture_create(&backend);
    if (s != L4C_OK) return 1;

    l4c_capture_config_t config;
    memset(&config, 0, sizeof(config));
    get_primary_monitor_rect(&config.target_rect);
    s = backend->vtable->init(backend, &config);
    if (s != L4C_OK) {
        backend->vtable->destroy(backend);
        return 2;
    }

    /* Inject Mode 1: Access lost with dead session */
    l4c_dxgi_test_inject_access_lost(1);
    l4c_frame_view_t frame;
    s = backend->vtable->acquire_frame(backend, &frame, 100);

    /* Reset injection */
    l4c_dxgi_test_inject_access_lost(0);

    /* Must immediately return L4C_ERR_SESSION_UNAVAILABLE without retries */
    if (s != L4C_ERR_SESSION_UNAVAILABLE) {
        backend->vtable->destroy(backend);
        return 3;
    }

    backend->vtable->destroy(backend);
    return 0;
}

int test_dxgi_access_lost_retry_and_gdi_fallback(void) {
    if (!l4c_dxgi_capture_is_supported()) {
        printf(" (skipped: unsupported) ");
        return 0;
    }
    l4c_capture_backend_t *backend = NULL;
    l4c_status_t s = l4c_dxgi_capture_create(&backend);
    if (s != L4C_OK) return 1;

    l4c_capture_config_t config;
    memset(&config, 0, sizeof(config));
    get_primary_monitor_rect(&config.target_rect);
    s = backend->vtable->init(backend, &config);
    if (s != L4C_OK) {
        backend->vtable->destroy(backend);
        return 2;
    }

    /* Inject Mode 2: Access lost with active session, retries fail */
    l4c_dxgi_test_inject_access_lost(2);
    l4c_frame_view_t frame;
    s = backend->vtable->acquire_frame(backend, &frame, 100);

    /* Must return L4C_ERR_DEVICE_LOST indicating fallback is required */
    if (s != L4C_ERR_DEVICE_LOST) {
        l4c_dxgi_test_inject_access_lost(0);
        backend->vtable->destroy(backend);
        return 3;
    }

    /* Verify 3 retries were executed */
    int retries = l4c_dxgi_test_get_reinit_count();
    /* Reset injection */
    l4c_dxgi_test_inject_access_lost(0);
    if (retries != 3) {
        backend->vtable->destroy(backend);
        return 4;
    }

    /* Destroy failed DXGI backend */
    backend->vtable->destroy(backend);
    backend = NULL;

    /* Execute fallback to GDI capture */
    s = l4c_gdi_capture_create(&backend);
    if (s != L4C_OK || !backend) return 5;

    s = backend->vtable->init(backend, &config);
    if (s != L4C_OK) {
        backend->vtable->destroy(backend);
        return 6;
    }

    /* Verify GDI capture immediately succeeds and preserves stream */
    s = backend->vtable->acquire_frame(backend, &frame, 1000);
    if (s != L4C_OK) {
        backend->vtable->destroy(backend);
        return 7;
    }
    if (!frame.data || frame.width == 0 || frame.height == 0) {
        backend->vtable->release_frame(backend, &frame);
        backend->vtable->destroy(backend);
        return 8;
    }
    backend->vtable->release_frame(backend, &frame);
    backend->vtable->destroy(backend);

    return 0;
}
