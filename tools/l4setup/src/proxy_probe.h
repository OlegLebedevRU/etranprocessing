#ifndef L4SETUP_PROXY_PROBE_H
#define L4SETUP_PROXY_PROBE_H
#include <stdbool.h>
bool setup_proxy_probe(int port, bool require_ready, int timeout_ms);
#endif
