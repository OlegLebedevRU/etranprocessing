#include "l4capture/idle_refresh.h"
#include "l4capture/quality_raster.h"

int test_idle_refresh_schedule(void) {
    if (l4c_idle_refresh_due(2000, 1000, 0, 1000) != true) return 1;
    if (l4c_idle_refresh_due(1999, 1000, 0, 1000)) return 2;
    if (l4c_idle_refresh_due(2500, 1000, 2000, 1000)) return 3;
    if (!l4c_idle_refresh_due(3000, 1000, 2000, 1000)) return 4;
    if (l4c_idle_refresh_due(3000, 0, 0, 1000)) return 5;
    if (l4c_idle_refresh_due(3000, 1000, 0, 0)) return 6;
    if (l4c_idle_refresh_due(3000, 3100, 0, 1000)) return 7;
    return 0;
}

int test_idle_refresh_motion_defers(void) {
    if (l4c_idle_refresh_due(3100, 2500, 2000, 1000)) return 1;
    if (!l4c_idle_refresh_due(3500, 2500, 2000, 1000)) return 2;
    return 0;
}

int test_quality_limit_width_preserves_full_screen(void) {
    uint32_t w = 3000, h = 2000;
    l4c_quality_limit_width(&w, &h, 1920);
    if (w != 1920 || h != 1280) return 1;
    w = 1920; h = 1080;
    l4c_quality_limit_width(&w, &h, 1920);
    if (w != 1920 || h != 1080) return 2;
    return 0;
}

int test_bootstrap_refresh_is_bounded(void) {
    if (l4c_bootstrap_refresh_due(2499, 1000, 0)) return 1;
    if (!l4c_bootstrap_refresh_due(2500, 1000, 0)) return 2;
    if (l4c_bootstrap_refresh_due(4499, 1000, 1)) return 3;
    if (!l4c_bootstrap_refresh_due(4500, 1000, 1)) return 4;
    if (!l4c_bootstrap_refresh_due(8500, 1000, 2)) return 5;
    if (l4c_bootstrap_refresh_due(9000, 1000, 3)) return 6;
    if (l4c_bootstrap_refresh_due(12000, 1000, 0)) return 7;
    return 0;
}
