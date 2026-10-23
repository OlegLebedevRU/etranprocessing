#define L4MEDIA_TESTING
#define main ingress_server_main
#include "../src/l4media_ingress.c"
#undef main
#include <assert.h>
#include <glob.h>

#include "test_media_common.h"

int main(void) {
    assert(media_schema_load("spec/hevc.schema.json"));
    glob_t paths; assert(!glob("spec/fixtures/*.json", 0, NULL, &paths));
    for (size_t i = 0; i < paths.gl_pathc; i++) {
        json_error_t error; json_t* item = json_load_file(paths.gl_pathv[i], JSON_REJECT_DUPLICATES, &error);
        assert(item);
        bool valid = ms_validate(json_object_get(json_object_get(g_media_schema, "$defs"), mv_string(item, "definition")), json_object_get(item, "payload"), 0);
        if (valid != json_is_true(json_object_get(item, "valid"))) fprintf(stderr, "Schema vector failed: %s\n", paths.gl_pathv[i]);
        assert(valid == json_is_true(json_object_get(item, "valid")));
        json_decref(item);
    }
    printf("PASS: %zu frozen schema vectors against C validator\n", paths.gl_pathc); globfree(&paths);
    assert(!media_v2_parse("{\"contract_version\":2,\"contract_version\":2}", "media_start"));
    fake_mounts = json_object(); g_janus_test_request = fake_janus;
    fake_chunked = true; /* Real pinned Janus uses chunked HTTP responses. */
    char output[16384]; int status;
    json_t* start = fixture("media_start");
    json_object_set_new(start, "lease_expires_at", json_integer(time(NULL) + 60));
    request("/api/v2/media/sessions/start", start, 422, output); /* feature/runtime gates */
    assert(!creates);
    g_media_hevc_enabled = true; mv_probe(); assert(g_media_hevc_verified);
    assert(creates == 1 && destroys == 1); /* disposable runtime probe */
    request("/api/v2/media/sessions/start", start, 201, output);
    MediaSession* session = find_session_by_id(mv_string(start, "session_id"));
    assert(session && session->state == MEDIA_STATE_STARTING && session->pin[0]);
    assert(strstr(output, "\"codec\":\"h265\""));
    assert(find_route(session->sn));
    char health_path[1024];
    snprintf(health_path, sizeof(health_path), "/api/v2/media/sessions/%s?sn=hevc%%2Dfixture&lease_id=%s&stream_instance_id=%s&descriptor_revision=%s",
        session->session_id, session->lease_id, session->stream_instance_id,
        mv_string(json_object_get(start, "descriptor"), "descriptor_revision"));
    media_v2_http("GET", health_path, NULL, output, sizeof(output), &status);
    assert(status == 200 && strstr(output, "\"transport\""));
    char encoded[] = "a%3Ab"; assert(mv_query_decode(encoded) && !strcmp(encoded, "a:b"));
    char invalid_escape[] = "%00"; assert(!mv_query_decode(invalid_escape));
    int before = creates;
    request("/api/v2/media/sessions/start", start, 200, output); assert(before == creates);
    fake_failure = 1;
    request("/api/v2/media/sessions/start", start, 502, output);
    assert(find_route(session->sn) && session->state == MEDIA_STATE_STARTING);
    fake_failure = 0;
    IngressClient feedback_client; memset(&feedback_client, 0, sizeof(feedback_client));
    feedback_client.keyframe_feedback_interval_ms = 500;
    uint8_t pli[12] = {0x81, 206, 0, 2};
    assert(coalesce_feedback(&feedback_client, pli, sizeof(pli), 1000) == sizeof(pli));
    assert(coalesce_feedback(&feedback_client, pli, sizeof(pli), 1200) == 0);
    assert(coalesce_feedback(&feedback_client, pli, sizeof(pli), 1500) == sizeof(pli));
    assert(feedback_client.feedback_dropped == 1);
    json_t* altered = json_deep_copy(start);
    json_t* track = json_array_get(json_object_get(json_object_get(altered, "descriptor"), "tracks"), 0);
    json_object_set_new(track, "fps", json_integer(10));
    request("/api/v2/media/sessions/start", altered, 409, output); json_decref(altered);
    assert(creates == before && destroys == 1);
    /* PT and fmtp mismatch on a living Janus mountpoint must NOT recreate. */
    char key[32]; snprintf(key, sizeof(key), "%u", session->mountpoint_id);
    json_t* mount = json_object_get(fake_mounts, key);
    json_object_set_new(mount, "videopt", json_integer(97));
    request("/api/v2/media/sessions/start", start, 409, output);
    json_object_set_new(mount, "videopt", json_integer(96));
    json_object_set_new(mount, "videofmtp", json_string("wrong"));
    request("/api/v2/media/sessions/start", start, 409, output);
    json_object_set_new(mount, "videofmtp", json_string("profile-space=0;profile-id=1;tier-flag=0;level-id=93;sprop-max-don-diff=0"));
    assert(destroys == 1);
    assert(media_v1_conflicts("/api/v1/media/sessions/stop", "{\"sn\":\"hevc-fixture\"}"));
    json_t* renew = fixture("media_renew");
    json_object_set_new(renew, "lease_expires_at", json_integer(time(NULL) + 120));
    request("/api/v2/media/sessions/renew", renew, 200, output);
    json_t* stop = fixture("media_stop");
    json_object_set_new(stop, "stream_instance_id", json_string("00000000-0000-4000-8000-000000000099"));
    request("/api/v2/media/sessions/stop", stop, 409, output); assert(find_route(session->sn));
    json_object_set_new(stop, "stream_instance_id", json_string(session->stream_instance_id));
    request("/api/v2/media/sessions/stop", stop, 200, output);
    request("/api/v2/media/sessions/stop", stop, 200, output); assert(!find_route(session->sn));
    request("/api/v2/media/sessions/renew", renew, 410, output);
    request("/api/v2/media/sessions/start", start, 200, output); assert(strstr(output, "\"state\":\"stopped\""));
    /* Reproduce the legacy ensure failure path against real shared handlers. */
    memset(g_media_sessions, 0, sizeof(g_media_sessions)); g_media_session_count = 1;
    session = &g_media_sessions[0]; strcpy(session->session_id, "legacy"); strcpy(session->sn, "legacy-device");
    session->state = MEDIA_STATE_ACTIVE; session->ttl_sec = 60; session->started_at = session->renewed_at = time(NULL);
    fake_failure = 1; const char* reason;
    handle_media_session_start("{\"session_id\":\"legacy\",\"sn\":\"legacy-device\"}", output, sizeof(output), &status, &reason);
    assert(status == 502 && !strstr(output, "\"state\":\"active\""));
    fake_failure = 0;
    json_decref(start); json_decref(stop); json_decref(renew); json_decref(fake_mounts);
    json_decref(g_media_schema); g_media_schema = NULL;
    puts("PASS: v2 gate, prepare/replay, ensure failure, PT/fmtp conflict, epochs, stop/renew and v1 failure regression");
    return 0;
}
