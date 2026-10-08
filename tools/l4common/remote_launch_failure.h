#pragma once
#include "remote_result.h"
/* An unsuccessful launch AFTER immutable recovery publication is different
 * from preparation94. This report never disarms recovery, proves rollback or
 * grants terminal success. Pending task/plan evidence remains operator-visible. */
#define L4_RECORD_REMOTE_LAUNCH_FAILURE 95u
#define L4_REMOTE_LAUNCH_FAILURE_BYTES 240u
typedef struct {
    L4RemoteResult result;
    ULONGLONG plan_sequence;
    DWORD stage,cleanup_error;
} L4RemoteLaunchFailure;
bool l4_remote_launch_failure_encode(const L4RemoteLaunchFailure* value,BYTE output[L4_REMOTE_LAUNCH_FAILURE_BYTES]);
bool l4_remote_launch_failure_decode(const void* bytes,DWORD size,L4RemoteLaunchFailure* value);
