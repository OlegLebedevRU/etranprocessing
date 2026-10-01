#include "proxy_client.h"
#include "../../l4pin/src/http_client.h"
#include "../../leo4proxy/src/policy_json.h"
#include <stdlib.h>
#include <string.h>

bool proxy_client_query_info(const wchar_t* url, int timeout_ms, Leo4ProxyInfo* out_info) {
    if (!url || !out_info) return false;
    memset(out_info, 0, sizeof(*out_info));
    char address[1024];
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, url, -1, address, sizeof(address), NULL, NULL)) return false;
    char* response = NULL;
    size_t length = 0;
    if (!http_get_simple(address, timeout_ms > 0 ? timeout_ms : 3000, &response, &length)) return false;
    PolicyJson json;
    bool ok = policy_json_parse(&json, response, length) &&
        policy_json_string(&json, policy_json_field(&json, 0, "status"), out_info->status, sizeof(out_info->status)) &&
        policy_json_bool(&json, policy_json_field(&json, 0, "certificate_found"), &out_info->certificate_found);
    if (ok) {
        bool ready = !strcmp(out_info->status, "ready");
        if (ready) {
            ok = out_info->certificate_found &&
                policy_json_string(&json, policy_json_field(&json, 0, "sn"), out_info->sn, sizeof(out_info->sn)) && out_info->sn[0] &&
                policy_json_string(&json, policy_json_field(&json, 0, "thumbprint"), out_info->thumbprint, sizeof(out_info->thumbprint)) &&
                strlen(out_info->thumbprint) == 40;
            for (size_t i = 0; ok && i < 40; i++) {
                char c = out_info->thumbprint[i];
                if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) ok = false;
            }
            out_info->cert_ready = ok;
        } else if (strcmp(out_info->status, "waiting_for_certificate")) ok = false;
        policy_json_string(&json, policy_json_field(&json, 0, "not_after"), out_info->not_after, sizeof(out_info->not_after));
        int listeners = policy_json_field(&json, 0, "listeners");
        if (listeners >= 0) policy_json_string(&json, policy_json_field(&json, listeners, "http_local"), out_info->http_local, sizeof(out_info->http_local));
        int upstreams = policy_json_field(&json, 0, "upstreams");
        if (upstreams >= 0) policy_json_string(&json, policy_json_field(&json, upstreams, "http_remote"), out_info->upstreams_http_remote, sizeof(out_info->upstreams_http_remote));
    }
    free(response);
    out_info->is_online = ok;
    return ok;
}
