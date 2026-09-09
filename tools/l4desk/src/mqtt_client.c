#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "mqtt_client.h"
#include "mqtt_protocol.h"
#include "ctl_protocol.h"
#include "desktop_state.h"
#include "dedup_cache.h"
#include "sn_discovery.h"
#include "log.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#pragma comment(lib, "ws2_32.lib")

typedef struct {
    SOCKET sock;
    CRITICAL_SECTION send_cs;
    uint16_t packet_id_counter;
    char sn[64];
    time_t last_presence_time;
    bool last_desk_avail;
    ScreenMetrics last_screen;
} MqttState;

static uint16_t get_next_packet_id(MqttState* st) {
    st->packet_id_counter++;
    if (st->packet_id_counter == 0) st->packet_id_counter = 1;
    return st->packet_id_counter;
}

static bool socket_send_all(SOCKET s, const unsigned char* buf, size_t len) {
    size_t total_sent = 0;
    while (total_sent < len) {
        int sent = send(s, (const char*)(buf + total_sent), (int)(len - total_sent), 0);
        if (sent <= 0) {
            return false;
        }
        total_sent += (size_t)sent;
    }
    return true;
}

static bool send_publish_packet(MqttState* st, const char* topic, const void* payload, size_t payload_len, uint8_t qos, uint8_t retain) {
    EnterCriticalSection(&st->send_cs);
    uint16_t pkt_id = (qos > 0) ? get_next_packet_id(st) : 0;
    unsigned char buf[4096];
    int len = mqtt_build_publish(buf, sizeof(buf), topic, payload, payload_len, pkt_id, qos, retain);
    bool ok = false;
    if (len > 0) {
        ok = socket_send_all(st->sock, buf, (size_t)len);
    }
    LeaveCriticalSection(&st->send_cs);
    return ok;
}

static void publish_presence(MqttState* st, const char* status) {
    char topic[128];
    snprintf(topic, sizeof(topic), "dev/%s/ctl", st->sn);

    ScreenMetrics screen;
    desktop_get_screen_metrics(&screen);
    bool desk_avail = desktop_is_interactive_available();

    char payload[512];
    int len = ctl_build_presence_payload(payload, sizeof(payload), status, desk_avail, &screen);
    if (len > 0) {
        send_publish_packet(st, topic, payload, (size_t)len, 1, 1); // retain=1, qos=1
        log_info("Published presence [%s]: %s (retain=1, qos=1)", status, payload);
        st->last_presence_time = time(NULL);
        st->last_desk_avail = desk_avail;
        st->last_screen = screen;
    }
}

static bool screen_changed(const ScreenMetrics* a, const ScreenMetrics* b) {
    return (a->virtual_x != b->virtual_x ||
            a->virtual_y != b->virtual_y ||
            a->virtual_width != b->virtual_width ||
            a->virtual_height != b->virtual_height);
}

int mqtt_client_run(const L4DeskConfig* config, HANDLE hStopEvent) {
    if (!config) return 1;

    MqttState state;
    memset(&state, 0, sizeof(state));
    state.sock = INVALID_SOCKET;
    InitializeCriticalSection(&state.send_cs);
    dedup_cache_init();

    // 1. Resolve terminal SN
    if (config->sn_explicitly_set && config->sn[0] != '\0') {
        strcpy_s(state.sn, sizeof(state.sn), config->sn);
        log_info("Using explicitly configured terminal SN: %s", state.sn);
    } else {
        log_info("Discovering terminal SN from Leo4Proxy on port %d...", config->proxy_http_port);
        if (!sn_discovery_wait_for_sn(config->proxy_http_port, state.sn, sizeof(state.sn), hStopEvent)) {
            log_error("Stop signal received while waiting for SN.");
            DeleteCriticalSection(&state.send_cs);
            return 0;
        }
    }

    int current_backoff = config->reconnect_sec > 0 ? config->reconnect_sec : 5;

    // 2. Main reconnection loop
    while (hStopEvent == NULL || WaitForSingleObject(hStopEvent, 0) != WAIT_OBJECT_0) {
        log_info("Connecting to MQTT broker at %s:%d (client_id=%s)...",
                 config->mqtt_host, config->mqtt_port, config->client_id);

        SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (s == INVALID_SOCKET) {
            log_error("socket() creation failed: %d", WSAGetLastError());
            Sleep((DWORD)current_backoff * 1000);
            continue;
        }

        // Disable Nagle's algorithm for minimal command latency
        int nodelay = 1;
        setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char*)&nodelay, sizeof(nodelay));

        struct sockaddr_in saddr;
        memset(&saddr, 0, sizeof(saddr));
        saddr.sin_family = AF_INET;
        saddr.sin_port = htons((u_short)config->mqtt_port);
        inet_pton(AF_INET, config->mqtt_host, &saddr.sin_addr);

        if (connect(s, (struct sockaddr*)&saddr, sizeof(saddr)) == SOCKET_ERROR) {
            log_warn("Failed to connect to broker at %s:%d (err=%d). Retrying in %ds...",
                     config->mqtt_host, config->mqtt_port, WSAGetLastError(), current_backoff);
            closesocket(s);
            if (WaitForSingleObject(hStopEvent, (DWORD)current_backoff * 1000) == WAIT_OBJECT_0) {
                break;
            }
            current_backoff = current_backoff < 60 ? current_backoff * 2 : 60;
            continue;
        }

        current_backoff = config->reconnect_sec > 0 ? config->reconnect_sec : 5;
        state.sock = s;

        // Prepare LWT: topic dev/<SN>/ctl, payload offline, retain=1, qos=1
        char will_topic[128];
        snprintf(will_topic, sizeof(will_topic), "dev/%s/ctl", state.sn);
        char will_payload[512];
        ctl_build_presence_payload(will_payload, sizeof(will_payload), "offline", false, NULL);

        unsigned char connect_buf[2048];
        int conn_len = mqtt_build_connect(connect_buf, sizeof(connect_buf),
                                         config->client_id,
                                         will_topic,
                                         will_payload,
                                         NULL,
                                         (uint16_t)config->keepalive_sec);
        if (conn_len <= 0 || !socket_send_all(s, connect_buf, (size_t)conn_len)) {
            log_error("Failed to send MQTT CONNECT packet");
            closesocket(s);
            state.sock = INVALID_SOCKET;
            Sleep(2000);
            continue;
        }

        // Wait for CONNACK (4 bytes: 0x20, 0x02, session_present, return_code)
        unsigned char connack_buf[4];
        int cr = recv(s, (char*)connack_buf, 4, 0);
        if (cr != 4 || connack_buf[0] != MQTT_PKT_CONNACK || connack_buf[3] != 0) {
            log_error("MQTT CONNACK failed or rejected (rc=%d)", cr == 4 ? connack_buf[3] : -1);
            closesocket(s);
            state.sock = INVALID_SOCKET;
            Sleep(2000);
            continue;
        }

        log_info("MQTT Connected successfully (rc=0)");

        // 3. Subscribe to srv/<SN>/ctl QoS 1
        char sub_topic[128];
        snprintf(sub_topic, sizeof(sub_topic), "srv/%s/ctl", state.sn);
        unsigned char sub_buf[256];
        uint16_t sub_pkt_id = get_next_packet_id(&state);
        int sub_len = mqtt_build_subscribe(sub_buf, sizeof(sub_buf), sub_topic, sub_pkt_id, 1);
        if (sub_len > 0) {
            socket_send_all(s, sub_buf, (size_t)sub_len);
            log_info("Subscribed to control topic: %s (QoS 1)", sub_topic);
        }

        // 4. Publish dev/<SN>/ctl presence online (retain=1, qos=1)
        publish_presence(&state, "online");

        // 5. Active connection polling loop
        time_t last_ping_time = time(NULL);
        time_t ping_interval = config->keepalive_sec > 4 ? config->keepalive_sec / 2 : 2;

        unsigned char rx_buf[16384];
        size_t rx_buf_len = 0;

        while (hStopEvent == NULL || WaitForSingleObject(hStopEvent, 0) != WAIT_OBJECT_0) {
            fd_set read_fds;
            FD_ZERO(&read_fds);
            FD_SET(s, &read_fds);

            struct timeval tv;
            tv.tv_sec = 0;
            tv.tv_usec = 100000; // 100 ms

            int sel_rc = select(0, &read_fds, NULL, NULL, &tv);
            if (sel_rc == SOCKET_ERROR) {
                log_warn("Socket select error: %d", WSAGetLastError());
                break;
            }

            if (sel_rc > 0 && FD_ISSET(s, &read_fds)) {
                int bytes_recvd = recv(s, (char*)(rx_buf + rx_buf_len), (int)(sizeof(rx_buf) - rx_buf_len), 0);
                if (bytes_recvd <= 0) {
                    log_warn("Socket disconnected by broker.");
                    break;
                }
                rx_buf_len += (size_t)bytes_recvd;

                // Process MQTT packets
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
                            // Acknowledge QoS 1 publish immediately with PUBACK
                            uint8_t qos = (pkt_flags >> 1) & 0x03;
                            if (qos == 1 && pkt_id_in > 0) {
                                unsigned char puback_buf[4];
                                int pa_len = mqtt_build_puback(puback_buf, pkt_id_in);
                                EnterCriticalSection(&state.send_cs);
                                socket_send_all(s, puback_buf, (size_t)pa_len);
                                LeaveCriticalSection(&state.send_cs);
                            }

                            // Process command payload
                            char resp_buf[1024];
                            size_t resp_len = 0;
                            bool should_pub = false;
                            uint8_t resp_qos = 1;

                            if (ctl_handle_command(payload_ptr, payload_len, state.sn,
                                                   resp_buf, sizeof(resp_buf), &resp_len,
                                                   &should_pub, &resp_qos)) {
                                if (should_pub && resp_len > 0) {
                                    char out_topic[128];
                                    snprintf(out_topic, sizeof(out_topic), "dev/%s/ctl", state.sn);
                                    send_publish_packet(&state, out_topic, resp_buf, resp_len, resp_qos, 0); // Commands/ACKs: retain=0
                                    log_debug("Published ACK/NACK to %s: %s", out_topic, resp_buf);
                                }
                            }
                        }
                    } else if (pkt_type == MQTT_PKT_PINGRESP) {
                        last_ping_time = time(NULL);
                    }

                    offset += total_pkt_len;
                }

                if (offset > 0) {
                    if (offset < rx_buf_len) {
                        memmove(rx_buf, rx_buf + offset, rx_buf_len - offset);
                    }
                    rx_buf_len -= offset;
                }
            }

            // Periodic PINGREQ keepalive
            time_t now = time(NULL);
            if (now - last_ping_time >= ping_interval) {
                unsigned char ping_buf[2];
                int p_len = mqtt_build_pingreq(ping_buf);
                EnterCriticalSection(&state.send_cs);
                socket_send_all(s, ping_buf, (size_t)p_len);
                LeaveCriticalSection(&state.send_cs);
                last_ping_time = now;
            }

            // Periodic presence check & republish
            ScreenMetrics cur_screen;
            desktop_get_screen_metrics(&cur_screen);
            bool cur_avail = desktop_is_interactive_available();

            bool changed = (cur_avail != state.last_desk_avail) || screen_changed(&cur_screen, &state.last_screen);
            if ((now - state.last_presence_time >= config->presence_interval_sec) ||
                (changed && (now - state.last_presence_time >= 5))) {
                publish_presence(&state, "online");
            }
        }

        // Graceful disconnect on shutdown
        if (hStopEvent != NULL && WaitForSingleObject(hStopEvent, 0) == WAIT_OBJECT_0) {
            log_info("Graceful shutdown: publishing dev/%s/ctl offline presence...", state.sn);
            publish_presence(&state, "offline");

            unsigned char disc_buf[2];
            int disc_len = mqtt_build_disconnect(disc_buf);
            if (disc_len > 0) {
                socket_send_all(s, disc_buf, (size_t)disc_len);
            }
            closesocket(s);
            state.sock = INVALID_SOCKET;
            break;
        }

        closesocket(s);
        state.sock = INVALID_SOCKET;
        Sleep(2000);
    }

    DeleteCriticalSection(&state.send_cs);
    dedup_cache_cleanup();
    return 0;
}
