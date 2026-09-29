#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WINSOCK_DEPRECATED_NO_WARNINGS
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#endif

#include "mqtt_client.h"
#include "mqtt_protocol.h"
#include "command_runner.h"
#include "tool_inventory.h"
#include "../../l4pin/src/cert_discovery.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include <objbase.h>

#pragma comment(lib, "ws2_32.lib")

typedef struct {
    SOCKET sock;
    CRITICAL_SECTION send_cs;
    uint16_t packet_id_seq;
    char sn[128];
    CommandContext current_cmd;
    HANDLE hWorkerThread;
} MqttClientState;

static bool send_publish_packet(MqttClientState* state, const char* topic,
                                const char* payload, size_t payload_len,
                                uint8_t qos, uint8_t retain);
static bool send_publish_with_properties(MqttClientState* state, const char* topic,
                                         const char* payload, size_t payload_len,
                                         uint8_t qos, uint8_t retain,
                                         const MqttUserProperty* properties,
                                         size_t property_count);

static void get_iso_timestamp(char* out_ts, size_t size) {
    SYSTEMTIME st;
    GetLocalTime(&st);
    snprintf(out_ts, size, "%04u-%02u-%02u %02u:%02u:%02u",
             st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
}

static bool ascii_identifier(const char* value, bool hex_only) {
    if (!value || !value[0]) return false;
    for (const unsigned char* p = (const unsigned char*)value; *p; p++) {
        if (hex_only ? !isxdigit(*p) : !(isalnum(*p) || *p == '-' || *p == '_')) return false;
    }
    return true;
}

static bool publish_certificate_connected_event(MqttClientState* state, int proxy_port,
                                                bool force) {
    static char last_snapshot[512] = { 0 };
    char inventory[12000], digest[65];
    char package_version[64] = { 0 };
    if (!tool_inventory_build(inventory, sizeof(inventory), digest)) {
        fprintf(stderr, "[CERT] Tool inventory scan failed; event deferred\n");
        return false;
    }
    tool_inventory_package_version(package_version);
    ProxyIdentity identity;
    cert_info local_cert = { 0 };
    cert_state local_state = cert_discover(NULL, &local_cert);
    if (config_query_identity_from_proxy(proxy_port, &identity) != 0 ||
        (local_state != CERT_VALID && local_state != CERT_EXPIRING) ||
        local_cert.cert_duplicates != 0 ||
        strcmp(identity.sn, state->sn) != 0 ||
        strcmp(identity.sn, local_cert.sn) != 0 ||
        _stricmp(identity.thumbprint, local_cert.thumbprint_hex) != 0 ||
        !ascii_identifier(identity.sn, false) ||
        strlen(identity.thumbprint) != 40 ||
        !ascii_identifier(identity.thumbprint, true) ||
        !ascii_identifier(identity.serial, true) ||
        strlen(identity.not_after) < 19) {
        printf("[CERT] Active proxy identity is not ready or does not match MQTT SN; event deferred\n");
        return false;
    }
    char snapshot[sizeof(last_snapshot)];
    int snapshot_len = snprintf(snapshot, sizeof(snapshot), "%s:%s:%s:%s:%s",
                                identity.thumbprint, identity.serial, identity.not_after,
                                package_version, digest);
    if (snapshot_len <= 0 || (size_t)snapshot_len >= sizeof(snapshot)) return false;
    if (!force && strcmp(snapshot, last_snapshot) == 0) return true;

    GUID guid;
    if (FAILED(CoCreateGuid(&guid))) return false;
    unsigned event_id = guid.Data1 & 0x7fffffffU;
    if (!event_id) event_id = 1;
    SYSTEMTIME utc;
    GetSystemTime(&utc);
    char timestamp[32];
    snprintf(timestamp, sizeof(timestamp), "%04u-%02u-%02uT%02u:%02u:%02uZ",
             utc.wYear, utc.wMonth, utc.wDay, utc.wHour, utc.wMinute, utc.wSecond);
    char not_after[24];
    memcpy(not_after, identity.not_after, 19);
    not_after[19] = '\0';
    not_after[10] = 'T';
    strcat_s(not_after, sizeof(not_after), "Z");

    char topic[160], payload[16384];
    char package_json[70];
    if (package_version[0]) snprintf(package_json, sizeof(package_json), "\"%s\"", package_version);
    else strcpy_s(package_json, sizeof(package_json), "null");
    char event_id_text[16];
    snprintf(event_id_text, sizeof(event_id_text), "%u", event_id);
    snprintf(topic, sizeof(topic), "dev/%s/evt", state->sn);
    int length = snprintf(payload, sizeof(payload),
        "{\"101\":%u,\"102\":\"%s\",\"200\":75,\"300\":[{\"324\":\"%s\",\"440\":\"l4con\",\"441\":\"%s\",\"442\":\"%s\",\"443\":\"%s\",\"444\":%s,\"445\":%s}],\"correlationData\":\"%08lX-%04hX-%04hX-%02X%02X-%02X%02X%02X%02X%02X%02X\"}",
        event_id, timestamp, state->sn, identity.thumbprint, identity.serial, not_after,
        inventory, package_json,
        guid.Data1, guid.Data2, guid.Data3,
        guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3],
        guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]);
    if (length <= 0 || (size_t)length >= sizeof(payload)) return false;
    char correlation_id[40] = { 0 };
    if (!json_extract_string(payload, "correlationData",
                             correlation_id, sizeof(correlation_id))) return false;
    const MqttUserProperty event_properties[] = {
        { "event_type_code", "75" },
        { "dev_event_id", event_id_text },
        { "dev_timestamp", timestamp },
        { "correlationData", correlation_id },
    };
    if (!send_publish_with_properties(state, topic, payload, (size_t)length,
                                      1, 0, event_properties, 4)) return false;
    strcpy_s(last_snapshot, sizeof(last_snapshot), snapshot);
    printf("[CERT] Published identity event 75 for SN %s (qos=1, retain=0)\n", state->sn);
    return true;
}

static uint16_t get_next_packet_id(MqttClientState* state) {
    state->packet_id_seq++;
    if (state->packet_id_seq == 0) state->packet_id_seq = 1;
    return state->packet_id_seq;
}

static bool socket_send_all(SOCKET sock, const unsigned char* buf, size_t len) {
    size_t total_sent = 0;
    while (total_sent < len) {
        int sent = send(sock, (const char*)(buf + total_sent), (int)(len - total_sent), 0);
        if (sent <= 0) return false;
        total_sent += (size_t)sent;
    }
    return true;
}

static bool send_publish_with_properties(MqttClientState* state, const char* topic,
                                         const char* payload, size_t payload_len,
                                         uint8_t qos, uint8_t retain,
                                         const MqttUserProperty* properties,
                                         size_t property_count) {
    unsigned char buf[32768];
    EnterCriticalSection(&state->send_cs);
    uint16_t pkt_id = (qos > 0) ? get_next_packet_id(state) : 0;
    int pkt_len = mqtt_build_publish_with_properties(buf, sizeof(buf), topic, payload,
                                                      payload_len, pkt_id, qos, retain,
                                                      properties, property_count);
    bool ok = false;
    if (pkt_len > 0) {
        ok = socket_send_all(state->sock, buf, (size_t)pkt_len);
    }
    LeaveCriticalSection(&state->send_cs);
    return ok;
}

static bool socket_recv_all(SOCKET sock, unsigned char* buf, size_t len) {
    size_t used = 0;
    while (used < len) {
        int received = recv(sock, (char*)buf + used, (int)(len - used), 0);
        if (received <= 0) return false;
        used += (size_t)received;
    }
    return true;
}

static bool read_connack_v5(SOCKET sock, unsigned char* out_reason) {
    unsigned char fixed = 0, length_bytes[4], body[1024];
    if (!socket_recv_all(sock, &fixed, 1) || fixed != MQTT_PKT_CONNACK) return false;
    uint32_t remaining = 0;
    int used = 0;
    do {
        if (used == 4 || !socket_recv_all(sock, &length_bytes[used], 1)) return false;
        used++;
    } while (length_bytes[used - 1] & 0x80);
    int parsed = 0;
    if (mqtt_decode_remaining_length(length_bytes, (size_t)used,
                                     &remaining, &parsed) != 0 ||
        remaining < 3 || remaining > sizeof(body) ||
        !socket_recv_all(sock, body, remaining)) return false;
    uint32_t properties_len = 0;
    int property_len_bytes = 0;
    if (mqtt_decode_remaining_length(body + 2, remaining - 2,
                                     &properties_len, &property_len_bytes) != 0 ||
        2U + (uint32_t)property_len_bytes + properties_len != remaining) return false;
    *out_reason = body[1];
    return true;
}

static bool send_publish_packet(MqttClientState* state, const char* topic,
                                const char* payload, size_t payload_len,
                                uint8_t qos, uint8_t retain) {
    return send_publish_with_properties(state, topic, payload, payload_len,
                                        qos, retain, NULL, 0);
}

static void output_chunk_callback(const char* topic, const char* json_envelope, size_t json_len, void* user_data) {
    MqttClientState* state = (MqttClientState*)user_data;
    if (!state || state->sock == INVALID_SOCKET) return;

    send_publish_packet(state, topic, json_envelope, json_len, 1, 0);
}

typedef struct {
    MqttClientState* state;
    CommandContext ctx;
} WorkerTaskParams;

static DWORD WINAPI command_worker_thread(LPVOID lpParam) {
    WorkerTaskParams* params = (WorkerTaskParams*)lpParam;
    if (!params) return 1;

    MqttClientState* state = params->state;
    CommandContext* ctx = &params->ctx;

    int exit_code = 0;
    uint64_t duration_ms = 0;

    printf("[CMD] Executing: '%s' (Shell: %s, Timeout: %ds, Session: %s)\n",
           ctx->command_line,
           ctx->shell == SHELL_POWERSHELL ? "PowerShell" : "cmd",
           ctx->ttl_sec,
           ctx->session_id);

    command_runner_execute(ctx, output_chunk_callback, state, &exit_code, &duration_ms);

    printf("[CMD] Finished: exit_code=%d, duration=%llums\n", exit_code, duration_ms);

    // Send final result to dev/<SN>/res
    if (state->sock != INVALID_SOCKET) {
        char res_topic[128];
        snprintf(res_topic, sizeof(res_topic), "dev/%s/res", state->sn);

        char res_payload[512];
        if (strlen(ctx->task_id_str) > 0) {
            snprintf(res_payload, sizeof(res_payload),
                     "{\"corr_data\":\"%s\",\"correlationData\":\"%s\",\"status_code\":\"200\",\"status\":\"completed\",\"exit_code\":%d,\"duration_ms\":%llu}",
                     ctx->task_id_str, ctx->task_id_str, exit_code, duration_ms);
        } else {
            snprintf(res_payload, sizeof(res_payload),
                     "{\"task_id\":%d,\"status_code\":\"200\",\"status\":\"completed\",\"exit_code\":%d,\"duration_ms\":%llu}",
                     ctx->task_id, exit_code, duration_ms);
        }

        send_publish_packet(state, res_topic, res_payload, strlen(res_payload), 1, 0);
        printf("[RES] Published execution result to %s: %s\n", res_topic, res_payload);
    }

    EnterCriticalSection(&state->send_cs);
    state->current_cmd.is_running = false;
    LeaveCriticalSection(&state->send_cs);

    free(params);
    return 0;
}

static void handle_incoming_publish(MqttClientState* state, const char* topic, const char* payload, size_t payload_len, const AppConfig* config) {
    char payload_str[8192];
    size_t copy_len = payload_len < sizeof(payload_str) - 1 ? payload_len : sizeof(payload_str) - 1;
    memcpy(payload_str, payload, copy_len);
    payload_str[copy_len] = '\0';

    if (config->verbose) {
        printf("[MQTT] Recv Topic: %s | Payload: %s\n", topic, payload_str);
    }

    // 1. Task notification on srv/<SN>/tsk: request task details via dev/<SN>/req
    if (strstr(topic, "/tsk") != NULL) {
        char task_id_str[128] = {0};
        json_extract_string(payload_str, "id", task_id_str, sizeof(task_id_str));
        if (strlen(task_id_str) == 0) {
            json_extract_string(payload_str, "task_id", task_id_str, sizeof(task_id_str));
        }
        if (strlen(task_id_str) == 0) {
            json_extract_string(payload_str, "ext_task_id", task_id_str, sizeof(task_id_str));
        }

        if (strlen(task_id_str) > 0) {
            char req_topic[128];
            snprintf(req_topic, sizeof(req_topic), "dev/%s/req", state->sn);

            char req_payload[256];
            snprintf(req_payload, sizeof(req_payload), "{\"corr_data\":\"%s\",\"correlationData\":\"%s\"}", task_id_str, task_id_str);

            send_publish_packet(state, req_topic, req_payload, strlen(req_payload), 0, 0);
            printf("[REQ] Task announcement received. Requested task body: %s -> %s\n", req_topic, req_payload);
        }
    }

    // 2. Cancellation message (Method 7002)
    int method_code = 0;
    if (!json_extract_int(payload_str, "method_code", &method_code)) {
        if (strstr(payload_str, "7001") != NULL) method_code = 7001;
        else if (strstr(payload_str, "7002") != NULL) method_code = 7002;
        else if (strstr(payload_str, "7003") != NULL) method_code = 7003;
        else if (strstr(payload_str, "7004") != NULL) method_code = 7004;
    }

    if (method_code == 7002 || strstr(payload_str, "\"cancel\"") != NULL) {
        // CMD_DIAG_CANCEL
        char session_id[64] = {0};
        char task_id_str[128] = {0};
        json_extract_string(payload_str, "session_id", session_id, sizeof(session_id));
        json_extract_string(payload_str, "id", task_id_str, sizeof(task_id_str));
        if (strlen(task_id_str) == 0) json_extract_string(payload_str, "task_id", task_id_str, sizeof(task_id_str));
        if (strlen(task_id_str) == 0) json_extract_string(payload_str, "corr_data", task_id_str, sizeof(task_id_str));
        if (strlen(task_id_str) == 0) json_extract_string(payload_str, "correlationData", task_id_str, sizeof(task_id_str));

        printf("[CANCEL] Cancellation requested for session: %s (task_id: %s)\n", session_id, task_id_str);

        EnterCriticalSection(&state->send_cs);
        if (state->current_cmd.is_running) {
            command_runner_request_cancel(&state->current_cmd);
        }
        LeaveCriticalSection(&state->send_cs);

        if (state->sock != INVALID_SOCKET && strlen(task_id_str) > 0) {
            char res_topic[128];
            snprintf(res_topic, sizeof(res_topic), "dev/%s/res", state->sn);
            char res_payload[256];
            snprintf(res_payload, sizeof(res_payload),
                     "{\"corr_data\":\"%s\",\"correlationData\":\"%s\",\"status_code\":\"200\",\"status\":\"cancelled\",\"exit_code\":130}",
                     task_id_str, task_id_str);
            send_publish_packet(state, res_topic, res_payload, strlen(res_payload), 1, 0);
        }
        return;
    }

    // 3. Application Ping / Pong (Method 7003)
    if (method_code == 7003 || strstr(payload_str, "\"ping\"") != NULL) {
        char task_id_str[128] = {0};
        json_extract_string(payload_str, "id", task_id_str, sizeof(task_id_str));
        if (strlen(task_id_str) == 0) json_extract_string(payload_str, "corr_data", task_id_str, sizeof(task_id_str));
        if (strlen(task_id_str) == 0) json_extract_string(payload_str, "correlationData", task_id_str, sizeof(task_id_str));

        printf("[PING] Application Ping received. Responding with Pong (7003)...\n");
        if (state->sock != INVALID_SOCKET && strlen(task_id_str) > 0) {
            char res_topic[128];
            snprintf(res_topic, sizeof(res_topic), "dev/%s/res", state->sn);
            char res_payload[256];
            snprintf(res_payload, sizeof(res_payload),
                     "{\"corr_data\":\"%s\",\"correlationData\":\"%s\",\"status_code\":\"200\",\"status\":\"pong\",\"role\":\"extra_service\"}",
                     task_id_str, task_id_str);
            send_publish_packet(state, res_topic, res_payload, strlen(res_payload), 1, 0);
        }
        return;
    }

    // 4. Session Keepalive / Lease Renewal (Method 7004)
    if (method_code == 7004 || strstr(payload_str, "\"keepalive\"") != NULL) {
        char session_id[64] = {0};
        char task_id_str[128] = {0};
        json_extract_string(payload_str, "session_id", session_id, sizeof(session_id));
        json_extract_string(payload_str, "id", task_id_str, sizeof(task_id_str));
        if (strlen(task_id_str) == 0) json_extract_string(payload_str, "corr_data", task_id_str, sizeof(task_id_str));

        printf("[KEEPALIVE] Session lease renewal received for session: %s\n", session_id);
        if (state->sock != INVALID_SOCKET && strlen(task_id_str) > 0) {
            char res_topic[128];
            snprintf(res_topic, sizeof(res_topic), "dev/%s/res", state->sn);
            char res_payload[256];
            snprintf(res_payload, sizeof(res_payload),
                     "{\"corr_data\":\"%s\",\"correlationData\":\"%s\",\"status_code\":\"200\",\"status\":\"keepalive_ack\"}",
                     task_id_str, task_id_str);
            send_publish_packet(state, res_topic, res_payload, strlen(res_payload), 1, 0);
        }
        return;
    }

    // 5. Execution message (from srv/<SN>/rsp or method 7001)
    if (method_code == 7001 || strstr(topic, "/rsp") != NULL) {
        // CMD_DIAG_EXEC
        char session_id[64] = {0};
        char task_id_str[128] = {0};
        char cmd_line[1024] = {0};
        char shell_str[32] = {0};
        char topic_out[128] = {0};
        int task_id = 0;
        int ttl_sec = config->default_cmd_timeout;
        int max_bytes = 1048576;

        json_extract_string(payload_str, "session_id", session_id, sizeof(session_id));
        json_extract_string(payload_str, "id", task_id_str, sizeof(task_id_str));
        if (strlen(task_id_str) == 0) {
            json_extract_string(payload_str, "task_id", task_id_str, sizeof(task_id_str));
        }
        json_extract_int(payload_str, "task_id", &task_id);

        // Try extracting command_line or command_id / args
        if (!json_extract_string(payload_str, "command_line", cmd_line, sizeof(cmd_line)) || strlen(cmd_line) == 0) {
            char command_id[128] = {0};
            if (json_extract_string(payload_str, "command_id", command_id, sizeof(command_id)) && strlen(command_id) > 0) {
                if (strcmp(command_id, "system_info") == 0) strncpy(cmd_line, "systeminfo", sizeof(cmd_line) - 1);
                else if (strcmp(command_id, "network_config") == 0 || strcmp(command_id, "network_info") == 0 || strcmp(command_id, "ipconfig") == 0) strncpy(cmd_line, "ipconfig /all", sizeof(cmd_line) - 1);
                else if (strcmp(command_id, "get_processes") == 0 || strcmp(command_id, "tasklist") == 0) strncpy(cmd_line, "tasklist", sizeof(cmd_line) - 1);
                else if (strcmp(command_id, "get_services") == 0 || strcmp(command_id, "services") == 0) strncpy(cmd_line, "sc query", sizeof(cmd_line) - 1);
                else if (strcmp(command_id, "disk_info") == 0 || strcmp(command_id, "disk_usage") == 0) strncpy(cmd_line, "wmic logicaldisk get caption,size,freespace", sizeof(cmd_line) - 1);
                else if (strcmp(command_id, "time") == 0) strncpy(cmd_line, "time /t & date /t", sizeof(cmd_line) - 1);
                else if (strcmp(command_id, "echo") == 0) strncpy(cmd_line, "echo Leo4 l4con Diagnostic Agent Active", sizeof(cmd_line) - 1);
                else if (strcmp(command_id, "ping_gateway") == 0) strncpy(cmd_line, "ping -n 4 127.0.0.1", sizeof(cmd_line) - 1);
                else strncpy(cmd_line, command_id, sizeof(cmd_line) - 1);
            }
        }

        if (strlen(cmd_line) == 0) {
            return; // Not a valid execution task
        }

        json_extract_string(payload_str, "shell", shell_str, sizeof(shell_str));
        json_extract_int(payload_str, "ttl_sec", &ttl_sec);
        json_extract_int(payload_str, "max_output_bytes", &max_bytes);
        if (!json_extract_string(payload_str, "topic", topic_out, sizeof(topic_out)) || strlen(topic_out) == 0) {
            snprintf(topic_out, sizeof(topic_out), "dev/%s/out", state->sn);
        }

        EnterCriticalSection(&state->send_cs);
        if (state->current_cmd.is_running) {
            // Cancel current or reject
            command_runner_request_cancel(&state->current_cmd);
            Sleep(100);
        }

        command_runner_init_context(&state->current_cmd);
        snprintf(state->current_cmd.session_id, sizeof(state->current_cmd.session_id), "%s", session_id);
        state->current_cmd.task_id = task_id;
        snprintf(state->current_cmd.task_id_str, sizeof(state->current_cmd.task_id_str), "%s", task_id_str);
        snprintf(state->current_cmd.command_line, sizeof(state->current_cmd.command_line), "%s", cmd_line);
        state->current_cmd.shell = (_stricmp(shell_str, "powershell") == 0 || _stricmp(shell_str, "ps") == 0) ? SHELL_POWERSHELL : SHELL_CMD;
        state->current_cmd.ttl_sec = ttl_sec > 0 ? ttl_sec : config->default_cmd_timeout;
        state->current_cmd.max_output_bytes = max_bytes > 0 ? max_bytes : 1048576;
        snprintf(state->current_cmd.out_topic, sizeof(state->current_cmd.out_topic), "%s", topic_out);
        state->current_cmd.enable_blacklist = config->enable_blacklist;
        state->current_cmd.is_running = true;

        WorkerTaskParams* params = (WorkerTaskParams*)malloc(sizeof(WorkerTaskParams));
        if (params) {
            params->state = state;
            params->ctx = state->current_cmd;
            HANDLE hThread = CreateThread(NULL, 0, command_worker_thread, params, 0, NULL);
            if (hThread) {
                CloseHandle(hThread);
            } else {
                free(params);
                state->current_cmd.is_running = false;
            }
        }
        LeaveCriticalSection(&state->send_cs);
    }
}

int mqtt_client_run(const AppConfig* config, HANDLE hStopEvent) {
    if (!config) return 1;

    // Ensure single instance per machine across sessions (Service session 0 and User session 1)
    HANDLE hMutex = CreateMutexW(NULL, FALSE, L"Global\\L4Con_SingleInstance_Mutex");
    DWORD mutex_err = GetLastError();

    if (hMutex == NULL) {
        if (mutex_err == ERROR_ACCESS_DENIED) {
            fprintf(stderr, "[ERROR] Another instance of l4con is already running as a Windows Service. Exiting.\n");
            return 1;
        }
        // Fallback to local session namespace
        hMutex = CreateMutexW(NULL, FALSE, L"L4Con_SingleInstance_Mutex");
        mutex_err = GetLastError();
    }

    if (mutex_err == ERROR_ALREADY_EXISTS || mutex_err == ERROR_ACCESS_DENIED) {
        fprintf(stderr, "[ERROR] Another instance of l4con is already running. Exiting to prevent duplicate broker subscriptions.\n");
        if (hMutex) CloseHandle(hMutex);
        return 1;
    }

    MqttClientState state;
    memset(&state, 0, sizeof(state));
    InitializeCriticalSection(&state.send_cs);
    state.sock = INVALID_SOCKET;

    char ts_buf[64];
    get_iso_timestamp(ts_buf, sizeof(ts_buf));

    // Resolve Serial Number (SN)
    // Rule:
    // 1. In Windows Service mode, Leo4Proxy is the strict source of truth for SN (CLI SN is ignored).
    // 2. In interactive/console mode, CLI --sn argument is used as a debug/test override.
    // 3. If SN is not explicitly provided in CLI args (or when running as a service),
    //    wait indefinitely in a loop until Leo4Proxy responds with valid SN.
    bool use_cli_sn = (!config->is_service && config->sn_explicitly_set && strlen(config->device_sn) > 0);

    if (use_cli_sn) {
        snprintf(state.sn, sizeof(state.sn), "%s", config->device_sn);
        get_iso_timestamp(ts_buf, sizeof(ts_buf));
        printf("[%s] [DEBUG] Using explicitly configured SN from CLI arguments: %s\n", ts_buf, state.sn);
    } else {
        bool sn_resolved = false;
        int query_attempt = 0;
        int retry_delay_ms = (config->reconnect_sec > 0 ? config->reconnect_sec : 5) * 1000;

        get_iso_timestamp(ts_buf, sizeof(ts_buf));
        printf("[%s] Leo4Proxy is the source of truth for device SN. Waiting for Leo4Proxy on port %d...\n",
               ts_buf, config->proxy_http_port);

        while (!sn_resolved) {
            if (WaitForSingleObject(hStopEvent, 0) == WAIT_OBJECT_0) {
                DeleteCriticalSection(&state.send_cs);
                if (hMutex) CloseHandle(hMutex);
                return 0;
            }

            query_attempt++;
            if (config_query_sn_from_proxy(config->proxy_http_port, state.sn, sizeof(state.sn)) == 0 && strlen(state.sn) > 0) {
                sn_resolved = true;
                get_iso_timestamp(ts_buf, sizeof(ts_buf));
                printf("[%s] [OK] Resolved device SN from Leo4Proxy: %s\n", ts_buf, state.sn);
                break;
            }

            get_iso_timestamp(ts_buf, sizeof(ts_buf));
            printf("[%s] [WARN] Leo4Proxy not responding / SN not available yet (attempt %d). Retrying in %ds...\n",
                   ts_buf, query_attempt, retry_delay_ms / 1000);

            if (WaitForSingleObject(hStopEvent, (DWORD)retry_delay_ms) == WAIT_OBJECT_0) {
                DeleteCriticalSection(&state.send_cs);
                if (hMutex) CloseHandle(hMutex);
                return 0;
            }
        }
    }

    char client_id[128];
    if (strlen(config->client_id) > 0) {
        snprintf(client_id, sizeof(client_id), "%s", config->client_id);
    } else {
        snprintf(client_id, sizeof(client_id), "%s_extra", state.sn);
    }

    char will_topic[128];
    snprintf(will_topic, sizeof(will_topic), "dev/%s/svc", state.sn);
    const char* will_payload = "svc_offline";

    char pub_topic[128];
    snprintf(pub_topic, sizeof(pub_topic), "dev/%s/svc", state.sn);
    const char* online_payload = "svc_online";

    char tsk_sub_topic[128];
    snprintf(tsk_sub_topic, sizeof(tsk_sub_topic), "srv/%s/tsk", state.sn);

    printf("[%s] Initializing l4con for device: %s\n", ts_buf, state.sn);
    printf("  Presence Topic:  %s (%s / %s)\n", will_topic, online_payload, will_payload);
    printf("  RPC Sub Topic:   %s\n", tsk_sub_topic);
    printf("  Target Broker:   %s:%d (Keepalive: %ds)\n\n", config->mqtt_host, config->mqtt_port, config->keepalive_sec);

    bool cert_event_sent = false;
    while (WaitForSingleObject(hStopEvent, 0) != WAIT_OBJECT_0) {
        get_iso_timestamp(ts_buf, sizeof(ts_buf));
        printf("[%s] Connecting to MQTT broker at %s:%d...\n", ts_buf, config->mqtt_host, config->mqtt_port);

        SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (s == INVALID_SOCKET) {
            fprintf(stderr, "[ERROR] socket() failed: %d\n", WSAGetLastError());
            WaitForSingleObject(hStopEvent, config->reconnect_sec * 1000);
            continue;
        }

        // Set TCP timeouts
        DWORD timeout_ms = 10000;
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout_ms, sizeof(timeout_ms));
        setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout_ms, sizeof(timeout_ms));

        struct sockaddr_in saddr;
        ZeroMemory(&saddr, sizeof(saddr));
        saddr.sin_family = AF_INET;
        saddr.sin_port = htons((u_short)config->mqtt_port);
        saddr.sin_addr.s_addr = inet_addr(config->mqtt_host);

        if (saddr.sin_addr.s_addr == INADDR_NONE) {
            struct hostent* he = gethostbyname(config->mqtt_host);
            if (he && he->h_addr_list[0]) {
                memcpy(&saddr.sin_addr, he->h_addr_list[0], sizeof(struct in_addr));
            } else {
                fprintf(stderr, "[ERROR] Resolving host %s failed\n", config->mqtt_host);
                closesocket(s);
                WaitForSingleObject(hStopEvent, config->reconnect_sec * 1000);
                continue;
            }
        }

        if (connect(s, (struct sockaddr*)&saddr, sizeof(saddr)) != 0) {
            fprintf(stderr, "[ERROR] connect() to %s:%d failed: %d\n", config->mqtt_host, config->mqtt_port, WSAGetLastError());
            closesocket(s);
            WaitForSingleObject(hStopEvent, config->reconnect_sec * 1000);
            continue;
        }

        state.sock = s;

        // 1. Send CONNECT packet
        unsigned char pkt_buf[4096];
        int conn_len = mqtt_build_connect(pkt_buf, sizeof(pkt_buf),
                                          client_id, will_topic, will_payload,
                                          config->role, (uint16_t)config->keepalive_sec);
        if (conn_len <= 0 || !socket_send_all(s, pkt_buf, (size_t)conn_len)) {
            fprintf(stderr, "[ERROR] Sending CONNECT packet failed\n");
            closesocket(s);
            state.sock = INVALID_SOCKET;
            WaitForSingleObject(hStopEvent, config->reconnect_sec * 1000);
            continue;
        }

        // 2. Read MQTT 5 CONNACK, including its property length.
        unsigned char connack_reason = 0xFF;
        if (!read_connack_v5(s, &connack_reason) || connack_reason != 0) {
            fprintf(stderr, "[ERROR] MQTT 5 CONNACK failed (reason=%u)\n",
                    (unsigned)connack_reason);
            closesocket(s);
            state.sock = INVALID_SOCKET;
            WaitForSingleObject(hStopEvent, config->reconnect_sec * 1000);
            continue;
        }

        get_iso_timestamp(ts_buf, sizeof(ts_buf));
        printf("[%s] [OK] MQTT Connected successfully. (rc=0)\n", ts_buf);

        // 3. Publish dev/{SN}/svc = svc_online (retain=1, qos=1)
        send_publish_packet(&state, pub_topic, online_payload, strlen(online_payload), 1, 1);
        printf("[%s] [OK] Published: %s = %s (retain=1, qos=1)\n", ts_buf, pub_topic, online_payload);

        if (!use_cli_sn) {
            cert_event_sent = publish_certificate_connected_event(&state, config->proxy_http_port,
                                                                   true);
        }
        time_t last_cert_event_attempt = time(NULL);

        // 4. Subscriptions (qos=1) - strictly srv/<SN>/tsk and srv/<SN>/rsp
        const char* sub_suffixes[] = {"tsk", "rsp", NULL};
        for (int i = 0; sub_suffixes[i] != NULL; i++) {
            char sub_topic[128];
            snprintf(sub_topic, sizeof(sub_topic), "srv/%s/%s", state.sn, sub_suffixes[i]);

            EnterCriticalSection(&state.send_cs);
            uint16_t sub_pkt_id = get_next_packet_id(&state);
            int sub_len = mqtt_build_subscribe(pkt_buf, sizeof(pkt_buf), sub_topic, sub_pkt_id, 1);
            if (sub_len > 0) {
                socket_send_all(s, pkt_buf, (size_t)sub_len);
            }
            LeaveCriticalSection(&state.send_cs);
            printf("[%s] [OK] Subscribed: %s (qos=1)\n", ts_buf, sub_topic);
        }

        // Main Connection & Polling Loop
        time_t last_ping_time = time(NULL);
        time_t ping_interval = config->keepalive_sec > 4 ? config->keepalive_sec / 2 : 2;

        unsigned char rx_buf[16384];
        size_t rx_buf_len = 0;

        while (WaitForSingleObject(hStopEvent, 0) != WAIT_OBJECT_0) {
            fd_set read_fds;
            FD_ZERO(&read_fds);
            FD_SET(s, &read_fds);

            struct timeval tv;
            tv.tv_sec = 0;
            tv.tv_usec = 200000; // 200 ms

            int sel_rc = select(0, &read_fds, NULL, NULL, &tv);
            if (sel_rc == SOCKET_ERROR) {
                fprintf(stderr, "[ERROR] select() socket error: %d\n", WSAGetLastError());
                break;
            }

            if (sel_rc > 0 && FD_ISSET(s, &read_fds)) {
                int bytes_recvd = recv(s, (char*)(rx_buf + rx_buf_len), (int)(sizeof(rx_buf) - rx_buf_len), 0);
                if (bytes_recvd <= 0) {
                    printf("[WARN] Socket disconnected by broker.\n");
                    break;
                }
                rx_buf_len += (size_t)bytes_recvd;

                // Process MQTT packets in rx_buf
                size_t offset = 0;
                while (offset < rx_buf_len) {
                    uint8_t pkt_type = rx_buf[offset] & 0xF0;
                    uint8_t pkt_flags = rx_buf[offset] & 0x0F;

                    uint32_t rem_len = 0;
                    int rem_bytes = 0;
                    if (mqtt_decode_remaining_length(rx_buf + offset + 1, rx_buf_len - (offset + 1), &rem_len, &rem_bytes) != 0) {
                        break; // Incomplete remaining length
                    }

                    size_t total_pkt_len = 1 + rem_bytes + rem_len;
                    if (offset + total_pkt_len > rx_buf_len) {
                        break; // Incomplete packet, wait for more data
                    }

                    const unsigned char* pkt_body = rx_buf + offset + 1 + rem_bytes;

                    if (pkt_type == MQTT_PKT_PUBLISH) {
                        char topic_in[256];
                        uint16_t pkt_id_in = 0;
                        const char* payload_ptr = NULL;
                        size_t payload_len = 0;

                        if (mqtt_parse_publish(pkt_body, rem_len, pkt_flags,
                                               topic_in, sizeof(topic_in),
                                               &pkt_id_in, &payload_ptr, &payload_len) == 0) {
                            // Send PUBACK if QoS 1
                            uint8_t qos = (pkt_flags >> 1) & 0x03;
                            if (qos == 1 && pkt_id_in > 0) {
                                unsigned char puback_buf[4];
                                int pa_len = mqtt_build_puback(puback_buf, pkt_id_in);
                                EnterCriticalSection(&state.send_cs);
                                socket_send_all(s, puback_buf, (size_t)pa_len);
                                LeaveCriticalSection(&state.send_cs);
                            }

                            handle_incoming_publish(&state, topic_in, payload_ptr, payload_len, config);
                        }
                    } else if (pkt_type == MQTT_PKT_PINGRESP) {
                        last_ping_time = time(NULL);
                    }

                    offset += total_pkt_len;
                }

                // Shift remaining unprocessed bytes
                if (offset > 0) {
                    if (offset < rx_buf_len) {
                        memmove(rx_buf, rx_buf + offset, rx_buf_len - offset);
                    }
                    rx_buf_len -= offset;
                }
            }

            // Periodic PINGREQ keepalive
            time_t now = time(NULL);
            if (!use_cli_sn && now - last_cert_event_attempt >= (cert_event_sent ? 60 : 15)) {
                last_cert_event_attempt = now;
                cert_event_sent = publish_certificate_connected_event(&state,
                                                                       config->proxy_http_port,
                                                                       false);
            }
            if (now - last_ping_time >= ping_interval) {
                unsigned char ping_buf[2];
                int p_len = mqtt_build_pingreq(ping_buf);
                EnterCriticalSection(&state.send_cs);
                socket_send_all(s, ping_buf, (size_t)p_len);
                LeaveCriticalSection(&state.send_cs);
                last_ping_time = now;
            }
        }

        // Graceful disconnect on shutdown
        if (WaitForSingleObject(hStopEvent, 0) == WAIT_OBJECT_0) {
            get_iso_timestamp(ts_buf, sizeof(ts_buf));
            printf("[%s] Service shutdown requested. Sending %s = %s (retain=1)...\n",
                   ts_buf, pub_topic, will_payload);

            // 1. Cancel any active command
            EnterCriticalSection(&state.send_cs);
            if (state.current_cmd.is_running) {
                command_runner_request_cancel(&state.current_cmd);
            }
            LeaveCriticalSection(&state.send_cs);

            // 2. Publish svc_offline retain=1
            send_publish_packet(&state, pub_topic, will_payload, strlen(will_payload), 1, 1);

            // 3. Send DISCONNECT
            unsigned char disc_buf[2];
            int d_len = mqtt_build_disconnect(disc_buf);
            EnterCriticalSection(&state.send_cs);
            socket_send_all(s, disc_buf, (size_t)d_len);
            LeaveCriticalSection(&state.send_cs);

            closesocket(s);
            state.sock = INVALID_SOCKET;
            break;
        }

        closesocket(s);
        state.sock = INVALID_SOCKET;
        printf("[WARN] Connection lost. Reconnecting in %d seconds...\n", config->reconnect_sec);
        WaitForSingleObject(hStopEvent, config->reconnect_sec * 1000);
    }

    DeleteCriticalSection(&state.send_cs);
    if (hMutex) {
        CloseHandle(hMutex);
    }
    return 0;
}
