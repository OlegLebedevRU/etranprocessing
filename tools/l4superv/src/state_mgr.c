#include "state_mgr.h"
#include "service_mgr.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <shlwapi.h>

#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "version.lib")

static void get_self_version(char* out_ver, size_t out_size) {
    if (!out_ver || out_size == 0) return;
    out_ver[0] = '\0';

    wchar_t exe_path[MAX_PATH] = { 0 };
    if (GetModuleFileNameW(NULL, exe_path, MAX_PATH) > 0) {
        DWORD handle = 0;
        DWORD size = GetFileVersionInfoSizeW(exe_path, &handle);
        if (size > 0) {
            void* block = malloc(size);
            if (block) {
                if (GetFileVersionInfoW(exe_path, handle, size, block)) {
                    struct LANGANDCODEPAGE {
                        WORD wLanguage;
                        WORD wCodePage;
                    } *lpTranslate;
                    UINT cbTranslate = 0;
                    if (VerQueryValueW(block, L"\\VarFileInfo\\Translation", (LPVOID*)&lpTranslate, &cbTranslate) &&
                        cbTranslate >= sizeof(struct LANGANDCODEPAGE)) {
                        wchar_t subBlock[128];
                        swprintf_s(subBlock, 128, L"\\StringFileInfo\\%04x%04x\\ProductVersion",
                                  lpTranslate[0].wLanguage, lpTranslate[0].wCodePage);
                        LPWSTR lpBuffer = NULL;
                        UINT dwBytes = 0;
                        if (VerQueryValueW(block, subBlock, (LPVOID*)&lpBuffer, &dwBytes) && dwBytes > 0) {
                            WideCharToMultiByte(CP_UTF8, 0, lpBuffer, -1, out_ver, (int)out_size, NULL, NULL);
                        }
                    }
                    if (out_ver[0] == '\0') {
                        VS_FIXEDFILEINFO* pFileInfo = NULL;
                        UINT uLen = 0;
                        if (VerQueryValueW(block, L"\\", (LPVOID*)&pFileInfo, &uLen) && uLen >= sizeof(VS_FIXEDFILEINFO)) {
                            snprintf(out_ver, out_size, "%u.%u.%u",
                                     HIWORD(pFileInfo->dwProductVersionMS),
                                     LOWORD(pFileInfo->dwProductVersionMS),
                                     HIWORD(pFileInfo->dwProductVersionLS));
                        }
                    }
                }
                free(block);
            }
        }
    }
    if (out_ver[0] == '\0') {
        strncpy_s(out_ver, out_size, "1.7.1", _TRUNCATE);
    }
}

static void parse_unknown_keys(const char* json, L4State* state) {
    if (!json || !state) return;

    const char* p = strchr(json, '{');
    if (!p) return;
    p++;

    while (*p) {
        while (*p && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' || *p == ',')) p++;
        if (*p == '}' || *p == '\0') break;

        if (*p != '\"') {
            while (*p && *p != ',' && *p != '}') p++;
            if (*p == ',') p++;
            continue;
        }

        p++; // skip opening quote
        const char* key_start = p;
        while (*p && *p != '\"') {
            if (*p == '\\' && *(p + 1)) p++;
            p++;
        }
        if (*p != '\"') break;

        size_t key_len = p - key_start;
        char key[64] = { 0 };
        if (key_len < sizeof(key)) {
            memcpy(key, key_start, key_len);
        }
        p++; // skip closing quote

        while (*p && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' || *p == ':')) p++;
        if (!*p || *p == '}') break;

        const char* val_start = p;
        if (*p == '\"') {
            p++;
            while (*p && *p != '\"') {
                if (*p == '\\' && *(p + 1)) p++;
                p++;
            }
            if (*p == '\"') p++;
        } else if (*p == '{') {
            int depth = 1;
            p++;
            while (*p && depth > 0) {
                if (*p == '\"') {
                    p++;
                    while (*p && *p != '\"') {
                        if (*p == '\\' && *(p + 1)) p++;
                        p++;
                    }
                    if (*p == '\"') p++;
                } else {
                    if (*p == '{') depth++;
                    else if (*p == '}') depth--;
                    p++;
                }
            }
        } else if (*p == '[') {
            int depth = 1;
            p++;
            while (*p && depth > 0) {
                if (*p == '\"') {
                    p++;
                    while (*p && *p != '\"') {
                        if (*p == '\\' && *(p + 1)) p++;
                        p++;
                    }
                    if (*p == '\"') p++;
                } else {
                    if (*p == '[') depth++;
                    else if (*p == ']') depth--;
                    p++;
                }
            }
        } else {
            while (*p && *p != ',' && *p != '}' && *p != '\r' && *p != '\n') p++;
        }

        size_t val_len = p - val_start;
        while (val_len > 0 && (val_start[val_len - 1] == ' ' || val_start[val_len - 1] == '\t' ||
                               val_start[val_len - 1] == '\r' || val_start[val_len - 1] == '\n')) {
            val_len--;
        }

        bool is_known = (_stricmp(key, "status") == 0 ||
                         _stricmp(key, "sn") == 0 ||
                         _stricmp(key, "thumbprint") == 0 ||
                         _stricmp(key, "not_after") == 0 ||
                         _stricmp(key, "hw_fingerprint") == 0 ||
                         _stricmp(key, "installer_base_path") == 0 ||
                         _stricmp(key, "services") == 0 ||
                         _stricmp(key, "last_check") == 0 ||
                         _stricmp(key, "updated_at") == 0 ||
                         _stricmp(key, "installed_version") == 0 ||
                         _stricmp(key, "installer_summary_path") == 0 ||
                         _stricmp(key, "last_cert_state") == 0);

        if (!is_known && key[0] != '\0' && val_len > 0) {
            if (state->num_unknown_keys < STATE_MAX_UNKNOWN_KEYS) {
                strcpy_s(state->unknown_keys[state->num_unknown_keys].key,
                         sizeof(state->unknown_keys[0].key), key);
                char* raw = (char*)malloc(val_len + 1);
                if (raw) {
                    memcpy(raw, val_start, val_len);
                    raw[val_len] = '\0';
                    state->unknown_keys[state->num_unknown_keys].raw_value = raw;
                    state->num_unknown_keys++;
                }
            }
        }
    }
}

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

void state_cleanup(L4State* state) {
    if (!state) return;
    for (int i = 0; i < state->num_unknown_keys; i++) {
        if (state->unknown_keys[i].raw_value) {
            free(state->unknown_keys[i].raw_value);
            state->unknown_keys[i].raw_value = NULL;
        }
        state->unknown_keys[i].key[0] = '\0';
    }
    state->num_unknown_keys = 0;
}

void state_init(L4State* state) {
    if (!state) return;
    memset(state, 0, sizeof(*state));
    strcpy_s(state->status, sizeof(state->status), "standby");
    get_self_version(state->installed_version, sizeof(state->installed_version));
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

    char existing_ver[64] = { 0 };
    if (json_get_string(buf, "installed_version", existing_ver, sizeof(existing_ver)) && existing_ver[0] != '\0') {
        strcpy_s(out_state->installed_version, sizeof(out_state->installed_version), existing_ver);
    }

    json_get_string(buf, "installer_summary_path", out_state->installer_summary_path, sizeof(out_state->installer_summary_path));
    if (out_state->installer_summary_path[0] == '\0') {
        wchar_t sum_path[MAX_PATH];
        swprintf_s(sum_path, MAX_PATH, L"%ls\\install_summary.json", base_path);
        if (PathFileExistsW(sum_path)) {
            w_to_utf8(sum_path, out_state->installer_summary_path, sizeof(out_state->installer_summary_path));
        }
    }

    json_get_string(buf, "last_cert_state", out_state->last_cert_state, sizeof(out_state->last_cert_state));

    __int64 ts = 0;
    if (json_get_int64(buf, "last_check", &ts)) {
        out_state->last_check = (time_t)ts;
    }
    if (json_get_int64(buf, "updated_at", &ts)) {
        out_state->updated_at = (time_t)ts;
    }

    parse_unknown_keys(buf, out_state);

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
    fprintf(f, "  \"installed_version\": \"%s\",\n", state->installed_version[0] ? state->installed_version : "1.6.0");
    if (state->installer_summary_path[0] != '\0') {
        fprintf(f, "  \"installer_summary_path\": "); write_json_escaped_string(f, state->installer_summary_path); fprintf(f, ",\n");
    }
    if (state->last_cert_state[0] != '\0') {
        fprintf(f, "  \"last_cert_state\": \"%s\",\n", state->last_cert_state);
    }
    fprintf(f, "  \"installer_base_path\": "); write_json_escaped_string(f, state->installer_base_path[0] != '\0' ? state->installer_base_path : "C:\\l4tools"); fprintf(f, ",\n");
    fprintf(f, "  \"services\": {\n");
    write_service_json(f, "mosquitto", &state->svc_mosquitto, false);
    write_service_json(f, "leo4proxy", &state->svc_leo4proxy, false);
    write_service_json(f, "l4con", &state->svc_l4con, false);
    write_service_json(f, "l4superv", &state->svc_l4superv, true);
    fprintf(f, "  },\n");
    fprintf(f, "  \"last_check\": %lld,\n", (long long)state->last_check);
    fprintf(f, "  \"updated_at\": %lld", (long long)state->updated_at);

    for (int i = 0; i < state->num_unknown_keys; i++) {
        if (state->unknown_keys[i].key[0] && state->unknown_keys[i].raw_value) {
            fprintf(f, ",\n  \"%s\": %s", state->unknown_keys[i].key, state->unknown_keys[i].raw_value);
        }
    }
    fprintf(f, "\n}\n");

    fclose(f);
    return true;
}
