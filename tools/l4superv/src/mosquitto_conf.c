#include "mosquitto_conf.h"
#include "service_mgr.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <shlwapi.h>

#pragma comment(lib, "shlwapi.lib")

static void get_forward_slash_path(const wchar_t* in_path, char* out_buf, size_t out_size) {
    char utf8[MAX_PATH * 3] = { 0 };
    WideCharToMultiByte(CP_UTF8, 0, in_path, -1, utf8, sizeof(utf8), NULL, NULL);
    for (size_t i = 0; utf8[i] && i + 1 < out_size; i++) {
        if (utf8[i] == '\\') {
            out_buf[i] = '/';
        } else {
            out_buf[i] = utf8[i];
        }
        out_buf[i + 1] = '\0';
    }
}

static void ensure_log_dir_exists(const wchar_t* base_path) {
    wchar_t mosq_dir[MAX_PATH];
    swprintf_s(mosq_dir, MAX_PATH, L"%s\\mosquitto", base_path);
    CreateDirectoryW(mosq_dir, NULL);

    wchar_t log_dir[MAX_PATH];
    swprintf_s(log_dir, MAX_PATH, L"%s\\mosquitto\\log", base_path);
    CreateDirectoryW(log_dir, NULL);
    svc_set_dir_permissions(log_dir);
}

bool mosquitto_conf_generate_standby(const wchar_t* base_path, int port) {
    if (!base_path) return false;
    ensure_log_dir_exists(base_path);

    wchar_t conf_path[MAX_PATH];
    swprintf_s(conf_path, MAX_PATH, L"%s\\mosquitto\\mosquitto.conf", base_path);

    char base_fwd[MAX_PATH * 3] = { 0 };
    get_forward_slash_path(base_path, base_fwd, sizeof(base_fwd));

    FILE* f = NULL;
    if (_wfopen_s(&f, conf_path, L"wb") != 0 || !f) {
        return false;
    }

    fprintf(f, "# ==============================================================================\n");
    fprintf(f, "# Mosquitto MQTT Broker Configuration (Standby / Neutral Mode - Local Only)\n");
    fprintf(f, "# Generated automatically by l4superv (Waiting for Leo4 Certificate)\n");
    fprintf(f, "# ==============================================================================\n\n");
    fprintf(f, "listener %d 127.0.0.1\n", port > 0 ? port : 1883);
    fprintf(f, "allow_anonymous true\n\n");
    fprintf(f, "persistence false\n");
    fprintf(f, "log_dest file %s/mosquitto/log/mosquitto.log\n", base_fwd);
    fprintf(f, "log_type error\n");
    fprintf(f, "log_type warning\n");
    fprintf(f, "log_type notice\n");
    fprintf(f, "log_type information\n");
    fprintf(f, "log_type subscribe\n");
    fprintf(f, "log_type unsubscribe\n");
    fprintf(f, "connection_messages true\n");

    fclose(f);
    return true;
}

bool mosquitto_conf_generate_active(const wchar_t* base_path,
                                    int port,
                                    const char* sn,
                                    const wchar_t* custom_tmpl_path) {
    if (!base_path || !sn || sn[0] == '\0') return false;
    ensure_log_dir_exists(base_path);

    wchar_t conf_path[MAX_PATH];
    swprintf_s(conf_path, MAX_PATH, L"%s\\mosquitto\\mosquitto.conf", base_path);

    char base_fwd[MAX_PATH * 3] = { 0 };
    get_forward_slash_path(base_path, base_fwd, sizeof(base_fwd));

    // Check if custom template exists
    if (custom_tmpl_path && custom_tmpl_path[0] != L'\0' && PathFileExistsW(custom_tmpl_path)) {
        FILE* ft = NULL;
        if (_wfopen_s(&ft, custom_tmpl_path, L"rb") == 0 && ft) {
            fseek(ft, 0, SEEK_END);
            long sz = ftell(ft);
            fseek(ft, 0, SEEK_SET);

            if (sz > 0 && sz < 1024 * 1024) {
                char* tmpl_data = (char*)malloc(sz + 1);
                if (tmpl_data) {
                    size_t read_bytes = fread(tmpl_data, 1, sz, ft);
                    tmpl_data[read_bytes] = '\0';
                    fclose(ft);

                    // Perform macro replacement (%SN%, %BASE_PATH%, %PORT%)
                    FILE* out_f = NULL;
                    if (_wfopen_s(&out_f, conf_path, L"wb") == 0 && out_f) {
                        for (size_t i = 0; i < read_bytes;) {
                            if (strncmp(tmpl_data + i, "%SN%", 4) == 0) {
                                fputs(sn, out_f);
                                i += 4;
                            } else if (strncmp(tmpl_data + i, "%BASE_PATH%", 11) == 0) {
                                fputs(base_fwd, out_f);
                                i += 11;
                            } else if (strncmp(tmpl_data + i, "%PORT%", 6) == 0) {
                                fprintf(out_f, "%d", port > 0 ? port : 1883);
                                i += 6;
                            } else {
                                fputc(tmpl_data[i], out_f);
                                i++;
                            }
                        }
                        fclose(out_f);
                        free(tmpl_data);
                        return true;
                    }
                    free(tmpl_data);
                }
            } else {
                fclose(ft);
            }
        }
    }

    // Default built-in active template
    FILE* f = NULL;
    if (_wfopen_s(&f, conf_path, L"wb") != 0 || !f) {
        return false;
    }

    fprintf(f, "# ==============================================================================\n");
    fprintf(f, "# Mosquitto MQTT Broker Configuration (Active Bridge Mode)\n");
    fprintf(f, "# Generated automatically by l4superv for Device SN: %s\n", sn);
    fprintf(f, "# ==============================================================================\n\n");
    fprintf(f, "# Local listener for internal terminal processes\n");
    fprintf(f, "listener %d 127.0.0.1\n", port > 0 ? port : 1883);
    fprintf(f, "allow_anonymous true\n\n");
    fprintf(f, "# Bridge configuration to leo4proxy (Native SChannel mTLS tunnel)\n");
    fprintf(f, "connection platerra-upstream\n");
    fprintf(f, "bridge_protocol_version mqttv50\n");
    fprintf(f, "address 127.0.0.1:18883\n\n");
    fprintf(f, "# Remote client identifier for external broker\n");
    fprintf(f, "remote_clientid %s\n\n", sn);
    fprintf(f, "# Disable Mosquitto $SYS status topics (required for external broker compatibility)\n");
    fprintf(f, "try_private false\n");
    fprintf(f, "notifications false\n\n");
    fprintf(f, "# Topic routing rules (topic <pattern> <direction> <QoS>)\n");
    fprintf(f, "# 1) Outbound responses and events from terminal to server:\n");
    fprintf(f, "topic dev/%s/out out 0\n", sn);
    fprintf(f, "topic dev/%s/# out 1\n\n", sn);
    fprintf(f, "# 2) Inbound commands from server to terminal:\n");
    fprintf(f, "topic srv/%s/rsp in 1\n", sn);
    fprintf(f, "topic srv/%s/# in 1\n\n", sn);
    fprintf(f, "# Connection reliability and keep-alive\n");
    fprintf(f, "cleansession true\n");
    fprintf(f, "restart_timeout 5 60\n");
    fprintf(f, "keepalive_interval 60\n\n");
    fprintf(f, "persistence false\n");
    fprintf(f, "log_dest file %s/mosquitto/log/mosquitto.log\n", base_fwd);
    fprintf(f, "log_type error\n");
    fprintf(f, "log_type warning\n");
    fprintf(f, "log_type notice\n");
    fprintf(f, "log_type information\n");
    fprintf(f, "log_type subscribe\n");
    fprintf(f, "log_type unsubscribe\n");
    fprintf(f, "connection_messages true\n");

    fclose(f);
    return true;
}

bool mosquitto_conf_is_standby(const wchar_t* base_path) {
    if (!base_path) return false;

    wchar_t conf_path[MAX_PATH];
    swprintf_s(conf_path, MAX_PATH, L"%s\\mosquitto\\mosquitto.conf", base_path);

    FILE* f = NULL;
    if (_wfopen_s(&f, conf_path, L"rb") != 0 || !f) {
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

    bool has_bridge = (strstr(buf, "connection platerra-upstream") != NULL);
    bool has_listener = (strstr(buf, "listener") != NULL);

    free(buf);
    return (!has_bridge && has_listener);
}

bool mosquitto_conf_is_active_with_sn(const wchar_t* base_path, const char* sn) {
    if (!base_path || !sn || sn[0] == '\0') return false;

    wchar_t conf_path[MAX_PATH];
    swprintf_s(conf_path, MAX_PATH, L"%s\\mosquitto\\mosquitto.conf", base_path);

    FILE* f = NULL;
    if (_wfopen_s(&f, conf_path, L"rb") != 0 || !f) {
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

    char search_remote[128];
    snprintf(search_remote, sizeof(search_remote), "remote_clientid %s", sn);

    bool has_bridge = (strstr(buf, "connection platerra-upstream") != NULL);
    bool has_sn = (strstr(buf, search_remote) != NULL);

    free(buf);
    return (has_bridge && has_sn);
}
