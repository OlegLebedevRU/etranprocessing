#include "json_min.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static const char* find_key_colon(const char* json, const char* key) {
    if (!json || !key) return NULL;
    size_t klen = strlen(key);

    const char* p = json;
    while ((p = strstr(p, key)) != NULL) {
        if (p > json && *(p - 1) == '"' && *(p + klen) == '"') {
            const char* colon = p + klen + 1;
            while (*colon && isspace((unsigned char)*colon)) colon++;
            if (*colon == ':') {
                return colon + 1;
            }
        }
        p += klen;
    }
    return NULL;
}

bool json_extract_str(const char* json, const char* key, char* out, size_t max_len) {
    if (!out || max_len == 0) return false;
    out[0] = '\0';

    const char* val = find_key_colon(json, key);
    if (!val) return false;

    while (*val && isspace((unsigned char)*val)) val++;
    if (*val != '"') return false;
    val++; // skip opening quote

    size_t idx = 0;
    while (*val && *val != '"') {
        if (*val == '\\' && *(val + 1)) {
            val++;
            char esc = *val;
            if (esc == 'n') esc = '\n';
            else if (esc == 'r') esc = '\r';
            else if (esc == 't') esc = '\t';
            if (idx + 1 < max_len) out[idx++] = esc;
        } else {
            if (idx + 1 < max_len) out[idx++] = *val;
        }
        val++;
    }

    out[idx] = '\0';
    return (*val == '"');
}

bool json_extract_int(const char* json, const char* key, int* out) {
    if (!out) return false;
    const char* val = find_key_colon(json, key);
    if (!val) return false;

    while (*val && isspace((unsigned char)*val)) val++;
    if (!isdigit((unsigned char)*val) && *val != '-') return false;

    char* endptr = NULL;
    long lval = strtol(val, &endptr, 10);
    if (endptr == val) return false;

    *out = (int)lval;
    return true;
}

bool json_extract_int64(const char* json, const char* key, int64_t* out) {
    if (!out) return false;
    const char* val = find_key_colon(json, key);
    if (!val) return false;

    while (*val && isspace((unsigned char)*val)) val++;
    if (!isdigit((unsigned char)*val) && *val != '-') return false;

    char* endptr = NULL;
    long long llval = strtoll(val, &endptr, 10);
    if (endptr == val) return false;

    *out = (int64_t)llval;
    return true;
}

bool json_extract_bool(const char* json, const char* key, bool* out) {
    if (!out) return false;
    const char* val = find_key_colon(json, key);
    if (!val) return false;

    while (*val && isspace((unsigned char)*val)) val++;
    if (strncmp(val, "true", 4) == 0) {
        *out = true;
        return true;
    }
    if (strncmp(val, "false", 5) == 0) {
        *out = false;
        return true;
    }
    return false;
}
