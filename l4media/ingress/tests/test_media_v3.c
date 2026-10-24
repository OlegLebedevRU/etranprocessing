#define L4MEDIA_TESTING
#define main ingress_server_main
#include "../src/l4media_ingress.c"
#undef main
#include <assert.h>
#include "test_media_common.h"

static void v3_request(const char* method, const char* path, json_t* value,
                       int selection, int expected, char* output) {
    char* body = value ? json_dumps(value, JSON_COMPACT) : NULL;
    int status = 0;
    media_v2_http_contract(method, path, body, selection, output, 16384, &status);
    free(body);
    if (status != expected) fprintf(stderr, "v3 HTTP %d expected %d: %s\n", status, expected, output);
    assert(status == expected);
    if (selection != 3) return;
    json_error_t error; json_t* response = json_loads(output, JSON_REJECT_DUPLICATES, &error);
    const char* definition = status >= 400 ? "rejection" : strstr(path, "capabilities") ?
        "media_capabilities" : !strcmp(method, "GET") ? "media_health" : "media_response";
    assert(ms_validate_root(g_media_v3_schema,
        json_object_get(json_object_get(g_media_v3_schema, "$defs"), definition), response, 0));
    json_decref(response);
}

static json_t* v3_start(const char* source) {
    json_t* value = fixture("media_start");
    json_object_set_new(value, "contract_version", json_integer(3));
    json_object_set_new(value, "lease_expires_at", json_integer(time(NULL) + 120));
    json_t* descriptor = json_object_get(value, "descriptor");
    json_object_set_new(descriptor, "contract_version", json_integer(3));
    json_object_set_new(descriptor, "source_type", json_string(source));
    json_object_set_new(descriptor, "profile_id", json_string("low"));
    json_t* track = json_array_get(json_object_get(descriptor, "tracks"), 0);
    json_object_set_new(track, "width", json_integer(3000));
    json_object_set_new(track, "height", json_integer(2000));
    json_object_set_new(track, "fps", json_integer(30));
    json_object_set_new(track, "level_id", json_integer(150));
    return value;
}
static json_t* v3_identity(MediaSession* session) {
    json_error_t error; json_t* descriptor = json_loads(session->descriptor, 0, &error);
    json_t* value = json_pack("{s:i,s:s,s:s,s:s,s:s,s:s}", "contract_version", session->contract_version,
        "sn", session->sn, "session_id", session->session_id, "lease_id", session->lease_id,
        "stream_instance_id", session->stream_instance_id, "descriptor_revision", mv_string(descriptor, "descriptor_revision"));
    json_decref(descriptor); return value;
}
int main(void) {
    lifecycle_safe_strcpy(g_janus_host,"127.0.0.1",sizeof(g_janus_host));
    assert(media_schema_load("spec/hevc.schema.json"));
    assert(media_camera_schema_load("spec/camera-v1/hevc.schema.json"));
    assert(media_v3_schema_load("spec/media-v3/media.schema.json"));
    const char* header = "GET / HTTP/1.1\r\nX-L4-Media-Contract: " MEDIA_V3_REVISION;
    char raw[1024];
    snprintf(raw, sizeof(raw), "%s\r\n\r\n", header); assert(media_contract_header(raw) == 3);
    assert(media_contract_header("GET / HTTP/1.1\r\nx-l4-media-contract:\tl4-media/3.0.0 \r\n\r\n") == 3);
    assert(media_contract_header("GET / HTTP/1.1\r\n\r\nX-L4-Media-Contract: l4-media/3.0.0") == 0);
    assert(media_contract_header("GET / HTTP/1.1\r\nX-L4-Media-Contract: l4-media/4.0.0\r\n\r\n") == -1);
    snprintf(raw, sizeof(raw), "%s\r\nX-L4-Media-Contract: " MEDIA_V3_REVISION "\r\n\r\n", header);
    assert(media_contract_header(raw) == -1);
    snprintf(raw, sizeof(raw), "%s\r\nX-L4-Camera-Contract: " MEDIA_CAMERA_REVISION "\r\n\r\n", header);
    assert(media_contract_header(raw) == -1);
    snprintf(raw, sizeof(raw), "%s\r\nX-L4-Camera-Cadence: camera-cadence/0.1.0-proposed\r\n\r\n", header);
    assert(media_contract_header(raw) == -1);
    fake_mounts = json_object(); g_janus_test_request = fake_janus;
    char output[16384];
    json_t* old = fixture("media_start");
    json_object_set_new(old,"lease_expires_at",json_integer(time(NULL)+120));
    request("/api/v2/media/sessions/start",old,422,output);json_decref(old);
    fake_failure = 1;
    v3_request("GET", "/api/v2/media/capabilities", NULL, 3, 503, output);
    fake_failure = 0;
    g_media_hevc_enabled = true; g_media_probe_at = 0;
    v3_request("GET", "/api/v2/media/capabilities", NULL, 3, 200, output);
    assert(g_media_v3_h265_verified && g_media_v3_h264_profiles == 7);
    assert(!strstr(output,"max_width") && !strstr(output,"camera_"));
    const char* redis_url = getenv("L4MEDIA_TEST_REDIS_URL");
    if (redis_url) assert(!media_redis_init(redis_url) && media_redis_ok());
    json_t* start = v3_start("desktop");
    v3_request("POST", "/api/v2/media/sessions/start", start, 0, 400, output);
    json_t* descriptor = json_object_get(start, "descriptor");
    json_object_set_new(descriptor, "contract_version", json_integer(2));
    v3_request("POST", "/api/v2/media/sessions/start", start, 3, 400, output);
    json_object_set_new(descriptor, "contract_version", json_integer(3));
    json_object_set_new(start, "contract_version", json_integer(2));
    v3_request("POST", "/api/v2/media/sessions/start", start, 3, 400, output);
    json_object_set_new(start, "contract_version", json_integer(3));
    v3_request("POST", "/api/v2/media/sessions/start", start, 3, 201, output);
    MediaSession* session = find_session_by_id(mv_string(start,"session_id"));
    assert(session && session->contract_version == 3 && find_route(session->sn));
    char expected[MEDIA_DESCRIPTOR_LIMIT]; strcpy(expected,session->descriptor);
    int created = creates;
    v3_request("POST", "/api/v2/media/sessions/start", start, 3, 200, output);
    assert(creates == created);
    JanusMountpointInfo info; assert(!janus_get_mountpoint_info(session->mountpoint_id, &info));
    assert(info.found && strstr(info.fmtp, "level-id=150"));
    /* Same mountpoint with a different actual fmtp must never be adopted. */
    char key[32]; snprintf(key,sizeof(key),"%u",session->mountpoint_id);
    json_t* mount = json_object_get(fake_mounts,key);
    json_object_set_new(mount,"videofmtp",json_string("profile-space=0;profile-id=1;tier-flag=0;level-id=153;sprop-max-don-diff=0"));
    v3_request("POST", "/api/v2/media/sessions/start", start, 3, 409, output);
    json_object_set_new(mount,"videofmtp",json_string("profile-space=0;profile-id=1;tier-flag=0;level-id=150;sprop-max-don-diff=0"));
    json_t* track = json_array_get(json_object_get(descriptor,"tracks"),0);
    json_object_set_new(track,"level_id",json_integer(153));
    v3_request("POST", "/api/v2/media/sessions/start", start, 3, 409, output);
    json_object_set_new(track,"level_id",json_integer(150));
    json_t* renew = v3_identity(session);
    json_object_set_new(renew,"lease_expires_at",json_integer(time(NULL)+180));
    v3_request("POST", "/api/v2/media/sessions/renew", renew, 3, 200, output);
    json_object_set_new(renew,"lease_id",json_string("00000000-0000-4000-8000-000000000090"));
    v3_request("POST", "/api/v2/media/sessions/renew", renew, 3, 409, output);
    json_object_set_new(renew,"lease_id",json_string(session->lease_id));
    char health[1024]; snprintf(health,sizeof(health),"/api/v2/media/sessions/%s?sn=%s&lease_id=%s&stream_instance_id=%s&descriptor_revision=%s",
        session->session_id,session->sn,session->lease_id,session->stream_instance_id,mv_string(renew,"descriptor_revision"));
    v3_request("GET",health,NULL,3,200,output);
    v3_request("GET",health,NULL,0,409,output);
    char legacy_body[256]; snprintf(legacy_body,sizeof(legacy_body),"{\"sn\":\"%s\"}",session->sn);
    assert(media_v1_conflicts("/api/v1/media/sessions/renew",legacy_body));
    if (redis_url) {
        /* Both the record and envelope retain version 3. Missing schema fails
         * closed; restoring never reinterprets the record as a v2 stream. */
        char redis_key[192], stored[16384];
        snprintf(redis_key,sizeof(redis_key),MEDIA_REDIS_PREFIX_SESSION "%s",session->session_id);
        assert(media_redis_get(redis_key,stored,sizeof(stored)));
        json_error_t error;json_t* record=json_loads(stored,JSON_REJECT_DUPLICATES,&error);
        assert(mv_integer(record,"format_version")==3);
        json_object_set_new(record,"format_version",json_integer(2));
        char* mixed=json_dumps(record,JSON_COMPACT);media_redis_set_ex(redis_key,mixed,300);
        free(mixed);json_decref(record);
        memset(g_media_sessions,0,sizeof(g_media_sessions));g_media_session_count=0;
        memset(&g_routes,0,sizeof(g_routes));
        media_v2_restore();assert(!g_media_session_count);
        media_redis_set_ex(redis_key,stored,300);
        json_t* saved=g_media_v3_schema;g_media_v3_schema=NULL;
        media_v2_restore(); assert(!g_media_session_count);
        g_media_v3_schema=saved;media_v2_restore();
        session=find_session_by_id(mv_string(start,"session_id"));
        assert(session && session->contract_version==3 && !strcmp(session->descriptor,expected));
        assert(find_route(session->sn));
        v3_request("GET",health,NULL,3,200,output);
        puts("PASS: real Redis v3 roundtrip and missing-reader rejection");
    } else puts("NOT RUN: real Redis roundtrip (requires isolated L4MEDIA_TEST_REDIS_URL)");
    json_t* stop=v3_identity(session);json_object_set_new(stop,"reason",json_string("user_stop"));
    json_object_set_new(stop,"stream_instance_id",json_string("00000000-0000-4000-8000-000000000091"));
    v3_request("POST","/api/v2/media/sessions/stop",stop,3,409,output);assert(find_route(session->sn));
    json_object_set_new(stop,"stream_instance_id",json_string(session->stream_instance_id));
    v3_request("POST","/api/v2/media/sessions/stop",stop,0,400,output);
    v3_request("POST","/api/v2/media/sessions/stop",stop,3,200,output);assert(!find_route(session->sn));
    v3_request("POST","/api/v2/media/sessions/stop",stop,3,200,output);
    json_decref(stop);json_decref(renew);json_decref(start);
    /* The same track object validates for every source and both policies,
     * including arbitrary native raster/cadence, with no camera preset. */
    const char* sources[]={"desktop","usb-camera","camera"};
    for (unsigned i=0;i<3;i++) {
        start=v3_start(sources[i]);descriptor=json_object_get(start,"descriptor");
        json_t* def=json_object_get(json_object_get(g_media_v3_schema,"$defs"),"descriptor");
        assert(ms_validate_root(g_media_v3_schema,def,descriptor,0));
        json_object_set_new(descriptor,"profile_id",json_string("default"));
        assert(ms_validate_root(g_media_v3_schema,def,descriptor,0));
        if (i == 2) json_object_set_new(json_array_get(json_object_get(descriptor,"tracks"),0),"level_id",json_integer(153));
        /* Reuse IDs only after clearing stopped test state, never in production. */
        memset(g_media_sessions,0,sizeof(g_media_sessions));g_media_session_count=0;
        v3_request("POST","/api/v2/media/sessions/start",start,3,201,output);
        session=find_session_by_id(mv_string(start,"session_id"));
        assert(!janus_get_mountpoint_info(session->mountpoint_id,&info));
        assert(strstr(info.fmtp,i == 2 ? "level-id=153" : "level-id=150"));
        stop=v3_identity(session);json_object_set_new(stop,"reason",json_string("user_stop"));
        v3_request("POST","/api/v2/media/sessions/stop",stop,3,200,output);
        json_decref(stop);json_decref(start);
    }
    media_redis_close();json_decref(fake_mounts);
    json_decref(g_media_schema);json_decref(g_media_camera_schema);json_decref(g_media_v3_schema);
    puts("PASS: v3 native geometry, source symmetry, strict revisions, actual fmtp, immutable identity and cleanup");
    return 0;
}
