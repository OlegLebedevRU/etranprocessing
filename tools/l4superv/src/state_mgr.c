#include "state_mgr.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool json_get_string(const char* json, const char* key, char* out_val, size_t out_val_size) {
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

static bool json_get_int64(const char* json, const char* key, __int64* out_val) {
    if (!json || !key || !out_val) return false;
    char search[128];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char* p = strstr(json, search);
    if (!p) return false;

    p += strlen(search);
    while (*p && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' || *p == ':')) p++;
    if (!*p) return false;

    *out_val = _atoi64(p);
    return true;
}

void state_init(L4State* state) {
    if (!state) return;
    memset(state, 0, sizeof(*state));
    strcpy_s(state->status, sizeof(state->status), "standby");
    state->last_check = 0;
    state->updated_at = time(NULL);
}

bool state_load(const wchar_t* base_path, L4State* out_state) {
    if (!base_path || !out_state) return false;
    state_init(out_state);

    wchar_t state_file[MAX_PATH];
    swprintf_s(state_file, MAX_PATH, L"%s\\state.json", base_path);

    FILE* f = NULL;
    if (_wfopen_s(&f, state_file, L"rb") != 0 || !f) {
        return false;
    }

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (sz <= 0 || sz > 1024 * 1024) {
        fclose(f);
        return false;
    }

    char* buf = (char*)malloc(sz + 1);
    if (!buf) {
        fclose(f);
        return false;
    }

    size_t read_bytes = fread(buf, 1, sz, f);
    buf[read_bytes] = '\0';
    fclose(f);

    json_get_string(buf, "status", out_state->status, sizeof(out_state->status));
    json_get_string(buf, "sn", out_state->sn, sizeof(out_state->sn));
    json_get_string(buf, "thumbprint", out_state->thumbprint, sizeof(out_state->thumbprint));
    json_get_string(buf, "not_after", out_state->not_after, sizeof(out_state->not_after));
    json_get_string(buf, "hw_fingerprint", out_state->hw_fingerprint, sizeof(out_state->hw_fingerprint));

    __int64 ts = 0;
    if (json_get_int64(buf, "last_check", &ts)) {
        out_state->last_check = (time_t)ts;
    }
    if (json_get_int64(buf, "updated_at", &ts)) {
        out_state->updated_at = (time_t)ts;
    }

    free(buf);
    return true;
}

bool state_save(const wchar_t* base_path, const L4State* state) {
    if (!base_path || !state) return false;

    wchar_t state_file[MAX_PATH];
    swprintf_s(state_file, MAX_PATH, L"%s\\state.json", base_path);

    FILE* f = NULL;
    if (_wfopen_s(&f, state_file, L"wb") != 0 || !f) {
        return false;
    }

    fprintf(f, "{\n");
    fprintf(f, "  \"status\": \"%s\",\n", state->status);
    fprintf(f, "  \"sn\": \"%s\",\n", state->sn);
    fprintf(f, "  \"thumbprint\": \"%s\",\n", state->thumbprint);
    fprintf(f, "  \"not_after\": \"%s\",\n", state->not_after);
    fprintf(f, "  \"hw_fingerprint\": \"%s\",\n", state->hw_fingerprint);
    fprintf(f, "  \"last_check\": %lld,\n", (long long)state->last_check);
    fprintf(f, "  \"updated_at\": %lld\n", (long long)state->updated_at);
    fprintf(f, "}\n");

    fclose(f);
    return true;
}
