#pragma once
#include <windows.h>
#include <stdbool.h>
#include "service_token.h"

/* Callback receives held child handle, actual PID and absolute probe deadline.
 * No process lookup/adoption. Caller must hold an authenticated EXE fence and
 * provide a protected working directory. Never used for service activation.
 * Cleanup kills only this job; up to 5s additional termination wait is bounded. */
typedef bool (*L4ChildCheck)(HANDLE process,DWORD pid,ULONGLONG deadline,void* context);
bool l4_child_probe(const wchar_t* executable,const wchar_t* directory,wchar_t* command,
    DWORD timeout_ms,L4ChildCheck check,void* context);
/* Production candidate path: actual captured service token, never caller-token
 * fallback. Original source epoch rechecked after job assignment before resume. */
bool l4_child_probe_service(const L4ServiceToken* token,const wchar_t* executable,
    const wchar_t* directory,wchar_t* command,DWORD timeout_ms,L4ChildCheck check,void* context);
/* Exact loopback listening TCP rows must belong to the held, live child. */
bool l4_child_listeners(HANDLE process,DWORD pid,WORD first,WORD second);
