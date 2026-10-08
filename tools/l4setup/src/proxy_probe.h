#ifndef L4SETUP_PROXY_PROBE_H
#define L4SETUP_PROXY_PROBE_H
#include <stdbool.h>
bool setup_proxy_probe(int port, bool require_ready, int timeout_ms);
/* Optional explicit selected thumbprint, case-insensitive exact health match. */
bool setup_proxy_probe_certificate(int port,int timeout_ms,const char* thumbprint);
/* Existing local health only; returns a validated public SHA1 thumbprint. */
bool setup_proxy_probe_thumbprint(int port,int timeout_ms,char thumbprint[64]);
/* Explicit normal-production readiness, never isolated candidate health. */
bool setup_proxy_probe_policy(int port,int timeout_ms,const char* thumbprint);
#endif
