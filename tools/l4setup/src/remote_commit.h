#pragma once
#include "updater_identity.h"
#include "../../l4common/journal_reader.h"
#include "update_metadata.h"
#define SETUP_RECORD_REMOTE_COMMIT 102u
#define SETUP_REMOTE_COMMIT_BYTES 16959u
typedef struct {
 char operation[37],predecessor[37],updater_origin[37];
 char suite_version[32],updater_version[32],arch[8];
 BYTE suite_root[32],updater_root[32],publisher[32],operation_sha256[32];
 SetupRootAsset updater;
 ULONGLONG operation_sequence,configs[12],finished_utc;
 DWORD start_types[4],pids[4];FILETIME births[4];wchar_t commands[4][2048];
} SetupRemoteCommit;
/* Pure typed codec: not authentication or final success proof. */
bool setup_remote_commit_encode(const SetupRemoteCommit* value,BYTE bytes[SETUP_REMOTE_COMMIT_BYTES]);
bool setup_remote_commit_decode(const void* bytes,DWORD size,SetupRemoteCommit* value);
/* Protected immutable snapshot, strict unique102/64/config20 binding only.
 * This is groundwork, NOT installed-source or target-signature authority.
 * Incomplete/malformed receipts fail closed; never repair/adopt active state. */
bool setup_remote_commit_snapshot(const L4JournalReader* reader,SetupRemoteCommit* value);
/* Historical target metadata admission and102 binding; not current SCM or a
 * substitute for the writer's real whole-suite terminal proof. Caller owns plan. */
bool setup_remote_commit_admit_target(const L4JournalReader* reader,const L4Layout* roots,
    SetupRemoteCommit* value,SetupOperationPlan** plan);
/* Closed until the actual whole-suite opaque completion proof is available. */
bool setup_remote_commit_save(L4Journal* original,const SetupRemoteCommit* value);
