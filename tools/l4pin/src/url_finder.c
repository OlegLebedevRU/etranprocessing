#include "url_finder.h"
#include "http_client.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * Extract a string field value from simple JSON by key name.
 */
static bool parse_json_field(const char* json, const char* field_name, char* out_val, size_t out_val_size) {
    if (!json || !field_name || !out_val || out_val_size == 0) return false;
    out_val[0] = '\0';

    char search_pattern[128];
    snprintf(search_pattern, sizeof(search_pattern), "\"%s\"", field_name);
    const char* pos = strstr(json, search_pattern);
    if (!pos) return false;

    pos += strlen(search_pattern);
    // Skip whitespace and colon
    while (*pos && (*pos == ' ' || *pos == '\t' || *pos == '\r' || *pos == '\n' || *pos == ':')) {
        pos++;
    }
    if (*pos != '"') return false;
    pos++; // skip opening quote

    size_t i = 0;
    while (*pos && *pos != '"' && i + 1 < out_val_size) {
        if (*pos == '\\' && *(pos + 1)) {
            pos++; // skip escape backslash
        }
        out_val[i++] = *pos++;
    }
    out_val[i] = '\0';
    return (i > 0);
}

/**
 * Normalize host:port string (replace 0.0.0.0 or [::] with 127.0.0.1).
 */
static void normalize_host_port(const char* in_hp, char* out_hp, size_t out_hp_size) {
    if (!in_hp || !out_hp || out_hp_size == 0) return;
    out_hp[0] = '\0';

    const char* p = in_hp;
    // Strip leading "http://" or "https://" if present
    if (_strnicmp(p, "http://", 7) == 0) p += 7;
    else if (_strnicmp(p, "https://", 8) == 0) p += 8;

    if (_strnicmp(p, "0.0.0.0:", 8) == 0) {
        snprintf(out_hp, out_hp_size, "127.0.0.1:%s", p + 8);
    } else if (_strnicmp(p, "[::]:", 5) == 0) {
        snprintf(out_hp, out_hp_size, "127.0.0.1:%s", p + 5);
    } else if (p[0] == ':') {
        snprintf(out_hp, out_hp_size, "127.0.0.1%s", p);
    } else {
        strncpy(out_hp, p, out_hp_size - 1);
        out_hp[out_hp_size - 1] = '\0';
    }
}

bool resolve_certificates_url(const char* cli_url_arg, char* out_url, size_t out_url_size) {
    if (!out_url || out_url_size < 32) return false;
    out_url[0] = '\0';

    // -----------------------------------------------------------------------
    // Strategy Step 1: Explicit CLI Argument
    // -----------------------------------------------------------------------
    if (cli_url_arg && cli_url_arg[0] != '\0') {
        strncpy(out_url, cli_url_arg, out_url_size - 1);
        out_url[out_url_size - 1] = '\0';
        printf("[URL Finder] Strategy: explicit CLI argument -> '%s'\n", out_url);
        return true;
    }

    // -----------------------------------------------------------------------
    // Strategy Step 2: Auto-discovery via leo4proxy
    // -----------------------------------------------------------------------
    printf("[URL Finder] Strategy: querying local leo4proxy metadata...\n");

    const char* probe_endpoints[] = {
        "https://127.0.0.1/_leo4/info?format=json",
        "http://127.0.0.1:18443/_leo4/info?format=json",
        "http://127.0.0.1:18443/_leo4/info"
    };
    const size_t num_probes = sizeof(probe_endpoints) / sizeof(probe_endpoints[0]);

    char* json_resp = NULL;
    size_t json_len = 0;
    const char* successful_probe = NULL;

    for (size_t i = 0; i < num_probes; i++) {
        // Fast probe with 1200ms timeout
        if (http_get_simple(probe_endpoints[i], 1200, &json_resp, &json_len) && json_resp && json_len > 0) {
            successful_probe = probe_endpoints[i];
            break;
        }
        if (json_resp) {
            free(json_resp);
            json_resp = NULL;
        }
    }

    if (json_resp && successful_probe) {
        char raw_http_local[128] = { 0 };
        if (parse_json_field(json_resp, "http_local", raw_http_local, sizeof(raw_http_local))) {
            char norm_http_local[128] = { 0 };
            normalize_host_port(raw_http_local, norm_http_local, sizeof(norm_http_local));

            if (norm_http_local[0] != '\0') {
                printf("[URL Finder] Discovered leo4proxy at %s (listener: %s)\n",
                       successful_probe, norm_http_local);

                // Check whether HTTPS or HTTP works for http_local
                char test_https_url[256];
                snprintf(test_https_url, sizeof(test_https_url), "https://%s/_leo4/info", norm_http_local);

                char* test_resp = NULL;
                size_t test_len = 0;
                bool https_ok = http_get_simple(test_https_url, 1000, &test_resp, &test_len);
                if (test_resp) free(test_resp);

                if (https_ok) {
                    snprintf(out_url, out_url_size, "https://%s/api/certificates", norm_http_local);
                } else {
                    // Try HTTP
                    snprintf(out_url, out_url_size, "http://%s/api/certificates", norm_http_local);
                }

                printf("[URL Finder] Resolved base URL via leo4proxy -> '%s'\n", out_url);
                free(json_resp);
                return true;
            }
        }
        free(json_resp);
    }

    // -----------------------------------------------------------------------
    // Strategy Step 3: Default Fallback URL
    // -----------------------------------------------------------------------
    strncpy(out_url, DEFAULT_CERTIFICATES_URL, out_url_size - 1);
    out_url[out_url_size - 1] = '\0';
    printf("[URL Finder] Strategy: default fallback -> '%s'\n", out_url);
    return true;
}
