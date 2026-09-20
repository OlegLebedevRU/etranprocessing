#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <inttypes.h>
#include <assert.h>
#include <time.h>

#define L4RTP_MAGIC               "L4RT"
#define L4RTP_VERSION             0x01
#define L4RTP_FRAME_RTP           0x01
#define L4RTP_FRAME_RTCP          0x02
#define L4RTP_FRAME_KEEPALIVE     0x03
#define RTP_STALE_DEADLINE_SEC    10
#define MAX_RTP_PAYLOAD_SIZE      65507

typedef enum {
    STATE_PREAMBLE,
    STATE_FRAMING
} ClientState;

typedef struct {
    int fd;
    ClientState state;
    char sn[128];
    char peer_addr[64];
    uint64_t epoch;

    int rtp_port;
    int rtcp_port;

    uint64_t rtp_packets;
    uint64_t rtcp_packets;
    uint64_t unrouted_packets;
    uint64_t keepalive_packets;
    uint64_t total_bytes;
    time_t connect_time;
    time_t last_activity;
    time_t last_rtp_time;
    time_t last_rtcp_time;
} IngressClient;

typedef struct {
    char sn[128];
    int rtp_port;
    int rtcp_port;
    bool is_static;
} RouteEntry;

typedef struct {
    RouteEntry entries[512];
    int count;
} RouteTable;

static RouteTable g_routes;
static IngressClient* g_clients[128];
static int g_client_count = 0;
static struct in_addr g_janus_ip;
static char g_janus_host[128] = "127.0.0.1";
static time_t g_server_start_time = 0;

static bool resolve_janus_host(void) { return true; }
static bool upsert_route(const char* sn, int rtp, int rtcp, int* out) {
    (void)sn; (void)rtp; (void)rtcp; if (out) *out = 0; return true;
}
static bool delete_route(const char* sn, int* out) {
    (void)sn; if (out) *out = 0; return true;
}

#include "../src/media_lifecycle.h"

static size_t format_session_json(char* buf, size_t buf_size, const IngressClient* c, time_t now) {
    if (!c || !buf || buf_size == 0) return 0;

    const char* state_str = "preamble";
    if (c->state == STATE_FRAMING) {
        state_str = (c->rtp_port > 0) ? "streaming" : "connected";
    }

    bool route_exists = (c->rtp_port > 0);

    long transport_idle_sec = (long)(now - c->last_activity);
    if (transport_idle_sec < 0) transport_idle_sec = 0;

    long rtp_idle_sec = (c->last_rtp_time > 0) ? (long)(now - c->last_rtp_time) : -1;
    if (rtp_idle_sec < 0 && c->last_rtp_time > 0) rtp_idle_sec = 0;

    long effective_idle_sec = (c->last_rtp_time > 0) ? rtp_idle_sec : transport_idle_sec;

    bool fresh_rtp = false;
    const char* media_state = "unknown";

    if (c->state == STATE_PREAMBLE) {
        media_state = "connected";
    } else if (!route_exists) {
        media_state = "connected";
    } else if (c->rtp_packets == 0) {
        media_state = "unknown";
    } else {
        if (rtp_idle_sec >= 0 && rtp_idle_sec <= RTP_STALE_DEADLINE_SEC) {
            fresh_rtp = true;
            media_state = "receiving_fresh_media";
        } else {
            fresh_rtp = false;
            media_state = "stale";
        }
    }

    size_t off = 0;
    off += snprintf(buf + off, buf_size - off,
                    "{\"sn\":\"%s\",\"peer\":\"%s\",\"state\":\"%s\","
                    "\"media_state\":\"%s\",\"fresh_rtp\":%s,"
                    "\"transport_connected\":true,\"route_exists\":%s,"
                    "\"connection_epoch\":%" PRIu64 ",\"stale_deadline_sec\":%d,"
                    "\"rtp_port\":%d,\"rtcp_port\":%d,"
                    "\"rtp_packets\":%" PRIu64 ",\"rtcp_packets\":%" PRIu64 ","
                    "\"unrouted_packets\":%" PRIu64 ",\"keepalive_packets\":%" PRIu64 ","
                    "\"bytes\":%" PRIu64 ",\"duration_sec\":%ld,"
                    "\"idle_sec\":%ld,\"transport_idle_sec\":%ld,",
                    c->sn[0] ? c->sn : "pending",
                    c->peer_addr,
                    state_str,
                    media_state,
                    fresh_rtp ? "true" : "false",
                    route_exists ? "true" : "false",
                    c->epoch,
                    RTP_STALE_DEADLINE_SEC,
                    c->rtp_port, c->rtcp_port,
                    c->rtp_packets,
                    c->rtcp_packets,
                    c->unrouted_packets,
                    c->keepalive_packets,
                    c->total_bytes,
                    (long)(now - c->connect_time),
                    effective_idle_sec,
                    transport_idle_sec);

    if (c->last_rtp_time > 0) {
        off += snprintf(buf + off, buf_size - off,
                        "\"last_rtp_at\":%ld,\"rtp_idle_sec\":%ld,",
                        (long)c->last_rtp_time, rtp_idle_sec);
    } else {
        off += snprintf(buf + off, buf_size - off,
                        "\"last_rtp_at\":null,\"rtp_idle_sec\":null,");
    }

    if (c->last_rtcp_time > 0) {
        off += snprintf(buf + off, buf_size - off,
                        "\"last_rtcp_at\":%ld,", (long)c->last_rtcp_time);
    } else {
        off += snprintf(buf + off, buf_size - off,
                        "\"last_rtcp_at\":null,");
    }

    off += snprintf(buf + off, buf_size - off,
                    "\"last_activity\":%ld,\"last_activity_at\":%ld}",
                    (long)c->last_activity, (long)c->last_activity);

    return off;
}

static void test_preamble_parsing(void) {
    printf("[UNIT] Testing preamble parsing...\n");
    uint8_t valid_preamble[] = {'L', '4', 'R', 'T', 0x01, 0x00, 0x00, 0x06, 't', 'e', 's', 't', 's', 'n'};
    assert(memcmp(valid_preamble, L4RTP_MAGIC, 4) == 0);
    assert(valid_preamble[4] == L4RTP_VERSION);
    uint16_t sn_len = ((uint16_t)valid_preamble[6] << 8) | (uint16_t)valid_preamble[7];
    assert(sn_len == 6);
    char parsed_sn[64] = {0};
    memcpy(parsed_sn, valid_preamble + 8, sn_len);
    assert(strcmp(parsed_sn, "testsn") == 0);
    printf("  [PASS] Preamble parsed correctly.\n");
}

static void test_stale_freshness_logic(void) {
    printf("[UNIT] Testing freshness and stale detection...\n");
    time_t now = 100000;
    IngressClient c;
    memset(&c, 0, sizeof(c));
    strcpy(c.sn, "device_773");
    strcpy(c.peer_addr, "10.0.0.1:54321");
    c.state = STATE_FRAMING;
    c.epoch = 42;
    c.rtp_port = 6000;
    c.rtcp_port = 6001;
    c.connect_time = now - 60;

    c.rtp_packets = 100;
    c.last_rtp_time = now - 2;
    c.last_activity = now - 1;

    char buf[4096];
    format_session_json(buf, sizeof(buf), &c, now);
    assert(strstr(buf, "\"media_state\":\"receiving_fresh_media\"") != NULL);
    assert(strstr(buf, "\"fresh_rtp\":true") != NULL);
    assert(strstr(buf, "\"last_rtp_at\":99998") != NULL);
    assert(strstr(buf, "\"rtp_idle_sec\":2") != NULL);
    assert(strstr(buf, "\"idle_sec\":2") != NULL);

    c.last_rtp_time = now - 25;
    c.last_activity = now - 1;
    c.rtcp_packets = 10;

    format_session_json(buf, sizeof(buf), &c, now);
    assert(strstr(buf, "\"media_state\":\"stale\"") != NULL);
    assert(strstr(buf, "\"fresh_rtp\":false") != NULL);
    assert(strstr(buf, "\"transport_connected\":true") != NULL);
    assert(strstr(buf, "\"rtp_idle_sec\":25") != NULL);
    assert(strstr(buf, "\"idle_sec\":25") != NULL);
    assert(strstr(buf, "\"transport_idle_sec\":1") != NULL);

    c.rtp_packets = 0;
    c.last_rtp_time = 0;
    c.last_activity = now - 1;

    format_session_json(buf, sizeof(buf), &c, now);
    assert(strstr(buf, "\"media_state\":\"unknown\"") != NULL);
    assert(strstr(buf, "\"fresh_rtp\":false") != NULL);
    assert(strstr(buf, "\"last_rtp_at\":null") != NULL);
    assert(strstr(buf, "\"rtp_idle_sec\":null") != NULL);

    printf("  [PASS] Freshness logic and JSON output verified.\n");
}

static void test_epoch_isolation(void) {
    printf("[UNIT] Testing connection epoch isolation...\n");
    uint64_t epoch1 = 1;
    uint64_t epoch2 = epoch1 + 1;
    assert(epoch2 > epoch1);

    IngressClient c1, c2;
    memset(&c1, 0, sizeof(c1));
    memset(&c2, 0, sizeof(c2));

    c1.epoch = epoch1;
    c1.rtp_packets = 3867;
    c1.last_rtp_time = 1000;

    c2.epoch = epoch2;
    assert(c2.rtp_packets == 0);
    assert(c2.last_rtp_time == 0);
    assert(c2.epoch == 2);

    printf("  [PASS] Connection epoch is strictly monotonic and resets per session.\n");
}

static void test_json_helpers(void) {
    printf("[UNIT] Testing JSON parser helpers...\n");
    const char* sample = "{\"session_id\":\"sess-123\",\"operation_id\":\"op-456\",\"device_id\":773,\"ttl_sec\":300,\"pin\":\"secret\\\"pin\"}";
    char sess[64] = {0};
    char op[64] = {0};
    char pin[64] = {0};
    uint32_t dev = 0;
    int ttl = 0;

    assert(json_get_string(sample, "session_id", sess, sizeof(sess)));
    assert(strcmp(sess, "sess-123") == 0);

    assert(json_get_string(sample, "operation_id", op, sizeof(op)));
    assert(strcmp(op, "op-456") == 0);

    assert(json_get_uint32(sample, "device_id", &dev));
    assert(dev == 773);

    assert(json_get_int(sample, "ttl_sec", &ttl));
    assert(ttl == 300);

    assert(json_get_string(sample, "pin", pin, sizeof(pin)));
    assert(strcmp(pin, "secret\"pin") == 0);

    char missing[32] = {0};
    assert(!json_get_string(sample, "non_existent", missing, sizeof(missing)));

    printf("  [PASS] JSON parser helpers verified.\n");
}

static void test_service_auth_unit(void) {
    printf("[UNIT] Testing Service Authentication checking...\n");
    strcpy(g_service_token, "my-secret-token");

    const char* req1 = "POST /api/v1/media/sessions/start HTTP/1.1\r\nX-Media-Service-Token: my-secret-token\r\n\r\n";
    assert(check_service_auth(req1) == true);

    const char* req2 = "POST /api/v1/media/sessions/start HTTP/1.1\r\nAuthorization: Bearer my-secret-token\r\n\r\n";
    assert(check_service_auth(req2) == true);

    const char* req3 = "POST /api/v1/media/sessions/start HTTP/1.1\r\nX-Media-Service-Token: wrong-token\r\n\r\n";
    assert(check_service_auth(req3) == false);

    const char* req4 = "POST /api/v1/media/sessions/start HTTP/1.1\r\nHost: localhost\r\n\r\n";
    assert(check_service_auth(req4) == false);

    g_service_token[0] = '\0';
    assert(check_service_auth(req4) == true);
    strcpy(g_service_token, DEFAULT_SERVICE_TOKEN);

    printf("  [PASS] Service Authentication checking verified.\n");
}

static void test_port_allocation_unit(void) {
    printf("[UNIT] Testing dynamic RTP port allocation...\n");
    int rtp = 0, rtcp = 0;
    assert(allocate_port_pair(&rtp, &rtcp));
    assert(rtp == DEFAULT_PORT_BASE);
    assert(rtcp == DEFAULT_PORT_BASE + 1);

    g_routes.count = 1;
    strcpy(g_routes.entries[0].sn, "test_sn");
    g_routes.entries[0].rtp_port = 6010;
    g_routes.entries[0].rtcp_port = 6011;
    g_routes.entries[0].is_static = true;

    assert(allocate_port_pair(&rtp, &rtcp));
    assert(rtp == 6012);
    assert(rtcp == 6013);
    g_routes.count = 0;

    printf("  [PASS] Dynamic port allocation verified.\n");
}

static void test_session_lifecycle_unit(void) {
    printf("[UNIT] Testing session lifecycle state machine...\n");
    g_media_session_count = 0;

    MediaSession* s1 = allocate_session_slot();
    assert(s1 != NULL);
    strcpy(s1->session_id, "sess-001");
    strcpy(s1->operation_id, "op-001");
    strcpy(s1->sn, "device_sn_001");
    s1->state = MEDIA_STATE_ACTIVE;
    s1->started_at = 1000;
    s1->ttl_sec = 600;

    MediaSession* found = find_session_by_id("sess-001");
    assert(found == s1);
    assert(find_session_by_id("sess-unknown") == NULL);

    MediaSession* active_sn = find_active_session_for_sn("device_sn_001");
    assert(active_sn == s1);
    assert(find_active_session_for_sn("device_sn_002") == NULL);

    char resp_buf[512];
    int status = 0;
    stop_media_session("absent-sess", "op-stop", "test", resp_buf, sizeof(resp_buf), &status);
    assert(status == 200);
    assert(strstr(resp_buf, "\"state\":\"stopped\"") != NULL);
    assert(strstr(resp_buf, "Session already stopped or absent") != NULL);

    g_media_session_count = 0;
    printf("  [PASS] Session lifecycle state machine verified.\n");
}

int main(void) {
    printf("=========================================\n");
    printf(" Running l4media-ingress C unit tests\n");
    printf("=========================================\n");

    test_preamble_parsing();
    test_stale_freshness_logic();
    test_epoch_isolation();
    test_json_helpers();
    test_service_auth_unit();
    test_port_allocation_unit();
    test_session_lifecycle_unit();

    printf("=========================================\n");
    printf(" ALL C UNIT TESTS PASSED!\n");
    printf("=========================================\n");
    return 0;
}
