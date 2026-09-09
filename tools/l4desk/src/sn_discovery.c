#include "sn_discovery.h"
#include "log.h"
#include <winhttp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma comment(lib, "winhttp.lib")

int sn_discovery_query_once(int proxy_port, char* out_sn, size_t out_sn_size) {
    if (!out_sn || out_sn_size == 0) return -1;
    out_sn[0] = '\0';

    HINTERNET hSession = WinHttpOpen(L"L4Desk/1.0",
                                    WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                    WINHTTP_NO_PROXY_NAME,
                                    WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return -1;

    WinHttpSetTimeouts(hSession, 1000, 1000, 2000, 2000);

    HINTERNET hConnect = WinHttpConnect(hSession, L"127.0.0.1", (INTERNET_PORT)proxy_port, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        return -1;
    }

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", L"/_leo4/sn",
                                           NULL, WINHTTP_NO_REFERER,
                                           WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return -1;
    }

    int rc = -1;
    if (WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                           WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(hRequest, NULL)) {

        DWORD dwStatusCode = 0;
        DWORD dwStatusSize = sizeof(dwStatusCode);
        if (WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                WINHTTP_HEADER_NAME_BY_INDEX, &dwStatusCode, &dwStatusSize, WINHTTP_NO_HEADER_INDEX)) {
            if (dwStatusCode == 200) {
                DWORD dwSize = 0;
                WinHttpQueryDataAvailable(hRequest, &dwSize);
                if (dwSize > 0 && dwSize < out_sn_size) {
                    DWORD dwDownloaded = 0;
                    if (WinHttpReadData(hRequest, out_sn, dwSize, &dwDownloaded)) {
                        out_sn[dwDownloaded] = '\0';
                        for (int i = (int)dwDownloaded - 1; i >= 0; i--) {
                            if (out_sn[i] == '\r' || out_sn[i] == '\n' || out_sn[i] == ' ' || out_sn[i] == '\t') {
                                out_sn[i] = '\0';
                            } else {
                                break;
                            }
                        }
                        if (strlen(out_sn) > 0) {
                            rc = 0;
                        }
                    }
                }
            }
        }
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return rc;
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
