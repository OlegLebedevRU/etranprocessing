#ifndef L4C_SCALE_H
#define L4C_SCALE_H

#include "types.h"

/* Catmull-Rom bicubic: лучше держит края текста, чем bilinear.
 * Покрытие: stretch-fit без crop/letterbox (контракт L4C-02). */
l4c_status_t l4c_scale_bicubic_bgra(
    const uint8_t *src,
    uint32_t src_w,
    uint32_t src_h,
    int32_t src_stride,
    uint8_t *dst,
    uint32_t dst_w,
    uint32_t dst_h,
    int32_t dst_stride
);

#endif /* L4C_SCALE_H */
