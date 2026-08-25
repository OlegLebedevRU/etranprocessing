/**
 * @file c_client_example.c
 * @brief C Client Example (extra_service role) using Eclipse Paho MQTT C (No-SSL).
 *
 * Connects to local Mosquitto Bridge at tcp://127.0.0.1:1883 as 'extra_service'
 * and publishes events in a 10-minute cycle to topic 'dev/<SN>/evt' (QoS 1, Retain 0)
 * with MQTT 5.0 User Properties and hardware event payload.
 *
 * Build:
 *   cl.exe /MT /O2 /W3 c_client_example.c /I <paho_include> paho-mqtt3a.lib winhttp.lib ws2_32.lib
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <windows.h>
#include <winhttp.h>
#include "MQTTAsync.h"

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "ws2_32.lib")

#define PROXY_HTTP_PORT 18443
#define DEFAULT_MQTT_URI "tcp://127.0.0.1:1883"
#define DEFAULT_SN       "a3b1234567c10221d290825"
#define DEFAULT_INTERVAL_SEC 600  /* 10 minutes */

static volatile int g_connected = 0;
static volatile int g_publish_delivered = 0;

/* Generates UUID v4 string */
static void generate_uuid(char* out_uuid, size_t size) {
    GUID guid;
    if (CoCreateGuid(&guid) == S_OK) {
        snprintf(out_uuid, size, "%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
            guid.Data1, guid.Data2, guid.Data3,
            guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3],
            guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]);
    } else {
        snprintf(out_uuid, size, "d9afcbfa-3d2c-4304-8e69-f644fef29f1f");
    }
}

/* Formats ISO-8601 timestamp with Moscow (+03:00) timezone offset */
static void get_iso_time_with_offset(char* out_time, size_t size) {
    SYSTEMTIME st;
    GetSystemTime(&st); /* UTC */

    /* Moscow time is UTC+3 */
    FILETIME ft;
    SystemTimeToFileTime(&st, &ft);
    ULARGE_INTEGER uli;
    uli.LowPart = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;
    uli.QuadPart += (ULONGLONG)3 * 3600 * 10000000; /* +3 hours */
    ft.dwLowDateTime = uli.LowPart;
    ft.dwHighDateTime = uli.HighPart;
    FileTimeToSystemTime(&ft, &st);

    snprintf(out_time, size, "%04u-%02u-%02uT%02u:%02u:%02u+03:00",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
}

/* Queries active Device SN from local Leo4Proxy REST endpoint */
static int query_device_sn(char* out_sn, size_t out_sn_size) {
    HINTERNET hSession = WinHttpOpen(L"Leo4Client/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return -1;

    HINTERNET hConnect = WinHttpConnect(hSession, L"127.0.0.1", PROXY_HTTP_PORT, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return -1; }

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", L"/_leo4/sn", NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
    if (!hRequest) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return -1; }

    int rc = -1;
    if (WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(hRequest, NULL)) {
        DWORD dwSize = 0;
        WinHttpQueryDataAvailable(hRequest, &dwSize);
        if (dwSize > 0 && dwSize < out_sn_size) {
            DWORD dwDownloaded = 0;
            if (WinHttpReadData(hRequest, out_sn, dwSize, &dwDownloaded)) {
                out_sn[dwDownloaded] = '\0';
                /* Trim trailing whitespace/newlines */
                for (int i = (int)dwDownloaded - 1; i >= 0; i--) {
                    if (out_sn[i] == '\r' || out_sn[i] == '\n' || out_sn[i] == ' ') {
                        out_sn[i] = '\0';
                    } else {
                        break;
                    }
                }
                rc = 0;
            }
        }
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return rc;
}

static void on_connected(void* context, char* cause) {
    (void)context; (void)cause;
    g_connected = 1;
    printf("[MQTT-C] Successfully connected to Mosquitto Bridge as 'extra_service'!\n");
}

static void on_connect_failure(void* context, MQTTAsync_failureData5* response) {
    (void)context;
    g_connected = 0;
    printf("[MQTT-C] Connection failed: rc=%d\n", response ? response->code : -1);
}

static void on_publish_success(void* context, MQTTAsync_successData5* response) {
    (void)context; (void)response;
    g_publish_delivered = 1;
    printf("    [ACK] Event delivered successfully to broker!\n");
}

static void on_publish_failure(void* context, MQTTAsync_failureData5* response) {
    (void)context;
    printf("    [WARN] Event delivery failed: rc=%d\n", response ? response->code : -1);
}

int main(int argc, char* argv[]) {
    int interval_sec = DEFAULT_INTERVAL_SEC;
    int run_once = 0;
    const char* mqtt_uri = DEFAULT_MQTT_URI;
    const char* username = "extra_service";

    for (int a = 1; a < argc; a++) {
        if (strcmp(argv[a], "--once") == 0 || strcmp(argv[a], "-1") == 0) {
            run_once = 1;
        } else if (strcmp(argv[a], "--interval") == 0 && a + 1 < argc) {
            interval_sec = atoi(argv[++a]);
            if (interval_sec <= 0) interval_sec = DEFAULT_INTERVAL_SEC;
        } else if (strcmp(argv[a], "--uri") == 0 && a + 1 < argc) {
            mqtt_uri = argv[++a];
        }
    }

    printf("================================================================\n");
    printf("  Leo4 IoT C Event Publisher (extra_service role)\n");
    printf("  Target: Mosquitto Bridge (%s, Plain TCP No-SSL)\n", mqtt_uri);
    printf("  Interval: %d sec (10 min), Topic: dev/<SN>/evt (QoS 1, Retain 0)\n", interval_sec);
    printf("================================================================\n\n");

    char sn[128] = { 0 };
    if (query_device_sn(sn, sizeof(sn)) != 0 || strlen(sn) == 0) {
        const char* env_sn = getenv("DEVICE_SN");
        if (env_sn && strlen(env_sn) > 0) {
            strncpy_s(sn, sizeof(sn), env_sn, _TRUNCATE);
        } else {
            strncpy_s(sn, sizeof(sn), DEFAULT_SN, _TRUNCATE);
        }
        printf("[INFO] Using configured Device SN: %s\n", sn);
    } else {
        printf("[INFO] Obtained active Device SN from local proxy: %s\n", sn);
    }

    char topic[256];
    snprintf(topic, sizeof(topic), "dev/%s/evt", sn);

    char client_id[128];
    snprintf(client_id, sizeof(client_id), "%s_extra_c", sn);

    MQTTAsync client;
    MQTTAsync_createOptions create_opts = MQTTAsync_createOptions_initializer5;
    create_opts.MQTTVersion = MQTTVERSION_5;

    int rc = MQTTAsync_createWithOptions(&client, mqtt_uri, client_id, MQTTCLIENT_PERSISTENCE_NONE, NULL, &create_opts);
    if (rc != MQTTASYNC_SUCCESS) {
        fprintf(stderr, "[FATAL] MQTTAsync_createWithOptions failed: %d\n", rc);
        return 1;
    }

    MQTTAsync_setConnected(client, NULL, on_connected);

    MQTTAsync_connectOptions conn_opts = MQTTAsync_connectOptions_initializer5;
    conn_opts.keepAliveInterval = 60;
    conn_opts.cleanstart = 1;
    conn_opts.MQTTVersion = MQTTVERSION_5;
    conn_opts.username = username;
    conn_opts.password = NULL;
    conn_opts.ssl = NULL; /* Plain TCP No-SSL */
    conn_opts.onFailure5 = on_connect_failure;

    printf("[INFO] Connecting to %s as '%s'...\n", mqtt_uri, username);
    rc = MQTTAsync_connect(client, &conn_opts);
    if (rc != MQTTASYNC_SUCCESS) {
        fprintf(stderr, "[FATAL] MQTTAsync_connect failed: %d\n", rc);
        MQTTAsync_destroy(&client);
        return 1;
    }

    /* Wait for initial connection */
    for (int wait_i = 0; wait_i < 20 && !g_connected; wait_i++) {
        Sleep(100);
    }

    int iteration = 0;
    int base_event_id = 36823;

    while (1) {
        iteration++;
        int dev_event_id = base_event_id + (iteration - 1);

        char iso_time[64];
        get_iso_time_with_offset(iso_time, sizeof(iso_time));

        char corr_id[64];
        generate_uuid(corr_id, sizeof(corr_id));

        time_t now_ts = time(NULL);
        char ts_str[32];
        snprintf(ts_str, sizeof(ts_str), "%lld", (long long)now_ts);

        char event_id_str[32];
        snprintf(event_id_str, sizeof(event_id_str), "%d", dev_event_id);

        /* Build JSON payload */
        char payload[512];
        snprintf(payload, sizeof(payload),
            "{\"101\":%d,\"102\":\"%s\",\"200\":888,\"300\":[{\"301\":\"044AFE42C76781\",\"302\":6,\"303\":0}]}",
            dev_event_id, iso_time);

        printf("\n[%s] Publishing Event #%d:\n", iso_time, iteration);
        printf("  Topic:           %s\n", topic);
        printf("  QoS:             1 (Retain: 0)\n");
        printf("  Payload:         %s\n", payload);
        printf("  User Properties: event_type_code=888, dev_event_id=%s, dev_timestamp=%s, correlation_id=%s\n",
            event_id_str, ts_str, corr_id);

        /* Set MQTT 5.0 User Properties */
        MQTTProperties props = MQTTProperties_initializer;
        MQTTProperty prop_type, prop_id, prop_ts, prop_corr;

        prop_type.identifier = MQTTPROPERTY_CODE_USER_PROPERTY;
        prop_type.value.data.data = "event_type_code";
        prop_type.value.data.len = (int)strlen("event_type_code");
        prop_type.value.value.data = "888";
        prop_type.value.value.len = (int)strlen("888");
        MQTTProperties_add(&props, &prop_type);

        prop_id.identifier = MQTTPROPERTY_CODE_USER_PROPERTY;
        prop_id.value.data.data = "dev_event_id";
        prop_id.value.data.len = (int)strlen("dev_event_id");
        prop_id.value.value.data = event_id_str;
        prop_id.value.value.len = (int)strlen(event_id_str);
        MQTTProperties_add(&props, &prop_id);

        prop_ts.identifier = MQTTPROPERTY_CODE_USER_PROPERTY;
        prop_ts.value.data.data = "dev_timestamp";
        prop_ts.value.data.len = (int)strlen("dev_timestamp");
        prop_ts.value.value.data = ts_str;
        prop_ts.value.value.len = (int)strlen(ts_str);
        MQTTProperties_add(&props, &prop_ts);

        prop_corr.identifier = MQTTPROPERTY_CODE_USER_PROPERTY;
        prop_corr.value.data.data = "correlation_id";
        prop_corr.value.data.len = (int)strlen("correlation_id");
        prop_corr.value.value.data = corr_id;
        prop_corr.value.value.len = (int)strlen(corr_id);
        MQTTProperties_add(&props, &prop_corr);

        /* Configure message */
        MQTTAsync_message pubmsg = MQTTAsync_message_initializer;
        pubmsg.payload = payload;
        pubmsg.payloadlen = (int)strlen(payload);
        pubmsg.qos = 1;
        pubmsg.retained = 0;
        pubmsg.properties = props;

        MQTTAsync_responseOptions resp_opts = MQTTAsync_responseOptions_initializer;
        resp_opts.onSuccess5 = on_publish_success;
        resp_opts.onFailure5 = on_publish_failure;

        g_publish_delivered = 0;
        rc = MQTTAsync_sendMessage(client, topic, &pubmsg, &resp_opts);
        if (rc != MQTTASYNC_SUCCESS) {
            printf("    [WARN] MQTTAsync_sendMessage failed: %d\n", rc);
        }

        MQTTProperties_free(&props);

        /* Wait for delivery acknowledgment */
        for (int i = 0; i < 30 && !g_publish_delivered; i++) {
            Sleep(100);
        }

        if (run_once) {
            printf("\n[INFO] Single event sent (--once). Exiting.\n");
            break;
        }

        printf("\n[SLEEP] Waiting %d seconds (10 minutes) until next event publication...\n", interval_sec);
        Sleep(interval_sec * 1000);
    }

    printf("[INFO] Disconnecting...\n");
    MQTTAsync_disconnect(client, NULL);
    MQTTAsync_destroy(&client);

    printf("[SUCCESS] C Extra Service completed.\n");
    return 0;
}
