/**
 * @file l4media_ingress.c
 * @brief High-performance L4RTP/1 stream ingress server.
 *
 * Receives framed RTP/RTCP streams over TCP from Nginx mTLS termination,
 * validates L4RTP/1 preamble and device Serial Number (SN),
 * routes RTP/RTCP packets via UDP to Janus WebRTC gateway,
 * and provides a lightweight HTTP control & stats API on port 9100.
 *
 * Supports dynamic route updates on the fly: changes made via
 * PUT/POST /routes/<sn> immediately re-route active streaming sessions
 * without reconnecting the TCP client.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <inttypes.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/epoll.h>

#define INGRESS_PORT              9000
#define CONTROL_PORT              9100
#define MAX_EPOLL_EVENTS          64
#define MAX_ROUTES                512
#define MAX_CLIENTS               128
#define RX_BUFFER_SIZE            (128 * 1024)   /* 128 KB buffer per client */
#define HTTP_REQ_BUFFER_SIZE      8192
#define HTTP_RESP_BUFFER_SIZE     (64 * 1024)    /* 64 KB response buffer */
#define CLIENT_IDLE_TIMEOUT_SEC   120            /* Reap clients idle for >2 min */
#define RTP_STALE_DEADLINE_SEC    10             /* Threshold for media freshness */
#define MAX_RTP_PAYLOAD_SIZE      65507          /* Max UDP payload */

#define L4RTP_MAGIC               "L4RT"
#define L4RTP_VERSION             0x01
#define L4RTP_FRAME_RTP           0x01
#define L4RTP_FRAME_RTCP          0x02
#define L4RTP_FRAME_KEEPALIVE     0x03

typedef struct {
    char sn[128];
    int rtp_port;
    int rtcp_port;
    bool is_static;
} RouteEntry;

typedef struct {
    RouteEntry entries[MAX_ROUTES];
    int count;
} RouteTable;

typedef enum {
    STATE_PREAMBLE,
    STATE_FRAMING
} ClientState;

typedef struct {
    int fd;
    ClientState state;
    char sn[128];
    char peer_addr[64];
    uint64_t epoch;                 /* Monotonic connection epoch ID */

    /* Buffer for partial TCP frame accumulation */
    uint8_t rx_buf[RX_BUFFER_SIZE];
    size_t rx_len;

    /* Destination UDP targets */
    struct sockaddr_in rtp_target;
    struct sockaddr_in rtcp_target;
    int rtp_port;
    int rtcp_port;

    /* Metrics & Time tracking */
    uint64_t rtp_packets;
    uint64_t rtcp_packets;
    uint64_t unrouted_packets;
    uint64_t keepalive_packets;
    uint64_t total_bytes;
    time_t connect_time;
    time_t last_activity;           /* Last byte received over transport (TCP) */
    time_t last_rtp_time;           /* Last RTP media frame packet received (0 if none) */
    time_t last_rtcp_time;          /* Last RTCP packet received (0 if none) */
} IngressClient;

/* Global state */
static volatile bool g_running = true;
static RouteTable g_routes;
static IngressClient* g_clients[MAX_CLIENTS];
static int g_client_count = 0;
static uint64_t g_next_epoch = 1;
static int g_udp_sock = -1;
static struct in_addr g_janus_ip;
static char g_janus_host[128] = "janus";
static char g_routes_file[256] = "/etc/l4media/routes.conf";
static time_t g_server_start_time = 0;

static void handle_signal(int sig) {
    (void)sig;
    g_running = false;
}

static int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static inline void safe_strcpy(char* dst, const char* src, size_t dst_size) {
    if (!dst || dst_size == 0) return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    size_t len = strlen(src);
    if (len >= dst_size) {
        len = dst_size - 1;
    }
    memcpy(dst, src, len);
    dst[len] = '\0';
}

/* DNS resolution for Janus host */
static bool resolve_janus_host(void) {
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;

    if (getaddrinfo(g_janus_host, NULL, &hints, &res) != 0 || !res) {
        fprintf(stderr, "[INGRESS] Warning: Could not resolve '%s' via DNS, fallback to 127.0.0.1\n", g_janus_host);
        inet_pton(AF_INET, "127.0.0.1", &g_janus_ip);
        if (res) freeaddrinfo(res);
        return false;
    }
    struct sockaddr_in* sin = (struct sockaddr_in*)res->ai_addr;
    g_janus_ip = sin->sin_addr;
    char ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &g_janus_ip, ip_str, sizeof(ip_str));
    printf("[INGRESS] Target host '%s' -> %s\n", g_janus_host, ip_str);
    freeaddrinfo(res);
    return true;
}

/* Dynamically update active streaming client target sockets */
static int update_client_targets_for_sn(const char* sn, int rtp_port, int rtcp_port) {
    int count = 0;
    for (int i = 0; i < g_client_count; i++) {
        IngressClient* c = g_clients[i];
        if (c && strcmp(c->sn, sn) == 0) {
            c->rtp_port = rtp_port;
            c->rtcp_port = rtcp_port;
            memset(&c->rtp_target, 0, sizeof(c->rtp_target));
            memset(&c->rtcp_target, 0, sizeof(c->rtcp_target));
            if (rtp_port > 0) {
                c->rtp_target.sin_family = AF_INET;
                c->rtp_target.sin_addr = g_janus_ip;
                c->rtp_target.sin_port = htons((uint16_t)rtp_port);
            }
            if (rtcp_port > 0) {
                c->rtcp_target.sin_family = AF_INET;
                c->rtcp_target.sin_addr = g_janus_ip;
                c->rtcp_target.sin_port = htons((uint16_t)rtcp_port);
            }
            count++;
            printf("[INGRESS] Dynamically updated target for active client %s (SN: %s) -> RTP:%d RTCP:%d\n",
                   c->peer_addr, c->sn, rtp_port, rtcp_port);
        }
    }
    return count;
}

/* Route Table Management */
static void load_routes(const char* filepath) {
    FILE* fp = fopen(filepath, "r");
    if (!fp) {
        fprintf(stderr, "[INGRESS] Warning: Cannot open routes file %s: %s\n", filepath, strerror(errno));
        return;
    }

    g_routes.count = 0;
    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        char* p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || *p == '\r' || *p == '\n' || *p == '\0') continue;

        char sn[128];
        int rtp = 0, rtcp = 0;
        if (sscanf(p, "%127s %d %d", sn, &rtp, &rtcp) == 3) {
            if (g_routes.count < MAX_ROUTES) {
                safe_strcpy(g_routes.entries[g_routes.count].sn, sn, sizeof(g_routes.entries[0].sn));
                g_routes.entries[g_routes.count].rtp_port = rtp;
                g_routes.entries[g_routes.count].rtcp_port = rtcp;
                g_routes.entries[g_routes.count].is_static = true;
                printf("[INGRESS] Route loaded: SN=%s -> RTP:%d RTCP:%d (static)\n", sn, rtp, rtcp);
                g_routes.count++;
            }
        }
    }
    fclose(fp);
    printf("[INGRESS] Total routes loaded: %d\n", g_routes.count);
}

static const RouteEntry* find_route(const char* sn) {
    for (int i = 0; i < g_routes.count; i++) {
        if (strcmp(g_routes.entries[i].sn, sn) == 0) {
            return &g_routes.entries[i];
        }
    }
    return NULL;
}

static bool upsert_route(const char* sn, int rtp, int rtcp, int* out_clients_updated) {
    resolve_janus_host(); /* Refresh host IP if DNS changed */

    bool found = false;
    for (int i = 0; i < g_routes.count; i++) {
        if (strcmp(g_routes.entries[i].sn, sn) == 0) {
            g_routes.entries[i].rtp_port = rtp;
            g_routes.entries[i].rtcp_port = rtcp;
            found = true;
            break;
        }
    }
    if (!found) {
        if (g_routes.count < MAX_ROUTES) {
            safe_strcpy(g_routes.entries[g_routes.count].sn, sn, sizeof(g_routes.entries[0].sn));
            g_routes.entries[g_routes.count].rtp_port = rtp;
            g_routes.entries[g_routes.count].rtcp_port = rtcp;
            g_routes.entries[g_routes.count].is_static = false;
            g_routes.count++;
        } else {
            fprintf(stderr, "[INGRESS] Route table full (%d), cannot add SN %s\n", MAX_ROUTES, sn);
            return false;
        }
    }

    int updated = update_client_targets_for_sn(sn, rtp, rtcp);
    if (out_clients_updated) *out_clients_updated = updated;
    printf("[INGRESS] Route upserted: SN=%s -> RTP:%d RTCP:%d (active sessions updated: %d)\n",
           sn, rtp, rtcp, updated);
    return true;
}

static bool delete_route(const char* sn, int* out_clients_updated) {
    bool found = false;
    for (int i = 0; i < g_routes.count; i++) {
        if (strcmp(g_routes.entries[i].sn, sn) == 0) {
            for (int j = i; j < g_routes.count - 1; j++) {
                g_routes.entries[j] = g_routes.entries[j + 1];
            }
            g_routes.count--;
            found = true;
            break;
        }
    }
    if (found) {
        int updated = update_client_targets_for_sn(sn, 0, 0);
        if (out_clients_updated) *out_clients_updated = updated;
        printf("[INGRESS] Route deleted: SN=%s (active sessions cleared: %d)\n", sn, updated);
        return true;
    }
    return false;
}

#include "media_lifecycle.h"

/* Socket Listeners */
static int create_tcp_listener(int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("[INGRESS] socket(TCP) failed");
        return -1;
    }

    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = INADDR_ANY;
    sin.sin_port = htons((uint16_t)port);

    if (bind(fd, (struct sockaddr*)&sin, sizeof(sin)) < 0) {
        fprintf(stderr, "[INGRESS] bind(port %d) failed: %s\n", port, strerror(errno));
        close(fd);
        return -1;
    }

    if (listen(fd, 64) < 0) {
        fprintf(stderr, "[INGRESS] listen(port %d) failed: %s\n", port, strerror(errno));
        close(fd);
        return -1;
    }

    set_nonblocking(fd);
    return fd;
}

static int create_udp_socket(void) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        perror("[INGRESS] socket(UDP) failed");
        return -1;
    }

    /* 1MB send buffer for UDP packets */
    int sndbuf = 1024 * 1024;
    setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));
    set_nonblocking(fd);
    return fd;
}

/* Ingress Client Handling */
static IngressClient* add_ingress_client(int epoll_fd, int client_fd, const char* peer_ip, int peer_port) {
    if (g_client_count >= MAX_CLIENTS) {
        fprintf(stderr, "[INGRESS] Max clients (%d) reached, rejecting %s:%d\n", MAX_CLIENTS, peer_ip, peer_port);
        close(client_fd);
        return NULL;
    }

    IngressClient* c = (IngressClient*)calloc(1, sizeof(IngressClient));
    if (!c) {
        close(client_fd);
        return NULL;
    }

    c->fd = client_fd;
    c->state = STATE_PREAMBLE;
    c->epoch = g_next_epoch++;
    snprintf(c->peer_addr, sizeof(c->peer_addr), "%s:%d", peer_ip, peer_port);
    c->connect_time = time(NULL);
    c->last_activity = c->connect_time;
    c->last_rtp_time = 0;
    c->last_rtcp_time = 0;

    /* Socket performance options */
    int flag = 1;
    setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
    set_nonblocking(client_fd);

    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.events = EPOLLIN | EPOLLRDHUP | EPOLLERR;
    ev.data.ptr = c;

    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &ev) < 0) {
        perror("[INGRESS] epoll_ctl ADD client failed");
        free(c);
        close(client_fd);
        return NULL;
    }

    g_clients[g_client_count++] = c;
    printf("[INGRESS] Connection accepted from %s (epoch #%" PRIu64 ", active: %d)\n",
           c->peer_addr, c->epoch, g_client_count);
    return c;
}

static void remove_ingress_client(int epoll_fd, IngressClient* c) {
    if (!c) return;

    time_t duration = time(NULL) - c->connect_time;
    printf("[INGRESS] Disconnected %s (SN: '%s', epoch #%" PRIu64 ", duration: %lds, RTP: %" PRIu64 " pkts, RTCP: %" PRIu64 " pkts, Total: %" PRIu64 " bytes)\n",
           c->peer_addr, c->sn[0] ? c->sn : "unknown", c->epoch, (long)duration,
           c->rtp_packets, c->rtcp_packets, c->total_bytes);

    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, c->fd, NULL);
    close(c->fd);

    for (int i = 0; i < g_client_count; i++) {
        if (g_clients[i] == c) {
            for (int j = i; j < g_client_count - 1; j++) {
                g_clients[j] = g_clients[j + 1];
            }
            g_clients[g_client_count - 1] = NULL;
            g_client_count--;
            break;
        }
    }
    free(c);
}

/* Process incoming bytes on Ingress client */
static void process_ingress_data(int epoll_fd, IngressClient* c) {
    while (true) {
        if (c->rx_len >= sizeof(c->rx_buf)) {
            fprintf(stderr, "[INGRESS] Buffer overflow from %s, disconnecting\n", c->peer_addr);
            remove_ingress_client(epoll_fd, c);
            return;
        }

        ssize_t n = recv(c->fd, c->rx_buf + c->rx_len, sizeof(c->rx_buf) - c->rx_len, 0);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break; /* All data currently read */
            }
            fprintf(stderr, "[INGRESS] recv error from %s: %s\n", c->peer_addr, strerror(errno));
            remove_ingress_client(epoll_fd, c);
            return;
        }
        if (n == 0) {
            /* Client disconnected */
            remove_ingress_client(epoll_fd, c);
            return;
        }

        c->rx_len += (size_t)n;
        c->last_activity = time(NULL);

        /* Parse state machine: PREAMBLE */
        if (c->state == STATE_PREAMBLE) {
            /* Preamble header: "L4RT" (4) + ver (1) + flags (1) + sn_len (2) = 8 bytes */
            if (c->rx_len < 8) {
                continue;
            }

            if (memcmp(c->rx_buf, L4RTP_MAGIC, 4) != 0 || c->rx_buf[4] != L4RTP_VERSION) {
                fprintf(stderr, "[INGRESS] Invalid preamble magic/version from %s (magic: %.4s, ver: 0x%02x)\n",
                        c->peer_addr, (char*)c->rx_buf, c->rx_buf[4]);
                remove_ingress_client(epoll_fd, c);
                return;
            }

            uint16_t sn_len = ((uint16_t)c->rx_buf[6] << 8) | (uint16_t)c->rx_buf[7];
            if (sn_len == 0 || sn_len > 127) {
                fprintf(stderr, "[INGRESS] Invalid SN length %u from %s\n", sn_len, c->peer_addr);
                remove_ingress_client(epoll_fd, c);
                return;
            }

            if (c->rx_len < (size_t)(8 + sn_len)) {
                /* Need more data for full SN */
                continue;
            }

            memcpy(c->sn, c->rx_buf + 8, sn_len);
            c->sn[sn_len] = '\0';

            /* Close any existing active connection with the same SN */
            for (int i = 0; i < g_client_count; i++) {
                IngressClient* old_c = g_clients[i];
                if (old_c && old_c != c && strcmp(old_c->sn, c->sn) == 0) {
                    printf("[INGRESS] Closing duplicate/stale session for SN '%s' (epoch #%" PRIu64 ") from %s (new connection from %s, epoch #%" PRIu64 ")\n",
                           c->sn, old_c->epoch, old_c->peer_addr, c->peer_addr, c->epoch);
                    remove_ingress_client(epoll_fd, old_c);
                    break;
                }
            }

            /* Route lookup */
            const RouteEntry* route = find_route(c->sn);
            if (route) {
                c->rtp_port = route->rtp_port;
                c->rtcp_port = route->rtcp_port;

                memset(&c->rtp_target, 0, sizeof(c->rtp_target));
                c->rtp_target.sin_family = AF_INET;
                c->rtp_target.sin_addr = g_janus_ip;
                c->rtp_target.sin_port = htons((uint16_t)route->rtp_port);

                memset(&c->rtcp_target, 0, sizeof(c->rtcp_target));
                c->rtcp_target.sin_family = AF_INET;
                c->rtcp_target.sin_addr = g_janus_ip;
                c->rtcp_target.sin_port = htons((uint16_t)route->rtcp_port);

                printf("[INGRESS] Preamble OK: SN=%s from %s -> routing to RTP:%d RTCP:%d\n",
                       c->sn, c->peer_addr, c->rtp_port, c->rtcp_port);
            } else {
                c->rtp_port = 0;
                c->rtcp_port = 0;
                memset(&c->rtp_target, 0, sizeof(c->rtp_target));
                memset(&c->rtcp_target, 0, sizeof(c->rtcp_target));
                printf("[INGRESS] Preamble OK: SN=%s from %s (no route configured; waiting for dynamic route)\n",
                       c->sn, c->peer_addr);
            }

            /* Shift buffer past preamble */
            size_t preamble_total = 8 + sn_len;
            memmove(c->rx_buf, c->rx_buf + preamble_total, c->rx_len - preamble_total);
            c->rx_len -= preamble_total;
            c->state = STATE_FRAMING;
        }

        /* Parse state machine: FRAMING (Batch processing) */
        if (c->state == STATE_FRAMING) {
            size_t off = 0;
            while (c->rx_len - off >= 4) {
                uint8_t type = c->rx_buf[off];
                uint16_t payload_len = ((uint16_t)c->rx_buf[off + 2] << 8) | (uint16_t)c->rx_buf[off + 3];

                if (payload_len > MAX_RTP_PAYLOAD_SIZE) {
                    fprintf(stderr, "[INGRESS] Frame payload length %u exceeds max %d from %s (SN: %s) — disconnecting\n",
                            payload_len, MAX_RTP_PAYLOAD_SIZE, c->peer_addr, c->sn);
                    remove_ingress_client(epoll_fd, c);
                    return;
                }

                size_t frame_total = 4 + payload_len;
                if (c->rx_len - off < frame_total) {
                    /* Incomplete frame, wait for more data */
                    break;
                }

                const uint8_t* payload = c->rx_buf + off + 4;
                if (type == L4RTP_FRAME_RTP) {
                    c->last_rtp_time = time(NULL);
                    if (c->rtp_port > 0) {
                        sendto(g_udp_sock, payload, payload_len, 0,
                               (struct sockaddr*)&c->rtp_target, sizeof(c->rtp_target));
                        c->rtp_packets++;
                        c->total_bytes += payload_len;
                    } else {
                        c->unrouted_packets++;
                    }
                } else if (type == L4RTP_FRAME_RTCP) {
                    c->last_rtcp_time = time(NULL);
                    if (c->rtcp_port > 0) {
                        sendto(g_udp_sock, payload, payload_len, 0,
                               (struct sockaddr*)&c->rtcp_target, sizeof(c->rtcp_target));
                        c->rtcp_packets++;
                        c->total_bytes += payload_len;
                    } else {
                        c->unrouted_packets++;
                    }
                } else if (type == L4RTP_FRAME_KEEPALIVE) {
                    c->keepalive_packets++;
                } else {
                    fprintf(stderr, "[INGRESS] Unknown frame type 0x%02x (%u bytes) from SN %s\n",
                            type, payload_len, c->sn);
                }

                off += frame_total;
            }

            if (off > 0) {
                if (c->rx_len > off) {
                    memmove(c->rx_buf, c->rx_buf + off, c->rx_len - off);
                }
                c->rx_len -= off;
            }
        }
    }
}

/* Parse rtp and rtcp ports from query string or JSON body */
static void parse_query_or_body_ports(const char* query, const char* body, int* rtp, int* rtcp) {
    if (query) {
        const char* p_rtp = strstr(query, "rtp=");
        if (p_rtp && (p_rtp == query || *(p_rtp - 1) == '?' || *(p_rtp - 1) == '&')) {
            *rtp = atoi(p_rtp + 4);
        }
        const char* p_rtcp = strstr(query, "rtcp=");
        if (p_rtcp && (p_rtcp == query || *(p_rtcp - 1) == '?' || *(p_rtcp - 1) == '&')) {
            *rtcp = atoi(p_rtcp + 5);
        }
    }
    if (body && (*rtp == 0 || *rtcp == 0)) {
        /* Simple fallback JSON parser for {"rtp": 6000, "rtcp": 6001} or rtp_port */
        const char* p1 = strstr(body, "\"rtp\"");
        if (!p1) p1 = strstr(body, "\"rtp_port\"");
        if (p1) {
            const char* colon = strchr(p1, ':');
            if (colon && *rtp == 0) *rtp = atoi(colon + 1);
        }
        const char* p2 = strstr(body, "\"rtcp\"");
        if (!p2) p2 = strstr(body, "\"rtcp_port\"");
        if (p2) {
            const char* colon = strchr(p2, ':');
            if (colon && *rtcp == 0) *rtcp = atoi(colon + 1);
        }
    }
}

/* Format session metrics and freshness into JSON object */
static size_t format_session_json(char* buf, size_t buf_size, const IngressClient* c, time_t now) {
    if (!c || !buf || buf_size == 0) return 0;

    const char* state_str = "preamble";
    if (c->state == STATE_FRAMING) {
        state_str = (c->rtp_port > 0) ? "streaming" : "connected";
    }

    bool route_exists = (c->rtp_port > 0);

    long transport_idle_sec = (long)(now - c->last_activity);
    if (transport_idle_sec < 0) transport_idle_sec = 0;

    long rtp_idle_sec = (c->last_rtp_time > 0) ? (long)(now - c->last_rtp_time) : -1;
    if (rtp_idle_sec < 0 && c->last_rtp_time > 0) rtp_idle_sec = 0;

    /*
     * Effective idle_sec:
     * For backward compatibility with clients that rely on idle_sec to gauge media liveness,
     * if RTP media has been received, idle_sec reflects media freshness (time since last RTP frame).
     * If no RTP media has been received yet, it falls back to transport idle time.
     */
    long effective_idle_sec = (c->last_rtp_time > 0) ? rtp_idle_sec : transport_idle_sec;

    bool fresh_rtp = false;
    const char* media_state = "unknown";

    if (c->state == STATE_PREAMBLE) {
        media_state = "connected";
    } else if (!route_exists) {
        media_state = "connected";
    } else if (c->rtp_packets == 0) {
        media_state = "unknown";
    } else {
        if (rtp_idle_sec >= 0 && rtp_idle_sec <= RTP_STALE_DEADLINE_SEC) {
            fresh_rtp = true;
            media_state = "receiving_fresh_media";
        } else {
            fresh_rtp = false;
            media_state = "stale";
        }
    }

    size_t off = 0;
    off += snprintf(buf + off, buf_size - off,
                    "{\"sn\":\"%s\",\"peer\":\"%s\",\"state\":\"%s\","
                    "\"media_state\":\"%s\",\"fresh_rtp\":%s,"
                    "\"transport_connected\":true,\"route_exists\":%s,"
                    "\"connection_epoch\":%" PRIu64 ",\"stale_deadline_sec\":%d,"
                    "\"rtp_port\":%d,\"rtcp_port\":%d,"
                    "\"rtp_packets\":%" PRIu64 ",\"rtcp_packets\":%" PRIu64 ","
                    "\"unrouted_packets\":%" PRIu64 ",\"keepalive_packets\":%" PRIu64 ","
                    "\"bytes\":%" PRIu64 ",\"duration_sec\":%ld,"
                    "\"idle_sec\":%ld,\"transport_idle_sec\":%ld,",
                    c->sn[0] ? c->sn : "pending",
                    c->peer_addr,
                    state_str,
                    media_state,
                    fresh_rtp ? "true" : "false",
                    route_exists ? "true" : "false",
                    c->epoch,
                    RTP_STALE_DEADLINE_SEC,
                    c->rtp_port, c->rtcp_port,
                    c->rtp_packets,
                    c->rtcp_packets,
                    c->unrouted_packets,
                    c->keepalive_packets,
                    c->total_bytes,
                    (long)(now - c->connect_time),
                    effective_idle_sec,
                    transport_idle_sec);

    if (c->last_rtp_time > 0) {
        off += snprintf(buf + off, buf_size - off,
                        "\"last_rtp_at\":%ld,\"rtp_idle_sec\":%ld,",
                        (long)c->last_rtp_time, rtp_idle_sec);
    } else {
        off += snprintf(buf + off, buf_size - off,
                        "\"last_rtp_at\":null,\"rtp_idle_sec\":null,");
    }

    if (c->last_rtcp_time > 0) {
        off += snprintf(buf + off, buf_size - off,
                        "\"last_rtcp_at\":%ld,", (long)c->last_rtcp_time);
    } else {
        off += snprintf(buf + off, buf_size - off,
                        "\"last_rtcp_at\":null,");
    }

    off += snprintf(buf + off, buf_size - off,
                    "\"last_activity\":%ld,\"last_activity_at\":%ld}",
                    (long)c->last_activity, (long)c->last_activity);

    return off;
}

/* HTTP Control API on port 9100 */
static void handle_control_request(int client_fd) {
    /* Set socket timeout to prevent slowloris hang */
    struct timeval tv;
    tv.tv_sec = 2;
    tv.tv_usec = 0;
    setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    char req_buf[HTTP_REQ_BUFFER_SIZE];
    ssize_t n = recv(client_fd, req_buf, sizeof(req_buf) - 1, 0);
    if (n <= 0) {
        close(client_fd);
        return;
    }
    req_buf[n] = '\0';

    /* Parse headers boundary and check Content-Length to read full body */
    const char* hdr_end = strstr(req_buf, "\r\n\r\n");
    int header_len = 0;
    if (hdr_end) {
        header_len = (int)(hdr_end - req_buf) + 4;
    } else {
        hdr_end = strstr(req_buf, "\n\n");
        if (hdr_end) header_len = (int)(hdr_end - req_buf) + 2;
    }

    int content_len = 0;
    const char* cl_hdr = strcasestr(req_buf, "Content-Length:");
    if (cl_hdr) {
        cl_hdr += 15;
        while (*cl_hdr == ' ' || *cl_hdr == '\t') cl_hdr++;
        content_len = atoi(cl_hdr);
    }

    if (header_len > 0 && content_len > 0) {
        int body_received = (int)n - header_len;
        while (body_received < content_len && n < (ssize_t)(sizeof(req_buf) - 1)) {
            ssize_t r = recv(client_fd, req_buf + n, (size_t)(sizeof(req_buf) - 1 - n), 0);
            if (r <= 0) break;
            n += r;
            body_received += (int)r;
            req_buf[n] = '\0';
        }
    }

    char method[16] = {0};
    char path[256] = {0};
    sscanf(req_buf, "%15s %255s", method, path);

    const char* body = strstr(req_buf, "\r\n\r\n");
    if (body) {
        body += 4;
    } else {
        body = strstr(req_buf, "\n\n");
        if (body) body += 2;
    }

    /* Handle CORS preflight */
    if (strcmp(method, "OPTIONS") == 0) {
        const char* options_resp =
            "HTTP/1.1 204 No Content\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Access-Control-Allow-Methods: GET, PUT, POST, DELETE, OPTIONS\r\n"
            "Access-Control-Allow-Headers: Content-Type, X-Media-Service-Token, Authorization\r\n"
            "Content-Length: 0\r\n"
            "Connection: close\r\n"
            "\r\n";
        send(client_fd, options_resp, strlen(options_resp), 0);
        close(client_fd);
        return;
    }

    char* resp_body = (char*)malloc(HTTP_RESP_BUFFER_SIZE);
    char* response = (char*)malloc(HTTP_RESP_BUFFER_SIZE + 1024);
    if (!resp_body || !response) {
        free(resp_body);
        free(response);
        close(client_fd);
        return;
    }
    resp_body[0] = '\0';

    int status_code = 200;
    const char* status_text = "OK";

    if (strcmp(method, "GET") == 0 && strcmp(path, "/routes") == 0) {
        /* GET /routes -> JSON array of all routes */
        size_t off = 0;
        off += snprintf(resp_body + off, HTTP_RESP_BUFFER_SIZE - off, "[");
        for (int i = 0; i < g_routes.count; i++) {
            off += snprintf(resp_body + off, HTTP_RESP_BUFFER_SIZE - off,
                            "%s{\"sn\":\"%s\",\"rtp_port\":%d,\"rtcp_port\":%d}",
                            (i > 0 ? "," : ""),
                            g_routes.entries[i].sn,
                            g_routes.entries[i].rtp_port,
                            g_routes.entries[i].rtcp_port);
        }
        snprintf(resp_body + off, HTTP_RESP_BUFFER_SIZE - off, "]\n");
    }
    else if (strcmp(method, "GET") == 0 && strncmp(path, "/routes/", 8) == 0) {
        /* GET /routes/<sn> -> JSON object for single route */
        char sn[128] = {0};
        safe_strcpy(sn, path + 8, sizeof(sn));
        char* q = strchr(sn, '?');
        if (q) *q = '\0';

        const RouteEntry* r = find_route(sn);
        if (r) {
            snprintf(resp_body, HTTP_RESP_BUFFER_SIZE,
                     "{\"status\":\"ok\",\"sn\":\"%s\",\"rtp_port\":%d,\"rtcp_port\":%d}\n",
                     r->sn, r->rtp_port, r->rtcp_port);
        } else {
            status_code = 404;
            status_text = "Not Found";
            snprintf(resp_body, HTTP_RESP_BUFFER_SIZE,
                     "{\"error\":\"route not found\",\"sn\":\"%s\"}\n", sn);
        }
    }
    else if ((strcmp(method, "PUT") == 0 || strcmp(method, "POST") == 0) &&
             strncmp(path, "/routes/", 8) == 0) {
        /* PUT/POST /routes/<sn>?rtp=PORT&rtcp=PORT (or JSON body) */
        char sn[128] = {0};
        char* qmark = strchr(path + 8, '?');
        int rtp = 0, rtcp = 0;

        if (qmark) {
            size_t sn_len = (size_t)(qmark - (path + 8));
            if (sn_len < sizeof(sn)) {
                memcpy(sn, path + 8, sn_len);
                sn[sn_len] = '\0';
            }
            parse_query_or_body_ports(qmark + 1, body, &rtp, &rtcp);
        } else {
            safe_strcpy(sn, path + 8, sizeof(sn));
            parse_query_or_body_ports(NULL, body, &rtp, &rtcp);
        }

        if (sn[0] && rtp > 0 && rtcp > 0) {
            int clients_updated = 0;
            if (upsert_route(sn, rtp, rtcp, &clients_updated)) {
                snprintf(resp_body, HTTP_RESP_BUFFER_SIZE,
                         "{\"status\":\"ok\",\"action\":\"upsert\",\"sn\":\"%s\","
                         "\"rtp_port\":%d,\"rtcp_port\":%d,\"active_clients_updated\":%d}\n",
                         sn, rtp, rtcp, clients_updated);
            } else {
                status_code = 500;
                status_text = "Internal Server Error";
                snprintf(resp_body, HTTP_RESP_BUFFER_SIZE,
                         "{\"error\":\"failed to upsert route (table full)\",\"sn\":\"%s\"}\n", sn);
            }
        } else {
            status_code = 400;
            status_text = "Bad Request";
            snprintf(resp_body, HTTP_RESP_BUFFER_SIZE,
                     "{\"error\":\"missing or invalid parameters; format: /routes/<sn>?rtp=PORT&rtcp=PORT\"}\n");
        }
    }
    else if (strcmp(method, "DELETE") == 0 && strncmp(path, "/routes/", 8) == 0) {
        /* DELETE /routes/<sn> */
        char sn[128] = {0};
        safe_strcpy(sn, path + 8, sizeof(sn));
        char* q = strchr(sn, '?');
        if (q) *q = '\0';

        int clients_cleared = 0;
        if (delete_route(sn, &clients_cleared)) {
            snprintf(resp_body, HTTP_RESP_BUFFER_SIZE,
                     "{\"status\":\"ok\",\"action\":\"delete\",\"sn\":\"%s\","
                     "\"active_clients_cleared\":%d}\n", sn, clients_cleared);
        } else {
            status_code = 404;
            status_text = "Not Found";
            snprintf(resp_body, HTTP_RESP_BUFFER_SIZE,
                     "{\"error\":\"route not found\",\"sn\":\"%s\"}\n", sn);
        }
    }
    else if (strcmp(method, "GET") == 0 && (strcmp(path, "/stats") == 0 || strcmp(path, "/") == 0)) {
        /* GET /stats -> JSON sessions and performance metrics */
        time_t now = time(NULL);
        size_t off = 0;
        off += snprintf(resp_body + off, HTTP_RESP_BUFFER_SIZE - off,
                        "{\"status\":\"ok\",\"uptime_sec\":%ld,\"stale_deadline_sec\":%d,"
                        "\"active_connections\":%d,\"sessions\":[",
                        (long)(now - g_server_start_time), RTP_STALE_DEADLINE_SEC, g_client_count);
        for (int i = 0; i < g_client_count; i++) {
            IngressClient* c = g_clients[i];
            if (!c) continue;
            if (i > 0) {
                off += snprintf(resp_body + off, HTTP_RESP_BUFFER_SIZE - off, ",");
            }
            off += format_session_json(resp_body + off, HTTP_RESP_BUFFER_SIZE - off, c, now);
        }
        snprintf(resp_body + off, HTTP_RESP_BUFFER_SIZE - off, "]}\n");
    }
    else if (strcmp(method, "GET") == 0 && strncmp(path, "/stats/", 7) == 0) {
        /* GET /stats/<sn> -> JSON object for single SN (connected or disconnected) */
        char sn[128] = {0};
        safe_strcpy(sn, path + 7, sizeof(sn));
        char* q = strchr(sn, '?');
        if (q) *q = '\0';

        time_t now = time(NULL);
        IngressClient* matched = NULL;
        for (int i = 0; i < g_client_count; i++) {
            if (g_clients[i] && strcmp(g_clients[i]->sn, sn) == 0) {
                matched = g_clients[i];
                break;
            }
        }

        if (matched) {
            size_t off = 0;
            off += snprintf(resp_body + off, HTTP_RESP_BUFFER_SIZE - off, "{\"status\":\"ok\",\"session\":");
            off += format_session_json(resp_body + off, HTTP_RESP_BUFFER_SIZE - off, matched, now);
            snprintf(resp_body + off, HTTP_RESP_BUFFER_SIZE - off, "}\n");
        } else {
            const RouteEntry* r = find_route(sn);
            snprintf(resp_body, HTTP_RESP_BUFFER_SIZE,
                     "{\"status\":\"ok\",\"sn\":\"%s\",\"state\":\"disconnected\","
                     "\"media_state\":\"disconnected\",\"transport_connected\":false,"
                     "\"route_exists\":%s,\"fresh_rtp\":false,\"rtp_packets\":0,"
                     "\"rtcp_packets\":0,\"bytes\":0,\"idle_sec\":null,\"last_rtp_at\":null}\n",
                     sn, r ? "true" : "false");
        }
    }
    else if (strcmp(method, "GET") == 0 && strcmp(path, "/health") == 0) {
        snprintf(resp_body, HTTP_RESP_BUFFER_SIZE,
                 "{\"status\":\"ok\",\"routes\":%d,\"active_media_sessions\":%d}\n",
                 g_routes.count, g_media_session_count);
    }
    else if (strcmp(method, "GET") == 0 && strcmp(path, "/api/v1/openapi.json") == 0) {
        handle_openapi_spec(resp_body, HTTP_RESP_BUFFER_SIZE, &status_code, &status_text);
    }
    else if (strncmp(path, "/api/v1/media/", 14) == 0) {
        if (!check_service_auth(req_buf)) {
            status_code = 401;
            status_text = "Unauthorized";
            snprintf(resp_body, HTTP_RESP_BUFFER_SIZE,
                     "{\"error\":\"unauthorized\",\"detail\":\"Invalid or missing service authentication token\"}\n");
        }
        else if (strcmp(method, "POST") == 0 && strcmp(path, "/api/v1/media/sessions/start") == 0) {
            handle_media_session_start(body, resp_body, HTTP_RESP_BUFFER_SIZE, &status_code, &status_text);
        }
        else if (strcmp(method, "GET") == 0 && strncmp(path, "/api/v1/media/sessions/", 23) == 0) {
            char session_id[64] = {0};
            safe_strcpy(session_id, path + 23, sizeof(session_id));
            char* q = strchr(session_id, '?');
            if (q) *q = '\0';
            get_media_session_health(session_id, resp_body, HTTP_RESP_BUFFER_SIZE, &status_code);
            status_text = (status_code == 200) ? "OK" : "Not Found";
        }
        else if (strcmp(method, "POST") == 0 && strncmp(path, "/api/v1/media/sessions/", 23) == 0 &&
                 strstr(path + 23, "/stop") != NULL) {
            char session_id[64] = {0};
            const char* start_id = path + 23;
            const char* stop_kw = strstr(start_id, "/stop");
            size_t id_len = (size_t)(stop_kw - start_id);
            if (id_len >= sizeof(session_id)) id_len = sizeof(session_id) - 1;
            memcpy(session_id, start_id, id_len);
            session_id[id_len] = '\0';

            char op_id[64] = {0};
            char reason[64] = {0};
            if (body) {
                json_get_string(body, "operation_id", op_id, sizeof(op_id));
                json_get_string(body, "reason", reason, sizeof(reason));
            }
            stop_media_session(session_id, op_id, reason, resp_body, HTTP_RESP_BUFFER_SIZE, &status_code);
            status_text = "OK";
        }
        else if (strcmp(method, "POST") == 0 && strcmp(path, "/api/v1/media/reconcile") == 0) {
            reconcile_resources(resp_body, HTTP_RESP_BUFFER_SIZE);
            status_code = 200;
            status_text = "OK";
        }
        else if (strcmp(method, "GET") == 0 && strcmp(path, "/api/v1/media/metrics") == 0) {
            handle_media_metrics(resp_body, HTTP_RESP_BUFFER_SIZE, &status_code, &status_text);
        }
        else {
            status_code = 404;
            status_text = "Not Found";
            snprintf(resp_body, HTTP_RESP_BUFFER_SIZE, "{\"error\":\"endpoint not found\"}\n");
        }
    }
    else {
        status_code = 404;
        status_text = "Not Found";
        snprintf(resp_body, HTTP_RESP_BUFFER_SIZE, "{\"error\":\"endpoint not found\"}\n");
    }

    int len = snprintf(response, HTTP_RESP_BUFFER_SIZE + 1024,
                       "HTTP/1.1 %d %s\r\n"
                       "Content-Type: application/json; charset=utf-8\r\n"
                       "Access-Control-Allow-Origin: *\r\n"
                       "Access-Control-Allow-Methods: GET, PUT, POST, DELETE, OPTIONS\r\n"
                       "Access-Control-Allow-Headers: Content-Type, X-Media-Service-Token, Authorization\r\n"
                       "Content-Length: %zu\r\n"
                       "Connection: close\r\n"
                       "\r\n"
                       "%s",
                       status_code, status_text, strlen(resp_body), resp_body);

    size_t total_sent = 0;
    while (total_sent < (size_t)len) {
        ssize_t s = send(client_fd, response + total_sent, (size_t)len - total_sent, 0);
        if (s <= 0) break;
        total_sent += (size_t)s;
    }

    free(resp_body);
    free(response);
    close(client_fd);
}

int main(int argc, char* argv[]) {
    signal(SIGPIPE, SIG_IGN);
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    g_server_start_time = time(NULL);

    /* Parse environment or args */
    const char* env_janus = getenv("JANUS_HOST");
    if (env_janus && strlen(env_janus) > 0) {
        safe_strcpy(g_janus_host, env_janus, sizeof(g_janus_host));
    }
    const char* env_janus_port = getenv("JANUS_ADMIN_PORT");
    if (env_janus_port && strlen(env_janus_port) > 0) {
        int p = atoi(env_janus_port);
        if (p > 0) g_janus_admin_port = p;
    }
    const char* env_janus_secret = getenv("JANUS_ADMIN_SECRET");
    if (env_janus_secret && strlen(env_janus_secret) > 0) {
        safe_strcpy(g_janus_admin_secret, env_janus_secret, sizeof(g_janus_admin_secret));
    }
    const char* env_token = getenv("L4MEDIA_SERVICE_TOKEN");
    if (env_token && strlen(env_token) > 0) {
        safe_strcpy(g_service_token, env_token, sizeof(g_service_token));
    }
    const char* env_routes = getenv("ROUTES_FILE");
    if (env_routes && strlen(env_routes) > 0) {
        safe_strcpy(g_routes_file, env_routes, sizeof(g_routes_file));
    }

    if (argc > 1) {
        safe_strcpy(g_routes_file, argv[1], sizeof(g_routes_file));
    }

    printf("[INGRESS] Starting l4media-ingress server (MVP)...\n");
    printf("[INGRESS] Target Janus host: %s\n", g_janus_host);
    printf("[INGRESS] Routes file: %s\n", g_routes_file);

    resolve_janus_host();
    load_routes(g_routes_file);
    media_redis_bootstrap();

    g_udp_sock = create_udp_socket();
    if (g_udp_sock < 0) {
        return 1;
    }

    int ingress_listen_fd = create_tcp_listener(INGRESS_PORT);
    if (ingress_listen_fd < 0) {
        return 1;
    }
    printf("[INGRESS] Media ingress listening on TCP 0.0.0.0:%d\n", INGRESS_PORT);

    int control_listen_fd = create_tcp_listener(CONTROL_PORT);
    if (control_listen_fd < 0) {
        return 1;
    }
    printf("[INGRESS] Control API listening on TCP 0.0.0.0:%d\n", CONTROL_PORT);

    int epoll_fd = epoll_create1(0);
    if (epoll_fd < 0) {
        perror("[INGRESS] epoll_create1 failed");
        return 1;
    }

    struct epoll_event ev;

    /* Ingress listener */
    memset(&ev, 0, sizeof(ev));
    ev.events = EPOLLIN;
    ev.data.fd = ingress_listen_fd;
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, ingress_listen_fd, &ev);

    /* Control listener */
    memset(&ev, 0, sizeof(ev));
    ev.events = EPOLLIN;
    ev.data.fd = control_listen_fd;
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, control_listen_fd, &ev);

    struct epoll_event events[MAX_EPOLL_EVENTS];
    time_t last_stats_log = time(NULL);

    printf("[INGRESS] Event loop initialized. Ready to receive connections.\n");
    fflush(stdout);

    while (g_running) {
        int n_events = epoll_wait(epoll_fd, events, MAX_EPOLL_EVENTS, 1000 /* 1s timeout */);

        time_t now = time(NULL);

        /* Check idle clients timeout */
        for (int i = 0; i < g_client_count; i++) {
            IngressClient* c = g_clients[i];
            if (c && (now - c->last_activity > CLIENT_IDLE_TIMEOUT_SEC)) {
                printf("[INGRESS] Client %s (SN: '%s') timed out after %lds idle\n",
                       c->peer_addr, c->sn[0] ? c->sn : "pending", (long)(now - c->last_activity));
                remove_ingress_client(epoll_fd, c);
                i--; /* Adjust index since remaining elements shift left */
            }
        }

        /* Check media sessions TTL expiration */
        for (int i = 0; i < g_media_session_count; i++) {
            MediaSession* ms = &g_media_sessions[i];
            if (ms->state == MEDIA_STATE_ACTIVE && ms->ttl_sec > 0) {
                if (now - ms->started_at >= ms->ttl_sec) {
                    printf("[INGRESS TTL] Media session %s (SN: %s) expired after %d seconds TTL\n",
                           ms->session_id, ms->sn, ms->ttl_sec);
                    char dummy_resp[512];
                    int dummy_status = 0;
                    stop_media_session(ms->session_id, "ttl-watchdog", "ttl_expired",
                                       dummy_resp, sizeof(dummy_resp), &dummy_status);
                }
            }
        }

        /* Periodic reconciliation every 60s */
        static time_t last_reconcile = 0;
        if (last_reconcile == 0) last_reconcile = now;
        if (now - last_reconcile >= 60) {
            last_reconcile = now;
            reconcile_resources(NULL, 0);
        }

        if (now - last_stats_log >= 10) {
            last_stats_log = now;
            if (g_client_count > 0) {
                printf("[INGRESS STATS] Active sessions: %d\n", g_client_count);
                for (int i = 0; i < g_client_count; i++) {
                    IngressClient* c = g_clients[i];
                    long rtp_idle = (c->last_rtp_time > 0) ? (long)(now - c->last_rtp_time) : -1;
                    const char* fresh_desc = "NO MEDIA";
                    if (c->last_rtp_time > 0) {
                        fresh_desc = (rtp_idle <= RTP_STALE_DEADLINE_SEC) ? "FRESH" : "STALE";
                    }
                    printf("  - SN: %-24s (epoch #%" PRIu64 ") | %s | %s [%s, rtp_idle:%lds] (RTP:%d RTCP:%d) | RTP: %" PRIu64 " pkts | RTCP: %" PRIu64 " pkts | %" PRIu64 " KB\n",
                           c->sn[0] ? c->sn : "(preamble)", c->epoch, c->peer_addr,
                           c->state == STATE_FRAMING ? (c->rtp_port > 0 ? "STREAMING" : "UNROUTED") : "PREAMBLE",
                           fresh_desc, (rtp_idle >= 0 ? rtp_idle : 0),
                           c->rtp_port, c->rtcp_port,
                           c->rtp_packets, c->rtcp_packets,
                           c->total_bytes / 1024);
                }
                fflush(stdout);
            }
        }

        if (n_events < 0) {
            if (errno == EINTR) continue;
            perror("[INGRESS] epoll_wait error");
            break;
        }

        for (int i = 0; i < n_events; i++) {
            if (events[i].data.fd == ingress_listen_fd) {
                /* Accept incoming ingress connection */
                while (true) {
                    struct sockaddr_in peer;
                    socklen_t peer_len = sizeof(peer);
                    int client_fd = accept(ingress_listen_fd, (struct sockaddr*)&peer, &peer_len);
                    if (client_fd < 0) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                        perror("[INGRESS] accept failed");
                        break;
                    }
                    char peer_ip[INET_ADDRSTRLEN];
                    inet_ntop(AF_INET, &peer.sin_addr, peer_ip, sizeof(peer_ip));
                    add_ingress_client(epoll_fd, client_fd, peer_ip, ntohs(peer.sin_port));
                }
            } else if (events[i].data.fd == control_listen_fd) {
                /* Accept control API connection */
                while (true) {
                    struct sockaddr_in peer;
                    socklen_t peer_len = sizeof(peer);
                    int client_fd = accept(control_listen_fd, (struct sockaddr*)&peer, &peer_len);
                    if (client_fd < 0) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                        break;
                    }
                    handle_control_request(client_fd);
                }
            } else {
                /* Ingress client activity */
                IngressClient* c = (IngressClient*)events[i].data.ptr;

                /* Safety check: ensure client is still registered in g_clients */
                bool is_valid = false;
                for (int k = 0; k < g_client_count; k++) {
                    if (g_clients[k] == c) {
                        is_valid = true;
                        break;
                    }
                }
                if (!is_valid) continue;

                if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                    remove_ingress_client(epoll_fd, c);
                } else if (events[i].events & EPOLLIN) {
                    process_ingress_data(epoll_fd, c);
                }
            }
        }
    }

    printf("[INGRESS] Shutting down l4media-ingress...\n");
    for (int i = 0; i < g_media_session_count; i++) {
        if (g_media_sessions[i].state == MEDIA_STATE_ACTIVE ||
            g_media_sessions[i].state == MEDIA_STATE_STARTING) {
            char dummy_resp[512];
            int dummy_status = 0;
            stop_media_session(g_media_sessions[i].session_id, "shutdown", "server_shutdown",
                               dummy_resp, sizeof(dummy_resp), &dummy_status);
        }
    }
    for (int i = g_client_count - 1; i >= 0; i--) {
        remove_ingress_client(epoll_fd, g_clients[i]);
    }
    close(ingress_listen_fd);
    close(control_listen_fd);
    if (g_udp_sock >= 0) close(g_udp_sock);
    close(epoll_fd);
    printf("[INGRESS] Stopped.\n");
    handle_signal(SIGTERM); /* set g_running=false if reached */
    media_redis_close();
    return 0;
}
