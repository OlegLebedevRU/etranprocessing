#ifndef L4C_COLOR_CONVERT_H
#define L4C_COLOR_CONVERT_H

#include "types.h"
#include "encoder_backend.h"

/* Контекст пула буферов цветовой конверсии */
typedef struct l4c_color_converter l4c_color_converter_t;

/* Создание конвертера с предварительным выделением плоскостей */
l4c_status_t l4c_color_converter_create(
    uint32_t width,
    uint32_t height,
    l4c_color_converter_t **out_converter
);

/* Преобразование BGRA top-down в планарный I420 (BT.601 limited range) */
l4c_status_t l4c_color_convert_bgra_to_i420(
    l4c_color_converter_t *converter,
    const uint8_t *bgra,
    int32_t bgra_stride,
    uint64_t pts_ms,
    l4c_raw_frame_t *out_raw
);

/* Уничтожение конвертера и освобождение памяти плоскостей */
void l4c_color_converter_destroy(l4c_color_converter_t *converter);

#endif /* L4C_COLOR_CONVERT_H */
