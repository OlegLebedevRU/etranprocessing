#include "l4capture/deadline.h"

l4c_status_t l4c_deadline_start(l4c_deadline_t *lease, uint64_t deadline, uint64_t seq, uint64_t now) {
    if (!lease || !seq) return L4C_ERR_INVALID_ARG;
    if (lease->active || lease->expired) return L4C_ERR_PROTOCOL;
    lease->deadline_tick_ms = deadline;
    lease->last_seq = seq;
    lease->last_tick_ms = now;
    lease->active = true;
    return l4c_deadline_check(lease, now);
}

l4c_status_t l4c_deadline_check(l4c_deadline_t *lease, uint64_t now) {
    if (!lease || !lease->active) return L4C_ERR_INVALID_ARG;
    if (lease->expired || now < lease->last_tick_ms || now >= lease->deadline_tick_ms) {
        lease->expired = true;
        return L4C_ERR_DEADLINE_EXPIRED;
    }
    lease->last_tick_ms = now;
    return L4C_OK;
}

l4c_status_t l4c_deadline_renew(l4c_deadline_t *lease, uint64_t deadline, uint64_t seq, uint64_t now, bool *applied) {
    l4c_status_t status;
    if (!applied) return L4C_ERR_INVALID_ARG;
    *applied = false;
    status = l4c_deadline_check(lease, now);
    if (status != L4C_OK) return status;
    if (seq <= lease->last_seq || deadline <= lease->deadline_tick_ms) return L4C_OK;
    lease->deadline_tick_ms = deadline;
    lease->last_seq = seq;
    *applied = true;
    return L4C_OK;
}

uint64_t l4c_deadline_remaining(l4c_deadline_t *lease, uint64_t now) {
    if (l4c_deadline_check(lease, now) != L4C_OK) return 0;
    return lease->deadline_tick_ms - now;
}