/* Deterministic production v3 lifecycle, independent wall/monotonic clocks. */
#include <time.h>
#include <stdint.h>
static time_t test_wall = 1800000000;
static unsigned long long test_mono = 1000000;
static long test_nsec = 900000000;
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
    json_t *start = start_request();
    send_v3("POST", "/api/v2/media/sessions/start", start, 201);
    MediaSession *s = find_session_by_id(mv_string(start, "session_id"));
    /* Receipt T+0.900s, expiry T+7s: only6.100s remain, never7s. */
    assert(s && s->monotonic_deadline_ms == 1006100 && realtime_samples == 1);
    uint64_t original = s->monotonic_deadline_ms;
    IngressClient telemetry = {0};
    strcpy(telemetry.sn, s->sn);
    telemetry.last_activity = test_wall;
    telemetry.last_activity_monotonic_ms = test_mono;
    telemetry.last_rtp_time = test_wall;
    telemetry.last_rtp_monotonic_ms = test_mono;
    telemetry.rtp_packets = 1;
    telemetry.camera_route_generation=s->route_generation;telemetry.epoch=s->minimum_camera_connection_epoch;
    g_clients[0] = &telemetry;
    g_client_count = 1;
    test_wall += 3600;
    test_mono += 1000;
    expire_media_session(s, test_wall);
    assert(s->state == MEDIA_STATE_STARTING);
    send_v3("POST", "/api/v2/media/sessions/start", start, 200);
    assert(s->monotonic_deadline_ms == original);
    assert(!ingress_client_idle(&telemetry, test_wall));
    char health[16384];
    mv_response(s, true, health, sizeof(health));
    json_error_t error;
    json_t *health_json = json_loads(health, 0, &error);
    assert(json_is_true(json_object_get(json_object_get(health_json, "transport"), "rtp_fresh")));
    json_decref(health_json);
    telemetry.camera_route_generation++;
    mv_response(s,true,health,sizeof(health));health_json=json_loads(health,0,&error);
    assert(json_is_false(json_object_get(json_object_get(health_json,"transport"),"rtp_fresh")));
    assert(json_is_false(json_object_get(json_object_get(health_json,"transport"),"connected")));json_decref(health_json);
    telemetry.camera_route_generation=s->route_generation;
    test_wall -= 7200;
    test_mono += 1000;
    expire_media_session(s, test_wall);
    assert(s->state == MEDIA_STATE_STARTING);
    json_t *renew = identity(s);
    json_object_set_new(renew, "lease_expires_at", json_integer(s->issuer_expiry_utc));
    send_v3("POST", "/api/v2/media/sessions/renew", renew, 200);
    assert(s->monotonic_deadline_ms == original);
    int64_t unchanged_anchor = s->issuer_expiry_utc;
    json_object_set_new(renew, "lease_expires_at", json_integer(unchanged_anchor - 2));
    send_v3("POST", "/api/v2/media/sessions/renew", renew, 200);
    assert(s->issuer_expiry_utc == unchanged_anchor && s->monotonic_deadline_ms == original);
    json_object_set_new(renew, "lease_expires_at", json_integer(s->issuer_expiry_utc + 3));
    send_v3("POST", "/api/v2/media/sessions/renew", renew, 200);
    assert(s->monotonic_deadline_ms == original + 3000);
    json_object_set_new(renew, "lease_id", json_string("00000000-0000-4000-8000-000000000099"));
    send_v3("POST", "/api/v2/media/sessions/renew", renew, 409);
    assert(s->monotonic_deadline_ms == original + 3000);
    json_object_set_new(renew, "lease_id", json_string(s->lease_id));
    g_client_count = 0;
    g_clients[0] = NULL;
    test_mono = s->monotonic_deadline_ms - 1;
    expire_media_session(s, test_wall);
    assert(s->state == MEDIA_STATE_STARTING);
    ++test_mono;
    expire_media_session(s, test_wall);
    assert(s->state == MEDIA_STATE_STOPPED && !find_route(s->sn));
    send_v3("POST", "/api/v2/media/sessions/renew", renew, 410);
    assert(s->state == MEDIA_STATE_STOPPED);
    /* A queued START keeps the original absolute expiry. Delay consumes its
     * fractional remaining interval, including equality and sub-ms rounding. */
    int64_t original_expiry = (int64_t)test_wall + 7;
    MediaSession late = {0};
    test_wall += 5;
    test_mono += 5000;
    test_nsec = 500000000;
    assert(media_v3_begin_deadline(&late, original_expiry));
    assert(late.monotonic_deadline_ms == test_mono + 1500);
    test_wall += 2;
    test_mono += 1500;
    test_nsec = 0;
    assert(!media_v3_begin_deadline(&late, original_expiry));
    assert(!media_v3_begin_deadline(&late, 0));
    test_nsec = 1;
    assert(!media_v3_begin_deadline(&late, original_expiry));
    assert(media_v3_begin_deadline(&late, (int64_t)test_wall + 1));
    assert(late.monotonic_deadline_ms == test_mono + 999);
    test_nsec = 0;
    realtime_read_delay = 200;
    uint64_t before_sample = test_mono;
    assert(media_v3_begin_deadline(&late, (int64_t)test_wall + 1));
    assert(test_mono == before_sample + 200 && late.monotonic_deadline_ms == before_sample + 1000);
    realtime_read_delay = 0;
    /* An incoming command gets only its seven remaining seconds, never60. */
    MediaSession budget = {0};
    assert(media_v3_begin_deadline(&budget, (int64_t)test_wall + 7));
    assert(budget.monotonic_deadline_ms == test_mono + 7000);
    assert(!media_v3_begin_deadline(&budget, (int64_t)test_wall));
    json_t *record = json_pack("{s:I,s:s}", "monotonic_deadline_ms", (json_int_t)budget.monotonic_deadline_ms,
                               "monotonic_boot_id", g_media_test_boot_id);
    MediaSession restored = {0};
    test_wall += 100000;
    assert(mv_restore_deadline(&restored, record, budget.issuer_expiry_utc) &&
           restored.monotonic_deadline_ms == budget.monotonic_deadline_ms);
    json_object_set_new(record, "monotonic_boot_id", json_string("22222222-2222-4222-8222-222222222222"));
    assert(!mv_restore_deadline(&restored, record, budget.issuer_expiry_utc));
    json_object_set_new(record, "monotonic_boot_id", json_string(g_media_test_boot_id));
    json_object_del(record, "monotonic_deadline_ms");
    assert(!mv_restore_deadline(&restored, record, budget.issuer_expiry_utc));
    json_object_set_new(record, "monotonic_deadline_ms", json_integer((json_int_t)test_mono));
    assert(!mv_restore_deadline(&restored, record, budget.issuer_expiry_utc));
    json_decref(record);
    uint64_t saved = test_mono;
    test_mono--;
    assert(!media_v3_renew_deadline(&budget, (int64_t)test_wall + 10));
    assert(g_media_monotonic_failed);
    test_mono = saved;
    g_media_monotonic_failed = false;
    g_media_monotonic_last = 0;
    json_decref(renew);
    json_decref(start);
    json_decref(fake_mounts);
    json_decref(g_media_schema);
    json_decref(g_media_camera_schema);
    json_decref(g_media_v3_schema);
    puts("PASS v3 original remaining TTL, wall forward/backward jumps, duplicate START/renew, identity fences, "
         "fractional receipt/late START, stale anchor isolation, equality expiry and clock regression");
    return 0;
}
