/* Negotiated camera reader; desktop/frozen-v2 requests never select this schema. */
#ifndef MEDIA_CAMERA_H
#define MEDIA_CAMERA_H

#define MEDIA_CAMERA_REVISION "siplite-camera/1.0.0"
static bool g_media_camera_enabled = false;
static bool g_media_camera_h265_verified = false;
static unsigned g_media_camera_h264_profiles = 0;
static time_t g_media_camera_probe_at = 0;

/* 0 absent, 1 negotiated, -1 unknown or duplicate. Inspect headers, never body. */
static inline int media_camera_header(const char* request) {
    const char* line = strchr(request, '\n');
    if (!line) return 0;
    line++;
    bool found = false;
    const char* name = "X-L4-Camera-Contract";
    while (*line && *line != '\r' && *line != '\n') {
        const char* end = strchr(line, '\n');
        if (!end) return -1;
        size_t length = (size_t)(end - line);
        if (length && line[length - 1] == '\r') length--;
        const char* colon = memchr(line, ':', length);
        if (colon && (size_t)(colon - line) == strlen(name) && !strncasecmp(line, name, strlen(name))) {
            if (found) return -1;
            found = true;
            const char* value = colon + 1;
            while (value < line + length && (*value == ' ' || *value == '\t')) value++;
            while (length && (line[length - 1] == ' ' || line[length - 1] == '\t')) length--;
            if ((size_t)(line + length - value) != strlen(MEDIA_CAMERA_REVISION) ||
                strncmp(value, MEDIA_CAMERA_REVISION, strlen(MEDIA_CAMERA_REVISION))) return -1;
        }
        line = end + 1;
    }
    return found ? 1 : 0;
}

static inline bool mc_extended(json_t* request) {
    return json_object_get(json_object_get(request, "descriptor"), "camera_contract_revision") != NULL;
}

static inline bool mc_session_extended(const MediaSession* session) {
    if (!session || session->contract_version != 2) return false;
    json_error_t error;
    json_t* descriptor = json_loads(session->descriptor, JSON_REJECT_DUPLICATES, &error);
    bool extended = json_object_get(descriptor, "camera_contract_revision") != NULL;
    json_decref(descriptor); return extended;
}

static inline bool mc_probe_format(const char* codec, const char* fmtp) {
    int rtp, rtcp; char pin[64]; unsigned char random[4];
    if (!allocate_port_pair(&rtp, &rtcp) || !mv_random_pin(pin, sizeof(pin)) ||
        getrandom(random, sizeof(random), 0) != (ssize_t)sizeof(random)) return false;
    uint32_t id = 1000000000u + (((uint32_t)random[0] << 16 | (uint32_t)random[1] << 8 | random[2]) % 1000000000u);
    JanusMountpointInfo info;
    if (janus_get_mountpoint_info(id, &info) || info.found) return false;
    if (janus_create_format(id, rtp, rtcp, pin, codec, 96, fmtp)) return false;
    bool ready = !janus_ensure_format(id, rtp, rtcp, pin, codec, 96, fmtp, true);
    bool cleaned = !janus_destroy_mountpoint(id, NULL, 0);
    return ready && cleaned;
}

/* This checks the running Janus create/info/delete path and exact fmtp. Browser
 * capabilities/decoded frames are separate evidence, as for the existing v2 probe. */
static inline void mc_probe(void) {
    if (!g_media_camera_enabled || !g_media_camera_schema || !g_media_dependencies_ready) {
        g_media_camera_h265_verified = false; g_media_camera_h264_profiles = 0;
        g_media_camera_probe_at = 0; return;
    }
    /* Janus create/info/delete is synchronous. Never add four control-plane
     * roundtrips to the packet loop while any publisher is live. Dependencies
     * are still checked by mv_probe; cached format facts belong to this fixed
     * Janus deployment and are refreshed when the media table becomes idle. */
    if (media_active_session_count()) return;
    time_t now = time(NULL);
    if (g_media_camera_probe_at && now - g_media_camera_probe_at < 30) return;
    g_media_camera_probe_at = now;
    g_media_camera_h264_profiles = 0; g_media_camera_h265_verified = false;
    const char* fmtp[] = {"profile-level-id=42e033;packetization-mode=1",
        "profile-level-id=4d0033;packetization-mode=1", "profile-level-id=640033;packetization-mode=1"};
    for (unsigned i = 0; i < 3; i++)
        if (mc_probe_format("h264", fmtp[i])) g_media_camera_h264_profiles |= 1u << i;
    if (g_media_hevc_enabled && g_media_hevc_verified)
        g_media_camera_h265_verified = mc_probe_format("h265",
            "profile-space=0;profile-id=1;tier-flag=0;level-id=153;sprop-max-don-diff=0");
}

static inline bool mc_track_ready(json_t* track) {
    if (!g_media_camera_enabled || !g_media_camera_schema || !g_media_dependencies_ready) return false;
    if (!strcmp(mv_string(track, "codec"), "h265"))
        return g_media_hevc_enabled && g_media_hevc_verified && g_media_camera_h265_verified;
    const char* profiles[] = {"baseline", "main", "high"};
    for (unsigned i = 0; i < 3; i++)
        if (!strcmp(mv_string(track, "codec_profile"), profiles[i])) return (g_media_camera_h264_profiles & (1u << i)) != 0;
    return false;
}

static inline void mc_capabilities(json_t* value) {
    json_object_set_new(value, "camera_contract_revisions", json_pack("[s]", MEDIA_CAMERA_REVISION));
    json_t* formats = json_array();
    json_t* h264 = json_pack("{s:s,s:[],s:i,s:i,s:i,s:i,s:i,s:s,s:i,s:s,s:[],s:b}",
        "codec", "h264", "codec_profiles", "max_width", 2592, "max_height", 1944,
        "max_fps", 30, "max_fps_above_full_hd", 20, "bit_depth", 8, "chroma", "420",
        "max_b_frames", 0, "packetization", "rfc6184", "level_ids", "runtime_verified", g_media_camera_h264_profiles != 0);
    const char* profiles[] = {"baseline", "main", "high"};
    for (unsigned i = 0; i < 3; i++)
        if (g_media_camera_h264_profiles & (1u << i))
            json_array_append_new(json_object_get(h264, "codec_profiles"), json_string(profiles[i]));
    if (!g_media_camera_h264_profiles)
        json_array_append_new(json_object_get(h264, "codec_profiles"), json_string("baseline"));
    json_array_append_new(formats, h264);
    if (g_media_hevc_enabled) {
        json_t* h265 = json_pack("{s:s,s:[s],s:i,s:i,s:i,s:i,s:i,s:s,s:i,s:s,s:[],s:b}",
            "codec", "h265", "codec_profiles", "main", "max_width", 2592, "max_height", 1944,
            "max_fps", 30, "max_fps_above_full_hd", 20, "bit_depth", 8, "chroma", "420",
            "max_b_frames", 0, "packetization", "rfc7798", "level_ids", "runtime_verified", g_media_camera_h265_verified);
        const int levels[] = {30, 60, 63, 90, 93, 120, 123, 150, 153};
        for (size_t i = 0; i < sizeof(levels) / sizeof(levels[0]); i++)
            json_array_append_new(json_object_get(h265, "level_ids"), json_integer(levels[i]));
        json_array_append_new(formats, h265);
    }
    json_object_set_new(value, "camera_formats", formats);
}

#endif
