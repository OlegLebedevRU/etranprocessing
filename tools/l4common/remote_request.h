#pragma once
#include "journal.h"
#define L4_RECORD_REMOTE_REQUEST 92u
enum {L4_REMOTE_SUITE=1,L4_REMOTE_UPDATER=2};
typedef struct {DWORD target;char version[32];ULONGLONG accepted_utc;} L4RemoteRequest;
/* SYSTEM controller storage boundary, not MQTT acceptance or stop permission.
 * Original7031 UUID is the journal header identity. No URL/path/actor/budget input.
 * Flush before returning. Exact retry retains timestamp; altered input conflicts.
 * Independent host/worker ownership must be established before sending202. */
bool l4_remote_request_save(L4Journal* journal,DWORD target,const char* version,L4RemoteRequest* saved);
bool l4_remote_request_load(L4Journal* journal,L4RemoteRequest* saved);
/* Pure canonical record92 decoder; no journal or request acceptance authority. */
bool l4_remote_request_decode(const L4Layout* roots,const void* bytes,DWORD size,L4RemoteRequest* saved);
