/* Production event-loop framing with real TCP socketpairs and UDP receivers. */
#define L4MEDIA_TESTING
#define main ingress_server_main
#include "../src/l4media_ingress.c"
#undef main
#include <assert.h>
static int udp_target(struct sockaddr_in *target) {
    int fd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
    assert(fd >= 0);
    memset(target, 0, sizeof(*target));
    target->sin_family = AF_INET;
    target->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    assert(!bind(fd, (struct sockaddr *)target, sizeof(*target)));
    socklen_t n = sizeof(*target);
    assert(!getsockname(fd, (struct sockaddr *)target, &n));
    return fd;
}
static IngressClient *connect_source(int pair[2]) {
    assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair));
    IngressClient *c = add_ingress_client(g_epoll_fd, pair[0], "fixture", 1);
    assert(c);
    const uint8_t preamble[] = {'L', '4', 'R', 'T', 1, 0, 0, 1, 'A'};
    assert(send(pair[1], preamble, sizeof(preamble), 0) == (ssize_t)sizeof(preamble));
    process_ingress_data(g_epoll_fd, c);
    assert(c->state == STATE_FRAMING);
    return c;
}
static const uint8_t payload[] = {0x80, 97, 0x12, 0x34, 0xfe, 0xdc, 0xba, 0x98, 1, 2, 3, 4, 0x26, 1, 0x80};
static void publish(IngressClient *c, int peer) {
    uint8_t frame[sizeof(payload) + 4] = {1, 0, 0, sizeof(payload)};
    memcpy(frame + 4, payload, sizeof(payload));
    assert(send(peer, frame, sizeof(frame), MSG_NOSIGNAL) == (ssize_t)sizeof(frame));
    process_ingress_data(g_epoll_fd, c);
}
static void received(int udp) {
    uint8_t data[128];
    assert(recv(udp, data, sizeof(data), 0) == (ssize_t)sizeof(payload));
    assert(!memcmp(data, payload, sizeof(payload)));
}
static void empty(int udp) {
    uint8_t data[128];
    assert(recv(udp, data, sizeof(data), 0) < 0 && errno == EAGAIN);
}
int main(void) {
    g_epoll_fd = epoll_create1(0);
    assert(g_epoll_fd >= 0);
    g_udp_sock = socket(AF_INET, SOCK_DGRAM, 0);
    assert(g_udp_sock >= 0);
    lifecycle_safe_strcpy(g_janus_host, "127.0.0.1", sizeof(g_janus_host));
    struct sockaddr_in a, b;
    int ua = udp_target(&a), ub = udp_target(&b);
    g_media_session_count = 1;
    MediaSession *owner = &g_media_sessions[0];
    strcpy(owner->session_id, "camera-fixture");
    strcpy(owner->sn, "A");
    owner->contract_version = 3;
    owner->camera_source = true;
    owner->route_generation = 1;
    owner->state = MEDIA_STATE_ACTIVE;
    assert(media_v3_begin_deadline(owner, (int64_t)time(NULL) + 60, time(NULL)));
    int changed;
    assert(upsert_route("A", ntohs(a.sin_port), ntohs(a.sin_port), &changed));
    int old[2];
    IngressClient *c = connect_source(old);
    assert(c->camera_route_generation == 1);
    uint64_t old_tag = (c->epoch << 2) | 1;
    publish(c, old[1]);
    received(ua);
    int late[2];
    assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, late));
    IngressClient *late_client = add_ingress_client(g_epoll_fd, late[0], "late-preamble", 1);
    assert(late_client);
    /* A new prepared camera route cannot inherit any already connected publisher. */
    owner->minimum_camera_connection_epoch = g_next_epoch;
    media_revoke_camera_clients("A");
    assert(upsert_route("A", ntohs(b.sin_port), ntohs(b.sin_port), &changed));
    owner->route_generation = 2;
    assert(c->camera_route_revoked && c->rtp_port == 0 && !camera_connection_current(c));
    c->tx_len = 4;
    c->tx_buf[0] = 2;
    assert(!flush_feedback(c));
    publish(c, old[1]);
    assert(g_client_count == 1 && !find_event_client(old_tag));
    empty(ub);
    uint8_t data[8];
    ssize_t closed = recv(old[1], data, sizeof(data), 0);
    assert(closed == 0 || (closed < 0 && errno == ECONNRESET));
    close(old[1]);
    int fresh[2];
    c = connect_source(fresh);
    assert(c->camera_route_generation == 2);
    publish(c, fresh[1]);
    received(ub);
    /* An older socket with no SN at prepare cannot displace the fresh source. */
    const uint8_t late_preamble[] = {'L', '4', 'R', 'T', 1, 0, 0, 1, 'A'};
    assert(send(late[1], late_preamble, sizeof(late_preamble), 0) == (ssize_t)sizeof(late_preamble));
    process_ingress_data(g_epoll_fd, late_client);
    assert(g_client_count == 1 && g_clients[0] == c);
    publish(c, fresh[1]);
    received(ub);
    close(late[1]);
    /* Even a generation change without retargeting is checked before forwarding. */
    owner->route_generation = 3;
    publish(c, fresh[1]);
    assert(!g_client_count);
    empty(ub);
    close(fresh[1]);
    /* A pre-existing unbound transport is fenced when preparing a camera too. */
    owner->state = MEDIA_STATE_STOPPED;
    delete_route("A", NULL);
    int unbound[2];
    c = connect_source(unbound);
    assert(!c->camera_route_generation);
    media_revoke_camera_clients("A");
    owner->state = MEDIA_STATE_ACTIVE;
    owner->route_generation = 4;
    assert(upsert_route("A", ntohs(a.sin_port), ntohs(a.sin_port), &changed));
    publish(c, unbound[1]);
    assert(!g_client_count);
    empty(ua);
    close(unbound[1]);
    /* Desktop/USB source reuse retains its existing dynamic target behavior. */
    owner->camera_source = false;
    int desktop[2];
    c = connect_source(desktop);
    assert(!c->camera_route_generation);
    publish(c, desktop[1]);
    received(ua);
    assert(upsert_route("A", ntohs(b.sin_port), ntohs(b.sin_port), &changed));
    owner->route_generation = 5;
    assert(!c->camera_route_revoked && camera_connection_current(c));
    publish(c, desktop[1]);
    received(ub);
    owner->monotonic_deadline_ms = 1;
    g_janus_admin_port = 0;
    publish(c, desktop[1]);
    assert(owner->state == MEDIA_STATE_STOPPED);
    empty(ub);
    remove_ingress_client(g_epoll_fd, c);
    close(desktop[1]);
    int overflow[2];
    assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, overflow));
    g_next_epoch = (UINT64_MAX >> 2) + 1;
    assert(!add_ingress_client(g_epoll_fd, overflow[0], "overflow", 1));
    assert(!g_client_count);
    close(overflow[1]);
    close(ua);
    close(ub);
    close(g_udp_sock);
    close(g_epoll_fd);
    puts("PASS camera captured route generation, old/unbound connection revocation, stale feedback/packet drop and "
         "original RTP; desktop reuse unchanged");
    return 0;
}
