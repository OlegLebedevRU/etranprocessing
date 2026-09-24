/**
 * @file media_redis.h
 * @brief Redis DB 2 ownership store for l4media media sessions / routes / mountpoints.
 *
 * Keys (TTL = session ttl_sec + refresh; never the 5-min signaling TTL):
 *   media:session:{session_id}       compact serialized session
 *   media:session-by-sn:{sn}         session_id pointer
 *   media:route:{sn}                 rtp:rtcp:mountpoint_id
 *   media:mountpoint:{mountpoint_id} session_id
 *
 * Optional: if REDIS_URL is unset or Redis is down, all ops no-op and
 * in-memory state remains authoritative (degraded mode).
 */
#ifndef MEDIA_REDIS_H
#define MEDIA_REDIS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include <hiredis/hiredis.h>

#define MEDIA_REDIS_PREFIX_SESSION      "media:session:"
#define MEDIA_REDIS_PREFIX_SESSION_BY_SN "media:session-by-sn:"
#define MEDIA_REDIS_PREFIX_ROUTE        "media:route:"
#define MEDIA_REDIS_PREFIX_MOUNTPOINT   "media:mountpoint:"
#define MEDIA_REDIS_DEFAULT_URL         "redis://redis:6379/2"
/* Ownership keys must outlive a stream pause; refresh on start/idempotent/health. */
#define MEDIA_REDIS_TTL_PAD_SEC         60

static redisContext* g_media_redis = NULL;
static int g_media_redis_db = 2;
static char g_media_redis_host[128] = "redis";
static int g_media_redis_port = 6379;

static inline int media_redis_parse_url(const char* url) {
    if (!url || !url[0]) return -1;
    const char* p = strstr(url, "://");
    p = p ? p + 3 : url;
    const char* slash = strchr(p, '/');
    const char* colon = memchr(p, ':', slash ? (size_t)(slash - p) : strlen(p));
    if (colon) {
        size_t hl = (size_t)(colon - p);
        if (hl >= sizeof(g_media_redis_host)) hl = sizeof(g_media_redis_host) - 1;
        memcpy(g_media_redis_host, p, hl);
        g_media_redis_host[hl] = '\0';
        g_media_redis_port = atoi(colon + 1);
    } else {
        size_t hl = slash ? (size_t)(slash - p) : strlen(p);
        if (hl >= sizeof(g_media_redis_host)) hl = sizeof(g_media_redis_host) - 1;
        memcpy(g_media_redis_host, p, hl);
        g_media_redis_host[hl] = '\0';
    }
    if (slash && slash[1]) g_media_redis_db = atoi(slash + 1);
    return 0;
}

static inline void media_redis_close(void) {
    if (g_media_redis) {
        redisFree(g_media_redis);
        g_media_redis = NULL;
    }
}

static inline int media_redis_init(const char* url) {
    const char* u = (url && url[0]) ? url : getenv("REDIS_URL");
    if (!u || !u[0]) {
        printf("[MEDIA REDIS] REDIS_URL not set — ownership stays in-memory\n");
        return 0;
    }
    media_redis_parse_url(u);
    struct timeval tv = {2, 0};
    g_media_redis = redisConnectWithTimeout(g_media_redis_host, g_media_redis_port, tv);
    if (!g_media_redis || g_media_redis->err) {
        fprintf(stderr, "[MEDIA REDIS] connect %s:%d failed: %s\n",
                g_media_redis_host, g_media_redis_port,
                g_media_redis ? g_media_redis->errstr : "alloc");
        media_redis_close();
        return -1;
    }
    redisReply* sel = redisCommand(g_media_redis, "SELECT %d", g_media_redis_db);
    if (!sel) {
        media_redis_close();
        return -1;
    }
    freeReplyObject(sel);
    redisReply* pong = redisCommand(g_media_redis, "PING");
    if (!pong) {
        media_redis_close();
        return -1;
    }
    freeReplyObject(pong);
    printf("[MEDIA REDIS] connected %s:%d db=%d\n",
           g_media_redis_host, g_media_redis_port, g_media_redis_db);
    return 0;
}

static inline int media_redis_ok(void) { return g_media_redis != NULL; }

static inline void media_redis_set_ex(const char* key, const char* val, int ttl_sec) {
    if (!g_media_redis || !key || !val) return;
    redisReply* r = redisCommand(g_media_redis, "SET %s %s EX %d", key, val, ttl_sec);
    if (r) freeReplyObject(r);
}

static inline void media_redis_del(const char* key) {
    if (!g_media_redis || !key) return;
    redisReply* r = redisCommand(g_media_redis, "DEL %s", key);
    if (r) freeReplyObject(r);
}

/* GET into caller buf; returns true if key existed. */
static inline bool media_redis_get(const char* key, char* out, size_t out_sz) {
    if (!g_media_redis || !key || !out || out_sz == 0) return false;
    out[0] = '\0';
    redisReply* r = redisCommand(g_media_redis, "GET %s", key);
    if (!r) return false;
    bool hit = (r->type == REDIS_REPLY_STRING && r->str);
    if (hit) {
        snprintf(out, out_sz, "%s", r->str);
    }
    freeReplyObject(r);
    return hit;
}

static inline int media_redis_ttl_for(int session_ttl_sec) {
    int ttl = session_ttl_sec + MEDIA_REDIS_TTL_PAD_SEC;
    if (ttl < 60) ttl = 60;
    return ttl;
}

/* ---- session serialization: field|field|... ---- */
/* session_id|operation_id|sn|device_id|mountpoint|rtp|rtcp|pin|state|ttl|created|started */

static inline void media_redis_save_session(const char* session_id, const char* operation_id,
                                           const char* sn, uint32_t device_id, uint32_t mountpoint_id,
                                           int rtp_port, int rtcp_port, const char* pin,
                                           int state, int ttl_sec, time_t created_at, time_t started_at) {
    if (!media_redis_ok() || !session_id || !session_id[0]) return;
    /* pin may be empty — use "-" so strtok-style parsers never collapse fields. */
    const char* pin_out = (pin && pin[0]) ? pin : "-";
    char val[512];
    snprintf(val, sizeof(val), "%s|%s|%s|%u|%u|%d|%d|%s|%d|%d|%ld|%ld",
             session_id ? session_id : "",
             operation_id ? operation_id : "",
             sn ? sn : "",
             device_id, mountpoint_id, rtp_port, rtcp_port,
             pin_out,
             state, ttl_sec, (long)created_at, (long)started_at);
    int ttl = media_redis_ttl_for(ttl_sec);

    char k_sess[192], k_sn[192], k_mp[96];
    snprintf(k_sess, sizeof(k_sess), MEDIA_REDIS_PREFIX_SESSION "%s", session_id);
    snprintf(k_sn, sizeof(k_sn), MEDIA_REDIS_PREFIX_SESSION_BY_SN "%s", sn ? sn : "");
    snprintf(k_mp, sizeof(k_mp), MEDIA_REDIS_PREFIX_MOUNTPOINT "%u", mountpoint_id);

    media_redis_set_ex(k_sess, val, ttl);
    if (sn && sn[0]) media_redis_set_ex(k_sn, session_id, ttl);
    media_redis_set_ex(k_mp, session_id, ttl);
}

static inline void media_redis_save_route(const char* sn, int rtp_port, int rtcp_port,
                                         uint32_t mountpoint_id, int ttl_sec) {
    if (!media_redis_ok() || !sn || !sn[0]) return;
    char val[64], key[192];
    snprintf(val, sizeof(val), "%d:%d:%u", rtp_port, rtcp_port, mountpoint_id);
    snprintf(key, sizeof(key), MEDIA_REDIS_PREFIX_ROUTE "%s", sn);
    media_redis_set_ex(key, val, media_redis_ttl_for(ttl_sec));
}

static inline void media_redis_drop_session(const char* session_id, const char* sn, uint32_t mountpoint_id) {
    if (!media_redis_ok()) return;
    char k[192];
    if (session_id && session_id[0]) {
        snprintf(k, sizeof(k), MEDIA_REDIS_PREFIX_SESSION "%s", session_id);
        media_redis_del(k);
    }
    if (sn && sn[0]) {
        snprintf(k, sizeof(k), MEDIA_REDIS_PREFIX_SESSION_BY_SN "%s", sn);
        media_redis_del(k);
        snprintf(k, sizeof(k), MEDIA_REDIS_PREFIX_ROUTE "%s", sn);
        media_redis_del(k);
    }
    snprintf(k, sizeof(k), MEDIA_REDIS_PREFIX_MOUNTPOINT "%u", mountpoint_id);
    media_redis_del(k);
}

/* True if Redis holds ownership for this SN (route or session-by-sn). */
static inline bool media_redis_owns_sn(const char* sn) {
    if (!media_redis_ok() || !sn || !sn[0]) return false;
    char key[192], buf[256];
    snprintf(key, sizeof(key), MEDIA_REDIS_PREFIX_ROUTE "%s", sn);
    if (media_redis_get(key, buf, sizeof(buf))) return true;
    snprintf(key, sizeof(key), MEDIA_REDIS_PREFIX_SESSION_BY_SN "%s", sn);
    return media_redis_get(key, buf, sizeof(buf));
}

/* Restore callback: caller fills MediaSession / routes. */
typedef void (*media_redis_restore_fn)(const char* session_id, const char* operation_id,
                                       const char* sn, uint32_t device_id, uint32_t mountpoint_id,
                                       int rtp_port, int rtcp_port, const char* pin,
                                       int state, int ttl_sec, time_t created_at, time_t started_at,
                                       void* user);

static inline int media_redis_restore_sessions(media_redis_restore_fn fn, void* user) {
    if (!media_redis_ok() || !fn) return 0;
    redisReply* r = redisCommand(g_media_redis, "KEYS " MEDIA_REDIS_PREFIX_SESSION "*");
    if (!r || r->type != REDIS_REPLY_ARRAY) {
        if (r) freeReplyObject(r);
        return 0;
    }
    int n = 0;
    for (size_t i = 0; i < r->elements; i++) {
        if (r->element[i]->type != REDIS_REPLY_STRING || !r->element[i]->str) continue;
        char val[512];
        if (!media_redis_get(r->element[i]->str, val, sizeof(val))) continue;

        /* Split on '|' keeping empty fields (strtok_r would collapse "||"). */
        char* fields[12];
        int nf = 0;
        char* p = val;
        while (nf < 12) {
            fields[nf++] = p;
            char* bar = strchr(p, '|');
            if (!bar) break;
            *bar = '\0';
            p = bar + 1;
        }
        if (nf < 12) continue;

        const char* pin_f = (strcmp(fields[7], "-") == 0) ? "" : fields[7];
        fn(fields[0], fields[1], fields[2],
           (uint32_t)strtoul(fields[3], NULL, 10),
           (uint32_t)strtoul(fields[4], NULL, 10),
           atoi(fields[5]), atoi(fields[6]), pin_f,
           atoi(fields[8]), atoi(fields[9]),
           (time_t)atol(fields[10]), (time_t)atol(fields[11]),
           user);
        n++;
    }
    freeReplyObject(r);
    return n;
}

#endif /* MEDIA_REDIS_H */
