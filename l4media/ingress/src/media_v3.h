/* Unified media 3.0.0 negotiation and runtime format evidence. No packet changes. */
#ifndef MEDIA_V3_H
#define MEDIA_V3_H
#define MEDIA_V3_REVISION "l4-media/3.0.0"
static unsigned g_media_v3_h264_profiles;
static bool g_media_v3_h265_verified;
static time_t g_media_v3_probe_at;

/* Existing camera negotiation remains independent. Unknown/duplicate/mixed
 * selectors fail before body parsing. Headers inside the body are never read. */
static inline int media_contract_header(const char* request) {
    int camera = media_camera_header(request);
    const char* line = strchr(request, '\n');
    if (!line) return camera;
    line++;
    bool found = false, camera_header = false;
    while (*line && *line != '\r' && *line != '\n') {
        const char* end = strchr(line, '\n');
        if (!end) return -1;
        size_t length = (size_t)(end - line);
        if (length && line[length - 1] == '\r') length--;
        const char* colon = memchr(line, ':', length);
        if (colon) {
            size_t name_length = (size_t)(colon - line);
            if (name_length >= 12 && !strncasecmp(line, "X-L4-Camera-", 12)) camera_header = true;
            const char* name = "X-L4-Media-Contract";
            if (name_length == strlen(name) && !strncasecmp(line, name, name_length)) {
                if (found) return -1;
                found = true;
                const char* value = colon + 1;
                while (value < line + length && (*value == ' ' || *value == '\t')) value++;
                while (length && (line[length - 1] == ' ' || line[length - 1] == '\t')) length--;
                if ((size_t)(line + length - value) != strlen(MEDIA_V3_REVISION) ||
                    strncmp(value, MEDIA_V3_REVISION, strlen(MEDIA_V3_REVISION))) return -1;
            }
        }
        line = end + 1;
    }
    return found ? (camera_header || camera != 0 ? -1 : 3) : camera;
}

static inline void m3_error_envelope(char* response, size_t size) {
    json_error_t error;
    json_t* value = json_loads(response, JSON_REJECT_DUPLICATES, &error);
    if (!value) value = json_pack("{s:s,s:s}", "code", "capacity_exhausted", "message", "Response too large");
    json_object_set_new(value, "contract_version", json_integer(3));
    mv_output(value, response, size);
}

static inline void m3_probe(void) {
    if (!g_media_v3_schema || !g_media_dependencies_ready) {
        g_media_v3_h264_profiles = 0; g_media_v3_h265_verified = false;
        g_media_v3_probe_at = 0; return;
    }
    /* Cache facts for this Janus deployment; do not block active publishers
     * with disposable synchronous probes. Actual START still verifies fmtp. */
    if (media_active_session_count()) return;
    time_t now = time(NULL);
    if (g_media_v3_probe_at && now - g_media_v3_probe_at < 30) return;
    g_media_v3_probe_at = now;
    g_media_v3_h264_profiles = 0; g_media_v3_h265_verified = false;
    const char* fmtp[] = {"profile-level-id=42e033;packetization-mode=1",
        "profile-level-id=4d0033;packetization-mode=1", "profile-level-id=640033;packetization-mode=1"};
    for (unsigned i = 0; i < 3; i++)
        if (mc_probe_format("h264", fmtp[i])) g_media_v3_h264_profiles |= 1u << i;
    if (g_media_hevc_enabled && g_media_hevc_verified)
        g_media_v3_h265_verified = mc_probe_format("h265",
            "profile-space=0;profile-id=1;tier-flag=0;level-id=153;sprop-max-don-diff=0");
}

static inline bool m3_track_ready(json_t* track) {
    if (!g_media_v3_schema || !g_media_dependencies_ready) return false;
    if (!strcmp(mv_string(track, "codec"), "h265"))
        return g_media_hevc_enabled && g_media_hevc_verified && g_media_v3_h265_verified;
    const char* profiles[] = {"baseline", "main", "high"};
    for (unsigned i = 0; i < 3; i++)
        if (!strcmp(mv_string(track, "codec_profile"), profiles[i]))
            return (g_media_v3_h264_profiles & (1u << i)) != 0;
    return false;
}

static inline void m3_capabilities(json_t* value) {
    json_object_set_new(value, "contract_version", json_integer(3));
    json_object_set_new(value, "media_contract_versions", json_pack("[i]", 3));
    json_t* codecs = json_array(); json_t* formats = json_array();
    const char* profiles[] = {"baseline", "main", "high"};
    for (unsigned i = 0; i < 3; i++) {
        if (!(g_media_v3_h264_profiles & (1u << i))) continue;
        if (!json_array_size(codecs)) json_array_append_new(codecs, json_string("h264"));
        json_array_append_new(formats, json_pack("{s:s,s:s,s:s,s:i}",
            "codec", "h264", "codec_profile", profiles[i], "packetization", "rfc6184", "clock_rate", 90000));
    }
    bool hevc = g_media_hevc_enabled && g_media_hevc_verified && g_media_v3_h265_verified && g_media_dependencies_ready;
    if (hevc) {
        json_array_append_new(codecs, json_string("h265"));
        json_array_append_new(formats, json_pack("{s:s,s:s,s:s,s:i}",
            "codec", "h265", "codec_profile", "main", "packetization", "rfc7798", "clock_rate", 90000));
    }
    json_object_set_new(value, "hevc_runtime_verified", json_boolean(hevc));
    json_object_set_new(value, "codecs", codecs); json_object_set_new(value, "formats", formats);
}
#endif
