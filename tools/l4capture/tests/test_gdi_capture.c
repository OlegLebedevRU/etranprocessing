#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "l4capture/gdi_capture.h"
#include "l4capture/limits.h"

int test_gdi_create_destroy(void) {
    l4c_capture_backend_t *backend = NULL;
    l4c_status_t s;
    s = l4c_gdi_capture_create(&backend);
    if (s != L4C_OK) return 1;
    if (!backend || !backend->vtable) return 2;
    if (!backend->impl_ctx) return 3;
    backend->vtable->destroy(backend);
    return 0;
}

int test_gdi_init_capture_release(void) {
    l4c_capture_backend_t *backend = NULL;
    l4c_capture_config_t config;
    l4c_frame_view_t frame;
    l4c_status_t s;
    s = l4c_gdi_capture_create(&backend);
    if (s != L4C_OK) return 1;
    memset(&config, 0, sizeof(config));
    config.capture_cursor = false;
    /* Use primary monitor if virtual screen exceeds 4K max pixels limit */
    if ((uint64_t)GetSystemMetrics(SM_CXVIRTUALSCREEN) * GetSystemMetrics(SM_CYVIRTUALSCREEN) > L4C_MAX_PIXELS_AREA) {
        config.target_rect.left = 0;
        config.target_rect.top = 0;
        config.target_rect.right = GetSystemMetrics(SM_CXSCREEN);
        config.target_rect.bottom = GetSystemMetrics(SM_CYSCREEN);
    }
    s = backend->vtable->init(backend, &config);
    if (s != L4C_OK) {
        backend->vtable->destroy(backend); return 2;
    }
    /* Capture one frame */
    s = backend->vtable->acquire_frame(backend, &frame, 1000);
    if (s != L4C_OK && s != L4C_ERR_SESSION_UNAVAILABLE) {
        backend->vtable->destroy(backend); return 3;
    }
    if (s == L4C_OK) {
        if (!frame.data || !frame.width || !frame.height) {
            backend->vtable->destroy(backend); return 4;
        }
        if (frame.stride <= 0) { backend->vtable->destroy(backend); return 5; }
        backend->vtable->release_frame(backend, &frame);
    }
    backend->vtable->destroy(backend);
    return 0;
}

int test_gdi_reuse_buffer(void) {
    l4c_capture_backend_t *backend = NULL;
    l4c_capture_config_t config;
    l4c_frame_view_t frame1, frame2;
    l4c_status_t s;
    s = l4c_gdi_capture_create(&backend);
    if (s != L4C_OK) return 1;
    memset(&config, 0, sizeof(config));
    config.capture_cursor = false;
    if ((uint64_t)GetSystemMetrics(SM_CXVIRTUALSCREEN) * GetSystemMetrics(SM_CYVIRTUALSCREEN) > L4C_MAX_PIXELS_AREA) {
        config.target_rect.left = 0;
        config.target_rect.top = 0;
        config.target_rect.right = GetSystemMetrics(SM_CXSCREEN);
        config.target_rect.bottom = GetSystemMetrics(SM_CYSCREEN);
    }
    s = backend->vtable->init(backend, &config);
    if (s != L4C_OK) { backend->vtable->destroy(backend); return 2; }
    s = backend->vtable->acquire_frame(backend, &frame1, 1000);
    if (s != L4C_OK) { backend->vtable->destroy(backend); return 3; }
    backend->vtable->release_frame(backend, &frame1);
    s = backend->vtable->acquire_frame(backend, &frame2, 1000);
    if (s != L4C_OK) { backend->vtable->destroy(backend); return 4; }
    /* Buffer pointer should be reused (zero-allocation) */
    if (frame1.data != frame2.data) { backend->vtable->destroy(backend); return 5; }
    backend->vtable->release_frame(backend, &frame2);
    backend->vtable->destroy(backend);
    return 0;
}

int test_gdi_overflow_reject(void) {
    l4c_capture_backend_t *backend = NULL;
    l4c_capture_config_t config;
    l4c_status_t s;
    s = l4c_gdi_capture_create(&backend);
    if (s != L4C_OK) return 1;
    memset(&config, 0, sizeof(config));
    config.capture_cursor = false;
    /* Request huge area: 4000x3000 = 12,000,000 > 8,294,400 */
    config.target_rect.left = 0;
    config.target_rect.top = 0;
    config.target_rect.right = 4000;
    config.target_rect.bottom = 3000;
    s = backend->vtable->init(backend, &config);
    if (s != L4C_ERR_OVERFLOW) { backend->vtable->destroy(backend); return 2; }
    backend->vtable->destroy(backend);
    return 0;
}
