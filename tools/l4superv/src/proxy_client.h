#pragma once
#include <windows.h>
#include <stdbool.h>

typedef struct {
    bool is_online;       // True if HTTP endpoint responded
    bool cert_ready;      // True if status == "ready" and SN is non-empty
    char sn[64];          // Serial number
    char thumbprint[64];  // Certificate thumbprint (hex)
    char not_after[64];   // Certificate expiration date
    char status[32];      // "ready", "waiting_for_certificate", "error", etc.
} Leo4ProxyInfo;

/**
 * Query GET http://127.0.0.1:18443/_leo4/info using WinHTTP.
 * Returns true if request succeeded, false otherwise.
 */
bool proxy_client_query_info(const wchar_t* url, int timeout_ms, Leo4ProxyInfo* out_info);
