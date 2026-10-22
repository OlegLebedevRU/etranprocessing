/* Codec-aware Janus adapter, shared by legacy H264 and v2 lifecycle. */
#ifndef MEDIA_JANUS_H
#define MEDIA_JANUS_H
#define LEGACY_VIDEO_FMTP "profile-level-id=42e01f;packetization-mode=1"

typedef struct {
    bool found;
    int videoport, videortcpport, payload_type;
    char pin[513], codec[16], fmtp[256];
    bool has_pin;
} JanusMountpointInfo;

static inline json_t* mj_find(json_t* value, const char* key, int depth) {
    if (!value || depth > 32) return NULL;
    json_t* found = json_object_get(value, key);
    if (found) return found;
    if (json_is_object(value)) {
        const char* name; json_t* child;
        json_object_foreach(value, name, child) {
            (void)name;
            found = mj_find(child, key, depth + 1); if (found) return found;
        }
    } else if (json_is_array(value)) {
        for (size_t i = 0; i < json_array_size(value); i++) {
            found = mj_find(json_array_get(value, i), key, depth + 1); if (found) return found;
        }
    }
    return NULL;
}

static inline json_t* mj_request(json_t* request) {
    json_t* envelope = json_pack("{s:s,s:s,s:s,s:s,s:O}", "janus", "message_plugin",
        "transaction", "l4media", "admin_secret", g_janus_admin_secret,
        "plugin", "janus.plugin.streaming", "request", request);
    char* text = json_dumps(envelope, JSON_COMPACT);
    json_decref(envelope);
    char* response = malloc(65536);
    json_t* result = NULL;
    if (text && response && janus_admin_request(text, response, 65536) == 0) {
        const char* body = strstr(response, "\r\n\r\n");
        json_error_t error;
        result = json_loads(body ? body + 4 : response, JSON_REJECT_DUPLICATES, &error);
    }
    free(text); free(response);
    return result;
}

static inline int janus_get_mountpoint_info(uint32_t id, JanusMountpointInfo* out) {
    memset(out, 0, sizeof(*out));
    json_t* request = json_pack("{s:s,s:i}", "request", "info", "id", (int)id);
    json_t* response = mj_request(request); json_decref(request);
    if (!response) return -1;
    json_t* info = mj_find(response, "info", 0);
    if (info) {
        json_t* media = json_object_get(info, "media");
        json_t* video = NULL;
        for (size_t i = 0; i < json_array_size(media); i++) {
            json_t* item = json_array_get(media, i);
            const char* kind = json_string_value(json_object_get(item, "type"));
            if (kind && !strcmp(kind, "video")) { video = item; break; }
        }
        /* Legacy test/older Janus info may omit type for the sole video track. */
        if (!video && json_array_size(media) == 1) video = json_array_get(media, 0);
        out->found = true;
        out->videoport = (int)json_integer_value(json_object_get(video, "port"));
        out->videortcpport = (int)json_integer_value(json_object_get(video, "rtcpport"));
        out->payload_type = (int)json_integer_value(json_object_get(video, "pt"));
        lifecycle_safe_strcpy(out->codec, json_string_value(json_object_get(video, "codec")), sizeof(out->codec));
        lifecycle_safe_strcpy(out->fmtp, json_string_value(json_object_get(video, "fmtp")), sizeof(out->fmtp));
        lifecycle_safe_strcpy(out->pin, json_string_value(json_object_get(info, "pin")), sizeof(out->pin));
        out->has_pin = out->pin[0] != 0;
    }
    json_decref(response); return 0;
}

static inline int janus_create_format(uint32_t id, int rtp, int rtcp, const char* pin,
                                      const char* codec, int pt, const char* fmtp) {
    json_t* request = json_pack("{s:s,s:s,s:i,s:b,s:b,s:i,s:i,s:i,s:s,s:s,s:b}",
        "request", "create", "type", "rtp", "id", (int)id, "video", 1, "audio", 0,
        "videoport", rtp, "videortcpport", rtcp, "videopt", pt, "videocodec", codec,
        "videofmtp", fmtp, "videobufferkf", 1);
    if (pin && pin[0]) json_object_set_new(request, "pin", json_string(pin));
    json_t* response = mj_request(request); json_decref(request);
    const char* result = json_string_value(mj_find(response, "streaming", 0));
    bool created = result && !strcmp(result, "created");
    json_decref(response);
    return created ? 0 : -1; /* Already-exists is NOT proof of descriptor identity. */
}

static inline int janus_ensure_format(uint32_t id, int rtp, int rtcp, const char* pin,
                                      const char* codec, int pt, const char* fmtp, bool strict) {
    JanusMountpointInfo info;
    if (janus_get_mountpoint_info(id, &info) != 0) return -1;
    if (info.found) {
        bool format = (!strcasecmp(info.codec, codec) && info.payload_type == pt && !strcmp(info.fmtp, fmtp));
        if (!strict && !info.codec[0] && !info.fmtp[0]) format = true;
        if (info.videoport != rtp || info.videortcpport != rtcp || strcmp(info.pin, pin ? pin : "") || !format)
            return -2; /* Never destroy/reconfigure a mountpoint with a live owner. */
        return 0;
    }
    return janus_create_format(id, rtp, rtcp, pin, codec, pt, fmtp);
}

static inline int janus_create_mountpoint(uint32_t id, int rtp, int rtcp, const char* pin,
                                         char* error, size_t error_size) {
    int result = janus_create_format(id, rtp, rtcp, pin, "h264", 96, LEGACY_VIDEO_FMTP);
    if (result && error && error_size) lifecycle_safe_strcpy(error, "Janus create failed", error_size);
    return result;
}

static inline int janus_mountpoint_ensure(uint32_t id, int rtp, int rtcp, const char* pin,
                                         char* error, size_t error_size) {
    int result = janus_ensure_format(id, rtp, rtcp, pin, "h264", 96, LEGACY_VIDEO_FMTP, false);
    if (result && error && error_size) lifecycle_safe_strcpy(error, "Janus unavailable or descriptor conflict", error_size);
    return result;
}
#endif
