#define L4MEDIA_TESTING
#define main ingress_server_main
#include "../src/l4media_ingress.c"
#undef main
#include <assert.h>
#include <glob.h>
#include "test_media_common.h"

static json_t* camera_fixture(const char* name) {
    char path[256]; snprintf(path, sizeof(path), "spec/camera-v1/fixtures/%s.json", name);
    json_error_t error; json_t* wrapper = json_load_file(path, JSON_REJECT_DUPLICATES, &error);
    assert(wrapper); json_t* value = json_deep_copy(json_object_get(wrapper, "payload"));
    json_decref(wrapper); return value;
}

static void camera_request(const char* method, const char* path, json_t* value,
                           bool negotiated, int expected, char* output) {
    char* body = value ? json_dumps(value, JSON_COMPACT) : NULL;
    int status = 0;
    media_v2_http_contract(method, path, body, negotiated, output, 16384, &status);
    free(body);
    if (status != expected) fprintf(stderr, "Unexpected camera HTTP%d expected%d: %s\n", status, expected, output);
    assert(status == expected);
    json_error_t error; json_t* response = json_loads(output, JSON_REJECT_DUPLICATES, &error);
    const char* definition = status >= 400 ? "rejection" : strstr(path, "capabilities") ?
        "media_capabilities" : !strcmp(method, "GET") ? "media_health" : "media_response";
    assert(ms_validate_root(g_media_camera_schema,
        json_object_get(json_object_get(g_media_camera_schema, "$defs"), definition), response, 0));
    json_decref(response);
}

static json_t* identity(MediaSession* session) {
    json_error_t error; json_t* descriptor = json_loads(session->descriptor, 0, &error);
    json_t* value = json_pack("{s:i,s:s,s:s,s:s,s:s,s:s}", "contract_version", 2, "sn", session->sn,
        "session_id", session->session_id, "lease_id", session->lease_id,
        "stream_instance_id", session->stream_instance_id, "descriptor_revision", mv_string(descriptor, "descriptor_revision"));
    json_decref(descriptor); return value;
}

int main(void) {
    assert(media_schema_load("spec/hevc.schema.json"));
    assert(media_camera_schema_load("spec/camera-v1/hevc.schema.json"));
    glob_t paths; assert(!glob("spec/camera-v1/fixtures/*.json", 0, NULL, &paths));
    for (size_t i = 0; i < paths.gl_pathc; i++) {
        json_error_t error; json_t* item = json_load_file(paths.gl_pathv[i], JSON_REJECT_DUPLICATES, &error);
        assert(item);
        bool valid = ms_validate_root(g_media_camera_schema,
            json_object_get(json_object_get(g_media_camera_schema, "$defs"), mv_string(item, "definition")),
            json_object_get(item, "payload"), 0);
        if (valid != json_is_true(json_object_get(item, "valid"))) fprintf(stderr, "Camera C schema vector failed: %s\n", paths.gl_pathv[i]);
        assert(valid == json_is_true(json_object_get(item, "valid"))); json_decref(item);
    }
    printf("PASS: %zu negotiated camera media vectors against production C validator\n", paths.gl_pathc);
    globfree(&paths);
    assert(media_camera_header("GET / HTTP/1.1\r\nHost: local\r\n\r\nX-L4-Camera-Contract: " MEDIA_CAMERA_REVISION) == 0);
    assert(media_camera_header("GET / HTTP/1.1\r\nx-l4-camera-contract:\t" MEDIA_CAMERA_REVISION " \r\n\r\n") == 1);
    assert(media_camera_header("GET / HTTP/1.1\r\nX-L4-Camera-Contract: unknown\r\n\r\n") == -1);
    assert(media_camera_header("GET / HTTP/1.1\r\nX-L4-Camera-Contract: " MEDIA_CAMERA_REVISION
        "\r\nX-L4-Camera-Contract: " MEDIA_CAMERA_REVISION "\r\n\r\n") == -1);
    fake_mounts = json_object(); g_janus_test_request = fake_janus; fake_chunked = true;
    const char* redis_url = getenv("L4MEDIA_TEST_REDIS_URL");
    if (redis_url) assert(!media_redis_init(redis_url) && media_redis_ok());
    char output[16384];
    json_t* legacy = fixture("media_start");
    json_object_set_new(legacy, "lease_expires_at", json_integer(time(NULL) + 60));
    request("/api/v2/media/sessions/start", legacy, 422, output);
    json_decref(legacy);
    json_t* start = camera_fixture("current_hevc_level41");
    json_object_set_new(start, "lease_expires_at", json_integer(time(NULL) + 60));
    camera_request("GET", "/api/v2/media/capabilities", NULL, true, 422, output);
    camera_request("POST", "/api/v2/media/sessions/start", start, false, 422, output);
    camera_request("POST", "/api/v2/media/sessions/start", start, true, 422, output);
    assert(!creates);
    g_media_hevc_enabled = true; mv_probe(); g_media_camera_enabled = true;
    camera_request("GET", "/api/v2/media/capabilities", NULL, true, 200, output);
    assert(g_media_camera_h265_verified && g_media_camera_h264_profiles == 7);
    camera_request("GET", "/api/v2/media/capabilities", NULL, false, 200, output);
    assert(!strstr(output, "camera_formats"));
    camera_request("POST", "/api/v2/media/sessions/start", start, true, 201, output);
    MediaSession* session = find_session_by_id(mv_string(start, "session_id"));
    assert(session && mc_session_extended(session));
    assert(strstr(session->descriptor, "\"level_id\":123"));
    int created = creates;
    g_media_camera_probe_at = time(NULL) - 31;
    mc_probe(); assert(creates == created); /* No extra synchronous probes over live RTP. */
    camera_request("POST", "/api/v2/media/sessions/start", start, true, 200, output);
    assert(creates == created);
    json_t* renew = identity(session);
    json_object_set_new(renew, "lease_expires_at", json_integer(time(NULL) + 120));
    camera_request("POST", "/api/v2/media/sessions/renew", renew, false, 422, output);
    camera_request("POST", "/api/v2/media/sessions/renew", renew, true, 200, output);
    char health[1024];
    snprintf(health, sizeof(health), "/api/v2/media/sessions/%s?sn=%s&lease_id=%s&stream_instance_id=%s&descriptor_revision=%s",
        session->session_id, session->sn, session->lease_id, session->stream_instance_id, mv_string(renew, "descriptor_revision"));
    camera_request("GET", health, NULL, false, 422, output);
    g_media_camera_enabled = false; /* Disabling writers must not strand cleanup/readers. */
    camera_request("GET", health, NULL, true, 200, output);
    camera_request("POST", "/api/v2/media/sessions/renew", renew, true, 200, output);
    json_t* stop = identity(session); json_object_set_new(stop, "reason", json_string("user_stop"));
    camera_request("POST", "/api/v2/media/sessions/stop", stop, false, 422, output);
    camera_request("POST", "/api/v2/media/sessions/stop", stop, true, 200, output);
    assert(!find_route(session->sn));
    g_media_camera_enabled = true;
    json_t* next = camera_fixture("native_hevc_level51");
    json_object_set_new(next, "lease_expires_at", json_integer(time(NULL) + 60));
    json_object_set_new(next, "session_id", json_string("00000000-0000-4000-8000-000000000021"));
    json_object_set_new(next, "operation_id", json_string("00000000-0000-4000-8000-000000000022"));
    json_object_set_new(json_object_get(next, "publisher"), "stream_instance_id", json_string("00000000-0000-4000-8000-000000000023"));
    json_object_set_new(json_object_get(next, "descriptor"), "descriptor_revision", json_string("00000000-0000-4000-8000-000000000024"));
    camera_request("POST", "/api/v2/media/sessions/start", next, true, 201, output);
    MediaSession* second = find_session_by_id(mv_string(next, "session_id"));
    assert(second && strstr(second->descriptor, "\"level_id\":153"));
    camera_request("POST", "/api/v2/media/sessions/stop", stop, true, 200, output);
    camera_request("POST", "/api/v2/media/sessions/renew", renew, true, 410, output);
    assert(find_route(second->sn) && second->state == MEDIA_STATE_STARTING);
    json_t* stop_second = identity(second); json_object_set_new(stop_second, "reason", json_string("user_stop"));
    camera_request("POST", "/api/v2/media/sessions/stop", stop_second, true, 200, output);
    /* H.264 High stays camera-only; it cannot loosen the desktop schema. */
    json_t* h264 = json_deep_copy(next);
    json_object_set_new(h264, "session_id", json_string("00000000-0000-4000-8000-000000000031"));
    json_object_set_new(h264, "operation_id", json_string("00000000-0000-4000-8000-000000000032"));
    json_object_set_new(json_object_get(h264, "publisher"), "stream_instance_id", json_string("00000000-0000-4000-8000-000000000033"));
    json_t* descriptor = json_object_get(h264, "descriptor");
    json_object_set_new(descriptor, "descriptor_revision", json_string("00000000-0000-4000-8000-000000000034"));
    json_t* track = json_array_get(json_object_get(descriptor, "tracks"), 0);
    const char* hevc_fields[] = {"profile_space", "profile_id", "tier_flag", "level_id", "sprop_max_don_diff"};
    for (size_t i = 0; i < sizeof(hevc_fields) / sizeof(hevc_fields[0]); i++) json_object_del(track, hevc_fields[i]);
    json_object_set_new(track, "codec", json_string("h264"));
    json_object_set_new(track, "codec_profile", json_string("high"));
    json_object_set_new(track, "packetization", json_string("rfc6184"));
    json_object_set_new(track, "profile_level_id", json_string("640033"));
    json_object_set_new(track, "packetization_mode", json_integer(1));
    camera_request("POST", "/api/v2/media/sessions/start", h264, true, 201, output);
    MediaSession* third = find_session_by_id(mv_string(h264, "session_id"));
    assert(third && strstr(third->descriptor, "\"codec_profile\":\"high\""));
    if (redis_url) {
        char expected[MEDIA_DESCRIPTOR_LIMIT]; strcpy(expected, third->descriptor);
        memset(g_media_sessions, 0, sizeof(g_media_sessions)); g_media_session_count = 0;
        memset(&g_routes, 0, sizeof(g_routes));
        json_t* saved_schema = g_media_camera_schema; g_media_camera_schema = NULL;
        media_v2_restore(); assert(!g_media_session_count); /* Never interpret new data as old H264. */
        g_media_camera_schema = saved_schema;
        media_v2_restore();
        third = find_session_by_id(mv_string(h264, "session_id"));
        assert(third && !strcmp(third->descriptor, expected) && mc_session_extended(third));
        assert(find_route(third->sn));
        puts("PASS: real Redis restore preserves camera revision/descriptor; missing reader fails closed");
    } else puts("NOT RUN: real Redis restore (set isolated L4MEDIA_TEST_REDIS_URL)");
    json_t* stop_third = identity(third); json_object_set_new(stop_third, "reason", json_string("user_stop"));
    camera_request("POST", "/api/v2/media/sessions/stop", stop_third, true, 200, output);
    json_decref(stop_third); json_decref(h264); media_redis_close();
    json_decref(start); json_decref(next); json_decref(stop); json_decref(stop_second); json_decref(renew);
    json_decref(fake_mounts); json_decref(g_media_schema); json_decref(g_media_camera_schema);
    puts("PASS: negotiation, profile probes, exact camera descriptor, feature-off cleanup and stale epoch isolation");
    return 0;
}
