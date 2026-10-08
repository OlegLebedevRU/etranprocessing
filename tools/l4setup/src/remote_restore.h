#pragma once
#include "remote_completion.h"
#define SETUP_RECORD_REMOTE_RESTORE 108u
#define SETUP_REMOTE_RESTORE_BYTES 384u
typedef struct {
 char operation[37],version[32],arch[8];BYTE operation_sha256[32],source_root[32];
 DWORD window,worker_pid,pids[4],verification_mode;FILETIME worker_birth,births[4];
 ULONGLONG generation,deadline_utc,operation_sequence,finished_utc,configs[12],restores[4];
} SetupRemoteRestoreProof;
typedef struct SetupRemoteRestore SetupRemoteRestore;
/* Pure fixed ABI1 codec, never rollback/clear authority. */
bool setup_remote_restore_decode(const void* bytes,DWORD size,SetupRemoteRestoreProof* proof);
/* Capture authenticated original source and actors under admitted69/clear state.
 * No source borrower is used after SCM changes. One-hop only. */
bool setup_remote_restore_prepare(L4Journal* journal,SetupInstalledSource* source,
 const SetupOperationPlan* operation,SetupRemoteRestore** context);
/* Actual old-owned service epochs, all12 old configs/ACLs/full inventory, four
 * app checks (window1 Sup drain+armed recovery instead of ordinary health),
 * fresh REQ/RSP+EVT/EVA, then both storage DONE guards and repeated local checks.
 * Writes108+bound103 RESTORED then clears. Error must be original nonzero error;
 * never accepts a caller assertion of successful restoration. Refusal retains
 * marker and recovery. Original journal/plan/native objects must remain owned. */
bool setup_remote_restore_finish(SetupRemoteRestore* context,L4Journal* journal,
 const SetupOperationPlan* operation,SetupServiceTransaction* transaction,
 SetupRemoteService* services[4],DWORD original_error,DWORD timeout_ms,L4RemoteOutcome* outcome);
void setup_remote_restore_free(SetupRemoteRestore* context);
