#include "summary.h"
#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void summary_add_warning(InstallSummaryData* data, const char* warn) {
    if (!data || !warn || data->warnings_count >= MAX_WARNINGS_COUNT) return;
    for (int i = 0; i < data->warnings_count; i++) {
        if (strcmp(data->warnings[i], warn) == 0) return;
    }
    strncpy_s(data->warnings[data->warnings_count++], 64, warn, _TRUNCATE);
}

static void escape_json_path(const wchar_t* in_path, char* out_buf, size_t out_size) {
    if (!in_path || !out_buf || out_size == 0) return;
    out_buf[0] = '\0';

    char utf8_path[MAX_PATH * 3] = { 0 };
    WideCharToMultiByte(CP_UTF8, 0, in_path, -1, utf8_path, sizeof(utf8_path), NULL, NULL);

    size_t out_idx = 0;
    for (size_t i = 0; utf8_path[i] && out_idx + 2 < out_size; i++) {
        if (utf8_path[i] == '\\') {
            out_buf[out_idx++] = '\\';
            out_buf[out_idx++] = '\\';
        } else {
            out_buf[out_idx++] = utf8_path[i];
        }
    }
    out_buf[out_idx] = '\0';
}

static void format_not_after_iso(const char* in_str, char* out_str, size_t out_size) {
    if (!in_str || !out_str || out_size == 0) return;
    out_str[0] = '\0';

    // Format: "YYYY-MM-DD HH:MM:SS UTC" -> "YYYY-MM-DDTHH:MM:SSZ"
    if (strlen(in_str) >= 19 && in_str[10] == ' ') {
        snprintf(out_str, out_size, "%.10sT%.8sZ", in_str, in_str + 11);
    } else {
        strncpy_s(out_str, out_size, in_str, _TRUNCATE);
    }
}

bool summary_write_json(const InstallSummaryData* data, const wchar_t* dest_dir) {
    if (!data || !dest_dir) return false;

    wchar_t tmp_path[MAX_PATH];
    wchar_t final_path[MAX_PATH];
    swprintf_s(tmp_path, MAX_PATH, L"%ls\\install_summary.json.tmp", dest_dir);
    swprintf_s(final_path, MAX_PATH, L"%ls\\install_summary.json", dest_dir);

    FILE* fp = NULL;
    if (_wfopen_s(&fp, tmp_path, L"wb") != 0 || !fp) {
        log_err("Failed to open %ls for writing summary", tmp_path);
        return false;
    }

    SYSTEMTIME st;
    GetSystemTime(&st);
    char ts[32];
    snprintf(ts, sizeof(ts), "%04u-%02u-%02uT%02u:%02u:%02uZ",
             st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

    char dest_escaped[MAX_PATH * 4] = { 0 };
    escape_json_path(data->dest[0] ? data->dest : dest_dir, dest_escaped, sizeof(dest_escaped));

    char not_after_iso[64] = { 0 };
    format_not_after_iso(data->cert.not_after, not_after_iso, sizeof(not_after_iso));

    char log_escaped[MAX_PATH * 4] = { 0 };
    escape_json_path(data->log_path, log_escaped, sizeof(log_escaped));

    fprintf(fp, "{\n");
    fprintf(fp, "  \"schema\": 1,\n");
    fprintf(fp, "  \"timestamp\": \"%s\",\n", ts);
    fprintf(fp, "  \"installer_version\": \"%s\",\n", data->installer_version);
    fprintf(fp, "  \"installed_version\": \"%s\",\n", data->installed_version[0] ? data->installed_version : data->installer_version);
    fprintf(fp, "  \"os\": \"%s\",\n", data->os);
    fprintf(fp, "  \"target_arch\": \"%s\",\n", data->target_arch);
    fprintf(fp, "  \"dest\": \"%s\",\n", dest_escaped);
    fprintf(fp, "  \"status\": \"%s\",\n", data->status);
    fprintf(fp, "  \"exit_code\": %d,\n", data->exit_code);
    fprintf(fp, "  \"phase\": \"%s\",\n", data->phase[0] ? data->phase : "finish");
    fprintf(fp, "  \"error_reason\": \"%s\",\n", data->error_reason);
    fprintf(fp, "  \"rollback\": \"%s\",\n", data->rollback[0] ? data->rollback : "none");
    fprintf(fp, "  \"log_path\": \"%s\",\n", log_escaped);

    // services
    fprintf(fp, "  \"services\": {\n");
    fprintf(fp, "    \"leo4proxy\": \"%s\",\n", data->service_leo4proxy[0] ? data->service_leo4proxy : "running");
    fprintf(fp, "    \"mosquitto\": \"%s\",\n", data->service_mosquitto[0] ? data->service_mosquitto : "running");
    fprintf(fp, "    \"l4con\": \"%s\",\n", data->service_l4con[0] ? data->service_l4con : "running");
    fprintf(fp, "    \"l4superv\": \"%s\"\n", data->service_l4superv[0] ? data->service_l4superv : "running");
    fprintf(fp, "  },\n");

    // cert
    fprintf(fp, "  \"cert\": {\n");
    fprintf(fp, "    \"state\": \"%s\",\n", cert_state_to_str(data->cert.state));
    fprintf(fp, "    \"reused\": %s,\n", data->cert.reused ? "true" : "false");
    fprintf(fp, "    \"reissued\": %s,\n", data->cert.reissued ? "true" : "false");
    fprintf(fp, "    \"thumbprint\": \"%s\",\n", data->cert.thumbprint);
    fprintf(fp, "    \"sn\": \"%s\",\n", data->cert.sn);
    fprintf(fp, "    \"not_after\": \"%s\"\n", not_after_iso);
    fprintf(fp, "  },\n");

    // drainage
    fprintf(fp, "  \"drainage\": {\n");
    fprintf(fp, "    \"services_stopped\": [");
    for (int i = 0; i < data->drainage.services_stopped_count; i++) {
        fprintf(fp, "%s\"%s\"", (i > 0 ? ", " : ""), data->drainage.services_stopped[i]);
    }
    fprintf(fp, "],\n");

    fprintf(fp, "    \"processes_killed\": [");
    for (int i = 0; i < data->drainage.processes_killed_count; i++) {
        fprintf(fp, "%s\"%s\"", (i > 0 ? ", " : ""), data->drainage.processes_killed[i]);
    }
    fprintf(fp, "],\n");

    fprintf(fp, "    \"ports_freed\": [");
    for (int i = 0; i < data->drainage.ports_freed_count; i++) {
        fprintf(fp, "%s%d", (i > 0 ? ", " : ""), data->drainage.ports_freed[i]);
    }
    fprintf(fp, "]\n");
    fprintf(fp, "  },\n");

    // probes
    fprintf(fp, "  \"probes\": {\n");
    fprintf(fp, "    \"proxy_info\": \"%s\",\n", data->probes.proxy_info[0] ? data->probes.proxy_info : "ok");
    fprintf(fp, "    \"mosquitto_port\": \"%s\",\n", data->probes.mosquitto_port[0] ? data->probes.mosquitto_port : "ok");
    fprintf(fp, "    \"user_session_id\": %d,\n", data->probes.user_session_id);
    fprintf(fp, "    \"l4desk_running\": %s,\n", data->probes.l4desk_running ? "true" : "false");
    fprintf(fp, "    \"ffmpeg_smoke_capture\": \"%s\",\n", data->probes.ffmpeg_smoke_capture[0] ? data->probes.ffmpeg_smoke_capture : "ok");
    fprintf(fp, "    \"desktop_locked\": %s,\n", data->probes.desktop_locked ? "true" : "false");
    fprintf(fp, "    \"network\": \"%s\",\n", data->probes.network[0] ? data->probes.network : "reachable");
    fprintf(fp, "    \"remote_input\": \"%s\"\n", data->probes.remote_input[0] ? data->probes.remote_input : "available");
    fprintf(fp, "  },\n");

    // warnings
    fprintf(fp, "  \"warnings\": [");
    for (int i = 0; i < data->warnings_count; i++) {
        fprintf(fp, "%s\"%s\"", (i > 0 ? ", " : ""), data->warnings[i]);
    }
    fprintf(fp, "]\n");

    fprintf(fp, "}\n");
    fflush(fp);
    fclose(fp);

    if (!MoveFileExW(tmp_path, final_path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        log_err("Failed to atomically replace install_summary.json (error %lu)", GetLastError());
        return false;
    }

    log_info("Successfully generated %ls", final_path);
    return true;
}

static char* replace_or_insert_json_string(
    const char* json,
    const char* key,
    const char* value,
    size_t* out_len
) {
    if (!json || !key || !value) return NULL;

    char key_pattern[128];
    snprintf(key_pattern, sizeof(key_pattern), "\"%s\"", key);

    const char* found = strstr(json, key_pattern);

    if (found) {
        // Key exists: replace value
        // Find colon after key
        const char* colon = strchr(found, ':');
        if (!colon) return NULL;

        // Find quote after colon
        const char* q1 = strchr(colon, '"');
        if (!q1) return NULL;

        const char* q2 = strchr(q1 + 1, '"');
        if (!q2) return NULL;

        // Replace between q1 and q2
        size_t prefix_len = (size_t)(q1 + 1 - json);
        size_t suffix_len = strlen(q2);
        size_t val_len = strlen(value);

        size_t total = prefix_len + val_len + suffix_len + 1;
        char* res = (char*)malloc(total);
        if (!res) return NULL;

        memcpy(res, json, prefix_len);
        memcpy(res + prefix_len, value, val_len);
        strcpy_s(res + prefix_len + val_len, suffix_len + 1, q2);

        if (out_len) *out_len = total - 1;
        return res;
    } else {
        // Key does not exist: insert right after the first '{'
        const char* brace = strchr(json, '{');
        if (!brace) return NULL;

        size_t prefix_len = (size_t)(brace + 1 - json);
        char insertion[512];
        snprintf(insertion, sizeof(insertion), "\n  \"%s\": \"%s\",", key, value);
        size_t ins_len = strlen(insertion);
        size_t suffix_len = strlen(brace + 1);

        size_t total = prefix_len + ins_len + suffix_len + 1;
        char* res = (char*)malloc(total);
        if (!res) return NULL;

        memcpy(res, json, prefix_len);
        memcpy(res + prefix_len, insertion, ins_len);
        strcpy_s(res + prefix_len + ins_len, suffix_len + 1, brace + 1);

        if (out_len) *out_len = total - 1;
        return res;
    }
}

bool state_patch_version(const wchar_t* dest_dir, const char* version, const wchar_t* summary_path) {
    if (!dest_dir || !version || !summary_path) return false;

    wchar_t state_file[MAX_PATH];
    wchar_t state_tmp[MAX_PATH];
    swprintf_s(state_file, MAX_PATH, L"%ls\\state.json", dest_dir);
    swprintf_s(state_tmp, MAX_PATH, L"%ls\\state.json.tmp", dest_dir);

    char summary_escaped[MAX_PATH * 4] = { 0 };
    escape_json_path(summary_path, summary_escaped, sizeof(summary_escaped));

    char dest_escaped[MAX_PATH * 4] = { 0 };
    escape_json_path(dest_dir, dest_escaped, sizeof(dest_escaped));

    FILE* fp = NULL;
    if (_wfopen_s(&fp, state_file, L"rb") != 0 || !fp) {
        // state.json does not exist yet: create a minimal valid state.json
        if (_wfopen_s(&fp, state_tmp, L"wb") == 0 && fp) {
            fprintf(fp, "{\n");
            fprintf(fp, "  \"installed_version\": \"%s\",\n", version);
            fprintf(fp, "  \"installer_summary_path\": \"%s\",\n", summary_escaped);
            fprintf(fp, "  \"installer_base_path\": \"%s\"\n", dest_escaped);
            fprintf(fp, "}\n");
            fclose(fp);
            MoveFileExW(state_tmp, state_file, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
            log_info("Created initial %ls with installed_version: %s", state_file, version);
            return true;
        }
        return false;
    }

    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (sz <= 0) {
        fclose(fp);
        return false;
    }

    char* content = (char*)malloc(sz + 1);
    if (!content) {
        fclose(fp);
        return false;
    }
    fread(content, 1, sz, fp);
    content[sz] = '\0';
    fclose(fp);

    // 1. Patch installed_version
    size_t len1 = 0;
    char* patched1 = replace_or_insert_json_string(content, "installed_version", version, &len1);
    free(content);
    if (!patched1) return false;

    // 2. Patch installer_summary_path
    size_t len2 = 0;
    char* patched2 = replace_or_insert_json_string(patched1, "installer_summary_path", summary_escaped, &len2);
    free(patched1);
    if (!patched2) return false;

    // Write to tmp and move
    if (_wfopen_s(&fp, state_tmp, L"wb") != 0 || !fp) {
        free(patched2);
        return false;
    }

    fwrite(patched2, 1, strlen(patched2), fp);
    fclose(fp);
    free(patched2);

    MoveFileExW(state_tmp, state_file, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    log_info("Updated installed_version and installer_summary_path in %ls.", state_file);
    return true;
}
