#pragma once
#include <windows.h>
#include <stdbool.h>
#include <stddef.h>
#include "../../l4common/remote_result.h"
#include "../../l4common/remote_launch_failure.h"
#include "../../l4common/remote_outcome.h"
#define L4_UPDATE_EVENT_CODE 76
#define L4_UPDATE_EVENT_TAG 449
/* Trusted SYSTEM operation outcome only. This is not the user-event IPC schema.
 * Operation identity is original7031 UUID; MQTT delivery identity is separate. */
typedef struct {
    const char* operation_id;
    const char* target;
    const char* result;
    const char* requested_version;
    const char* resolved_version; /* NULL before resolution failure. */
    const char* previous_version;
    DWORD error;
    ULONGLONG started_at,finished_at; /* UTC FILETIME, 100ns ticks. */
} L4UpdateOutcome;
/* Pure serialization, no disk/network side effects or delivery assertion. */
bool update_event_json(const L4UpdateOutcome* outcome,unsigned delivery_id,
    const char* delivery_correlation,char* output,size_t capacity);
/* Original private-journal reader must authenticate result provenance first.
 * Pure terminal-preparation adapter; no network, retries or delivery claim. */
bool update_event_preparation_json(const L4RemoteResult* result,unsigned delivery_id,
    const char* delivery_correlation,char* output,size_t capacity);
bool update_event_launch_failure_json(const L4RemoteLaunchFailure* failure,unsigned delivery_id,
    const char* delivery_correlation,char* output,size_t capacity);
/* Caller must prove actual protected marker clear for SUCCESS/RESTORED. */
bool update_event_outcome_json(const L4RemoteOutcome* outcome,unsigned delivery_id,
    const char* delivery_correlation,char* output,size_t capacity);
