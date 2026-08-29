#include "state_mgr.h"
#include "service_mgr.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void w_to_utf8(const wchar_t* wstr, char* out_buf, size_t out_size) {
    if (!out_buf || out_size == 0) return;
    out_buf[0] = '\0';
    if (!wstr) return;
    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, out_buf, (int)out_size, NULL, NULL);
}

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

void state_update_services(const wchar_t* base_path, L4State* state) {
    if (!base_path || !state) return;

    w_to_utf8(base_path, state->installer_base_path, sizeof(state->installer_base_path));

    svc_inspect(SVC_NAME_MOSQUITTO, base_path,
                L"mosquitto\\mosquitto.exe",
                L"mosquitto\\mosquitto.conf",
                L"mosquitto\\log\\mosquitto.log",
                &state->svc_mosquitto);

    svc_inspect(SVC_NAME_LEO4PROXY, base_path,
                L"leo4proxy\\leo4proxy.exe",
                NULL, NULL,
                &state->svc_leo4proxy);

    svc_inspect(SVC_NAME_L4CON, base_path,
                L"l4con\\l4con.exe",
                NULL, NULL,
                &state->svc_l4con);

    svc_inspect(SVC_NAME_L4SUPERV, base_path,
                L"l4superv\\l4superv.exe",
                L"l4superv.json", NULL,
                &state->svc_l4superv);
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
    json_get_string(buf, "installer_base_path", out_state->installer_base_path, sizeof(out_state->installer_base_path));

    __int64 ts = 0;
    if (json_get_int64(buf, "last_check", &ts)) {
        out_state->last_check = (time_t)ts;
    }
    if (json_get_int64(buf, "updated_at", &ts)) {
        out_state->updated_at = (time_t)ts;
    }

    free(buf);
    state_update_services(base_path, out_state);
    return true;
}

static void write_json_escaped_string(FILE* f, const char* str) {
    if (!str) {
        fputs("\"\"", f);
        return;
    }
    fputc('\"', f);
    for (const char* p = str; *p; p++) {
        if (*p == '\\') {
            fputs("\\\\", f);
        } else if (*p == '\"') {
            fputs("\\\"", f);
        } else {
            fputc(*p, f);
        }
    }
    fputc('\"', f);
}

static void write_service_json(FILE* f, const char* name, const L4ServiceState* s, bool is_last) {
    fprintf(f, "    \"%s\": {\n", name);
    fprintf(f, "      \"installed_path\": "); write_json_escaped_string(f, s->installed_path); fprintf(f, ",\n");
    fprintf(f, "      \"runtime_pid\": %lu,\n", s->runtime_pid);
    fprintf(f, "      \"runtime_exe\": "); write_json_escaped_string(f, s->runtime_exe); fprintf(f, ",\n");
    if (s->config_path[0] != '\0') {
        fprintf(f, "      \"config_path\": "); write_json_escaped_string(f, s->config_path); fprintf(f, ",\n");
    }
    if (s->log_path[0] != '\0') {
        fprintf(f, "      \"log_path\": "); write_json_escaped_string(f, s->log_path); fprintf(f, ",\n");
    }
    fprintf(f, "      \"path_match\": %s,\n", s->path_match ? "true" : "false");
    fprintf(f, "      \"status\": \"%s\"\n", s->status);
    fprintf(f, "    }%s\n", is_last ? "" : ",");
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
    fprintf(f, "  \"installer_base_path\": "); write_json_escaped_string(f, state->installer_base_path[0] != '\0' ? state->installer_base_path : "C:\\l4tools"); fprintf(f, ",\n");
    fprintf(f, "  \"services\": {\n");
    write_service_json(f, "mosquitto", &state->svc_mosquitto, false);
    write_service_json(f, "leo4proxy", &state->svc_leo4proxy, false);
    write_service_json(f, "l4con", &state->svc_l4con, false);
    write_service_json(f, "l4superv", &state->svc_l4superv, true);
    fprintf(f, "  },\n");
    fprintf(f, "  \"last_check\": %lld,\n", (long long)state->last_check);
    fprintf(f, "  \"updated_at\": %lld\n", (long long)state->updated_at);
    fprintf(f, "}\n");

    fclose(f);
    return true;
}
