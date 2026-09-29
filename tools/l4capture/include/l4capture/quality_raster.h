#ifndef L4CAPTURE_QUALITY_RASTER_H
#define L4CAPTURE_QUALITY_RASTER_H

#include <stdint.h>

/* Fit the complete source into an even H.264 raster without upscaling. */
static inline void l4c_quality_fit_raster(uint32_t *width, uint32_t *height,
                                          uint32_t max_width, uint32_t max_height) {
    uint32_t source_width, source_height;
    uint64_t scaled_width, scaled_height;
    if (!width || !height || !*width || !*height || !max_width || !max_height) return;
    source_width = *width;
    source_height = *height;
    if (source_width > max_width || source_height > max_height) {
        if ((uint64_t)source_width * max_height > (uint64_t)source_height * max_width) {
            scaled_width = max_width;
            scaled_height = (uint64_t)source_height * max_width / source_width;
        } else {
            scaled_height = max_height;
            scaled_width = (uint64_t)source_width * max_height / source_height;
        }
        *width = (uint32_t)scaled_width;
        *height = (uint32_t)scaled_height;
    }
    *width &= ~1u;
    *height &= ~1u;
}

/* Legacy width-only helper retained for existing callers/tests. */
static inline void l4c_quality_limit_width(uint32_t *width, uint32_t *height, uint32_t max_width) {
    if (!width || !height || !max_width || *width <= max_width) return;
    l4c_quality_fit_raster(width, height, max_width, UINT32_MAX);
}

#endif
