#pragma once
#include <windows.h>
#include <stdbool.h>

typedef struct {char email[256],thumbprint[64];bool machine;} L4ProxyCertificate;
/* Independently authenticated source SCM command. Known non-certificate options
 * are consumed by arity; unknown/action/missing/oversized/non-ASCII options refuse.
 * Match current normal CLI effective last-wins certificate selection. */
bool l4_proxy_certificate_source(const wchar_t* command,L4ProxyCertificate* profile);
/* Dedicated probe whitelist, mandatory trailing --version compatibility guard.
 * argc includes exe, --update-probe and three numeric arguments (checked by caller).
 * No normal startup/control/network options accepted here. */
bool l4_proxy_certificate_probe(int argc,char** argv,L4ProxyCertificate* profile);
/* Bounded correct CRT/CommandLineToArgvW quoting, fixed options only. */
bool l4_proxy_certificate_command(const wchar_t* executable,const L4ProxyCertificate* profile,
    WORD http,WORD mqtt,DWORD timeout,wchar_t command[2048]);

/* Fixed plain-loopback signal profile from authenticated old command. Existing
 * normal CLI defaults only, never probe/budget defaults or learned responses. */
bool l4_proxy_signal_source(const wchar_t* command,WORD* http,WORD* mqtt,char expected_thumb[64]);
