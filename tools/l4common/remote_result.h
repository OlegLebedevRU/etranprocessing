#pragma once
#include <windows.h>
#include <stdbool.h>
#define L4_RECORD_REMOTE_PREPARATION_RESULT 94u
#define L4_REMOTE_RESULT_BYTES 200u
enum {L4_REMOTE_RESULT_FAILED=1,L4_REMOTE_RESULT_CANCELLED=2};
/* Terminal pre-switch outcome only. No success/partial-apply authority. Owned
 * strings survive controller/journal teardown; delivery UUID is never stored.
 * Pure codec validation does not authenticate provenance. Read through the
 * original private journal and setup's request/host/route binding adapter. */
typedef struct {
    char operation_id[37],requested_version[32],previous_version[32],resolved_version[32];
    DWORD target,result,error;
    ULONGLONG started_at,finished_at;
} L4RemoteResult;
bool l4_remote_result_encode(const L4RemoteResult* value,BYTE output[L4_REMOTE_RESULT_BYTES]);
bool l4_remote_result_decode(const void* bytes,DWORD size,L4RemoteResult* value);
