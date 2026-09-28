#include <stdint.h>
#include <stdbool.h>
#include "l4capture/clock.h"

int test_pts_session_relative_wall(void) {
    uint64_t base = 0;
    bool rebased = false;
    uint64_t pts;

    /* First call latches base and returns 0 */
    pts = l4c_pts_session_ms(&base, 12345, 99999, &rebased);
    if (pts != 0) return 1;
    if (base != 12345) return 2;
    if (rebased) return 3;

    /* +100 ms wall */
    pts = l4c_pts_session_ms(&base, 12445, 0, &rebased);
    if (pts != 100) return 4;
    if (rebased) return 5;

    /* Same capture stamp → same session PTS (non-decreasing, no jump) */
    pts = l4c_pts_session_ms(&base, 12445, 0, &rebased);
    if (pts != 100) return 6;
    if (rebased) return 7;

    /* Idle gap is preserved (not frozen at claimed fps) */
    pts = l4c_pts_session_ms(&base, 15000, 0, &rebased);
    if (pts != 2655) return 8;
    if (rebased) return 9;

    /* capture_pts_ms == 0 → fallback_now */
    {
        uint64_t base2 = 0;
        pts = l4c_pts_session_ms(&base2, 0, 500, &rebased);
        if (pts != 0 || base2 != 500 || rebased) return 10;
        pts = l4c_pts_session_ms(&base2, 0, 800, &rebased);
        if (pts != 300 || rebased) return 11;
    }

    /* capture < base → rebase, return 0 */
    pts = l4c_pts_session_ms(&base, 100, 0, &rebased);
    if (pts != 0) return 12;
    if (!rebased) return 13;
    if (base != 100) return 14;

    /* After rebase, wall resumes */
    pts = l4c_pts_session_ms(&base, 250, 0, &rebased);
    if (pts != 150 || rebased) return 15;

    /* NULL base is safe */
    pts = l4c_pts_session_ms(NULL, 42, 0, &rebased);
    if (pts != 0) return 16;

    return 0;
}
