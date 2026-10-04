#ifndef LEO4_UPSTREAM_PROBE_H
#define LEO4_UPSTREAM_PROBE_H
#include "schannel_tls.h"
#define UPSTREAM_PROBE_BUDGET_MS 8000
typedef struct {
    const ProxyConfig* config;
    CredHandle* creds;
    int channel;
    bool enabled, admission, acquired;
    char verdict[32];
    Leo4Endpoint selected;
    DWORD elapsed_ms;
    int attempts;
} UpstreamProbe;
/* Independent worker budgets. Caller owns configuration/credentials until join. */
void upstream_probe_all(UpstreamProbe probes[ENDPOINT_CHANNELS]);
#endif
