/**
 * @file c_client_example.c
 * @brief C Client Example using Eclipse Paho MQTT (Plain TCP) + Leo4Proxy (SChannel mTLS).
 *
 * NOTE: Paho C is compiled with PAHO_WITH_SSL=OFF. No OpenSSL DLLs or PEM files required!
 * The local Leo4Proxy running at 127.0.0.1:18883 transparently terminates mTLS with
 * the non-exportable certificate from Windows Certificate Store.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <winhttp.h>
#include "MQTTAsync.h"

#pragma comment(lib, "winhttp.lib")

#define PROXY_HTTP_PORT 18443
#define PROXY_MQTT_URI  "tcp://127.0.0.1:18883"

// Helper to query active SN from Leo4Proxy HTTP endpoint
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
    printf("[MQTT-C] Successfully connected to Leo4 MQTT Broker via SChannel Proxy!\n");
}

static void on_connect_failure(void* context, MQTTAsync_failureData5* response) {
    (void)context;
    printf("[MQTT-C] Connection failed: rc=%d\n", response ? response->code : -1);
}

int main(void) {
    printf("================================================================\n");
    printf("  Leo4 IoT C Client (Paho C Plain TCP + Leo4Proxy mTLS)\n");
    printf("================================================================\n\n");

    char sn[128] = { 0 };
    if (query_device_sn(sn, sizeof(sn)) != 0) {
        printf("[WARN] Could not query SN from local proxy, using default test SN.\n");
        strncpy_s(sn, sizeof(sn), "a4b0000773c82116d210826", _TRUNCATE);
    } else {
        printf("[INFO] Obtained active Device SN from local proxy: %s\n", sn);
    }

    MQTTAsync client;
    MQTTAsync_createOptions create_opts = MQTTAsync_createOptions_initializer5;
    create_opts.MQTTVersion = MQTTVERSION_5;

    int rc = MQTTAsync_createWithOptions(&client, PROXY_MQTT_URI, sn, MQTTCLIENT_PERSISTENCE_NONE, NULL, &create_opts);
    if (rc != MQTTASYNC_SUCCESS) {
        fprintf(stderr, "[FATAL] MQTTAsync_createWithOptions failed: %d\n", rc);
        return 1;
    }

    MQTTAsync_setConnected(client, NULL, on_connected);

    MQTTAsync_connectOptions conn_opts = MQTTAsync_connectOptions_initializer5;
    conn_opts.keepAliveInterval = 60;
    conn_opts.cleanstart = 1;
    conn_opts.MQTTVersion = MQTTVERSION_5;
    conn_opts.ssl = NULL; // Plain TCP to 127.0.0.1:18883 (no OpenSSL required!)
    conn_opts.onFailure5 = on_connect_failure;

    printf("[INFO] Connecting to %s...\n", PROXY_MQTT_URI);
    rc = MQTTAsync_connect(client, &conn_opts);
    if (rc != MQTTASYNC_SUCCESS) {
        fprintf(stderr, "[FATAL] MQTTAsync_connect failed: %d\n", rc);
        MQTTAsync_destroy(&client);
        return 1;
    }

    Sleep(3000);

    printf("[INFO] Disconnecting...\n");
    MQTTAsync_disconnect(client, NULL);
    MQTTAsync_destroy(&client);

    printf("[SUCCESS] C Client demonstration completed.\n");
    return 0;
}
