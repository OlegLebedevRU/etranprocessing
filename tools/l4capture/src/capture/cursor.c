#include <windows.h>
#include "l4capture/cursor.h"

l4c_status_t l4c_cursor_draw(HDC hdc_target, const l4c_rect_t *target_rect) {
    CURSORINFO ci;
    ICONINFO ii;
    int draw_x, draw_y;
    if (!hdc_target || !target_rect) return L4C_ERR_INVALID_ARG;
    ci.cbSize = sizeof(ci);
    if (!GetCursorInfo(&ci)) return L4C_OK;
    if (!(ci.flags & CURSOR_SHOWING)) return L4C_OK;
    if (!GetIconInfo(ci.hCursor, &ii)) return L4C_OK;
    draw_x = ci.ptScreenPos.x - target_rect->left - (int)ii.xHotspot;
    draw_y = ci.ptScreenPos.y - target_rect->top - (int)ii.yHotspot;
    DrawIconEx(hdc_target, draw_x, draw_y, ci.hCursor, 0, 0, 0, NULL, DI_NORMAL);
    if (ii.hbmMask) DeleteObject(ii.hbmMask);
    if (ii.hbmColor) DeleteObject(ii.hbmColor);
    return L4C_OK;
}
