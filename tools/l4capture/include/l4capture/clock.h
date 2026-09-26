#ifndef L4C_CLOCK_H
#define L4C_CLOCK_H
#include "types.h"
uint64_t l4c_now_monotonic_ms(void);
/* Session-relative media PTS (ms) from a capture wall stamp.
 * First call latches *base_ms and returns 0. Later calls return
 * capture_pts_ms - *base_ms (never decreasing). Rebase (and *out_rebased)
 * only if capture_pts_ms < *base_ms. */
uint64_t l4c_pts_session_ms(uint64_t *base_ms, uint64_t capture_pts_ms,
                            uint64_t fallback_now_ms, bool *out_rebased);
#endif