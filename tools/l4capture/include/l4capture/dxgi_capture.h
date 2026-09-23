#ifndef L4C_DXGI_CAPTURE_H
#define L4C_DXGI_CAPTURE_H

#include "capture_backend.h"
#include <stdbool.h>

#include <windows.h>
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4201)
#endif
#include <dxgi1_2.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* 
 * Проверка поддержки DXGI Desktop Duplication на текущей ОС/оборудовании.
 * Выполняет безопасный probe без утечек памяти за время <= 2 секунд.
 * Возвращает true на Windows 8+ с поддержкой DXGI 1.2 и D3D11;
 * Возвращает false на Windows 7 или при отсутствии совместимого оборудования.
 */
bool l4c_dxgi_capture_is_supported(void);

/* 
 * Фабричный метод создания экземпляра DXGI capture backend.
 * Выделяет структуру бэкенда и привязывает виртуальную таблицу функций.
 */
l4c_status_t l4c_dxgi_capture_create(l4c_capture_backend_t **out_backend);

/*
 * Геометрические преобразования и поворот растра (DXGI_MODE_ROTATION).
 */
void l4c_dxgi_calc_rotation_dims(uint32_t in_w, uint32_t in_h, DXGI_MODE_ROTATION rot,
                                 uint32_t *out_w, uint32_t *out_h);

void l4c_dxgi_rotate_bgra(const uint8_t *src, uint32_t src_w, uint32_t src_h, int32_t src_stride,
                          uint8_t *dst, int32_t dst_stride, DXGI_MODE_ROTATION rotation);

/*
 * Наложение формы аппаратного курсора DXGI поверх BGRA-растра.
 */
void l4c_dxgi_draw_cursor(uint8_t *frame_data, uint32_t frame_w, uint32_t frame_h, int32_t frame_stride,
                          const DXGI_OUTDUPL_POINTER_SHAPE_INFO *shape, const uint8_t *shape_data,
                          int32_t pos_x, int32_t pos_y);

/*
 * Тестовые хуки для валидации ACCESS_LOST и деградации в тестах.
 */
void l4c_dxgi_test_inject_access_lost(int mode);
int l4c_dxgi_test_get_reinit_count(void);

#ifdef __cplusplus
}
#endif

#endif /* L4C_DXGI_CAPTURE_H */
