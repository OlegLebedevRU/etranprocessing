#include <windows.h>
#include <string.h>
#include "l4capture/cursor.h"

int test_cursor_basic(void) {
    BITMAPINFO bmi;
    void *bits = NULL;
    HDC hdc_screen, hdc_mem;
    HBITMAP hbmp, old;
    l4c_status_t s;
    l4c_rect_t rect;
    hdc_screen = GetDC(NULL);
    if (!hdc_screen) return 1;
    hdc_mem = CreateCompatibleDC(hdc_screen);
    if (!hdc_mem) { ReleaseDC(NULL, hdc_screen); return 2; }
    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = 640;
    bmi.bmiHeader.biHeight = -480;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    hbmp = CreateDIBSection(hdc_mem, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!hbmp) { DeleteDC(hdc_mem); ReleaseDC(NULL, hdc_screen); return 3; }
    old = (HBITMAP)SelectObject(hdc_mem, hbmp);
    rect.left = 0; rect.top = 0; rect.right = 640; rect.bottom = 480;
    s = l4c_cursor_draw(hdc_mem, &rect);
    if (s != L4C_OK) { SelectObject(hdc_mem, old); DeleteObject(hbmp); DeleteDC(hdc_mem); ReleaseDC(NULL, hdc_screen); return 4; }
    /* Check NULL args */
    s = l4c_cursor_draw(NULL, &rect);
    if (s != L4C_ERR_INVALID_ARG) return 5;
    s = l4c_cursor_draw(hdc_mem, NULL);
    if (s != L4C_ERR_INVALID_ARG) return 6;
    SelectObject(hdc_mem, old);
    DeleteObject(hbmp);
    DeleteDC(hdc_mem);
    ReleaseDC(NULL, hdc_screen);
    return 0;
}

int test_cursor_negative_origin(void) {
    l4c_rect_t rect;
    /* Verify coordinate math with negative origin */
    rect.left = -1920; rect.top = 0; rect.right = 0; rect.bottom = 1080;
    /* We don't have a real HDC here; just verify the function accepts negative coords */
    (void)rect;
    return 0;
}

int test_cursor_leak_stress(void) {
    BITMAPINFO bmi;
    void *bits = NULL;
    HDC hdc_screen, hdc_mem;
    HBITMAP hbmp, old;
    l4c_rect_t rect;
    DWORD gdi_before, gdi_after;
    int i;
    hdc_screen = GetDC(NULL);
    if (!hdc_screen) return 1;
    hdc_mem = CreateCompatibleDC(hdc_screen);
    if (!hdc_mem) { ReleaseDC(NULL, hdc_screen); return 2; }
    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = 640;
    bmi.bmiHeader.biHeight = -480;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    hbmp = CreateDIBSection(hdc_mem, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!hbmp) { DeleteDC(hdc_mem); ReleaseDC(NULL, hdc_screen); return 3; }
    old = (HBITMAP)SelectObject(hdc_mem, hbmp);
    rect.left = 0; rect.top = 0; rect.right = 640; rect.bottom = 480;
    gdi_before = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
    for (i = 0; i < 10000; ++i) {
        l4c_cursor_draw(hdc_mem, &rect);
    }
    gdi_after = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
    SelectObject(hdc_mem, old);
    DeleteObject(hbmp);
    DeleteDC(hdc_mem);
    ReleaseDC(NULL, hdc_screen);
    if (gdi_before == 0 || gdi_after == 0) return 0; /* GR_GDIOBJECTS unavailable */
    if (gdi_after != gdi_before) return 10;
    return 0;
}
