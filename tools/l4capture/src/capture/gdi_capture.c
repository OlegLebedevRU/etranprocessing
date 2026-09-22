#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include "l4capture/gdi_capture.h"
#include "l4capture/cursor.h"
#include "l4capture/clock.h"
#include "l4capture/limits.h"

typedef struct {
    HDC hdc_screen;
    HDC hdc_mem;
    HBITMAP hbmp;
    HBITMAP old_hbmp;
    uint8_t *bits;
    uint32_t width;
    uint32_t height;
    int32_t stride;
    size_t buffer_size;
    l4c_rect_t physical_rect;
    l4c_capture_config_t config;
    uint64_t geometry_generation;
} gdi_ctx_t;

static l4c_status_t gdi_init(struct l4c_capture_backend *self, const l4c_capture_config_t *config) {
    gdi_ctx_t *ctx;
    BITMAPINFO bmi;
    int vx, vy, vw, vh;
    l4c_raster_layout_t layout;
    l4c_status_t s;
    if (!self || !config) return L4C_ERR_INVALID_ARG;
    ctx = (gdi_ctx_t *)self->impl_ctx;
    if (!ctx) return L4C_ERR_INVALID_ARG;
    ctx->config = *config;
    /* Determine geometry */
    if (config->target_rect.left == 0 && config->target_rect.top == 0 &&
        config->target_rect.right == 0 && config->target_rect.bottom == 0) {
        vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
        vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
        vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
        vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
        ctx->physical_rect.left = vx;
        ctx->physical_rect.top = vy;
        ctx->physical_rect.right = vx + vw;
        ctx->physical_rect.bottom = vy + vh;
    } else {
        ctx->physical_rect = config->target_rect;
        vw = config->target_rect.right - config->target_rect.left;
        vh = config->target_rect.bottom - config->target_rect.top;
    }
    if (vw <= 0 || vh <= 0) return L4C_ERR_INVALID_ARG;
    ctx->width = (uint32_t)vw;
    ctx->height = (uint32_t)vh;
    /* Check pixel area limit */
    s = l4c_bgra_layout(ctx->width, ctx->height, &layout);
    if (s != L4C_OK) return s;
    ctx->stride = (int32_t)layout.stride;
    ctx->buffer_size = layout.bytes;
    /* Create screen DC */
    ctx->hdc_screen = GetDC(NULL);
    if (!ctx->hdc_screen) return L4C_ERR_FATAL;
    ctx->hdc_mem = CreateCompatibleDC(ctx->hdc_screen);
    if (!ctx->hdc_mem) { ReleaseDC(NULL, ctx->hdc_screen); ctx->hdc_screen = NULL; return L4C_ERR_FATAL; }
    /* Create DIB section */
    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = (LONG)ctx->width;
    bmi.bmiHeader.biHeight = -((LONG)ctx->height); /* top-down */
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    ctx->hbmp = CreateDIBSection(ctx->hdc_mem, &bmi, DIB_RGB_COLORS, (void **)&ctx->bits, NULL, 0);
    if (!ctx->hbmp) { DeleteDC(ctx->hdc_mem); ReleaseDC(NULL, ctx->hdc_screen); ctx->hdc_mem = NULL; ctx->hdc_screen = NULL; return L4C_ERR_FATAL; }
    ctx->old_hbmp = (HBITMAP)SelectObject(ctx->hdc_mem, ctx->hbmp);
    ctx->geometry_generation = 1;
    return L4C_OK;
}

static l4c_status_t gdi_acquire(struct l4c_capture_backend *self, l4c_frame_view_t *out_frame, uint32_t timeout_ms) {
    gdi_ctx_t *ctx;
    HDESK desktop;
    (void)timeout_ms;
    if (!self || !out_frame) return L4C_ERR_INVALID_ARG;
    ctx = (gdi_ctx_t *)self->impl_ctx;
    if (!ctx || !ctx->hdc_screen || !ctx->hdc_mem) return L4C_ERR_INVALID_ARG;
    /* Check desktop availability */
    desktop = OpenInputDesktop(0, FALSE, DESKTOP_SWITCHDESKTOP);
    if (!desktop) return L4C_ERR_SESSION_UNAVAILABLE;
    CloseDesktop(desktop);
    /* BitBlt capture */
    if (!BitBlt(ctx->hdc_mem, 0, 0, (int)ctx->width, (int)ctx->height,
                ctx->hdc_screen, ctx->physical_rect.left, ctx->physical_rect.top,
                SRCCOPY | CAPTUREBLT)) return L4C_ERR_DEVICE_LOST;
    /* Cursor overlay */
    if (ctx->config.capture_cursor) {
        (void)l4c_cursor_draw(ctx->hdc_mem, &ctx->physical_rect);
    }
    /* Fill frame view */
    out_frame->data = ctx->bits;
    out_frame->width = ctx->width;
    out_frame->height = ctx->height;
    out_frame->stride = ctx->stride;
    out_frame->buffer_size = ctx->buffer_size;
    out_frame->physical_rect = ctx->physical_rect;
    out_frame->pts_ms = l4c_now_monotonic_ms();
    out_frame->geometry_generation = ctx->geometry_generation;
    return L4C_OK;
}

static void gdi_release(struct l4c_capture_backend *self, l4c_frame_view_t *frame) {
    (void)self;
    (void)frame;
    /* DIB buffer persists in backend; nothing to release per-frame */
}

static void gdi_destroy(struct l4c_capture_backend *self) {
    gdi_ctx_t *ctx;
    if (!self) return;
    ctx = (gdi_ctx_t *)self->impl_ctx;
    if (!ctx) return;
    if (ctx->hdc_mem) {
        if (ctx->old_hbmp) SelectObject(ctx->hdc_mem, ctx->old_hbmp);
        DeleteDC(ctx->hdc_mem);
    }
    if (ctx->hbmp) DeleteObject(ctx->hbmp);
    if (ctx->hdc_screen) ReleaseDC(NULL, ctx->hdc_screen);
    free(ctx);
    self->impl_ctx = NULL;
}

static const l4c_capture_backend_vtable_t gdi_vtable = {
    gdi_init, gdi_acquire, gdi_release, gdi_destroy
};

l4c_status_t l4c_gdi_capture_create(l4c_capture_backend_t **out_backend) {
    l4c_capture_backend_t *backend;
    gdi_ctx_t *ctx;
    if (!out_backend) return L4C_ERR_INVALID_ARG;
    ctx = (gdi_ctx_t *)calloc(1, sizeof(gdi_ctx_t));
    if (!ctx) return L4C_ERR_OUT_OF_MEMORY;
    backend = (l4c_capture_backend_t *)calloc(1, sizeof(l4c_capture_backend_t));
    if (!backend) { free(ctx); return L4C_ERR_OUT_OF_MEMORY; }
    backend->vtable = &gdi_vtable;
    backend->impl_ctx = ctx;
    *out_backend = backend;
    return L4C_OK;
}
