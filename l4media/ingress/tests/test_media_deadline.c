/* Exercise the production handlers, without any external Janus or Redis. */
#define main ingress_server_main
#include "../src/l4media_ingress.c"
#undef main
#include <assert.h>

static MediaSession* active(time_t now) {
    memset(g_media_sessions, 0, sizeof(g_media_sessions));
    g_media_session_count = 1;
    MediaSession* s = &g_media_sessions[0];
    strcpy(s->session_id, "old-lease");
    strcpy(s->sn, "deadline-test");
    s->state = MEDIA_STATE_ACTIVE;
    s->ttl_sec = 60;
    s->started_at = s->renewed_at = now;
    return s;
}

int main(void) {
    time_t now = time(NULL);
    char body[512], response[2048];
    int status, ttl = 600;
    bool bound;
    assert(media_lease_ttl("{}", now, &ttl, &bound) && !bound && ttl == 600);
    snprintf(body, sizeof(body), "{\"lease_expires_at\":%ld}", (long)now + 3);
    assert(media_lease_ttl(body, now, &ttl, &bound) && bound && ttl == 3);
    const char* invalid[] = {"null", "\"123\"", "1.5", "9223372036854775808", "-1"};
    for (size_t i = 0; i < sizeof(invalid)/sizeof(invalid[0]); i++) {
        snprintf(body, sizeof(body), "{\"lease_expires_at\":%s}", invalid[i]);
        assert(!media_lease_ttl(body, now, &ttl, &bound));
    }
    MediaSession* s = active(now);
    snprintf(body, sizeof(body), "{\"sn\":\"deadline-test\",\"lease_id\":\"wrong\",\"lease_expires_at\":%ld}", (long)now + 120);
    renew_media_session(s->session_id, body, response, sizeof(response), &status);
    assert(status == 409 && s->ttl_sec == 60 && s->renewed_at == now);
    snprintf(body, sizeof(body), "{\"sn\":\"deadline-test\",\"lease_id\":\"old-lease\",\"lease_expires_at\":%ld}", (long)now + 120);
    renew_media_session(s->session_id, body, response, sizeof(response), &status);
    assert(status == 200 && s->renewed_at + s->ttl_sec == now + 120);
    strcpy(s->session_id, "stream-epoch");
    snprintf(body, sizeof(body), "{\"sn\":\"deadline-test\",\"lease_id\":\"old-lease\",\"stream_instance_id\":\"stream-epoch\",\"lease_expires_at\":%ld}", (long)now + 180);
    renew_media_session(s->session_id, body, response, sizeof(response), &status);
    assert(status == 200 && s->renewed_at + s->ttl_sec == now + 180);
    s->renewed_at = now - 180;
    renew_media_session(s->session_id, body, response, sizeof(response), &status);
    assert(status == 404); /* Expired media must not be resurrected. */
    g_janus_admin_port = 0;
    get_media_session_health(s->session_id, response, sizeof(response), &status);
    assert(status == 200 && s->state == MEDIA_STATE_STOPPED);
    assert(strstr(response, "\"state\":\"stopped\""));
    const char* status_text;
    s = active(now);
    handle_media_session_start("{\"session_id\":\"new-lease\",\"sn\":\"deadline-test\"}", response, sizeof(response), &status, &status_text);
    assert(status == 409 && s->state == MEDIA_STATE_ACTIVE);
    s->renewed_at = now - 60;
    handle_media_session_start("{\"session_id\":\"new-lease\",\"sn\":\"deadline-test\"}", response, sizeof(response), &status, &status_text);
    assert(s->state == MEDIA_STATE_STOPPED && status != 409);
    puts("Media lease deadline, renewal identity and expired-session exclusion passed");
    return 0;
}
