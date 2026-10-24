/* Negotiated camera reader; desktop/frozen-v2 requests never select this schema. */
#ifndef MEDIA_CAMERA_H
#define MEDIA_CAMERA_H

#define MEDIA_CAMERA_REVISION "siplite-camera/1.0.0"
static bool g_media_camera_enabled = false;
static bool g_media_camera_h265_verified = false;
static unsigned g_media_camera_h264_profiles = 0;
static time_t g_media_camera_probe_at = 0;

#define MEDIA_CAMERA11_REVISION "siplite-camera/1.1.0"
#define MEDIA_CAMERA_CADENCE "camera-cadence/0.1.0-proposed"
static bool g_media_camera11_enabled = false;

/* 0 desktop, 1 constant camera1.0, 2 complete camera1.1, -1 mixed/partial. */
static inline int media_camera_header(const char* request) {
    const char* names[] = {"X-L4-Camera-Contract", "X-L4-Camera-Cadence",
        "X-L4-Camera-Enrollment", "X-L4-Camera-Admission"};
    const char* expected[] = {MEDIA_CAMERA11_REVISION, MEDIA_CAMERA_CADENCE,
        "camera-enrollment/0.2.0-proposed", "camera-ctl-monotonic/0.2.0-proposed"};
    bool seen[4] = {false}; int revision = 0;
    const char* line = strchr(request, '\n'); if (!line) return 0; line++;
    while (*line && *line != '\r' && *line != '\n') {
        const char* end = strchr(line, '\n'); if (!end) return -1;
        size_t length = (size_t)(end - line);
        if (length && line[length - 1] == '\r') length--;
        const char* colon = memchr(line, ':', length);
        for (unsigned i = 0; colon && i < 4; i++) {
            if ((size_t)(colon - line) != strlen(names[i]) || strncasecmp(line, names[i], strlen(names[i]))) continue;
            if (seen[i]) return -1;
            seen[i] = true;
            const char* value = colon + 1;
            while (value < line + length && (*value == ' ' || *value == '\t')) value++;
            while (length && (line[length - 1] == ' ' || line[length - 1] == '\t')) length--;
            size_t count = (size_t)(line + length - value);
            if (i == 0 && count == strlen(MEDIA_CAMERA_REVISION) && !strncmp(value, MEDIA_CAMERA_REVISION, count)) revision = 1;
            else if (count == strlen(expected[i]) && !strncmp(value, expected[i], count)) { if (i == 0) revision = 2; }
            else return -1;
        }
        line = end + 1;
    }
    if (revision == 2) return seen[1] && seen[2] && seen[3] ? 2 : -1;
    if (seen[1] || seen[2] || seen[3]) return -1;
    return revision;
}

static inline int mc_descriptor_revision(json_t* descriptor) {
    const char* revision = mv_string(descriptor, "camera_contract_revision");
    if (!revision[0]) return 0;
    if (!strcmp(revision, MEDIA_CAMERA_REVISION)) return 1;
    if (!strcmp(revision, MEDIA_CAMERA11_REVISION)) return 2;
    return -1;
}

static inline json_t* mc_schema(int revision) {
    return revision == 2 ? g_media_camera11_schema : revision == 1 ? g_media_camera_schema : revision == 0 ? g_media_schema : NULL;
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
