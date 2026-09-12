#pragma once
#include <windows.h>
#include <stdbool.h>

typedef struct {
    bool is_online;              // True if HTTP endpoint responded
    bool cert_ready;             // True if status == "ready" and SN is non-empty
    bool certificate_found;      // Direct value of "certificate_found" from _leo4/info
    char sn[64];                 // Serial number
    char thumbprint[64];         // Certificate thumbprint (hex)
    char not_after[64];          // Certificate expiration date
    char status[32];             // "ready", "waiting_for_certificate", "error", etc.
    char http_local[128];        // listener http_local (e.g. "127.0.0.1:18443")
    char upstreams_http_remote[256]; // upstream http_remote (e.g. "https://iot-processing.ru:443")
} Leo4ProxyInfo;

/**
 * Query GET http://127.0.0.1:18443/_leo4/info using WinHTTP.
 * Returns true if request succeeded, false otherwise.
 */
bool proxy_client_query_info(const wchar_t* url, int timeout_ms, Leo4ProxyInfo* out_info);
