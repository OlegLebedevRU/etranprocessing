#ifndef L4CAPTURE_QUALITY_RASTER_H
#define L4CAPTURE_QUALITY_RASTER_H

#include <stdint.h>

/* Limit encoded width while retaining the complete source aspect ratio. */
static inline void l4c_quality_limit_width(uint32_t *width, uint32_t *height, uint32_t max_width) {
    uint32_t source_width;
    if (!width || !height || !max_width || *width <= max_width) return;
    source_width = *width;
    *height = (uint32_t)(((uint64_t)*height * max_width / source_width) & ~1ull);
    *width = max_width & ~1u;
}

#endif
