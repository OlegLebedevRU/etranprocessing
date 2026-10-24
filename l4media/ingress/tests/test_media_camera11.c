#define L4MEDIA_TESTING
#define main ingress_server_main
#include "../src/l4media_ingress.c"
#undef main
#include <assert.h>
#include "test_media_common.h"

static void call(const char* path, json_t* body, int selected, int expected) {
    char output[16384]; int status = 0;
    char* text = body ? json_dumps(body, JSON_COMPACT) : NULL;
    media_v2_http_contract(body ? "POST" : "GET", path, text, selected, output, sizeof(output), &status);
    free(text);
    if (status != expected) fprintf(stderr, "camera11 expected%d got%d: %s\n", expected, status, output);
    assert(status == expected);
    if (status < 400) {
        json_error_t error; json_t* value = json_loads(output, 0, &error);
        const char* def = body ? "media_response" : "media_capabilities";
        assert(ms_validate_root(g_media_camera11_schema,
            json_object_get(json_object_get(g_media_camera11_schema, "$defs"), def), value, 0));
        json_decref(value);
    }
}

int main(void) {
    assert(media_schema_load("spec/hevc.schema.json"));
    assert(media_camera_schema_load("spec/camera-v1/hevc.schema.json"));
    assert(media_camera11_schema_load("spec/camera-v1.1/hevc.schema.json"));
    assert(media_camera_header("GET / HTTP/1.1\r\nX-L4-Camera-Contract: " MEDIA_CAMERA11_REVISION "\r\n\r\n") == -1);
    const char* complete = "GET / HTTP/1.1\r\nX-L4-Camera-Contract: " MEDIA_CAMERA11_REVISION
        "\r\nX-L4-Camera-Cadence: " MEDIA_CAMERA_CADENCE
        "\r\nX-L4-Camera-Enrollment: camera-enrollment/0.2.0-proposed"
        "\r\nX-L4-Camera-Admission: camera-ctl-monotonic/0.2.0-proposed\r\n\r\n";
    assert(media_camera_header(complete) == 2);
    assert(media_camera_header("GET / HTTP/1.1\r\nX-L4-Camera-Contract: " MEDIA_CAMERA_REVISION
        "\r\nX-L4-Camera-Cadence: " MEDIA_CAMERA_CADENCE "\r\n\r\n") == -1);
    json_error_t error;
    json_t* vectors = json_load_file("spec/camera-v1.1/canonical-vectors.json", JSON_REJECT_DUPLICATES, &error);
    assert(vectors);
    const char* names[] = {"descriptor", "binding"};
    for (unsigned i = 0; i < 2; i++) {
        char key[64]; snprintf(key, sizeof(key), "%s_canonical", names[i]);
        char* encoded = json_dumps(json_object_get(vectors, names[i]), JSON_COMPACT | JSON_SORT_KEYS);
        assert(encoded && !strcmp(encoded, mv_string(vectors, key))); free(encoded);
    }
    assert(ms_validate_root(g_media_camera11_schema,
        json_object_get(json_object_get(g_media_camera11_schema, "$defs"), "camera_observation"), json_object_get(vectors, "observation"), 0));
    json_t* start = fixture("media_start");
    json_object_set(start, "descriptor", json_object_get(vectors, "descriptor"));
    json_object_set_new(start, "lease_expires_at", json_integer(time(NULL) + 60));
    fake_mounts = json_object(); g_janus_test_request = fake_janus; fake_chunked = true;
    char legacy_output[16384];
    request("/api/v2/media/sessions/start", start, 422, legacy_output);
    g_media_hevc_enabled = true; g_media_camera_enabled = true; mv_probe(); mc_probe();
    call("/api/v2/media/capabilities", NULL, 2, 422);
    call("/api/v2/media/sessions/start", start, 2, 422);
    g_media_camera11_enabled = true;
    call("/api/v2/media/capabilities", NULL, 2, 200);
    call("/api/v2/media/sessions/start", start, 1, 400);
    call("/api/v2/media/sessions/start", start, 2, 201);
    int allocated = creates;
    call("/api/v2/media/sessions/start", start, 2, 200); assert(creates == allocated);
    json_t* descriptor = json_object_get(start, "descriptor");
    json_object_set_new(descriptor, "geometry_generation", json_integer(18));
    call("/api/v2/media/sessions/start", start, 2, 409); assert(creates == allocated);
    json_object_set_new(descriptor, "cadence_revision", json_string("wrong"));
    call("/api/v2/media/sessions/start", start, 2, 400);
    MediaSession* session = find_session_by_id(mv_string(start, "session_id")); assert(session);
    json_t* identity = json_pack("{s:i,s:s,s:s,s:s,s:s,s:s,s:I}", "contract_version", 2,
        "session_id", session->session_id, "sn", session->sn, "lease_id", session->lease_id,
        "stream_instance_id", session->stream_instance_id, "descriptor_revision", mv_string(descriptor, "descriptor_revision"),
        "lease_expires_at", (json_int_t)time(NULL) + 90);
    call("/api/v2/media/sessions/renew", identity, 1, 422);
    call("/api/v2/media/sessions/renew", identity, 2, 200);
    json_object_del(identity, "lease_expires_at"); json_object_set_new(identity, "reason", json_string("user_stop"));
    g_media_camera11_enabled = false;
    call("/api/v2/media/sessions/stop", identity, 1, 422);
    call("/api/v2/media/sessions/stop", identity, 2, 200);
    json_decref(identity); json_decref(start); json_decref(vectors); json_decref(fake_mounts);
    puts("PASS: camera1.1 bundle, C canonical bytes, geometry/policy immutability, gated START, exact-version renew/STOP");
    return 0;
}
