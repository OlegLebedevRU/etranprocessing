#pragma once
#include "layout.h"

/* Captures an existing RUNNING service token and verifies its configured account.
 * Read-only SCM/process access; missing/stopped/changed/denied is a failure.
 * Caller owns the returned impersonation token. Never fabricates a logon token. */
bool l4_access_service_token(const wchar_t* service, HANDLE* token);
/* Only SYSTEM, enabled owner-capable Administrators, or this enabled service SID.
 * Never grants privileged receipt ownership to an ordinary account/user SID. */
bool l4_access_private_sid(HANDLE token, const wchar_t* service, BYTE sid[SECURITY_MAX_SID_SIZE]);
bool l4_access_private_descriptor(const wchar_t* service, bool directory,
                                  PSECURITY_DESCRIPTOR* descriptor);
/* Real file I/O under the supplied impersonation token, in a locked data leaf.
 * Checks create/write/flush/read/append/rename/delete; removes only unique probes.
 * Restores the caller thread token, including on failures. */
bool l4_access_probe(const L4Layout* layout, const wchar_t* relative, HANDLE token);
bool l4_access_read_probe(const L4Layout* layout, const wchar_t* relative, HANDLE token);
typedef struct {
    HANDLE proxy, broker, console, supervisor, desktop;
} L4AccessActors;
/* Offline provisioning: may replace ACLs. Never call on a live deployment.
 * All actor tokens/privileged identities must be ready before any write. */
bool l4_access_prepare(const L4Layout* layout, const L4AccessActors* actors);
/* Live pre-stop gate: does not create directories or replace ACLs. Only unique
 * temporary probe files; all actor tokens must have been captured beforehand. */
bool l4_access_verify(const L4Layout* layout, const L4AccessActors* actors);
