#pragma once
#include "remote_result.h"
#define L4_RECORD_REMOTE_OUTCOME 103u
#define L4_REMOTE_OUTCOME_BYTES 280u
enum {L4_REMOTE_OUTCOME_SUCCESS=1,L4_REMOTE_OUTCOME_RESTORED=2,L4_REMOTE_OUTCOME_RECOVERY_REQUIRED=3};
typedef struct {
 /* Same owned identity fields as preparation, but result uses OUTCOME enum.
  * Never pass this payload through the preparation94 codec. */
 L4RemoteResult result;
 ULONGLONG plan_sequence,proof_sequence;
 BYTE plan_sha256[32],proof_sha256[32];
} L4RemoteOutcome;
/* Pure canonical codec, not native completion/rollback/clear authority.
 * SUCCESS requires102 reference; RESTORED requires108 reference at the reader
 * boundary. REQUIRED carries no completion proof and must never clear state. */
bool l4_remote_outcome_encode(const L4RemoteOutcome* value,BYTE bytes[L4_REMOTE_OUTCOME_BYTES]);
bool l4_remote_outcome_decode(const void* bytes,DWORD size,L4RemoteOutcome* value);
