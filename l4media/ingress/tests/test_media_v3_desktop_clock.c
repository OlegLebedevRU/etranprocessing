/* Deterministic production v3 lifecycle, independent wall/monotonic clocks. */
#include <time.h>
#include <stdint.h>
static time_t test_wall = 1800000000;
static unsigned long long test_mono = 1000000;
static long test_nsec = 0;
static unsigned realtime_samples;
static uint64_t realtime_read_delay;
static time_t test_time(time_t *output) {
    if (output)
        *output = test_wall;
    return test_wall;
}
#define L4MEDIA_TESTING
#define time test_time
#define main ingress_server_main
#include "../src/l4media_ingress.c"
#undef main
#undef time
#include <assert.h>
#include "test_media_common.h"
static uint64_t monotonic(void) { return (uint64_t)test_mono; }
static bool realtime(struct timespec *output) {
    ++realtime_samples;
    output->tv_sec = test_wall;
    output->tv_nsec = test_nsec;
    test_mono += realtime_read_delay;
    return true;
}
static void send_v3(const char *method, const char *path, json_t *value, int expected) {
    char *body = value ? json_dumps(value, JSON_COMPACT) : NULL;
    char output[16384];
    int code = 0;
    media_v2_http_contract(method, path, body, 3, output, sizeof(output), &code);
    free(body);
    if (code != expected)
        fprintf(stderr, "HTTP%d expected%d: %s\n", code, expected, output);
    assert(code == expected);
}
static json_t *start_request(void) {
    json_t *value = fixture("media_start"), *d = json_object_get(value, "descriptor");
    json_object_set_new(value, "contract_version", json_integer(3));
    json_object_set_new(value, "lease_expires_at", json_integer(test_wall + 7));
    json_object_set_new(d, "contract_version", json_integer(3));
    json_object_set_new(d, "source_type", json_string("camera"));
    json_object_set_new(d, "source_id", json_string("cam:ip000001"));
    json_t *track = json_array_get(json_object_get(d, "tracks"), 0);
    json_object_set_new(track, "width", json_integer(2592));
    json_object_set_new(track, "height", json_integer(1944));
    json_object_set_new(track, "fps", json_integer(20));
    json_object_set_new(track, "level_id", json_integer(153));
    return value;
}
static json_t *identity(MediaSession *s) {
    json_error_t error;
    json_t *d = json_loads(s->descriptor, 0, &error);
    json_t *v = json_pack("{s:i,s:s,s:s,s:s,s:s,s:s}", "contract_version", 3, "sn", s->sn, "session_id", s->session_id,
                          "lease_id", s->lease_id, "stream_instance_id", s->stream_instance_id, "descriptor_revision",
                          mv_string(d, "descriptor_revision"));
    json_decref(d);
    return v;
}
/* Captured by the actual MenuBuilder keepalive -> video_v2 HTTP helper.
 * The renewal body is submitted unchanged to the production MEDIA handler. */
static void actual_bff_vectors(void) {
    json_error_t error;
    json_t *packet = json_load_file("tests/fixtures/desktop-renew-vectors.json", JSON_REJECT_DUPLICATES, &error);
    assert(packet && !strcmp(mv_string(packet, "contract"), "l4-media/3.0.0"));
    json_t *vectors = json_object_get(packet, "vectors"), *v;
    size_t i;
    json_array_foreach(vectors, i, v) {
        memset(g_media_sessions, 0, sizeof(g_media_sessions));
        g_media_session_count = 0;
        g_media_monotonic_last = 0; /* each vector is an independent process timeline */
        test_mono = 5000;
        test_wall = (time_t)mv_integer(v, "initial_utc_seconds");
        test_nsec = 0;
        json_t *request = json_object_get(v, "request"), *renew = json_object_get(request, "body");
        json_t *headers = json_object_get(request, "headers");
        assert(json_object_size(headers) == 1);
        assert(!strcmp(mv_string(headers, "x-l4-media-contract"), "l4-media/3.0.0"));
        json_t *start = start_request();
        json_object_set(start, "session_id", json_object_get(renew, "session_id"));
        json_object_set_new(start, "lease_expires_at", json_integer(test_wall + 60));
        json_object_set_new(json_object_get(start, "descriptor"), "source_type", json_string("desktop"));
        send_v3("POST", "/api/v2/media/sessions/start", start, 201);
        MediaSession *session = find_session_by_id(mv_string(renew, "session_id"));
        assert(session && !session->camera_source);
        test_wall = (time_t)mv_integer(v, "receipt_utc_seconds");
        test_mono = (uint64_t)mv_integer(v, "receipt_monotonic_ms");
        send_v3(mv_string(request, "method"), mv_string(request, "path"), renew, 200);
        assert(session->monotonic_deadline_ms == (uint64_t)mv_integer(v, "expected_deadline_monotonic_ms"));
        assert(session->monotonic_deadline_ms - test_mono == (uint64_t)mv_integer(v, "expected_remaining_ms"));
        json_t *stop = identity(session);
        json_object_set_new(stop, "reason", json_string("user_stop"));
        send_v3("POST", "/api/v2/media/sessions/stop", stop, 200);
        json_decref(stop);
        json_decref(start);
    }
    json_decref(packet);
    test_mono = 1000000;
}
int main(void) {
    (void)request;
    lifecycle_safe_strcpy(g_janus_host, "127.0.0.1", sizeof(g_janus_host));
    assert(media_schema_load("spec/hevc.schema.json"));
    assert(media_camera_schema_load("spec/camera-v1/hevc.schema.json"));
    assert(media_v3_schema_load("spec/media-v3/media.schema.json"));
    g_media_test_monotonic_ms = monotonic;
    g_media_test_realtime = realtime;
    g_media_test_boot_id = "11111111-1111-4111-8111-111111111111";
    fake_mounts = json_object();
    g_janus_test_request = fake_janus;
    g_media_hevc_enabled = true;
    g_media_hevc_verified = true;
    send_v3("GET", "/api/v2/media/capabilities", NULL, 200);
    actual_bff_vectors();
    const char *sources[] = {"desktop", "usb-camera"};
    int jumps[] = {3600, -3600};
    unsigned failures = 0;
    for (unsigned source = 0; source < 2; ++source)
        for (unsigned j = 0; j < 2; ++j) {
            memset(g_media_sessions, 0, sizeof(g_media_sessions));
            g_media_session_count = 0;
            test_wall = 1800000000;
            test_mono += 100000;
            test_nsec = 0;
            json_t *start = start_request();
            json_t *descriptor = json_object_get(start, "descriptor");
            json_object_set_new(descriptor, "source_type", json_string(sources[source]));
            json_object_set_new(start, "lease_expires_at", json_integer(test_wall + 60));
            send_v3("POST", "/api/v2/media/sessions/start", start, 201);
            MediaSession *session = find_session_by_id(mv_string(start, "session_id"));
            assert(session && !session->camera_source);
            test_wall += jumps[j] + 1;
            test_mono += 1000;
            json_t *renew = identity(session);
            json_object_set_new(renew, "lease_expires_at", json_integer(test_wall + 60));
            send_v3("POST", "/api/v2/media/sessions/renew", renew, 200);
            uint64_t remaining = session->monotonic_deadline_ms - test_mono;
            fprintf(stderr, "%s wall_jump=%d remaining_ms=%" PRIu64 " expected_ms=60000\n", sources[source], jumps[j],
                    remaining);
            if (remaining != 60000)
                ++failures;
            uint64_t accepted = session->monotonic_deadline_ms;
            test_mono += 1000;
            test_wall += 1;
            send_v3("POST", "/api/v2/media/sessions/renew", renew, 200);
            assert(session->monotonic_deadline_ms == accepted); /* exact expiry replay */
            test_nsec = 900000000;
            json_object_set_new(renew, "lease_expires_at", json_integer(test_wall + 60));
            send_v3("POST", "/api/v2/media/sessions/renew", renew, 200);
            assert(session->monotonic_deadline_ms == test_mono + 59100);
            /* Expired and over-limit live deadlines never mutate accepted state. */
            accepted = session->monotonic_deadline_ms;
            int64_t anchor = session->issuer_expiry_utc;
            json_object_set_new(renew, "lease_expires_at", json_integer(test_wall));
            send_v3("POST", "/api/v2/media/sessions/renew", renew, 410);
            assert(session->monotonic_deadline_ms == accepted && session->issuer_expiry_utc == anchor);
            json_object_set_new(renew, "lease_expires_at", json_integer(test_wall + 7201));
            send_v3("POST", "/api/v2/media/sessions/renew", renew, 410);
            assert(session->monotonic_deadline_ms == accepted && session->issuer_expiry_utc == anchor);
            /* A live lease may expire while the precise UTC receipt is read.
             * Recheck the old timer before committing any replacement. */
            test_mono = accepted - 100;
            test_wall += 1;
            realtime_read_delay = 200;
            json_object_set_new(renew, "lease_expires_at", json_integer(test_wall + 60));
            send_v3("POST", "/api/v2/media/sessions/renew", renew, 410);
            assert(session->monotonic_deadline_ms == accepted && session->issuer_expiry_utc == anchor);
            realtime_read_delay = 0;
            test_mono = accepted + 200;
            expire_media_session(session, test_wall);
            json_object_set_new(renew, "lease_expires_at", json_integer(test_wall + 60));
            send_v3("POST", "/api/v2/media/sessions/renew", renew, 410);
            json_t *stop = identity(session);
            json_object_set_new(stop, "reason", json_string("user_stop"));
            send_v3("POST", "/api/v2/media/sessions/stop", stop, 200);
            json_decref(stop);
            json_decref(renew);
            json_decref(start);
        }
    json_decref(fake_mounts);
    json_decref(g_media_schema);
    json_decref(g_media_camera_schema);
    json_decref(g_media_v3_schema);
    assert(!failures);
    puts("PASS desktop/USB v3 live-UTC renew after positive/negative wall jumps");
    return 0;
}
