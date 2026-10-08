#pragma once
#include "update_event.h"
#include "../../l4common/remote_status.h"
bool update_status_result_json(const L4RemoteStatus* status,unsigned delivery_id,
    const char* correlation,char* output,size_t capacity);
/* Authenticated original private-journal status -> pure event76/449 payload.
 * Uses the SYSTEM/canonical live reader, never user-event IPC or caller outcome.
 * Pending operation yields ERROR_IO_PENDING and empty output. No publication,
 * retry queue, task acknowledgement or delivery claim. */
bool update_status_event_json(const L4Layout* layout,const wchar_t* operation,
    unsigned delivery_id,const char* correlation,char* output,size_t capacity);
/* Recorded history only, not current actor ownership/readiness or stop authority. */
bool update_status_rpc_json(const L4RemoteStatus* status,const char* task_id,char* output,size_t capacity);
