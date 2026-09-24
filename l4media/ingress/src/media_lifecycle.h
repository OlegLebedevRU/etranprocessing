/**
 * @file media_lifecycle.h
 * @brief On-demand WebRTC media session lifecycle, Janus mountpoint orchestration,
 *        and route reconciliation for l4media.
 */

#ifndef MEDIA_LIFECYCLE_H
#define MEDIA_LIFECYCLE_H

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdint.h>
#include <stdbool.h>
#include <inttypes.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#define MAX_MEDIA_SESSIONS        128
#define DEFAULT_PORT_BASE         6010
#define DEFAULT_PORT_MAX          6200
#define OPENAPI_FILE_PATH         "/etc/l4media/openapi.json"
#define DEFAULT_SERVICE_TOKEN     "l4media-service-secret-token"
#define DEFAULT_JANUS_ADMIN_PORT  7088
#define DEFAULT_JANUS_ADMIN_SECRET "janusoverlord"

typedef enum {
    MEDIA_STATE_EMPTY = 0,
    MEDIA_STATE_STARTING,
    MEDIA_STATE_ACTIVE,
    MEDIA_STATE_STOPPING,
    MEDIA_STATE_STOPPED,
    MEDIA_STATE_FAILED
} MediaSessionState;

typedef struct {
    char session_id[64];
    char operation_id[64];
    char sn[128];
    uint32_t device_id;
    uint32_t mountpoint_id;
    int rtp_port;
    int rtcp_port;
    char pin[64];
    MediaSessionState state;
    int ttl_sec;
    time_t created_at;
    time_t started_at;
    time_t stopped_at;
    char stop_reason[64];
    uint64_t final_rtp_packets;
    uint64_t final_bytes;
    time_t last_rtp_at;
} MediaSession;

/* Lifecycle configuration and metrics */
static int g_janus_admin_port = DEFAULT_JANUS_ADMIN_PORT;
static char g_janus_admin_secret[128] = DEFAULT_JANUS_ADMIN_SECRET;
static char g_service_token[128] = DEFAULT_SERVICE_TOKEN;

static uint64_t g_metric_sessions_started = 0;
static uint64_t g_metric_sessions_stopped = 0;
static uint64_t g_metric_sessions_failed = 0;
static uint64_t g_metric_orphans_mountpoints = 0;
static uint64_t g_metric_orphans_routes = 0;

static MediaSession g_media_sessions[MAX_MEDIA_SESSIONS];
static int g_media_session_count = 0;

/* ========================================================================= */
/* String and JSON Helper Functions                                          */
/* ========================================================================= */

static inline void lifecycle_safe_strcpy(char* dst, const char* src, size_t dst_size) {
    if (!dst || dst_size == 0) return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    size_t len = strlen(src);
    if (len >= dst_size) len = dst_size - 1;
    memcpy(dst, src, len);
    dst[len] = '\0';
}

static inline bool json_find_key(const char* json, const char* key, const char** val_start) {
    if (!json || !key || !val_start) return false;
    char needle[128];
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    const char* p = strstr(json, needle);
    if (!p) return false;
    p += strlen(needle);
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    if (*p != ':') return false;
    p++;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    *val_start = p;
    return true;
}

static inline bool json_get_string(const char* json, const char* key, char* out, size_t out_sz) {
    if (!out || out_sz == 0) return false;
    out[0] = '\0';
    const char* p = NULL;
    if (!json_find_key(json, key, &p)) return false;
    if (*p != '"') return false;
    p++;
    size_t i = 0;
    while (*p && *p != '"' && i < out_sz - 1) {
        if (*p == '\\' && *(p + 1)) {
            p++;
        }
        out[i++] = *p++;
    }
    out[i] = '\0';
    return true;
}

static inline bool json_get_int(const char* json, const char* key, int* out) {
    const char* p = NULL;
    if (!json_find_key(json, key, &p)) return false;
    char* end = NULL;
    long v = strtol(p, &end, 10);
    if (end == p) return false;
    if (out) *out = (int)v;
    return true;
}

static inline bool json_get_uint32(const char* json, const char* key, uint32_t* out) {
    const char* p = NULL;
    if (!json_find_key(json, key, &p)) return false;
    char* end = NULL;
    unsigned long v = strtoul(p, &end, 10);
    if (end == p) return false;
    if (out) *out = (uint32_t)v;
    return true;
}

/* ========================================================================= */
/* Service Authentication                                                    */
/* ========================================================================= */

static inline bool check_service_auth(const char* req_buf) {
    if (!g_service_token[0]) {
        return true; /* Disabled if token is explicitly empty */
    }

    size_t tok_len = strlen(g_service_token);

    /* Check X-Media-Service-Token header */
    const char* hdr = strcasestr(req_buf, "X-Media-Service-Token:");
    if (hdr) {
        hdr += 22;
        while (*hdr == ' ' || *hdr == '\t') hdr++;
        if (strncmp(hdr, g_service_token, tok_len) == 0) {
            char next = hdr[tok_len];
            if (next == '\r' || next == '\n' || next == ' ' || next == '\t' || next == '\0') {
                return true;
            }
        }
    }

    /* Check Authorization: Bearer <token> */
    const char* auth = strcasestr(req_buf, "Authorization:");
    if (auth) {
        auth += 14;
        while (*auth == ' ' || *auth == '\t') auth++;
        if (strncasecmp(auth, "Bearer", 6) == 0) {
            auth += 6;
            while (*auth == ' ' || *auth == '\t') auth++;
            if (strncmp(auth, g_service_token, tok_len) == 0) {
                char next = auth[tok_len];
                if (next == '\r' || next == '\n' || next == ' ' || next == '\t' || next == '\0') {
                    return true;
                }
            }
        }
    }

    return false;
}

/* ========================================================================= */
/* Janus Admin HTTP API Client                                               */
/* ========================================================================= */

static inline int janus_admin_request(const char* body, char* resp_buf, size_t resp_buf_sz) {
    if (!resp_buf || resp_buf_sz == 0) return -1;
    resp_buf[0] = '\0';

    int sfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sfd < 0) {
        perror("[INGRESS JANUS] socket creation failed");
        return -1;
    }

    struct timeval tv;
    tv.tv_sec = 3;
    tv.tv_usec = 0;
    setsockopt(sfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sfd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    struct sockaddr_in saddr;
    memset(&saddr, 0, sizeof(saddr));
    saddr.sin_family = AF_INET;
    saddr.sin_port = htons((uint16_t)g_janus_admin_port);

    resolve_janus_host();
    saddr.sin_addr = g_janus_ip;

    if (connect(sfd, (struct sockaddr*)&saddr, sizeof(saddr)) < 0) {
        fprintf(stderr, "[INGRESS JANUS] connect to %s:%d failed: %s\n",
                g_janus_host, g_janus_admin_port, strerror(errno));
        close(sfd);
        return -1;
    }

    char req_hdr[512];
    int hlen = snprintf(req_hdr, sizeof(req_hdr),
                        "POST /admin HTTP/1.1\r\n"
                        "Host: %s:%d\r\n"
                        "Content-Type: application/json\r\n"
                        "Content-Length: %zu\r\n"
                        "Connection: close\r\n\r\n",
                        g_janus_host, g_janus_admin_port, strlen(body));

    if (send(sfd, req_hdr, (size_t)hlen, 0) < 0 ||
        send(sfd, body, strlen(body), 0) < 0) {
        perror("[INGRESS JANUS] send failed");
        close(sfd);
        return -1;
    }

    size_t total = 0;
    while (total < resp_buf_sz - 1) {
        ssize_t n = recv(sfd, resp_buf + total, resp_buf_sz - 1 - total, 0);
        if (n <= 0) break;
        total += (size_t)n;
    }
    resp_buf[total] = '\0';
    close(sfd);

    if (strncmp(resp_buf, "HTTP/1.1 200", 12) != 0 &&
        strncmp(resp_buf, "HTTP/1.0 200", 12) != 0) {
        fprintf(stderr, "[INGRESS JANUS] Unexpected HTTP response status\n");
        return -1;
    }

    return 0;
}

static inline int janus_destroy_mountpoint(uint32_t mountpoint_id, char* err_buf, size_t err_sz) {
    char req[512];
    snprintf(req, sizeof(req),
             "{\"janus\":\"message_plugin\",\"transaction\":\"mp_del_%" PRIu32 "\","
             "\"admin_secret\":\"%s\",\"plugin\":\"janus.plugin.streaming\","
             "\"request\":{\"request\":\"destroy\",\"id\":%" PRIu32 "}}",
             mountpoint_id, g_janus_admin_secret, mountpoint_id);

    char* resp = (char*)malloc(16384);
    if (!resp) return -1;
    int res = janus_admin_request(req, resp, 16384);
    if (res != 0) {
        if (err_buf && err_sz > 0) lifecycle_safe_strcpy(err_buf, "Janus connection failed", err_sz);
        free(resp);
        return -1;
    }
    free(resp);
    return 0;
}

static inline int janus_create_mountpoint(uint32_t mountpoint_id, int rtp_port, int rtcp_port,
                                          const char* pin, char* err_buf, size_t err_sz) {
    char req[1024];
    if (pin && pin[0]) {
        snprintf(req, sizeof(req),
                 "{\"janus\":\"message_plugin\",\"transaction\":\"mp_cr_%" PRIu32 "\","
                 "\"admin_secret\":\"%s\",\"plugin\":\"janus.plugin.streaming\","
                 "\"request\":{\"request\":\"create\",\"type\":\"rtp\",\"id\":%" PRIu32 ","
                 "\"name\":\"dev-%" PRIu32 "\",\"description\":\"L4Desk stream %" PRIu32 "\","
                 "\"video\":true,\"audio\":false,\"videoport\":%d,\"videortcpport\":%d,"
                 "\"videopt\":96,\"videocodec\":\"h264\",\"videofmtp\":\"profile-level-id=42e01f;packetization-mode=1\","
                 "\"pin\":\"%s\"}}",
                 mountpoint_id, g_janus_admin_secret, mountpoint_id, mountpoint_id, mountpoint_id,
                 rtp_port, rtcp_port, pin);
    } else {
        snprintf(req, sizeof(req),
                 "{\"janus\":\"message_plugin\",\"transaction\":\"mp_cr_%" PRIu32 "\","
                 "\"admin_secret\":\"%s\",\"plugin\":\"janus.plugin.streaming\","
                 "\"request\":{\"request\":\"create\",\"type\":\"rtp\",\"id\":%" PRIu32 ","
                 "\"name\":\"dev-%" PRIu32 "\",\"description\":\"L4Desk stream %" PRIu32 "\","
                 "\"video\":true,\"audio\":false,\"videoport\":%d,\"videortcpport\":%d,"
                 "\"videopt\":96,\"videocodec\":\"h264\",\"videofmtp\":\"profile-level-id=42e01f;packetization-mode=1\"}}",
                 mountpoint_id, g_janus_admin_secret, mountpoint_id, mountpoint_id, mountpoint_id,
                 rtp_port, rtcp_port);
    }

    char* resp = (char*)malloc(16384);
    if (!resp) return -1;
    int res = janus_admin_request(req, resp, 16384);
    if (res != 0) {
        if (err_buf && err_sz > 0) lifecycle_safe_strcpy(err_buf, "Janus connection failed", err_sz);
        free(resp);
        return -1;
    }

    const char* b = strstr(resp, "\r\n\r\n");
    if (!b) b = resp;

    if (strstr(b, "\"streaming\":\"created\"")) {
        free(resp);
        return 0;
    }

    /* If mountpoint already exists, destroy and recreate */
    if (strstr(b, "already exists") || strstr(b, "456")) {
        printf("[INGRESS JANUS] Mountpoint %" PRIu32 " already exists, destroying to recreate...\n", mountpoint_id);
        janus_destroy_mountpoint(mountpoint_id, NULL, 0);
        res = janus_admin_request(req, resp, 16384);
        b = strstr(resp, "\r\n\r\n");
        if (!b) b = resp;
        if (strstr(b, "\"streaming\":\"created\"")) {
            free(resp);
            return 0;
        }
    }

    if (err_buf && err_sz > 0) {
        char reason[128] = {0};
        json_get_string(b, "error", reason, sizeof(reason));
        if (reason[0]) {
            lifecycle_safe_strcpy(err_buf, reason, err_sz);
        } else {
            lifecycle_safe_strcpy(err_buf, "Janus streaming mountpoint creation failed", err_sz);
        }
    }
    free(resp);
    return -1;
}

static inline int janus_list_mountpoints(uint32_t* ids, int max_ids, int* out_count) {
    if (!ids || max_ids <= 0 || !out_count) return -1;
    *out_count = 0;

    char req[512];
    snprintf(req, sizeof(req),
             "{\"janus\":\"message_plugin\",\"transaction\":\"mp_list\","
             "\"admin_secret\":\"%s\",\"plugin\":\"janus.plugin.streaming\","
             "\"request\":{\"request\":\"list\"}}",
             g_janus_admin_secret);

    char* resp = (char*)malloc(65536);
    if (!resp) return -1;
    int res = janus_admin_request(req, resp, 65536);
    if (res != 0) {
        free(resp);
        return -1;
    }

    const char* b = strstr(resp, "\r\n\r\n");
    if (!b) b = resp;

    int count = 0;
    const char* p = b;
    while ((p = strstr(p, "\"id\":")) != NULL && count < max_ids) {
        p += 5;
        while (*p == ' ' || *p == '\t') p++;
        char* end = NULL;
        unsigned long v = strtoul(p, &end, 10);
        if (end > p) {
            ids[count++] = (uint32_t)v;
            p = end;
        }
    }
    *out_count = count;
    free(resp);
    return 0;
}

/* ========================================================================= */
/* Session and Port Pool Management                                          */
/* ========================================================================= */

static inline bool allocate_port_pair(int* out_rtp, int* out_rtcp) {
    for (int p = DEFAULT_PORT_BASE; p <= DEFAULT_PORT_MAX; p += 2) {
        bool route_used = false;
        for (int i = 0; i < g_routes.count; i++) {
            if (g_routes.entries[i].rtp_port == p || g_routes.entries[i].rtcp_port == p + 1 ||
                g_routes.entries[i].rtp_port == p + 1 || g_routes.entries[i].rtcp_port == p) {
                route_used = true;
                break;
            }
        }
        if (route_used) continue;

        bool session_used = false;
        for (int i = 0; i < g_media_session_count; i++) {
            if ((g_media_sessions[i].state == MEDIA_STATE_ACTIVE ||
                 g_media_sessions[i].state == MEDIA_STATE_STARTING) &&
                (g_media_sessions[i].rtp_port == p || g_media_sessions[i].rtcp_port == p + 1 ||
                 g_media_sessions[i].rtp_port == p + 1 || g_media_sessions[i].rtcp_port == p)) {
                session_used = true;
                break;
            }
        }
        if (!session_used) {
            *out_rtp = p;
            *out_rtcp = p + 1;
            return true;
        }
    }
    return false;
}

static inline MediaSession* find_session_by_id(const char* session_id) {
    if (!session_id || !session_id[0]) return NULL;
    for (int i = 0; i < g_media_session_count; i++) {
        if (strcmp(g_media_sessions[i].session_id, session_id) == 0) {
            return &g_media_sessions[i];
        }
    }
    return NULL;
}

static inline MediaSession* find_active_session_for_sn(const char* sn) {
    if (!sn || !sn[0]) return NULL;
    for (int i = 0; i < g_media_session_count; i++) {
        if ((g_media_sessions[i].state == MEDIA_STATE_ACTIVE ||
             g_media_sessions[i].state == MEDIA_STATE_STARTING) &&
            strcmp(g_media_sessions[i].sn, sn) == 0) {
            return &g_media_sessions[i];
        }
    }
    return NULL;
}

static inline MediaSession* allocate_session_slot(void) {
    for (int i = 0; i < g_media_session_count; i++) {
        if (g_media_sessions[i].state == MEDIA_STATE_EMPTY) {
            return &g_media_sessions[i];
        }
    }
    if (g_media_session_count < MAX_MEDIA_SESSIONS) {
        MediaSession* s = &g_media_sessions[g_media_session_count++];
        memset(s, 0, sizeof(MediaSession));
        return s;
    }
    int oldest_idx = -1;
    time_t oldest_time = 0;
    for (int i = 0; i < g_media_session_count; i++) {
        if (g_media_sessions[i].state == MEDIA_STATE_STOPPED ||
            g_media_sessions[i].state == MEDIA_STATE_FAILED) {
            if (oldest_idx < 0 || g_media_sessions[i].stopped_at < oldest_time) {
                oldest_idx = i;
                oldest_time = g_media_sessions[i].stopped_at;
            }
        }
    }
    if (oldest_idx >= 0) {
        MediaSession* s = &g_media_sessions[oldest_idx];
        memset(s, 0, sizeof(MediaSession));
        return s;
    }
    return NULL;
}

/* ========================================================================= */
/* Lifecycle Operations & Endpoints Handlers                                 */
/* ========================================================================= */

static inline void stop_media_session(const char* session_id, const char* operation_id, const char* reason,
                                      char* resp_body, size_t resp_sz, int* status_code) {
    (void)operation_id;
    MediaSession* s = find_session_by_id(session_id);
    if (!s) {
        s = find_active_session_for_sn(session_id);
    }
    if (!s && strncmp(session_id, "media-", 6) == 0) {
        s = find_active_session_for_sn(session_id + 6);
    }
    if (!s || s->state == MEDIA_STATE_STOPPED) {
        *status_code = 200;
        if (resp_body && resp_sz > 0) {
            snprintf(resp_body, resp_sz,
                     "{\"status\":\"success\",\"session_id\":\"%s\",\"state\":\"stopped\","
                     "\"detail\":\"Session already stopped or absent\"}\n",
                     session_id);
        }
        return;
    }

    time_t now = time(NULL);
    s->state = MEDIA_STATE_STOPPING;

    /* Harvest final packet counters from client if connected */
    for (int i = 0; i < g_client_count; i++) {
        if (g_clients[i] && strcmp(g_clients[i]->sn, s->sn) == 0) {
            s->final_rtp_packets = g_clients[i]->rtp_packets;
            s->final_bytes = g_clients[i]->total_bytes;
            s->last_rtp_at = g_clients[i]->last_rtp_time;
            break;
        }
    }

    /* Delete route from ingress */
    int cleared = 0;
    delete_route(s->sn, &cleared);

    /* Destroy mountpoint in Janus */
    janus_destroy_mountpoint(s->mountpoint_id, NULL, 0);

    s->state = MEDIA_STATE_STOPPED;
    s->stopped_at = now;
    lifecycle_safe_strcpy(s->stop_reason, reason && reason[0] ? reason : "user_closed", sizeof(s->stop_reason));
    g_metric_sessions_stopped++;

    long duration = (long)(s->stopped_at - s->started_at);
    if (duration < 0) duration = 0;

    *status_code = 200;
    if (resp_body && resp_sz > 0) {
        snprintf(resp_body, resp_sz,
                 "{\"status\":\"success\",\"session_id\":\"%s\",\"state\":\"stopped\","
                 "\"duration_sec\":%ld,\"rtp_packets\":%" PRIu64 ",\"bytes\":%" PRIu64 ","
                 "\"started_at\":%ld,\"stopped_at\":%ld,\"stop_reason\":\"%s\"}\n",
                 s->session_id, duration, s->final_rtp_packets, s->final_bytes,
                 (long)s->started_at, (long)s->stopped_at, s->stop_reason);
    }
}

static inline void handle_media_session_start(const char* body, char* resp_body, size_t resp_sz,
                                              int* status_code, const char** status_text) {
    if (!body || !body[0]) {
        *status_code = 400;
        *status_text = "Bad Request";
        snprintf(resp_body, resp_sz, "{\"error\":\"invalid_request\",\"detail\":\"Missing JSON body\"}\n");
        return;
    }

    char session_id[64] = {0};
    char operation_id[64] = {0};
    char sn[128] = {0};
    uint32_t device_id = 0;
    char pin[64] = {0};
    int rtp_port = 0;
    int rtcp_port = 0;
    int ttl_sec = 600;

    json_get_string(body, "session_id", session_id, sizeof(session_id));
    json_get_string(body, "operation_id", operation_id, sizeof(operation_id));
    json_get_string(body, "sn", sn, sizeof(sn));
    json_get_uint32(body, "device_id", &device_id);
    json_get_string(body, "pin", pin, sizeof(pin));
    json_get_int(body, "rtp_port", &rtp_port);
    json_get_int(body, "rtcp_port", &rtcp_port);
    json_get_int(body, "ttl_sec", &ttl_sec);

    if (!session_id[0] || !sn[0]) {
        *status_code = 400;
        *status_text = "Bad Request";
        snprintf(resp_body, resp_sz, "{\"error\":\"invalid_request\",\"detail\":\"session_id and sn are required\"}\n");
        return;
    }

    if (ttl_sec <= 0) ttl_sec = 600;
    if (ttl_sec < 10) ttl_sec = 10;
    if (ttl_sec > 7200) ttl_sec = 7200;

    /* 1. Idempotency on session_id */
    MediaSession* existing = find_session_by_id(session_id);
    if (existing) {
        if (existing->state == MEDIA_STATE_ACTIVE || existing->state == MEDIA_STATE_STARTING) {
            /* Ensure mountpoint exists — recreate after Janus restart */
            janus_create_mountpoint(existing->mountpoint_id, existing->rtp_port,
                                    existing->rtcp_port, existing->pin, NULL, 0);
            int _clients_updated = 0;
            upsert_route(existing->sn, existing->rtp_port, existing->rtcp_port, &_clients_updated);
            *status_code = 200;
            *status_text = "OK";
            snprintf(resp_body, resp_sz,
                     "{\"status\":\"success\",\"session_id\":\"%s\",\"operation_id\":\"%s\","
                     "\"state\":\"active\",\"device_id\":%" PRIu32 ",\"sn\":\"%s\","
                     "\"mountpoint_id\":%" PRIu32 ",\"rtp_port\":%d,\"rtcp_port\":%d,"
                     "\"janus_ws\":\"/janus-ws\",\"pin\":\"%s\",\"ttl_sec\":%d,"
                     "\"created_at\":%ld,\"started_at\":%ld}\n",
                     existing->session_id, existing->operation_id,
                     existing->device_id, existing->sn, existing->mountpoint_id,
                     existing->rtp_port, existing->rtcp_port, existing->pin,
                     existing->ttl_sec, (long)existing->created_at, (long)existing->started_at);
            return;
        } else if (existing->state == MEDIA_STATE_STOPPED) {
            if (operation_id[0] && strcmp(existing->operation_id, operation_id) == 0) {
                *status_code = 200;
                *status_text = "OK";
                snprintf(resp_body, resp_sz,
                         "{\"status\":\"success\",\"session_id\":\"%s\",\"operation_id\":\"%s\","
                         "\"state\":\"stopped\",\"detail\":\"Session already terminated\"}\n",
                         existing->session_id, existing->operation_id);
                return;
            } else {
                *status_code = 409;
                *status_text = "Conflict";
                snprintf(resp_body, resp_sz,
                         "{\"error\":\"session_terminated\",\"detail\":\"Session %s already terminated\"}\n",
                         session_id);
                return;
            }
        }
    }

    /* 2. Device mutual exclusion check */
    MediaSession* active_on_device = find_active_session_for_sn(sn);
    if (active_on_device && strcmp(active_on_device->session_id, session_id) != 0) {
        *status_code = 409;
        *status_text = "Conflict";
        snprintf(resp_body, resp_sz,
                 "{\"error\":\"session_busy\",\"detail\":\"Active media session already exists for device '%s'\",\"active_session_id\":\"%s\"}\n",
                 sn, active_on_device->session_id);
        return;
    }

    /* 3. Port allocation */
    if (rtp_port <= 0 || rtcp_port <= 0) {
        if (!allocate_port_pair(&rtp_port, &rtcp_port)) {
            *status_code = 503;
            *status_text = "Service Unavailable";
            snprintf(resp_body, resp_sz,
                     "{\"error\":\"port_exhaustion\",\"detail\":\"No available RTP port pairs in range %d-%d\"}\n",
                     DEFAULT_PORT_BASE, DEFAULT_PORT_MAX);
            return;
        }
    }

    uint32_t mountpoint_id = (device_id > 0) ? device_id : (uint32_t)rtp_port;

    /* 4. Slot allocation */
    MediaSession* s = allocate_session_slot();
    if (!s) {
        *status_code = 503;
        *status_text = "Service Unavailable";
        snprintf(resp_body, resp_sz,
                 "{\"error\":\"max_sessions_exceeded\",\"detail\":\"Media session table is full (max %d)\"}\n",
                 MAX_MEDIA_SESSIONS);
        return;
    }

    lifecycle_safe_strcpy(s->session_id, session_id, sizeof(s->session_id));
    lifecycle_safe_strcpy(s->operation_id, operation_id, sizeof(s->operation_id));
    lifecycle_safe_strcpy(s->sn, sn, sizeof(s->sn));
    s->device_id = device_id;
    s->mountpoint_id = mountpoint_id;
    s->rtp_port = rtp_port;
    s->rtcp_port = rtcp_port;
    lifecycle_safe_strcpy(s->pin, pin, sizeof(s->pin));
    s->ttl_sec = ttl_sec;
    s->state = MEDIA_STATE_STARTING;
    s->created_at = time(NULL);

    /* 5. Janus mountpoint creation */
    char janus_err[256] = {0};
    if (janus_create_mountpoint(mountpoint_id, rtp_port, rtcp_port, pin, janus_err, sizeof(janus_err)) != 0) {
        s->state = MEDIA_STATE_FAILED;
        lifecycle_safe_strcpy(s->stop_reason, "janus_create_failed", sizeof(s->stop_reason));
        g_metric_sessions_failed++;
        *status_code = 502;
        *status_text = "Bad Gateway";
        snprintf(resp_body, resp_sz,
                 "{\"error\":\"janus_error\",\"detail\":\"Failed to create Janus mountpoint: %s\"}\n",
                 janus_err[0] ? janus_err : "Unknown error");
        return;
    }

    /* 6. Ingress route creation */
    int clients_updated = 0;
    if (!upsert_route(sn, rtp_port, rtcp_port, &clients_updated)) {
        /* Compensating cleanup */
        janus_destroy_mountpoint(mountpoint_id, NULL, 0);
        s->state = MEDIA_STATE_FAILED;
        lifecycle_safe_strcpy(s->stop_reason, "route_upsert_failed", sizeof(s->stop_reason));
        g_metric_sessions_failed++;
        *status_code = 500;
        *status_text = "Internal Server Error";
        snprintf(resp_body, resp_sz,
                 "{\"error\":\"route_creation_failed\",\"detail\":\"Failed to add route to ingress table\"}\n");
        return;
    }

    /* 7. Activation */
    s->state = MEDIA_STATE_ACTIVE;
    s->started_at = time(NULL);
    g_metric_sessions_started++;

    *status_code = 201;
    *status_text = "Created";
    snprintf(resp_body, resp_sz,
             "{\"status\":\"success\",\"session_id\":\"%s\",\"operation_id\":\"%s\","
             "\"state\":\"active\",\"device_id\":%" PRIu32 ",\"sn\":\"%s\","
             "\"mountpoint_id\":%" PRIu32 ",\"rtp_port\":%d,\"rtcp_port\":%d,"
             "\"janus_ws\":\"/janus-ws\",\"pin\":\"%s\",\"ttl_sec\":%d,"
             "\"created_at\":%ld,\"started_at\":%ld}\n",
             s->session_id, s->operation_id, s->device_id, s->sn,
             s->mountpoint_id, s->rtp_port, s->rtcp_port, s->pin,
             s->ttl_sec, (long)s->created_at, (long)s->started_at);
}

static inline void get_media_session_health(const char* session_id, char* resp_body, size_t resp_sz, int* status_code) {
    MediaSession* s = find_session_by_id(session_id);
    if (!s) {
        *status_code = 404;
        snprintf(resp_body, resp_sz,
                 "{\"error\":\"session_not_found\",\"detail\":\"No session with id '%s'\"}\n",
                 session_id);
        return;
    }

    time_t now = time(NULL);
    const char* state_str = "active";
    switch (s->state) {
        case MEDIA_STATE_STARTING: state_str = "starting"; break;
        case MEDIA_STATE_ACTIVE:   state_str = "active"; break;
        case MEDIA_STATE_STOPPING: state_str = "stopping"; break;
        case MEDIA_STATE_STOPPED:  state_str = "stopped"; break;
        case MEDIA_STATE_FAILED:   state_str = "failed"; break;
        default:                   state_str = "unknown"; break;
    }

    bool transport_connected = false;
    bool fresh_rtp = false;
    const char* media_state = (s->state == MEDIA_STATE_ACTIVE) ? "waiting_media" : "disconnected";
    uint64_t rtp_pkts = s->final_rtp_packets;
    uint64_t bytes = s->final_bytes;
    time_t last_rtp = s->last_rtp_at;
    long idle_sec = -1;

    for (int i = 0; i < g_client_count; i++) {
        IngressClient* c = g_clients[i];
        if (c && strcmp(c->sn, s->sn) == 0) {
            transport_connected = true;
            rtp_pkts = c->rtp_packets;
            bytes = c->total_bytes;
            last_rtp = c->last_rtp_time;
            long rtp_idle = (c->last_rtp_time > 0) ? (long)(now - c->last_rtp_time) : -1;
            long trans_idle = (long)(now - c->last_activity);
            long idl = (c->last_rtp_time > 0) ? rtp_idle : trans_idle;
            if (idl < 0) idl = 0;
            idle_sec = idl;

            if (c->rtp_packets > 0 && rtp_idle >= 0 && rtp_idle <= RTP_STALE_DEADLINE_SEC) {
                fresh_rtp = true;
                media_state = "live";
            } else if (c->rtp_packets > 0) {
                fresh_rtp = false;
                media_state = "stale";
            } else {
                fresh_rtp = false;
                media_state = "waiting_media";
            }
            break;
        }
    }

    long elapsed = (long)((s->stopped_at > 0 ? s->stopped_at : now) - s->started_at);
    if (elapsed < 0) elapsed = 0;
    long ttl_rem = s->ttl_sec - elapsed;
    if (ttl_rem < 0) ttl_rem = 0;

    size_t off = 0;
    off += snprintf(resp_body + off, resp_sz - off,
                    "{\"status\":\"ok\",\"session_id\":\"%s\",\"operation_id\":\"%s\",\"state\":\"%s\","
                    "\"device_id\":%" PRIu32 ",\"sn\":\"%s\",\"mountpoint_id\":%" PRIu32 ","
                    "\"rtp_port\":%d,\"rtcp_port\":%d,\"pin\":\"%s\",\"ttl_sec\":%d,"
                    "\"elapsed_sec\":%ld,\"ttl_remaining_sec\":%ld,"
                    "\"created_at\":%ld,\"started_at\":%ld,",
                    s->session_id, s->operation_id, state_str,
                    s->device_id, s->sn, s->mountpoint_id,
                    s->rtp_port, s->rtcp_port, s->pin, s->ttl_sec,
                    elapsed, ttl_rem,
                    (long)s->created_at, (long)s->started_at);

    if (s->stopped_at > 0) {
        off += snprintf(resp_body + off, resp_sz - off,
                        "\"stopped_at\":%ld,\"stop_reason\":\"%s\",",
                        (long)s->stopped_at, s->stop_reason);
    } else {
        off += snprintf(resp_body + off, resp_sz - off,
                        "\"stopped_at\":null,\"stop_reason\":null,");
    }

    off += snprintf(resp_body + off, resp_sz - off,
                    "\"media_state\":\"%s\",\"transport_connected\":%s,\"fresh_rtp\":%s,"
                    "\"rtp_packets\":%" PRIu64 ",\"bytes\":%" PRIu64 ",",
                    media_state, transport_connected ? "true" : "false",
                    fresh_rtp ? "true" : "false",
                    rtp_pkts, bytes);

    if (last_rtp > 0) {
        off += snprintf(resp_body + off, resp_sz - off,
                        "\"last_rtp_at\":%ld,\"idle_sec\":%ld}\n",
                        (long)last_rtp, idle_sec >= 0 ? idle_sec : 0);
    } else {
        off += snprintf(resp_body + off, resp_sz - off,
                        "\"last_rtp_at\":null,\"idle_sec\":null}\n");
    }

    *status_code = 200;
}

static inline void reconcile_resources(char* resp_body, size_t resp_sz) {
    time_t now = time(NULL);
    int cleaned_mountpoints = 0;
    int cleaned_routes = 0;

    /* 1. Reconcile Janus mountpoints */
    uint32_t j_ids[MAX_MEDIA_SESSIONS * 2];
    int j_count = 0;
    if (janus_list_mountpoints(j_ids, MAX_MEDIA_SESSIONS * 2, &j_count) == 0) {
        for (int i = 0; i < j_count; i++) {
            uint32_t mid = j_ids[i];
            if (mid == 1) continue; /* Default static demo mountpoint */

            bool has_active = false;
            for (int s = 0; s < g_media_session_count; s++) {
                if ((g_media_sessions[s].state == MEDIA_STATE_ACTIVE ||
                     g_media_sessions[s].state == MEDIA_STATE_STARTING) &&
                    g_media_sessions[s].mountpoint_id == mid) {
                    has_active = true;
                    break;
                }
            }
            if (!has_active) {
                printf("[INGRESS RECONCILE] Destroying orphan Janus mountpoint %" PRIu32 "\n", mid);
                janus_destroy_mountpoint(mid, NULL, 0);
                cleaned_mountpoints++;
                g_metric_orphans_mountpoints++;
            }
        }
    }

    /* 2. Reconcile Ingress routes */
    for (int i = 0; i < g_routes.count; i++) {
        if (g_routes.entries[i].is_static) continue; /* Static route from routes.conf */

        bool has_active = false;
        for (int s = 0; s < g_media_session_count; s++) {
            if ((g_media_sessions[s].state == MEDIA_STATE_ACTIVE ||
                 g_media_sessions[s].state == MEDIA_STATE_STARTING) &&
                strcmp(g_media_sessions[s].sn, g_routes.entries[i].sn) == 0) {
                has_active = true;
                break;
            }
        }
        if (!has_active) {
            printf("[INGRESS RECONCILE] Removing orphan dynamic route for SN %s\n", g_routes.entries[i].sn);
            int cleared = 0;
            delete_route(g_routes.entries[i].sn, &cleared);
            cleaned_routes++;
            g_metric_orphans_routes++;
            i--;
        }
    }

    int active_sessions = 0;
    for (int s = 0; s < g_media_session_count; s++) {
        if (g_media_sessions[s].state == MEDIA_STATE_ACTIVE ||
            g_media_sessions[s].state == MEDIA_STATE_STARTING) {
            active_sessions++;
        }
    }

    if (resp_body && resp_sz > 0) {
        snprintf(resp_body, resp_sz,
                 "{\"status\":\"ok\",\"reconciled_at\":%ld,\"active_sessions\":%d,"
                 "\"orphans_cleaned_mountpoints\":%d,\"orphans_cleaned_routes\":%d}\n",
                 (long)now, active_sessions, cleaned_mountpoints, cleaned_routes);
    }
}

static inline void handle_media_metrics(char* resp_body, size_t resp_sz, int* status_code, const char** status_text) {
    time_t now = time(NULL);
    int active_sessions = 0;
    uint64_t total_rtp = 0;
    uint64_t total_bytes = 0;

    for (int i = 0; i < g_media_session_count; i++) {
        if (g_media_sessions[i].state == MEDIA_STATE_ACTIVE ||
            g_media_sessions[i].state == MEDIA_STATE_STARTING) {
            active_sessions++;
        }
        total_rtp += g_media_sessions[i].final_rtp_packets;
        total_bytes += g_media_sessions[i].final_bytes;
    }

    for (int i = 0; i < g_client_count; i++) {
        if (g_clients[i]) {
            total_rtp += g_clients[i]->rtp_packets;
            total_bytes += g_clients[i]->total_bytes;
        }
    }

    *status_code = 200;
    *status_text = "OK";
    snprintf(resp_body, resp_sz,
             "{\"status\":\"ok\",\"uptime_sec\":%ld,\"active_media_sessions\":%d,"
             "\"total_sessions_started\":%" PRIu64 ",\"total_sessions_stopped\":%" PRIu64 ","
             "\"total_sessions_failed\":%" PRIu64 ",\"orphans_cleaned_mountpoints\":%" PRIu64 ","
             "\"orphans_cleaned_routes\":%" PRIu64 ",\"total_rtp_packets\":%" PRIu64 ","
             "\"total_bytes\":%" PRIu64 "}\n",
             (long)(now - g_server_start_time), active_sessions,
             g_metric_sessions_started, g_metric_sessions_stopped,
             g_metric_sessions_failed, g_metric_orphans_mountpoints,
             g_metric_orphans_routes, total_rtp, total_bytes);
}

static inline void handle_openapi_spec(char* resp_body, size_t resp_sz, int* status_code, const char** status_text) {
    FILE* fp = fopen(OPENAPI_FILE_PATH, "r");
    if (fp) {
        size_t n = fread(resp_body, 1, resp_sz - 1, fp);
        resp_body[n] = '\0';
        fclose(fp);
        *status_code = 200;
        *status_text = "OK";
        return;
    }
    *status_code = 200;
    *status_text = "OK";
    snprintf(resp_body, resp_sz,
             "{\"openapi\":\"3.0.3\",\"info\":{\"title\":\"l4media API\",\"version\":\"1.0.0\"}}\n");
}

#endif /* MEDIA_LIFECYCLE_H */
