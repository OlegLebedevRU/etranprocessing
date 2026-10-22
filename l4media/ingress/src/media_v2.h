/* HEVC contract 2.0.0 adapter. Uses the same session table, lease watchdog,
 * route allocator, Janus adapter and teardown as v1. */
#ifndef MEDIA_V2_H
#define MEDIA_V2_H
#include <sys/random.h>

static bool g_media_hevc_enabled = false;
static bool g_media_hevc_verified = false;
static bool g_media_dependencies_ready = false;
static time_t g_media_probe_at = 0;

static inline const char* mv_string(json_t* value, const char* key) {
    const char* result = json_string_value(json_object_get(value, key));
    return result ? result : "";
}
static inline long long mv_integer(json_t* value, const char* key) {
    return (long long)json_number_value(json_object_get(value, key));
}
static inline void mv_output(json_t* value, char* response, size_t size) {
    char* text = json_dumps(value, JSON_COMPACT | JSON_SORT_KEYS);
    if (!text || strlen(text) >= size) snprintf(response, size, "{\"contract_version\":2,\"code\":\"capacity_exhausted\",\"message\":\"Response too large\"}");
    else lifecycle_safe_strcpy(response, text, size);
    free(text); json_decref(value);
}
static inline void mv_error(const char* code, int http, char* response, size_t size, int* status) {
    *status = http;
    mv_output(json_pack("{s:i,s:s,s:s}", "contract_version", 2, "code", code, "message", code), response, size);
}
static inline bool mv_random_pin(char* pin, size_t size) {
    unsigned char random[24]; size_t offset = 0;
    while (offset < sizeof(random)) {
        ssize_t n = getrandom(random + offset, sizeof(random) - offset, 0);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;
        offset += (size_t)n;
    }
    if (size < sizeof(random) * 2 + 1) return false;
    for (size_t i = 0; i < sizeof(random); i++) snprintf(pin + i * 2, 3, "%02x", random[i]);
    return true;
}
static inline void mv_format(json_t* descriptor, const char** codec, int* pt, char* fmtp, size_t size) {
    json_t* track = json_array_get(json_object_get(descriptor, "tracks"), 0);
    *codec = mv_string(track, "codec"); *pt = (int)mv_integer(track, "payload_type");
    if (!strcmp(*codec, "h265")) snprintf(fmtp, size,
        "profile-space=0;profile-id=1;tier-flag=0;level-id=%lld;sprop-max-don-diff=0", mv_integer(track, "level_id"));
    else snprintf(fmtp, size, "profile-level-id=%s;packetization-mode=1", mv_string(track, "profile_level_id"));
}
static inline int mv_ensure(MediaSession* session) {
    json_error_t error;
    json_t* descriptor = json_loads(session->descriptor, JSON_REJECT_DUPLICATES, &error);
    if (!descriptor) return -1;
    const char* codec; int pt; char fmtp[256];
    mv_format(descriptor, &codec, &pt, fmtp, sizeof(fmtp));
    int result = janus_ensure_format(session->mountpoint_id, session->rtp_port, session->rtcp_port,
                                    session->pin, codec, pt, fmtp, true);
    json_decref(descriptor); return result;
}

/* An actual create/info/delete roundtrip on the running Janus, never a package
 * version assumption. It proves mountpoint support; browser decode is TASK-40. */
static inline void mv_probe(void) {
    time_t now = time(NULL);
    if (g_media_probe_at && now - g_media_probe_at < 5) return;
    g_media_probe_at = now; g_media_hevc_verified = false;
    uint32_t ids[256]; int count = 0;
    g_media_dependencies_ready = janus_list_mountpoints(ids, 256, &count) == 0;
    if (!g_media_hevc_enabled || !g_media_dependencies_ready || !g_media_schema) return;
    int rtp, rtcp; char pin[64];
    if (!allocate_port_pair(&rtp, &rtcp) || !mv_random_pin(pin, sizeof(pin))) return;
    unsigned char random[4];
    if (getrandom(random, sizeof(random), 0) != (ssize_t)sizeof(random)) return;
    uint32_t id = 1000000000u + (((uint32_t)random[0] << 16 | (uint32_t)random[1] << 8 | random[2]) % 1000000000u);
    JanusMountpointInfo info;
    if (janus_get_mountpoint_info(id, &info) || info.found) return;
    const char* fmtp = "profile-space=0;profile-id=1;tier-flag=0;level-id=93;sprop-max-don-diff=0";
    if (janus_create_format(id, rtp, rtcp, pin, "h265", 96, fmtp)) return;
    bool ready = !janus_ensure_format(id, rtp, rtcp, pin, "h265", 96, fmtp, true);
    /* We created exactly this disposable mountpoint. Never delete a pre-existing ID. */
    bool cleaned = janus_destroy_mountpoint(id, NULL, 0) == 0;
    g_media_hevc_verified = ready && cleaned;
}

static inline void mv_persist(MediaSession* session) {
    if (!media_redis_ok() || session->contract_version != 2) return;
    json_error_t error;
    json_t* request = json_loads(session->v2_request, JSON_REJECT_DUPLICATES, &error);
    if (!request) return;
    json_object_set_new(request, "lease_expires_at", json_integer(session->renewed_at + session->ttl_sec));
    json_object_set_new(request, "pin", json_string(session->pin));
    json_t* record = json_pack("{s:i,s:o,s:i,s:i,s:i,s:i,s:i,s:I,s:I,s:I,s:s}",
        "format_version", 2, "request", request, "mountpoint_id", (int)session->mountpoint_id,
        "rtp_port", session->rtp_port, "rtcp_port", session->rtcp_port, "state", (int)session->state,
        "route_generation", (int)session->route_generation,
        "created_at", (json_int_t)session->created_at, "started_at", (json_int_t)session->started_at,
        "renewed_at", (json_int_t)session->renewed_at, "stop_reason", session->stop_reason);
    char* value = json_dumps(record, JSON_COMPACT | JSON_SORT_KEYS);
    if (value) {
        char key[192]; snprintf(key, sizeof(key), MEDIA_REDIS_PREFIX_SESSION "%s", session->session_id);
        media_redis_set_ex(key, value, media_redis_ttl_for(session->ttl_sec));
        if (session->state == MEDIA_STATE_ACTIVE || session->state == MEDIA_STATE_STARTING) {
            snprintf(key, sizeof(key), MEDIA_REDIS_PREFIX_SESSION_BY_SN "%s", session->sn);
            media_redis_set_ex(key, session->session_id, media_redis_ttl_for(session->ttl_sec));
            snprintf(key, sizeof(key), MEDIA_REDIS_PREFIX_MOUNTPOINT "%u", session->mountpoint_id);
            media_redis_set_ex(key, session->session_id, media_redis_ttl_for(session->ttl_sec));
            media_redis_save_route(session->sn, session->rtp_port, session->rtcp_port, session->mountpoint_id, session->ttl_sec);
        }
    }
    free(value); json_decref(record);
}

static inline bool mv_fill(MediaSession* session, json_t* request) {
    char* descriptor = json_dumps(json_object_get(request, "descriptor"), JSON_COMPACT | JSON_SORT_KEYS);
    char* serialized = json_dumps(request, JSON_COMPACT | JSON_SORT_KEYS);
    if (!descriptor || !serialized || strlen(descriptor) >= sizeof(session->descriptor) || strlen(serialized) >= sizeof(session->v2_request)) {
        free(descriptor); free(serialized); return false;
    }
    session->contract_version = 2;
    lifecycle_safe_strcpy(session->session_id, mv_string(request, "session_id"), sizeof(session->session_id));
    lifecycle_safe_strcpy(session->operation_id, mv_string(request, "operation_id"), sizeof(session->operation_id));
    lifecycle_safe_strcpy(session->lease_id, mv_string(request, "lease_id"), sizeof(session->lease_id));
    json_t* publisher = json_object_get(request, "publisher");
    lifecycle_safe_strcpy(session->sn, mv_string(publisher, "sn"), sizeof(session->sn));
    lifecycle_safe_strcpy(session->stream_instance_id, mv_string(publisher, "stream_instance_id"), sizeof(session->stream_instance_id));
    lifecycle_safe_strcpy(session->pin, mv_string(request, "pin"), sizeof(session->pin));
    lifecycle_safe_strcpy(session->descriptor, descriptor, sizeof(session->descriptor));
    lifecycle_safe_strcpy(session->v2_request, serialized, sizeof(session->v2_request));
    free(descriptor); free(serialized); return true;
}

static inline bool mv_identity(MediaSession* session, json_t* request) {
    if (!session || session->contract_version != 2) return false;
    json_error_t error;
    json_t* descriptor = json_loads(session->descriptor, JSON_REJECT_DUPLICATES, &error);
    bool match = descriptor && !strcmp(session->sn, mv_string(request, "sn")) &&
        !strcmp(session->session_id, mv_string(request, "session_id")) &&
        !strcmp(session->lease_id, mv_string(request, "lease_id")) &&
        !strcmp(session->stream_instance_id, mv_string(request, "stream_instance_id")) &&
        !strcmp(mv_string(descriptor, "descriptor_revision"), mv_string(request, "descriptor_revision"));
    json_decref(descriptor); return match;
}

static inline void mv_response(MediaSession* session, bool health, char* response, size_t size) {
    json_error_t error;
    json_t* descriptor = json_loads(session->descriptor, JSON_REJECT_DUPLICATES, &error);
    const char* state = session->state == MEDIA_STATE_ACTIVE ? "active" :
        session->state == MEDIA_STATE_STARTING ? "starting" : session->state == MEDIA_STATE_FAILED ? "failed" : "stopped";
    json_t* value = json_pack("{s:i,s:s,s:s,s:i,s:s,s:o,s:{s:s,s:s},s:s,s:s,s:i,s:i,s:i,s:s,s:s}",
        "contract_version", 2, "session_id", session->session_id, "stream_instance_id", session->stream_instance_id,
        "route_generation", (int)session->route_generation, "state", state, "descriptor", descriptor,
        "publisher", "sn", session->sn, "stream_instance_id", session->stream_instance_id,
        "lease_id", session->lease_id, "operation_id", session->operation_id,
        "mountpoint_id", (int)session->mountpoint_id, "rtp_port", session->rtp_port,
        "rtcp_port", session->rtcp_port, "janus_ws", "/janus-ws", "pin", session->pin);
    if (health) {
        bool connected = false, fresh = false; uint64_t packets = session->final_rtp_packets, bytes = session->final_bytes;
        time_t last = session->last_rtp_at;
        for (int i = 0; i < g_client_count; i++) {
            IngressClient* client = g_clients[i];
            if (client && !strcmp(client->sn, session->sn) && session->state != MEDIA_STATE_STOPPED) {
                connected = true; packets = client->rtp_packets; bytes = client->total_bytes; last = client->last_rtp_time;
                fresh = packets && last && time(NULL) - last <= RTP_STALE_DEADLINE_SEC; break;
            }
        }
        json_t* transport = json_pack("{s:b,s:I,s:I,s:o,s:b}", "connected", connected,
            "rtp_packets", (json_int_t)packets, "bytes", (json_int_t)bytes,
            "last_rtp_at_ms", last ? json_integer((json_int_t)last * 1000) : json_null(), "rtp_fresh", fresh);
        json_object_set_new(value, "transport", transport);
    }
    mv_output(value, response, size);
}

static inline void mv_start(json_t* request, char* response, size_t size, int* status) {
    time_t now = time(NULL); long long deadline = mv_integer(request, "lease_expires_at");
    if (deadline <= now || deadline - now > 7200) { mv_error("expired", 410, response, size, status); return; }
    MediaSession candidate; memset(&candidate, 0, sizeof(candidate));
    if (!mv_fill(&candidate, request)) { mv_error("invalid_payload", 400, response, size, status); return; }
    for (int i = 0; i < g_media_session_count; i++) {
        MediaSession* other = &g_media_sessions[i];
        if (other->contract_version == 2 && strcmp(other->session_id, candidate.session_id) &&
            (!strcmp(other->operation_id, candidate.operation_id) || !strcmp(other->stream_instance_id, candidate.stream_instance_id))) {
            mv_error("conflict", 409, response, size, status); return;
        }
    }
    MediaSession* session = find_session_by_id(candidate.session_id);
    if (session) {
        bool same = session->contract_version == 2 && !strcmp(session->sn, candidate.sn) &&
            !strcmp(session->lease_id, candidate.lease_id) && !strcmp(session->stream_instance_id, candidate.stream_instance_id) &&
            !strcmp(session->operation_id, candidate.operation_id) && !strcmp(session->descriptor, candidate.descriptor) &&
            (!json_object_get(request, "pin") || !strcmp(session->pin, candidate.pin));
        if (!same) { mv_error("conflict", 409, response, size, status); return; }
        expire_media_session(session, now);
        if (session->state == MEDIA_STATE_ACTIVE || session->state == MEDIA_STATE_STARTING) {
            int result = mv_ensure(session);
            if (result) { mv_error(result == -2 ? "conflict" : "janus_unavailable", result == -2 ? 409 : 502, response, size, status); return; }
        }
        *status = 200; mv_response(session, false, response, size); return;
    }
    MediaSession* owner = find_active_session_for_sn(candidate.sn);
    expire_media_session(owner, now); owner = find_active_session_for_sn(candidate.sn);
    if (owner) { mv_error("conflict", 409, response, size, status); return; }
    const char* codec; int pt; char fmtp[256];
    mv_format(json_object_get(request, "descriptor"), &codec, &pt, fmtp, sizeof(fmtp));
    if (!strcmp(codec, "h265") && (!g_media_hevc_enabled || !g_media_hevc_verified)) {
        mv_error("unsupported", 422, response, size, status); return;
    }
    if (!candidate.pin[0] && !mv_random_pin(candidate.pin, sizeof(candidate.pin))) {
        mv_error("capacity_exhausted", 503, response, size, status); return;
    }
    if (!allocate_port_pair(&candidate.rtp_port, &candidate.rtcp_port) || !(session = allocate_session_slot())) {
        mv_error("capacity_exhausted", 503, response, size, status); return;
    }
    candidate.mountpoint_id = (uint32_t)candidate.rtp_port;
    if (g_route_generation >= INT32_MAX) {
        mv_error("capacity_exhausted", 503, response, size, status); return;
    }
    candidate.route_generation = ++g_route_generation;
    candidate.created_at = candidate.started_at = candidate.renewed_at = now;
    candidate.ttl_sec = (int)(deadline - now); candidate.state = MEDIA_STATE_STARTING;
    /* Newly allocated IDs must be absent. Never adopt or clean someone else's mountpoint. */
    JanusMountpointInfo info;
    if (janus_get_mountpoint_info(candidate.mountpoint_id, &info) || info.found) {
        mv_error(info.found ? "conflict" : "janus_unavailable", info.found ? 409 : 502, response, size, status); return;
    }
    if (janus_create_format(candidate.mountpoint_id, candidate.rtp_port, candidate.rtcp_port,
                             candidate.pin, codec, pt, fmtp)) {
        mv_error("janus_unavailable", 502, response, size, status); return;
    }
    if (mv_ensure(&candidate)) {
        janus_destroy_mountpoint(candidate.mountpoint_id, NULL, 0);
        mv_error("descriptor_mismatch", 409, response, size, status); return;
    }
    int changed = 0;
    if (!upsert_route(candidate.sn, candidate.rtp_port, candidate.rtcp_port, &changed)) {
        janus_destroy_mountpoint(candidate.mountpoint_id, NULL, 0);
        mv_error("capacity_exhausted", 503, response, size, status); return;
    }
    *session = candidate;
    for (int i = 0; i < g_client_count; i++)
        if (g_clients[i] && !strcmp(g_clients[i]->sn, session->sn)) g_clients[i]->keyframe_feedback_interval_ms = 500;
    expire_media_session(session, time(NULL));
    if (session->state == MEDIA_STATE_STOPPED) { mv_error("expired", 410, response, size, status); return; }
    mv_persist(session); g_metric_sessions_started++;
    *status = 201; mv_response(session, false, response, size);
}

static inline void mv_update(json_t* request, bool stop, bool health, char* response, size_t size, int* status) {
    MediaSession* session = find_session_by_id(mv_string(request, "session_id"));
    if (!mv_identity(session, request)) { mv_error("lease_mismatch", 409, response, size, status); return; }
    expire_media_session(session, time(NULL));
    if (stop) {
        stop_media_session(session->session_id, NULL, mv_string(request, "reason"), NULL, 0, status);
        mv_persist(session);
    } else if (!health) {
        long long deadline = mv_integer(request, "lease_expires_at"); time_t now = time(NULL);
        if (session->state != MEDIA_STATE_ACTIVE && session->state != MEDIA_STATE_STARTING) {
            mv_error("expired", 410, response, size, status); return;
        }
        if (deadline <= now || deadline - now > 7200) { mv_error("expired", 410, response, size, status); return; }
        session->ttl_sec = (int)(deadline - now); session->renewed_at = now; mv_persist(session);
    }
    *status = 200; mv_response(session, health, response, size);
}

static inline bool media_v1_conflicts(const char* path, const char* body) {
    char id[64] = {0}, sn[128] = {0};
    const char* prefix = "/api/v1/media/sessions/";
    if (!strncmp(path, prefix, strlen(prefix))) {
        lifecycle_safe_strcpy(id, path + strlen(prefix), sizeof(id));
        char* end = strpbrk(id, "/?"); if (end) *end = 0;
    }
    MediaSession* session = find_session_by_id(id);
    if (session && session->contract_version == 2) return true;
    if (body) {
        json_get_string(body, "session_id", id, sizeof(id));
        json_get_string(body, "sn", sn, sizeof(sn));
    }
    session = find_session_by_id(id);
    if (!session) session = find_active_session_for_sn(sn);
    return session && session->contract_version == 2;
}

static inline void media_v2_restore(void) {
    if (!media_redis_ok() || !g_media_schema) return;
    redisReply* keys = redisCommand(g_media_redis, "KEYS " MEDIA_REDIS_PREFIX_SESSION "*");
    if (!keys || keys->type != REDIS_REPLY_ARRAY) { if (keys) freeReplyObject(keys); return; }
    for (size_t i = 0; i < keys->elements; i++) {
        char serialized[16384];
        if (!keys->element[i]->str || !media_redis_get(keys->element[i]->str, serialized, sizeof(serialized)) || serialized[0] != '{') continue;
        json_error_t error; json_t* record = json_loads(serialized, JSON_REJECT_DUPLICATES, &error);
        json_t* request = json_object_get(record, "request");
        if (mv_integer(record, "format_version") != 2 || !ms_validate(json_object_get(json_object_get(g_media_schema, "$defs"), "media_start"), request, 0)) {
            json_decref(record); continue; /* New record without a descriptor is never implicit H264. */
        }
        if (find_session_by_id(mv_string(request, "session_id"))) { json_decref(record); continue; }
        MediaSession candidate; memset(&candidate, 0, sizeof(candidate));
        if (!mv_fill(&candidate, request)) { json_decref(record); continue; }
        candidate.mountpoint_id = (uint32_t)mv_integer(record, "mountpoint_id");
        candidate.rtp_port = (int)mv_integer(record, "rtp_port"); candidate.rtcp_port = (int)mv_integer(record, "rtcp_port");
        candidate.route_generation = (uint32_t)mv_integer(record, "route_generation");
        candidate.created_at = (time_t)mv_integer(record, "created_at"); candidate.started_at = (time_t)mv_integer(record, "started_at");
        candidate.renewed_at = (time_t)mv_integer(record, "renewed_at");
        long long deadline = mv_integer(request, "lease_expires_at");
        int stored_state = (int)mv_integer(record, "state");
        bool live = stored_state == MEDIA_STATE_ACTIVE || stored_state == MEDIA_STATE_STARTING;
        if (!candidate.pin[0] || candidate.rtp_port < 1 || candidate.rtp_port > 65535 || candidate.rtcp_port < 1 ||
            candidate.rtcp_port > 65535 || !candidate.mountpoint_id || !candidate.route_generation ||
            deadline - candidate.renewed_at <= 0 || deadline - candidate.renewed_at > 7200 ||
            find_active_session_for_sn(candidate.sn)) { json_decref(record); continue; }
        candidate.ttl_sec = (int)(deadline - candidate.renewed_at);
        candidate.state = live && deadline > time(NULL) ? MEDIA_STATE_STARTING : MEDIA_STATE_STOPPED;
        MediaSession* session = allocate_session_slot();
        int changed;
        if (session && (candidate.state == MEDIA_STATE_STOPPED || upsert_route(candidate.sn, candidate.rtp_port, candidate.rtcp_port, &changed))) {
            *session = candidate;
            if (candidate.route_generation > g_route_generation) g_route_generation = candidate.route_generation;
        }
        json_decref(record);
    }
    freeReplyObject(keys);
}

static inline bool mv_query_decode(char* text) {
    char* output = text;
    for (char* input = text; *input; input++) {
        unsigned char value = (unsigned char)*input;
        if (value == '%') {
            if (!input[1] || !input[2] || !isxdigit((unsigned char)input[1]) || !isxdigit((unsigned char)input[2])) return false;
            char digits[3] = {input[1], input[2], 0};
            value = (unsigned char)strtoul(digits, NULL, 16); input += 2;
            if (!value) return false;
        } else if (value == '+') value = ' ';
        *output++ = (char)value;
    }
    *output = 0; return true;
}

static inline void media_v2_http(const char* method, const char* path, const char* body,
                                char* response, size_t size, int* status) {
    if (!g_media_schema) { mv_error("unsupported", 503, response, size, status); return; }
    if (!strcmp(method, "GET") && !strcmp(path, "/api/v2/media/capabilities")) {
        mv_probe();
        const char* revision = getenv("L4MEDIA_SOURCE_REVISION");
        json_t* value = json_pack("{s:i,s:[i,i],s:s,s:b,s:b,s:[s],s:[{s:s,s:s,s:s,s:i}],s:i,s:b,s:b}",
            "contract_version", 2, "media_contract_versions", 1, 2, "server_revision", revision && revision[0] ? revision : "unknown",
            "dependencies_ready", g_media_dependencies_ready, "hevc_runtime_verified", g_media_hevc_verified,
            "codecs", "h264", "formats", "codec", "h264", "codec_profile", "baseline", "packetization", "rfc6184", "clock_rate", 90000,
            "max_video_tracks", 1, "audio_supported", 0, "reverse_rtcp", 1);
        if (g_media_hevc_verified) {
            json_array_append_new(json_object_get(value, "codecs"), json_string("h265"));
            json_array_append_new(json_object_get(value, "formats"), json_pack("{s:s,s:s,s:s,s:i}",
                "codec", "h265", "codec_profile", "main", "packetization", "rfc7798", "clock_rate", 90000));
        }
        *status = 200; mv_output(value, response, size); return;
    }
    const char* definition = NULL; bool stop = false;
    if (!strcmp(method, "POST")) {
        if (!strcmp(path, "/api/v2/media/sessions/start")) definition = "media_start";
        else if (!strcmp(path, "/api/v2/media/sessions/renew")) definition = "media_renew";
        else if (!strcmp(path, "/api/v2/media/sessions/stop")) { definition = "media_stop"; stop = true; }
    }
    if (definition) {
        json_t* request = media_v2_parse(body, definition);
        if (!request) { mv_error("invalid_payload", 400, response, size, status); return; }
        if (!strcmp(definition, "media_start")) mv_start(request, response, size, status);
        else mv_update(request, stop, false, response, size, status);
        json_decref(request); return;
    }
    const char* prefix = "/api/v2/media/sessions/";
    if (!strcmp(method, "GET") && !strncmp(path, prefix, strlen(prefix))) {
        char query[2048]; lifecycle_safe_strcpy(query, path + strlen(prefix), sizeof(query));
        char* args = strchr(query, '?');
        if (!args) { mv_error("invalid_payload", 400, response, size, status); return; }
        *args++ = 0;
        json_t* request = json_pack("{s:i,s:s}", "contract_version", 2, "session_id", query);
        bool valid = true; char* save = NULL;
        for (char* part = strtok_r(args, "&", &save); part; part = strtok_r(NULL, "&", &save)) {
            char* equal = strchr(part, '=');
            if (!equal) { valid = false; break; } *equal++ = 0;
            if (!mv_query_decode(part) || !mv_query_decode(equal)) { valid = false; break; }
            if (json_object_get(request, part)) { valid = false; break; }
            json_object_set_new(request, part, json_string(equal));
        }
        valid = valid && ms_validate(json_object_get(json_object_get(g_media_schema, "$defs"), "media_identity"), request, 0);
        if (valid) mv_update(request, false, true, response, size, status);
        else mv_error("invalid_payload", 400, response, size, status);
        json_decref(request); return;
    }
    mv_error("unsupported", 404, response, size, status);
}
#endif
