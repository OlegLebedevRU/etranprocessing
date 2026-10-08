#pragma once
#include "layout.h"

typedef struct L4ServiceToken L4ServiceToken;
/* Fixed Leo4Proxy only, independently authenticated expected original inventory.
 * Read-only SCM capture of its actual RUNNING own-process LocalSystem/session0
 * primary token, held process + creation epoch. No fabricated logon/account. */
bool l4_service_token_capture(const L4ServiceInventory* expected,L4ServiceToken** result);
bool l4_service_token_verify(const L4ServiceToken* token);
void l4_service_token_close(L4ServiceToken* token);
/* Creates suspended/no handles/no console in original token session. Uses a
 * fresh non-inherited environment and private thread privilege scope, restores
 * caller before return and checks original epoch plus exact child token identity.
 * Caller assigns its own job before resume; returned PI handles are caller-owned.
 * Failure cleans only its own suspended process, never falls back to caller token. */
bool l4_service_token_create(const L4ServiceToken* token,const wchar_t* executable,
    const wchar_t* directory,wchar_t* command,PROCESS_INFORMATION* child);
