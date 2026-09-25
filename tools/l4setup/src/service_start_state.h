#pragma once

#include <windows.h>

typedef enum {
    SERVICE_START_WAIT,
    SERVICE_START_SUCCEEDED,
    SERVICE_START_FAILED
} ServiceStartDecision;

static inline ServiceStartDecision service_start_decide(const SERVICE_STATUS_PROCESS* status) {
    if (status->dwCurrentState == SERVICE_RUNNING) return SERVICE_START_SUCCEEDED;
    if (status->dwCurrentState == SERVICE_START_PENDING) return SERVICE_START_WAIT;
    return SERVICE_START_FAILED;
}
