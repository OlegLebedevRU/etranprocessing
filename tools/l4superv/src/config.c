#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <shlwapi.h>

#pragma comment(lib, "shlwapi.lib")

static void trim_whitespace(char* s) {
    if (!s) return;
    char* end = s + strlen(s) - 1;
    while (end >= s && (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n' || *end == '\"')) {
        *end = '\0';
        end--;
    }
    char* start = s;
    while (*start && (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n' || *start == '\"')) {
        start++;
    }
    if (start != s) {
        memmove(s, start, strlen(start) + 1);
    }
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

static bool json_get_int(const char* json, const char* key, int* out_val) {
    if (!json || !key || !out_val) return false;
    char search[128];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char* p = strstr(json, search);
    if (!p) return false;

    p += strlen(search);
    while (*p && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' || *p == ':')) p++;
    if (!*p) return false;

    *out_val = atoi(p);
    return true;
}

static bool json_get_bool(const char* json, const char* key, bool* out_val) {
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

void config_init_defaults(L4SupervConfig* cfg, const wchar_t* exe_path) {
    if (!cfg) return;
    memset(cfg, 0, sizeof(*cfg));

    // 1. Check environment variable %L4_TOOLS_BASE_PATH%
    wchar_t env_path[MAX_PATH] = { 0 };
    DWORD env_len = GetEnvironmentVariableW(L"L4_TOOLS_BASE_PATH", env_path, MAX_PATH);
    if (env_len > 0 && env_len < MAX_PATH) {
        wcscpy_s(cfg->base_path, MAX_PATH, env_path);
    } else if (exe_path && exe_path[0] != L'\0') {
        // Detect parent directory of tool
        wchar_t resolved[MAX_PATH] = { 0 };
        wcscpy_s(resolved, MAX_PATH, exe_path);
        PathRemoveFileSpecW(resolved); // remove filename (e.g. l4superv.exe or bin)
        
        // If inside bin, remove bin
        wchar_t* last_slash = wcsrchr(resolved, L'\\');
        if (last_slash && (_wcsicmp(last_slash + 1, L"bin") == 0 || _wcsicmp(last_slash + 1, L"x86") == 0 || _wcsicmp(last_slash + 1, L"x64") == 0)) {
            PathRemoveFileSpecW(resolved);
            last_slash = wcsrchr(resolved, L'\\');
            if (last_slash && (_wcsicmp(last_slash + 1, L"bin") == 0 || _wcsicmp(last_slash + 1, L"x86") == 0 || _wcsicmp(last_slash + 1, L"x64") == 0)) {
                PathRemoveFileSpecW(resolved);
            }
        }
        
        // If inside l4superv folder, remove l4superv to reach base tools folder
        last_slash = wcsrchr(resolved, L'\\');
        if (last_slash && (_wcsicmp(last_slash + 1, L"l4superv") == 0 || _wcsicmp(last_slash + 1, L"l4install") == 0)) {
            PathRemoveFileSpecW(resolved);
        }

        if (resolved[0] != L'\0') {
            wcscpy_s(cfg->base_path, MAX_PATH, resolved);
        } else {
            wcscpy_s(cfg->base_path, MAX_PATH, L4_DEFAULT_BASE_PATH);
        }
    } else {
        wcscpy_s(cfg->base_path, MAX_PATH, L4_DEFAULT_BASE_PATH);
    }

    swprintf_s(cfg->config_file, MAX_PATH, L"%s\\l4superv.json", cfg->base_path);
    wcscpy_s(cfg->proxy_url, 256, L4_DEFAULT_PROXY_URL);
    cfg->poll_interval_sec = L4_DEFAULT_POLL_INTERVAL_SEC;
    cfg->watchdog_interval_sec = L4_DEFAULT_WATCHDOG_INTERVAL_SEC;
    cfg->standby_poll_sec = L4_DEFAULT_STANDBY_POLL_SEC;
    cfg->pending_pin_check_sec = L4_DEFAULT_PENDING_PIN_CHECK_SEC;
    cfg->watchdog_enabled = true;
    cfg->mosquitto_port = L4_DEFAULT_MOSQUITTO_PORT;
    swprintf_s(cfg->mosquitto_template_path, MAX_PATH, L"%s\\mosquitto.conf.tmpl", cfg->base_path);
    cfg->auto_reset_on_clone = true;
    cfg->auto_start_leo4proxy = true;
    cfg->auto_start_mosquitto = true;
    cfg->auto_start_l4con = true;
    cfg->auto_start_l4desk = true;
    cfg->leo4proxy_args[0] = L'\0';
    cfg->l4con_args[0] = L'\0';
    wcscpy_s(cfg->l4desk_args, 512, L"--run --presence-interval 30");
    wcscpy_s(cfg->l4desk_mode, 64, L"user_session");
}

bool config_load_json(L4SupervConfig* cfg, const wchar_t* json_path) {
    if (!cfg || !json_path) return false;

    FILE* f = NULL;
    if (_wfopen_s(&f, json_path, L"rb") != 0 || !f) {
        return false;
    }

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (sz <= 0 || sz > 1024 * 1024) {
        fclose(f);
        return false;
    }

    char* buffer = (char*)malloc(sz + 1);
    if (!buffer) {
        fclose(f);
        return false;
    }

    size_t read_bytes = fread(buffer, 1, sz, f);
    buffer[read_bytes] = '\0';
    fclose(f);

    char val[MAX_PATH];
    if (json_get_string(buffer, "base_path", val, sizeof(val))) {
        MultiByteToWideChar(CP_UTF8, 0, val, -1, cfg->base_path, MAX_PATH);
    }
    if (json_get_string(buffer, "proxy_url", val, sizeof(val))) {
        MultiByteToWideChar(CP_UTF8, 0, val, -1, cfg->proxy_url, 256);
    }
    if (json_get_string(buffer, "proxy_info_url", val, sizeof(val))) {
        MultiByteToWideChar(CP_UTF8, 0, val, -1, cfg->proxy_url, 256);
    }
    if (json_get_string(buffer, "mosquitto_template", val, sizeof(val))) {
        MultiByteToWideChar(CP_UTF8, 0, val, -1, cfg->mosquitto_template_path, MAX_PATH);
    }

    int int_val = 0;
    if (json_get_int(buffer, "poll_interval_sec", &int_val) && int_val > 0) {
        cfg->poll_interval_sec = int_val;
    }
    if (json_get_int(buffer, "watchdog_interval_sec", &int_val) && int_val > 0) {
        cfg->watchdog_interval_sec = int_val;
    }
    if (json_get_int(buffer, "standby_poll_sec", &int_val) && int_val > 0) {
        cfg->standby_poll_sec = int_val;
    }
    if (json_get_int(buffer, "pending_pin_check_sec", &int_val) && int_val > 0) {
        cfg->pending_pin_check_sec = int_val;
    }
    if (json_get_int(buffer, "mosquitto_port", &int_val) && int_val > 0) {
        cfg->mosquitto_port = int_val;
    }

    bool bool_val = false;
    if (json_get_bool(buffer, "watchdog_enabled", &bool_val)) {
        cfg->watchdog_enabled = bool_val;
    }
    if (json_get_bool(buffer, "auto_reset_on_clone", &bool_val)) {
        cfg->auto_reset_on_clone = bool_val;
    }
    if (json_get_bool(buffer, "auto_start_leo4proxy", &bool_val)) {
        cfg->auto_start_leo4proxy = bool_val;
    }
    if (json_get_bool(buffer, "auto_start_mosquitto", &bool_val)) {
        cfg->auto_start_mosquitto = bool_val;
    }
    if (json_get_bool(buffer, "auto_start_l4con", &bool_val)) {
        cfg->auto_start_l4con = bool_val;
    }

    const char* p_desk = strstr(buffer, "\"l4desk\"");
    if (p_desk) {
        bool auto_val = false;
        if (json_get_bool(p_desk, "auto_start", &auto_val)) {
            cfg->auto_start_l4desk = auto_val;
        }
        char args_val[512];
        if (json_get_string(p_desk, "args", args_val, sizeof(args_val))) {
            MultiByteToWideChar(CP_UTF8, 0, args_val, -1, cfg->l4desk_args, 512);
        }
        char mode_val[64];
        if (json_get_string(p_desk, "mode", mode_val, sizeof(mode_val))) {
            MultiByteToWideChar(CP_UTF8, 0, mode_val, -1, cfg->l4desk_mode, 64);
        }
    }

    free(buffer);
    return true;
}

bool config_save_json(const L4SupervConfig* cfg, const wchar_t* json_path) {
    if (!cfg || !json_path) return false;

    FILE* f = NULL;
    if (_wfopen_s(&f, json_path, L"wb") != 0 || !f) {
        return false;
    }

    char utf8_base[MAX_PATH * 3] = { 0 };
    char utf8_proxy[512] = { 0 };
    char utf8_tmpl[MAX_PATH * 3] = { 0 };
    char utf8_l4desk_args[512] = { 0 };

    WideCharToMultiByte(CP_UTF8, 0, cfg->base_path, -1, utf8_base, sizeof(utf8_base), NULL, NULL);
    WideCharToMultiByte(CP_UTF8, 0, cfg->proxy_url, -1, utf8_proxy, sizeof(utf8_proxy), NULL, NULL);
    WideCharToMultiByte(CP_UTF8, 0, cfg->mosquitto_template_path, -1, utf8_tmpl, sizeof(utf8_tmpl), NULL, NULL);
    WideCharToMultiByte(CP_UTF8, 0, cfg->l4desk_args, -1, utf8_l4desk_args, sizeof(utf8_l4desk_args), NULL, NULL);

    // Escape backslashes for valid JSON
    char escaped_base[MAX_PATH * 4] = { 0 };
    size_t k = 0;
    for (size_t i = 0; utf8_base[i] && k + 2 < sizeof(escaped_base); i++) {
        if (utf8_base[i] == '\\') {
            escaped_base[k++] = '\\';
            escaped_base[k++] = '\\';
        } else {
            escaped_base[k++] = utf8_base[i];
        }
    }
    escaped_base[k] = '\0';

    fprintf(f, "{\n");
    fprintf(f, "  \"base_path\": \"%s\",\n", escaped_base);
    fprintf(f, "  \"proxy_url\": \"%s\",\n", utf8_proxy);
    fprintf(f, "  \"poll_interval_sec\": %d,\n", cfg->poll_interval_sec);
    fprintf(f, "  \"watchdog_interval_sec\": %d,\n", cfg->watchdog_interval_sec);
    fprintf(f, "  \"standby_poll_sec\": %d,\n", cfg->standby_poll_sec);
    fprintf(f, "  \"pending_pin_check_sec\": %d,\n", cfg->pending_pin_check_sec);
    fprintf(f, "  \"watchdog_enabled\": %s,\n", cfg->watchdog_enabled ? "true" : "false");
    fprintf(f, "  \"mosquitto_port\": %d,\n", cfg->mosquitto_port);
    fprintf(f, "  \"auto_reset_on_clone\": %s,\n", cfg->auto_reset_on_clone ? "true" : "false");
    fprintf(f, "  \"services\": {\n");
    fprintf(f, "    \"leo4proxy\": { \"auto_start\": %s },\n", cfg->auto_start_leo4proxy ? "true" : "false");
    fprintf(f, "    \"mosquitto\": { \"auto_start\": %s },\n", cfg->auto_start_mosquitto ? "true" : "false");
    fprintf(f, "    \"l4con\":     { \"auto_start\": %s },\n", cfg->auto_start_l4con ? "true" : "false");
    fprintf(f, "    \"l4desk\":    { \"auto_start\": %s, \"mode\": \"user_session\", \"args\": \"%s\" }\n",
            cfg->auto_start_l4desk ? "true" : "false", utf8_l4desk_args);
    fprintf(f, "  }\n");
    fprintf(f, "}\n");

    fclose(f);
    return true;
}
