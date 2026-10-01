#include "url_finder.h"
#include "http_client.h"
#include "../../leo4proxy/src/policy_json.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Port 443 is the reverse listener, not the known local HTTP API. */
bool certificates_url_from_info(const char* text, size_t length, char* out, size_t size) {
    PolicyJson json;
    char status[32], listener[128];
    if (!out || !size || !policy_json_parse(&json, text, length) ||
        !policy_json_string(&json, policy_json_field(&json, 0, "status"), status, sizeof(status)) ||
        strcmp(status, "ready")) return false;
    int listeners = policy_json_field(&json, 0, "listeners");
    if (listeners < 0 || json.tokens[listeners].type != '{' ||
        !policy_json_string(&json, policy_json_field(&json, listeners, "http_local"), listener, sizeof(listener))) return false;

    /* Never send enrollment credentials to an arbitrary host from metadata. */
    const char* port = NULL;
    static const char* prefixes[] = { "127.0.0.1:", "localhost:", "0.0.0.0:", "[::1]:", "[::]:" };
    for (size_t i = 0; i < sizeof(prefixes) / sizeof(prefixes[0]); i++) {
        size_t n = strlen(prefixes[i]);
        if (!_strnicmp(listener, prefixes[i], n)) { port = listener + n; break; }
    }
    if (!port || !*port) return false;
    unsigned long number = 0;
    for (const char* p = port; *p; p++) {
        if (*p < '0' || *p > '9' || number > 6553) return false;
        number = number * 10 + (unsigned long)(*p - '0');
    }
    if (!number || number > 65535) return false;
    int n = snprintf(out, size, "http://127.0.0.1:%lu/api/certificates", number);
    return n > 0 && (size_t)n < size;
}

bool resolve_certificates_url(const char* cli_url_arg, char* out_url, size_t out_url_size) {
    if (!out_url || out_url_size < 32) return false;
    out_url[0] = 0;
    if (cli_url_arg && cli_url_arg[0]) {
        if (strlen(cli_url_arg) >= out_url_size) return false;
        strcpy_s(out_url, out_url_size, cli_url_arg);
        printf("[URL Finder] Using explicit endpoint.\n");
        return true;
    }
    char* response = NULL;
    size_t length = 0;
    bool resolved = http_get_simple("http://127.0.0.1:18443/_leo4/info", 400, &response, &length) &&
        certificates_url_from_info(response, length, out_url, out_url_size);
    free(response);
    if (resolved) {
        printf("[URL Finder] Using local leo4proxy certificates route.\n");
        return true;
    }
    if (strlen(DEFAULT_CERTIFICATES_URL) >= out_url_size) return false;
    strcpy_s(out_url, out_url_size, DEFAULT_CERTIFICATES_URL);
    printf("[URL Finder] Local proxy unavailable/not ready; using default certificates endpoint.\n");
    return true;
}
