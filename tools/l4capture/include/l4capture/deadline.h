#ifndef L4C_DEADLINE_H
#define L4C_DEADLINE_H
#include "types.h"
typedef struct {
    uint64_t deadline_tick_ms;
    uint64_t last_seq;
    uint64_t last_tick_ms;
    bool active;
    bool expired;
} l4c_deadline_t;

l4c_status_t l4c_deadline_start(l4c_deadline_t *lease, uint64_t deadline, uint64_t seq, uint64_t now);
l4c_status_t l4c_deadline_check(l4c_deadline_t *lease, uint64_t now);
l4c_status_t l4c_deadline_renew(l4c_deadline_t *lease, uint64_t deadline, uint64_t seq, uint64_t now, bool *applied);
uint64_t l4c_deadline_remaining(l4c_deadline_t *lease, uint64_t now);
#endif