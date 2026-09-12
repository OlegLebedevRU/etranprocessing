#include "proxy_client.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winhttp.h>

#pragma comment(lib, "winhttp.lib")

static bool json_get_bool_field(const char* json, const char* key, bool* out_val) {
    if (!json || !key || !out_val) return false;
    char search[128];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char* p = strstr(json, search);
    if (!p) return false;

    p += strlen(search);
    while (*p && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' || *p == ':')) p++;
    if (_strnicmp(p, "true", 4) == 0) {
        *out_val = true;
        return true;
    } else if (_strnicmp(p, "false", 5) == 0) {
        *out_val = false;
        return true;
    }
    return false;
}

static bool json_get_field(const char* json, const char* key, char* out_val, size_t out_val_size) {
    if (!json || !key || !out_val || out_val_size == 0) return false;
    out_val[0] = '\0';

    char search[128];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char* p = strstr(json, search);
    if (!p) return false;

    p += strlen(search);
    while (*p && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' || *p == ':')) p++;
    if (*p != '\"') return false;
    p++;

    size_t i = 0;
    while (*p && *p != '\"' && i + 1 < out_val_size) {
        if (*p == '\\' && *(p + 1)) {
            p++;
        }
        out_val[i++] = *p++;
    }
    out_val[i] = '\0';
    return true;
}

bool proxy_client_query_info(const wchar_t* url, int timeout_ms, Leo4ProxyInfo* out_info) {
    if (!out_info) return false;
    memset(out_info, 0, sizeof(*out_info));

    URL_COMPONENTSW urlComp;
    memset(&urlComp, 0, sizeof(urlComp));
    urlComp.dwStructSize = sizeof(urlComp);
    urlComp.dwHostNameLength = (DWORD)-1;
    urlComp.dwUrlPathLength = (DWORD)-1;

    if (!WinHttpCrackUrl(url, (DWORD)wcslen(url), 0, &urlComp)) {
        return false;
    }

    wchar_t host[128] = { 0 };
    wcsncpy_s(host, 128, urlComp.lpszHostName, urlComp.dwHostNameLength);

    wchar_t path[256] = { 0 };
    if (urlComp.dwUrlPathLength > 0) {
        wcsncpy_s(path, 256, urlComp.lpszUrlPath, urlComp.dwUrlPathLength);
    } else {
        wcscpy_s(path, 256, L"/_leo4/info");
    }

    HINTERNET hSession = WinHttpOpen(
        L"L4Superv/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0
    );
    if (!hSession) return false;

    if (timeout_ms <= 0) timeout_ms = 3000;
    WinHttpSetTimeouts(hSession, timeout_ms / 2, timeout_ms / 2, timeout_ms, timeout_ms);

    HINTERNET hConnect = WinHttpConnect(hSession, host, urlComp.nPort, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        return false;
    }

    DWORD dwFlags = (urlComp.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(
        hConnect,
        L"GET",
        path,
        NULL,
        NULL,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        dwFlags
    );
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    if (dwFlags & WINHTTP_FLAG_SECURE) {
        DWORD secFlags = SECURITY_FLAG_IGNORE_UNKNOWN_CA |
                         SECURITY_FLAG_IGNORE_CERT_CN_INVALID |
                         SECURITY_FLAG_IGNORE_CERT_DATE_INVALID;
        WinHttpSetOption(hRequest, WINHTTP_OPTION_SECURITY_FLAGS, &secFlags, sizeof(secFlags));
    }

    BOOL ok = WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    if (ok) {
        ok = WinHttpReceiveResponse(hRequest, NULL);
    }

    if (!ok) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    DWORD dwStatusCode = 0;
    DWORD dwSize = sizeof(dwStatusCode);
    WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &dwStatusCode, &dwSize, WINHTTP_NO_HEADER_INDEX);

    // Endpoint reached
    out_info->is_online = true;

    // Read response body
    char* resp_buffer = NULL;
    size_t total_len = 0;

    DWORD dwBytesAvailable = 0;
    while (WinHttpQueryDataAvailable(hRequest, &dwBytesAvailable) && dwBytesAvailable > 0) {
        char* new_buf = (char*)realloc(resp_buffer, total_len + dwBytesAvailable + 1);
        if (!new_buf) break;
        resp_buffer = new_buf;

        DWORD dwRead = 0;
        if (WinHttpReadData(hRequest, resp_buffer + total_len, dwBytesAvailable, &dwRead)) {
            total_len += dwRead;
        } else {
            break;
        }
    }

    if (resp_buffer) {
        resp_buffer[total_len] = '\0';

        json_get_field(resp_buffer, "status", out_info->status, sizeof(out_info->status));
        if (!json_get_field(resp_buffer, "sn", out_info->sn, sizeof(out_info->sn)) || out_info->sn[0] == '\0') {
            if (!json_get_field(resp_buffer, "serial_number", out_info->sn, sizeof(out_info->sn)) || out_info->sn[0] == '\0') {
                json_get_field(resp_buffer, "client_id", out_info->sn, sizeof(out_info->sn));
            }
        }
        json_get_field(resp_buffer, "thumbprint", out_info->thumbprint, sizeof(out_info->thumbprint));
        json_get_field(resp_buffer, "not_after", out_info->not_after, sizeof(out_info->not_after));

        bool bval = false;
        if (json_get_bool_field(resp_buffer, "certificate_found", &bval)) {
            out_info->certificate_found = bval;
        }

        if (strcmp(out_info->status, "ready") == 0 && out_info->sn[0] != '\0') {
            out_info->cert_ready = true;
            if (!out_info->certificate_found) {
                out_info->certificate_found = true;
            }
        }

        json_get_field(resp_buffer, "http_local", out_info->http_local, sizeof(out_info->http_local));
        json_get_field(resp_buffer, "http_remote", out_info->upstreams_http_remote, sizeof(out_info->upstreams_http_remote));

        free(resp_buffer);
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    return true;
}
