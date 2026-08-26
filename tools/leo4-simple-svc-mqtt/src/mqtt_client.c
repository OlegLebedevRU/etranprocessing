/**
 * @file mqtt_client.c
 * @brief Lightweight zero-dependency MQTT 3.1.1 Presence and LWT client (extra_service).
 */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WINSOCK_DEPRECATED_NO_WARNINGS
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <winhttp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "mqtt_client.h"

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "winhttp.lib")

#define MQTT_PKT_CONNECT     0x10
#define MQTT_PKT_CONNACK     0x20
#define MQTT_PKT_PUBLISH     0x30
#define MQTT_PKT_PUBACK      0x40
#define MQTT_PKT_PINGREQ     0xC0
#define MQTT_PKT_PINGRESP    0xD0
#define MQTT_PKT_DISCONNECT  0xE0

#define MQTT_FLAG_CLEAN_SESSION 0x02
#define MQTT_FLAG_WILL_FLAG     0x04
#define MQTT_FLAG_WILL_QOS1     0x08
#define MQTT_FLAG_WILL_RETAIN   0x20
#define MQTT_FLAG_USERNAME      0x80

static void get_iso_timestamp(char* out_ts, size_t size) {
    SYSTEMTIME st;
    GetLocalTime(&st);
    snprintf(out_ts, size, "%04u-%02u-%02u %02u:%02u:%02u",
             st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
}

int mqtt_client_query_sn(int proxy_port, char* out_sn, size_t out_sn_size) {
    if (!out_sn || out_sn_size == 0) return -1;
    out_sn[0] = '\0';

    HINTERNET hSession = WinHttpOpen(L"Leo4SimpleSvcMqtt/1.0",
                                    WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                    WINHTTP_NO_PROXY_NAME,
                                    WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return -1;

    // Set short connect and receive timeouts (2 seconds)
    WinHttpSetTimeouts(hSession, 1000, 1000, 2000, 2000);

    HINTERNET hConnect = WinHttpConnect(hSession, L"127.0.0.1", (INTERNET_PORT)proxy_port, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        return -1;
    }

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", L"/_leo4/sn",
                                           NULL, WINHTTP_NO_REFERER,
                                           WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return -1;
    }

    int rc = -1;
    if (WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                           WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(hRequest, NULL)) {

        DWORD dwSize = 0;
        WinHttpQueryDataAvailable(hRequest, &dwSize);
        if (dwSize > 0 && dwSize < out_sn_size) {
            DWORD dwDownloaded = 0;
            if (WinHttpReadData(hRequest, out_sn, dwSize, &dwDownloaded)) {
                out_sn[dwDownloaded] = '\0';
                // Trim trailing whitespaces and newlines
                for (int i = (int)dwDownloaded - 1; i >= 0; i--) {
                    if (out_sn[i] == '\r' || out_sn[i] == '\n' || out_sn[i] == ' ') {
                        out_sn[i] = '\0';
                    } else {
                        break;
                    }
                }
                if (strlen(out_sn) > 0) {
                    rc = 0;
                }
            }
        }
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return rc;
}

static int encode_remaining_length(unsigned char* buf, uint32_t length) {
    int count = 0;
    do {
        unsigned char d = (unsigned char)(length % 128);
        length /= 128;
        if (length > 0) {
            d |= 128;
        }
        buf[count++] = d;
    } while (length > 0);
    return count;
}

static int write_utf8_string(unsigned char* buf, const char* str) {
    size_t len = strlen(str);
    buf[0] = (unsigned char)((len >> 8) & 0xFF);
    buf[1] = (unsigned char)(len & 0xFF);
    memcpy(buf + 2, str, len);
    return (int)(2 + len);
}

static int build_connect_packet(unsigned char* buf, size_t max_len,
                                const char* client_id,
                                const char* will_topic,
                                const char* will_payload,
                                const char* username,
                                uint16_t keepalive_sec) {
    unsigned char payload_buf[1024];
    int p_pos = 0;

    // Variable Header (10 bytes):
    // Protocol Name: "MQTT" (length 4)
    p_pos += write_utf8_string(payload_buf + p_pos, "MQTT");
    // Protocol Level: 4 (MQTT 3.1.1)
    payload_buf[p_pos++] = 4;
    // Connect Flags: UserName | WillRetain | WillQoS1 | WillFlag | CleanSession = 0xAE
    payload_buf[p_pos++] = MQTT_FLAG_USERNAME | MQTT_FLAG_WILL_RETAIN | MQTT_FLAG_WILL_QOS1 | MQTT_FLAG_WILL_FLAG | MQTT_FLAG_CLEAN_SESSION;
    // Keep Alive
    payload_buf[p_pos++] = (unsigned char)((keepalive_sec >> 8) & 0xFF);
    payload_buf[p_pos++] = (unsigned char)(keepalive_sec & 0xFF);

    // Payload:
    // 1. Client Identifier
    p_pos += write_utf8_string(payload_buf + p_pos, client_id);
    // 2. Will Topic
    p_pos += write_utf8_string(payload_buf + p_pos, will_topic);
    // 3. Will Payload
    p_pos += write_utf8_string(payload_buf + p_pos, will_payload);
    // 4. Username
    p_pos += write_utf8_string(payload_buf + p_pos, username);

    // Fixed Header:
    int out_pos = 0;
    buf[out_pos++] = MQTT_PKT_CONNECT;
    out_pos += encode_remaining_length(buf + out_pos, (uint32_t)p_pos);

    if ((size_t)(out_pos + p_pos) > max_len) return -1;
    memcpy(buf + out_pos, payload_buf, p_pos);
    return out_pos + p_pos;
}

static int build_publish_packet(unsigned char* buf, size_t max_len,
                                const char* topic,
                                const char* payload,
                                uint16_t packet_id,
                                uint8_t qos,
                                uint8_t retain) {
    unsigned char body_buf[1024];
    int b_pos = 0;

    // Variable header:
    b_pos += write_utf8_string(body_buf + b_pos, topic);
    if (qos > 0) {
        body_buf[b_pos++] = (unsigned char)((packet_id >> 8) & 0xFF);
        body_buf[b_pos++] = (unsigned char)(packet_id & 0xFF);
    }

    // Payload:
    size_t payload_len = strlen(payload);
    if ((size_t)(b_pos + payload_len) > sizeof(body_buf)) return -1;
    memcpy(body_buf + b_pos, payload, payload_len);
    b_pos += (int)payload_len;

    // Fixed header:
    // Packet type PUBLISH (3) | QoS | Retain
    uint8_t header_byte = (uint8_t)(MQTT_PKT_PUBLISH | ((qos & 0x03) << 1) | (retain & 0x01));

    int out_pos = 0;
    buf[out_pos++] = header_byte;
    out_pos += encode_remaining_length(buf + out_pos, (uint32_t)b_pos);

    if ((size_t)(out_pos + b_pos) > max_len) return -1;
    memcpy(buf + out_pos, body_buf, b_pos);
    return out_pos + b_pos;
}

static int build_pingreq_packet(unsigned char* buf) {
    buf[0] = (unsigned char)MQTT_PKT_PINGREQ;
    buf[1] = 0x00;
    return 2;
}

static int build_disconnect_packet(unsigned char* buf) {
    buf[0] = (unsigned char)MQTT_PKT_DISCONNECT;
    buf[1] = 0x00;
    return 2;
}

static bool sock_send_all(SOCKET sock, const unsigned char* buf, int len) {
    int total_sent = 0;
    while (total_sent < len) {
        int sent = send(sock, (const char*)(buf + total_sent), len - total_sent, 0);
        if (sent <= 0) {
            return false;
        }
        total_sent += sent;
    }
    return true;
}

static bool sock_recv_exact(SOCKET sock, unsigned char* buf, int len, int timeout_ms, HANDLE stop_event) {
    int total_received = 0;
    DWORD start_tick = GetTickCount();

    while (total_received < len) {
        if (stop_event && WaitForSingleObject(stop_event, 0) == WAIT_OBJECT_0) {
            return false;
        }

        DWORD elapsed = GetTickCount() - start_tick;
        if ((int)elapsed >= timeout_ms) {
            return false;
        }
        int remaining_timeout = timeout_ms - (int)elapsed;

        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(sock, &read_fds);

        struct timeval tv;
        tv.tv_sec = remaining_timeout / 1000;
        tv.tv_usec = (remaining_timeout % 1000) * 1000;

        int select_rc = select(0, &read_fds, NULL, NULL, &tv);
        if (select_rc <= 0) {
            return false;
        }

        int n = recv(sock, (char*)(buf + total_received), len - total_received, 0);
        if (n <= 0) {
            return false;
        }
        total_received += n;
    }
    return true;
}

static int sock_wait_connack(SOCKET sock, int timeout_ms, HANDLE stop_event) {
    unsigned char header[2];
    if (!sock_recv_exact(sock, header, 2, timeout_ms, stop_event)) {
        return -1;
    }

    if (header[0] != MQTT_PKT_CONNACK || header[1] != 0x02) {
        return -2;
    }

    unsigned char body[2];
    if (!sock_recv_exact(sock, body, 2, timeout_ms, stop_event)) {
        return -1;
    }

    // body[0] = Session Present, body[1] = Connect Return code (0 = Accepted)
    return (int)body[1];
}

static bool sock_wait_puback(SOCKET sock, uint16_t expected_pid, int timeout_ms, HANDLE stop_event) {
    unsigned char header[2];
    if (!sock_recv_exact(sock, header, 2, timeout_ms, stop_event)) {
        return false;
    }

    if (header[0] != MQTT_PKT_PUBACK || header[1] != 0x02) {
        return false;
    }

    unsigned char body[2];
    if (!sock_recv_exact(sock, body, 2, timeout_ms, stop_event)) {
        return false;
    }

    uint16_t pid = (uint16_t)((body[0] << 8) | body[1]);
    return (pid == expected_pid);
}

int mqtt_client_run(const AppConfig* config, HANDLE stop_event) {
    char ts[64];
    get_iso_timestamp(ts, sizeof(ts));

    printf("[%s] [INFO] Starting %s (role: %s)\n", ts, LEO4_SVC_APP_NAME, config->role);
    printf("[%s] [INFO] Target Broker: %s:%d (Plain TCP No-SSL)\n", ts, config->mqtt_host, config->mqtt_port);

    char sn[128] = { 0 };

    // Resolve SN
    if (strlen(config->device_sn) > 0) {
        strncpy_s(sn, sizeof(sn), config->device_sn, _TRUNCATE);
        printf("[%s] [INFO] Using configured Device SN: %s\n", ts, sn);
    }

    unsigned char packet_buf[2048];

    // Main Service / Reconnect Loop
    while (WaitForSingleObject(stop_event, 0) == WAIT_TIMEOUT) {
        // If SN not yet resolved, query Leo4Proxy REST endpoint
        if (strlen(sn) == 0) {
            get_iso_timestamp(ts, sizeof(ts));
            if (mqtt_client_query_sn(config->proxy_http_port, sn, sizeof(sn)) == 0 && strlen(sn) > 0) {
                printf("[%s] [INFO] Obtained active Device SN from local proxy: %s\n", ts, sn);
            } else {
                const char* env_sn = getenv("DEVICE_SN");
                if (env_sn && strlen(env_sn) > 0) {
                    strncpy_s(sn, sizeof(sn), env_sn, _TRUNCATE);
                    printf("[%s] [INFO] Using environment Device SN: %s\n", ts, sn);
                } else {
                    printf("[%s] [WARN] Could not query Device SN from proxy :%d. Retrying...\n",
                           ts, config->proxy_http_port);
                    // Wait 2 seconds before retry
                    for (int w = 0; w < 4 && WaitForSingleObject(stop_event, 500) == WAIT_TIMEOUT; w++) {}
                    if (WaitForSingleObject(stop_event, 0) == WAIT_OBJECT_0) break;
                    continue;
                }
            }
        }

        char presence_topic[256];
        snprintf(presence_topic, sizeof(presence_topic), "dev/%s/svc", sn);

        char client_id[128];
        snprintf(client_id, sizeof(client_id), "%s_extra", sn);

        const char* offline_payload = "svc_offline";
        const char* online_payload  = "svc_online";

        get_iso_timestamp(ts, sizeof(ts));
        printf("[%s] [INFO] Connecting to broker %s:%d as client_id '%s'...\n",
               ts, config->mqtt_host, config->mqtt_port, client_id);

        SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sock == INVALID_SOCKET) {
            get_iso_timestamp(ts, sizeof(ts));
            fprintf(stderr, "[%s] [ERROR] socket() failed: %d\n", ts, WSAGetLastError());
            for (int w = 0; w < config->reconnect_sec * 2 && WaitForSingleObject(stop_event, 500) == WAIT_TIMEOUT; w++) {}
            continue;
        }

        // Set socket TCP_NODELAY and KeepAlive
        int nodelay = 1;
        setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (const char*)&nodelay, sizeof(nodelay));

        struct sockaddr_in sin;
        memset(&sin, 0, sizeof(sin));
        sin.sin_family = AF_INET;
        sin.sin_port = htons((u_short)config->mqtt_port);

        struct in_addr addr;
        if (inet_pton(AF_INET, config->mqtt_host, &addr) == 1) {
            sin.sin_addr = addr;
        } else {
            struct hostent* he = gethostbyname(config->mqtt_host);
            if (he && he->h_addr_list[0]) {
                memcpy(&sin.sin_addr, he->h_addr_list[0], sizeof(struct in_addr));
            } else {
                sin.sin_addr.s_addr = inet_addr(config->mqtt_host);
            }
        }

        // Set non-blocking mode for connect timeout
        u_long mode = 1;
        ioctlsocket(sock, FIONBIO, &mode);

        int connect_rc = connect(sock, (struct sockaddr*)&sin, sizeof(sin));
        if (connect_rc == SOCKET_ERROR) {
            int err = WSAGetLastError();
            if (err == WSAEWOULDBLOCK) {
                fd_set write_fds, err_fds;
                FD_ZERO(&write_fds);
                FD_ZERO(&err_fds);
                FD_SET(sock, &write_fds);
                FD_SET(sock, &err_fds);

                struct timeval tv;
                tv.tv_sec = 3; // 3 sec connect timeout
                tv.tv_usec = 0;

                int sel = select(0, NULL, &write_fds, &err_fds, &tv);
                if (sel <= 0 || FD_ISSET(sock, &err_fds)) {
                    get_iso_timestamp(ts, sizeof(ts));
                    printf("[%s] [WARN] Connection to %s:%d failed. Retrying in %d sec...\n",
                           ts, config->mqtt_host, config->mqtt_port, config->reconnect_sec);
                    closesocket(sock);
                    for (int w = 0; w < config->reconnect_sec * 2 && WaitForSingleObject(stop_event, 500) == WAIT_TIMEOUT; w++) {}
                    continue;
                }
            } else {
                get_iso_timestamp(ts, sizeof(ts));
                printf("[%s] [WARN] connect() failed (%d). Retrying in %d sec...\n",
                       ts, err, config->reconnect_sec);
                closesocket(sock);
                for (int w = 0; w < config->reconnect_sec * 2 && WaitForSingleObject(stop_event, 500) == WAIT_TIMEOUT; w++) {}
                continue;
            }
        }

        // Return socket to blocking mode
        mode = 0;
        ioctlsocket(sock, FIONBIO, &mode);

        // 1. Build and send CONNECT packet with Will (LWT: dev/<SN>/svc = svc_offline, retain=1, QoS=1)
        int conn_len = build_connect_packet(packet_buf, sizeof(packet_buf),
                                            client_id,
                                            presence_topic,
                                            offline_payload,
                                            config->role,
                                            (uint16_t)config->keepalive_sec);
        if (conn_len <= 0 || !sock_send_all(sock, packet_buf, conn_len)) {
            get_iso_timestamp(ts, sizeof(ts));
            fprintf(stderr, "[%s] [ERROR] Failed to send CONNECT packet.\n", ts);
            closesocket(sock);
            continue;
        }

        get_iso_timestamp(ts, sizeof(ts));
        printf("[%s] [INFO] Sent CONNECT with LWT: %s -> %s (retain=1, qos=1)\n",
               ts, presence_topic, offline_payload);

        // 2. Wait for CONNACK
        int connack_rc = sock_wait_connack(sock, 5000, stop_event);
        if (connack_rc != 0) {
            get_iso_timestamp(ts, sizeof(ts));
            fprintf(stderr, "[%s] [ERROR] CONNACK failed or rejected (rc: %d).\n", ts, connack_rc);
            closesocket(sock);
            for (int w = 0; w < config->reconnect_sec * 2 && WaitForSingleObject(stop_event, 500) == WAIT_TIMEOUT; w++) {}
            continue;
        }

        get_iso_timestamp(ts, sizeof(ts));
        printf("[%s] [MQTT] Connection accepted (CONNACK rc=0)\n", ts);

        // 3. Publish online status: dev/<SN>/svc = svc_online (retain=1, QoS=1, packet_id=1)
        uint16_t online_pid = 1;
        int pub_len = build_publish_packet(packet_buf, sizeof(packet_buf),
                                           presence_topic,
                                           online_payload,
                                           online_pid,
                                           1, // QoS 1
                                           1  // Retain 1
        );

        if (pub_len > 0 && sock_send_all(sock, packet_buf, pub_len)) {
            get_iso_timestamp(ts, sizeof(ts));
            printf("[%s] [PRESENCE] Sent status: %s = %s (retain=1, qos=1)\n",
                   ts, presence_topic, online_payload);

            if (sock_wait_puback(sock, online_pid, 3000, stop_event)) {
                get_iso_timestamp(ts, sizeof(ts));
                printf("[%s] [ACK] Received PUBACK for %s = %s\n", ts, presence_topic, online_payload);
            }
        }

        // 4. Connected loop: ping every keepalive / 2, process incoming messages, check stop_event
        DWORD last_ping_tick = GetTickCount();
        DWORD session_start_tick = GetTickCount();
        DWORD ping_interval_ms = (DWORD)(config->keepalive_sec > 10 ? config->keepalive_sec / 2 : 5) * 1000;
        bool socket_alive = true;
        bool timed_out_exit = false;

        while (socket_alive && WaitForSingleObject(stop_event, 0) == WAIT_TIMEOUT) {
            // Check run_seconds limit
            if (config->run_seconds > 0) {
                DWORD elapsed_sec = (GetTickCount() - session_start_tick) / 1000;
                if (elapsed_sec >= (DWORD)config->run_seconds) {
                    timed_out_exit = true;
                    break;
                }
            }

            fd_set read_fds;
            FD_ZERO(&read_fds);
            FD_SET(sock, &read_fds);

            struct timeval tv;
            tv.tv_sec = 1;
            tv.tv_usec = 0;

            int sel = select(0, &read_fds, NULL, NULL, &tv);
            if (sel > 0 && FD_ISSET(sock, &read_fds)) {
                unsigned char in_buf[512];
                int bytes_read = recv(sock, (char*)in_buf, sizeof(in_buf), 0);
                if (bytes_read <= 0) {
                    get_iso_timestamp(ts, sizeof(ts));
                    printf("[%s] [WARN] Connection lost with broker (recv: %d).\n", ts, bytes_read);
                    socket_alive = false;
                    break;
                }

                if (config->verbose) {
                    get_iso_timestamp(ts, sizeof(ts));
                    printf("[%s] [DEBUG] Received %d bytes from broker (type: 0x%02X)\n",
                           ts, bytes_read, in_buf[0]);
                }
            } else if (sel < 0) {
                socket_alive = false;
                break;
            }

            // Periodic PINGREQ
            DWORD now = GetTickCount();
            if (now - last_ping_tick >= ping_interval_ms) {
                unsigned char ping_pkt[2];
                int ping_len = build_pingreq_packet(ping_pkt);
                if (!sock_send_all(sock, ping_pkt, ping_len)) {
                    get_iso_timestamp(ts, sizeof(ts));
                    printf("[%s] [WARN] Failed to send PINGREQ. Connection lost.\n", ts);
                    socket_alive = false;
                    break;
                }
                last_ping_tick = now;
                if (config->verbose) {
                    get_iso_timestamp(ts, sizeof(ts));
                    printf("[%s] [DEBUG] Sent PINGREQ (keepalive)\n", ts);
                }
            }
        }

        // 5. Check if stop was requested (graceful shutdown)
        if (WaitForSingleObject(stop_event, 0) == WAIT_OBJECT_0 || timed_out_exit) {
            get_iso_timestamp(ts, sizeof(ts));
            printf("[%s] [PRESENCE] Graceful shutdown requested. Publishing %s = %s (retain=1)...\n",
                   ts, presence_topic, offline_payload);

            if (socket_alive) {
                uint16_t offline_pid = 2;
                int off_len = build_publish_packet(packet_buf, sizeof(packet_buf),
                                                   presence_topic,
                                                   offline_payload,
                                                   offline_pid,
                                                   1, // QoS 1
                                                   1  // Retain 1
                );
                if (off_len > 0) {
                    sock_send_all(sock, packet_buf, off_len);
                    sock_wait_puback(sock, offline_pid, 1000, NULL);
                }

                unsigned char disc_pkt[2];
                int disc_len = build_disconnect_packet(disc_pkt);
                sock_send_all(sock, disc_pkt, disc_len);
            }

            closesocket(sock);
            get_iso_timestamp(ts, sizeof(ts));
            printf("[%s] [SUCCESS] Clean disconnect completed.\n", ts);
            break;
        }

        // Abnormal disconnect: close socket and loop to reconnect
        closesocket(sock);
        get_iso_timestamp(ts, sizeof(ts));
        printf("[%s] [INFO] Reconnecting in %d seconds...\n", ts, config->reconnect_sec);
        for (int w = 0; w < config->reconnect_sec * 2 && WaitForSingleObject(stop_event, 500) == WAIT_TIMEOUT; w++) {}
    }

    return 0;
}
