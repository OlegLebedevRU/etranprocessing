#ifndef L4C_CURSOR_H
#define L4C_CURSOR_H

#include "types.h"
#include <windows.h>

/* Отрисовка текущего аппаратного курсора на целевой контекст HDC */
l4c_status_t l4c_cursor_draw(HDC hdc_target, const l4c_rect_t *target_rect);

#endif /* L4C_CURSOR_H */
