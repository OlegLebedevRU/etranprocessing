#pragma once
#include <windows.h>
#include <stdbool.h>
#include "registry_target.h"
typedef bool (*L4RegistrySink)(const BYTE* bytes,DWORD size,void* context);
/* version=NULL means catalog.json/catalog.json.sig under l4tools/metadata.
 * Otherwise canonical version + one fixed release/root/layout filename only.
 * All requests use the supplied LOOPBACK proxy port, fixed host:443 and TLS1.2.
 * No proxy discovery/bypass/direct fallback, redirects, auth or client certificate.
 * Sink sees provisional bytes: caller must discard on failure and authenticate
 * the completed document/archive before using it. Nothing is installed here. */
bool l4_registry_fetch(WORD proxy_port,const char* version,const char* file,ULONGLONG limit,
    DWORD timeout_ms,L4RegistrySink sink,void* context,ULONGLONG* received);
/* Bounded metadata only. Caller frees returned buffer; zero output on refusal. */
bool l4_registry_metadata(WORD proxy_port,const char* version,const char* file,DWORD timeout_ms,
    BYTE** bytes,DWORD* size);
