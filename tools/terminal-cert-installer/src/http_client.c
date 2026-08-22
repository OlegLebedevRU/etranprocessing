#include "http_client.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winhttp.h>
#include <wincrypt.h>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "crypt32.lib")

#define USER_AGENT L"EtranTerminalCertInstaller/1.0"

bool generate_tosign(char* out_tosign, size_t out_tosign_size) {
    if (!out_tosign || out_tosign_size < 32) return false;

    char computer_name[MAX_COMPUTERNAME_LENGTH + 1] = "TERMINAL";
    DWORD comp_len = sizeof(computer_name);
    GetComputerNameA(computer_name, &comp_len);

    DWORD tick = GetTickCount();
    SYSTEMTIME st;
    GetSystemTime(&st);

    char raw_info[256];
    snprintf(raw_info, sizeof(raw_info), "%s_%04d%02d%02d_%02d%02d%02d_%lu",
             computer_name, st.wYear, st.wMonth, st.wDay,
             st.wHour, st.wMinute, st.wSecond, tick);

    DWORD b64_len = 0;
    if (!CryptBinaryToStringA((const BYTE*)raw_info, (DWORD)strlen(raw_info),
                              CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, NULL, &b64_len)) {
        return false;
    }

    if (b64_len > out_tosign_size) return false;

    return CryptBinaryToStringA((const BYTE*)raw_info, (DWORD)strlen(raw_info),
                                CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, out_tosign, &b64_len);
}

typedef struct {
    WCHAR host[256];
    INTERNET_PORT port;
    WCHAR path[512];
    BOOL is_https;
} ParsedUrl;

static bool parse_url(const char* url_str, ParsedUrl* parsed) {
    if (!url_str || !parsed) return false;
    memset(parsed, 0, sizeof(ParsedUrl));

    WCHAR w_url[1024];
    MultiByteToWideChar(CP_UTF8, 0, url_str, -1, w_url, 1024);

    URL_COMPONENTS urlComp;
    memset(&urlComp, 0, sizeof(urlComp));
    urlComp.dwStructSize = sizeof(urlComp);
    urlComp.dwHostNameLength = (DWORD)-1;
    urlComp.dwUrlPathLength = (DWORD)-1;
    urlComp.dwExtraInfoLength = (DWORD)-1;

    if (!WinHttpCrackUrl(w_url, (DWORD)wcslen(w_url), 0, &urlComp)) {
        return false;
    }

    wcsncpy(parsed->host, urlComp.lpszHostName, urlComp.dwHostNameLength);
    parsed->host[urlComp.dwHostNameLength] = L'\0';

    parsed->port = urlComp.nPort;
    parsed->is_https = (urlComp.nScheme == INTERNET_SCHEME_HTTPS);

    if (urlComp.dwUrlPathLength > 0) {
        wcsncpy(parsed->path, urlComp.lpszUrlPath, urlComp.dwUrlPathLength);
        parsed->path[urlComp.dwUrlPathLength] = L'\0';
    } else {
        wcscpy(parsed->path, L"/");
    }

    // If path does not contain "/api/certificates" and does not contain "/certificates" and does not end with ".ashx", append default
    if (wcsstr(parsed->path, L"/api/certificates") == NULL &&
        wcsstr(parsed->path, L"/certificates") == NULL &&
        wcsstr(parsed->path, L"Dispatcher.ashx") == NULL) {
        // Remove trailing slash if any
        size_t plen = wcslen(parsed->path);
        if (plen > 0 && parsed->path[plen - 1] == L'/') {
            parsed->path[plen - 1] = L'\0';
        }
        wcscat(parsed->path, L"/certificates/Dispatcher.ashx");
    }

    return true;
}

static bool send_http_request(
    const ParsedUrl* parsed,
    const WCHAR* verb,
    const WCHAR* query_and_path,
    const char* post_body,
    char** out_response,
    size_t* out_response_len
) {
    if (out_response) *out_response = NULL;
    if (out_response_len) *out_response_len = 0;

    HINTERNET hSession = WinHttpOpen(
        USER_AGENT,
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0
    );
    if (!hSession) {
        fprintf(stderr, "WinHttpOpen failed: %lu\n", GetLastError());
        return false;
    }

    HINTERNET hConnect = WinHttpConnect(hSession, parsed->host, parsed->port, 0);
    if (!hConnect) {
        fprintf(stderr, "WinHttpConnect failed: %lu\n", GetLastError());
        WinHttpCloseHandle(hSession);
        return false;
    }

    DWORD req_flags = parsed->is_https ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(
        hConnect,
        verb,
        query_and_path,
        NULL,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        req_flags
    );
    if (!hRequest) {
        fprintf(stderr, "WinHttpOpenRequest failed: %lu\n", GetLastError());
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    if (parsed->is_https) {
        // Disable SSL/TLS certificate validation as requested
        DWORD sec_flags = SECURITY_FLAG_IGNORE_UNKNOWN_CA |
                          SECURITY_FLAG_IGNORE_CERT_CN_INVALID |
                          SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
                          SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
        WinHttpSetOption(hRequest, WINHTTP_OPTION_SECURITY_FLAGS, &sec_flags, sizeof(sec_flags));
    }

    // Set headers if POST
    const WCHAR* headers = NULL;
    DWORD headers_len = 0;
    if (post_body) {
        headers = L"Content-Type: text/plain; charset=utf-8\r\n";
        headers_len = (DWORD)wcslen(headers);
    }

    DWORD body_len = post_body ? (DWORD)strlen(post_body) : 0;
    BOOL bSend = WinHttpSendRequest(
        hRequest,
        headers,
        headers_len,
        (LPVOID)post_body,
        body_len,
        body_len,
        0
    );
    if (!bSend && GetLastError() == ERROR_WINHTTP_CLIENT_AUTH_CERT_NEEDED) {
        // Server requested client certificate; send anonymous token (no client cert during enrollment)
        WinHttpSetOption(hRequest, WINHTTP_OPTION_CLIENT_CERT_CONTEXT, WINHTTP_NO_CLIENT_CERT_CONTEXT, 0);
        bSend = WinHttpSendRequest(
            hRequest,
            headers,
            headers_len,
            (LPVOID)post_body,
            body_len,
            body_len,
            0
        );
    }
    if (!bSend) {
        fprintf(stderr, "WinHttpSendRequest failed: %lu\n", GetLastError());
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    if (!WinHttpReceiveResponse(hRequest, NULL)) {
        fprintf(stderr, "WinHttpReceiveResponse failed: %lu\n", GetLastError());
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    DWORD status_code = 0;
    DWORD status_code_size = sizeof(status_code);
    WinHttpQueryHeaders(
        hRequest,
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX,
        &status_code,
        &status_code_size,
        WINHTTP_NO_HEADER_INDEX
    );

    if (status_code != 200) {
        fprintf(stderr, "HTTP error status: %lu\n", status_code);
    }

    // Read response body
    size_t capacity = 4096;
    size_t total_read = 0;
    char* resp_buf = (char*)malloc(capacity);
    if (!resp_buf) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    DWORD bytes_available = 0;
    while (WinHttpQueryDataAvailable(hRequest, &bytes_available) && bytes_available > 0) {
        if (total_read + bytes_available + 1 > capacity) {
            capacity = (total_read + bytes_available + 1) * 2;
            char* new_buf = (char*)realloc(resp_buf, capacity);
            if (!new_buf) {
                free(resp_buf);
                WinHttpCloseHandle(hRequest);
                WinHttpCloseHandle(hConnect);
                WinHttpCloseHandle(hSession);
                return false;
            }
            resp_buf = new_buf;
        }

        DWORD bytes_read = 0;
        if (WinHttpReadData(hRequest, resp_buf + total_read, bytes_available, &bytes_read)) {
            total_read += bytes_read;
        } else {
            break;
        }
    }

    resp_buf[total_read] = '\0';

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    if (out_response) *out_response = resp_buf;
    else free(resp_buf);

    if (out_response_len) *out_response_len = total_read;

    return (status_code == 200 || total_read > 0);
}

bool http_check(
    const char* base_url,
    const char* pin,
    const char* tosign,
    const char* v,
    char** out_response,
    size_t* out_response_len
) {
    ParsedUrl parsed;
    if (!parse_url(base_url, &parsed)) {
        fprintf(stderr, "Failed to parse URL: %s\n", base_url);
        return false;
    }

    WCHAR query_and_path[1024];
    WCHAR w_pin[64] = { 0 };
    WCHAR w_tosign[256] = { 0 };
    WCHAR w_v[32] = { 0 };

    MultiByteToWideChar(CP_UTF8, 0, pin, -1, w_pin, 64);
    MultiByteToWideChar(CP_UTF8, 0, tosign ? tosign : "", -1, w_tosign, 256);
    MultiByteToWideChar(CP_UTF8, 0, v ? v : "26", -1, w_v, 32);

    // Format: /api/certificates?function=check&pin=...&tosign=...&v=...
    _snwprintf(query_and_path, sizeof(query_and_path) / sizeof(WCHAR),
               L"%s?function=check&pin=%s&tosign=%s&v=%s",
               parsed.path, w_pin, w_tosign, w_v);

    return send_http_request(&parsed, L"GET", query_and_path, NULL, out_response, out_response_len);
}

bool http_setup(
    const char* base_url,
    const char* pin,
    const char* sign,
    const char* pkcs10_b64,
    char** out_response,
    size_t* out_response_len
) {
    ParsedUrl parsed;
    if (!parse_url(base_url, &parsed)) {
        fprintf(stderr, "Failed to parse URL: %s\n", base_url);
        return false;
    }

    WCHAR query_and_path[1024];
    WCHAR w_pin[64] = { 0 };
    WCHAR w_sign[128] = { 0 };

    MultiByteToWideChar(CP_UTF8, 0, pin, -1, w_pin, 64);
    MultiByteToWideChar(CP_UTF8, 0, sign ? sign : "", -1, w_sign, 128);

    // Format: /api/certificates?function=setup&pin=...&cpserial=...
    _snwprintf(query_and_path, sizeof(query_and_path) / sizeof(WCHAR),
               L"%s?function=setup&pin=%s&cpserial=%s",
               parsed.path, w_pin, w_sign);

    return send_http_request(&parsed, L"POST", query_and_path, pkcs10_b64, out_response, out_response_len);
}
