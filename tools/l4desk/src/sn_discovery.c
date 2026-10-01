#include "sn_discovery.h"
#include "../../l4pin/src/http_client.h"
#include "log.h"
#include <winhttp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma comment(lib, "winhttp.lib")

int sn_discovery_query_once(int proxy_port, char* out_sn, size_t out_sn_size) {
    if (!out_sn || out_sn_size == 0 || proxy_port < 1 || proxy_port > 65535) return -1;
    out_sn[0] = '\0';
    char url[128]; sprintf_s(url, sizeof(url), "http://127.0.0.1:%d/_leo4/sn", proxy_port);
    char* body = NULL; size_t size = 0;
    if (!http_get_simple(url, 1200, &body, &size)) return -1;
    while (size && (body[size-1] == '\r' || body[size-1] == '\n' || body[size-1] == ' ' || body[size-1] == '\t')) size--;
    bool valid = size > 0 && size < out_sn_size;
    for (size_t i=0; i<size; i++) {
        unsigned char ch = (unsigned char)body[i];
        if (ch <= 32 || ch >= 127 || ch == '/' || ch == '\\' || ch == '"') valid = false;
    }
    if (valid) { memcpy(out_sn, body, size); out_sn[size] = '\0'; }
    free(body); return valid ? 0 : -1;
}

bool sn_discovery_wait_for_sn(int proxy_port, char* out_sn, size_t out_sn_size, HANDLE hStopEvent) {
    if (sn_discovery_query_once(proxy_port, out_sn, out_sn_size) == 0) {
        return true;
    }

    log_info("Waiting for Leo4Proxy on port %d to obtain terminal Serial Number (SN)...", proxy_port);

    while (hStopEvent == NULL || WaitForSingleObject(hStopEvent, 2000) != WAIT_OBJECT_0) {
        if (sn_discovery_query_once(proxy_port, out_sn, out_sn_size) == 0) {
            log_info("Discovered terminal SN: %s", out_sn);
            return true;
        }
        log_debug("Proxy not ready yet, retrying in 2 seconds...");
    }

    return false;
}
