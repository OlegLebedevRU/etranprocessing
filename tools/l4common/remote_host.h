#pragma once
#include "remote_request.h"
#include "journal_reader.h"
#include "active_updater.h"
#define L4_RECORD_REMOTE_HOST_ACK 93u
typedef struct {
    DWORD pid;ULONGLONG birth,executable_size;BYTE executable_sha256[32];
    L4RemoteRequest request;char source_version[32],arch[8],updater_version[32];DWORD format_version;
} L4RemoteHostReceipt;
typedef struct L4RemoteHost L4RemoteHost;
typedef struct {HANDLE file;wchar_t path[MAX_PATH];ULONGLONG size;BYTE sha256[32];char updater_version[32];} L4RemoteHostImage;
/* Borrowed file in an owned fence, valid until host_close. Child authenticates
 * this image against installed owner-signed installer metadata before ACK. */
bool l4_remote_host_pin_self(const L4Layout* layout,const wchar_t* operation,const char* arch,
                            L4RemoteHost** host,L4RemoteHostImage* image);
/* Primary SYSTEM canonical installed L4Con only. Caller has already flushed92.
 * On successful ownership handoff closes *journal before starting the independent
 * SCM host (outside the console Job). Never enables an absent controller engine.
 * Success requires a durable93 plus fresh SCM/System/image/process-epoch proof.
 * A timeout never kills/adopts a live host; no suite services are changed here. */
bool l4_remote_host_launch(L4Journal** journal,const char* arch,DWORD timeout_ms,
                          L4RemoteHost** host,L4RemoteHostReceipt* receipt);
/* Owner-local acceptance bootstrap only: primary SYSTEM/session0 running the
 * exact pinned active installed setup image. Caller has verified protected local
 * physical-operator intent and source before92. Same host/ACK rules; no weakening
 * of the canonical Con launch entry or catalog/worker/recovery admission. */
bool l4_remote_host_launch_local(L4Journal** journal,const char* arch,DWORD timeout_ms,
                          L4RemoteHost** host,L4RemoteHostReceipt* receipt);
/* Wait only the exact retained admitted controller process; no SCM stop/kill,
 * replacement epoch adoption or outcome claim. Child exit is diagnostic. */
bool l4_remote_host_wait(L4RemoteHost* host,DWORD timeout_ms,DWORD* exit_code);
/* Fresh live ACK/status snapshot only: no source-release authority or repair.
 * Success proves current host ownership, not terminal update success/readiness. */
bool l4_remote_host_observe(const L4Layout* layout,const wchar_t* operation,
                           DWORD timeout_ms,L4RemoteHostReceipt* receipt);
/* Signed/authenticated controller invokes after its real engine preflight.
 * Own SCM/System/module binding is rechecked; one original epoch only. */
bool l4_remote_host_ack(L4Journal* journal,const char* arch,L4RemoteHostReceipt* receipt);
/* Locked original journal read only; caller must separately prove the live
 * controller epoch. Never creates/reissues ACK or adopts another host. */
bool l4_remote_host_load(L4Journal* journal,L4RemoteHostReceipt* receipt);
/* Read-only canonical92/93 decode from an already authenticated full snapshot.
 * layout is the ORIGINAL source layout. No live host/SCM ownership evidence.
 * Missing93 is ERROR_IO_PENDING; duplicate/malformed records always refuse. */
bool l4_remote_host_snapshot(const L4JournalReader* reader,const L4Layout* layout,L4RemoteHostReceipt* receipt);
/* Exact private SCM contract; child uses for admission and owned cleanup. */
bool l4_remote_host_identity(const L4Layout* layout,const wchar_t* operation,const char* arch,
    wchar_t service[80],wchar_t executable[MAX_PATH],wchar_t command[1024]);
bool l4_remote_host_self(const L4Layout* layout,const wchar_t* operation,const char* arch);
bool l4_remote_host_retire(const L4Layout* layout,const wchar_t* operation,const char* arch);
void l4_remote_host_close(L4RemoteHost* host);
