#include "l4capture/deadline.h"

int test_deadline_basic(void) {
    l4c_deadline_t lease;
    l4c_status_t s;
    memset(&lease, 0, sizeof(lease));
    s = l4c_deadline_start(&lease, 10000, 1, 100);
    if (s != L4C_OK) return 1;
    if (!lease.active) return 2;
    s = l4c_deadline_check(&lease, 5000);
    if (s != L4C_OK) return 3;
    return 0;
}

int test_deadline_expired(void) {
    l4c_deadline_t lease;
    l4c_status_t s;
    memset(&lease, 0, sizeof(lease));
    s = l4c_deadline_start(&lease, 1000, 1, 500);
    if (s != L4C_OK) return 1;
    /* Exactly at deadline — expired. */
    s = l4c_deadline_check(&lease, 1000);
    if (s != L4C_ERR_DEADLINE_EXPIRED) return 2;
    /* Past deadline — still expired. */
    s = l4c_deadline_check(&lease, 2000);
    if (s != L4C_ERR_DEADLINE_EXPIRED) return 3;
    /* Before start tick — expired (time went backward). */
    memset(&lease, 0, sizeof(lease));
    s = l4c_deadline_start(&lease, 5000, 1, 1000);
    if (s != L4C_OK) return 4;
    s = l4c_deadline_check(&lease, 999);
    if (s != L4C_ERR_DEADLINE_EXPIRED) return 5;
    return 0;
}

int test_deadline_renew(void) {
    l4c_deadline_t lease;
    l4c_status_t s;
    bool applied;
    memset(&lease, 0, sizeof(lease));
    s = l4c_deadline_start(&lease, 5000, 1, 100);
    if (s != L4C_OK) return 1;
    /* Valid renewal: bigger deadline, bigger seq. */
    s = l4c_deadline_renew(&lease, 10000, 2, 200, &applied);
    if (s != L4C_OK || !applied) return 2;
    if (lease.deadline_tick_ms != 10000) return 3;
    return 0;
}

int test_deadline_dedup(void) {
    l4c_deadline_t lease;
    l4c_status_t s;
    bool applied;
    memset(&lease, 0, sizeof(lease));
    s = l4c_deadline_start(&lease, 5000, 1, 100);
    if (s != L4C_OK) return 1;
    /* Duplicate seq — should not apply. */
    s = l4c_deadline_renew(&lease, 10000, 1, 200, &applied);
    if (s != L4C_OK || applied) return 2;
    /* Smaller deadline — should not apply. */
    s = l4c_deadline_renew(&lease, 4000, 2, 200, &applied);
    if (s != L4C_OK || applied) return 3;
    /* Smaller seq — should not apply. */
    s = l4c_deadline_renew(&lease, 10000, 1, 200, &applied);
    if (s != L4C_OK || applied) return 4;
    return 0;
}

int test_deadline_remaining(void) {
    l4c_deadline_t lease;
    l4c_status_t s;
    uint64_t rem;
    memset(&lease, 0, sizeof(lease));
    s = l4c_deadline_start(&lease, 5000, 1, 100);
    if (s != L4C_OK) return 1;
    rem = l4c_deadline_remaining(&lease, 3000);
    if (rem != 2000) return 2;
    /* After expiry — 0. */
    rem = l4c_deadline_remaining(&lease, 5000);
    if (rem != 0) return 3;
    return 0;
}
