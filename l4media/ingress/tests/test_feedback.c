/* Exercise the production implementation with real nonblocking sockets. */
#define main ingress_server_main
#include "../src/l4media_ingress.c"
#undef main
#include <assert.h>

static int janus_socket(struct sockaddr_in* target) {
    int fd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
    assert(fd >= 0);
    memset(target, 0, sizeof(*target));
    target->sin_family = AF_INET;
    target->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    assert(bind(fd, (struct sockaddr*)target, sizeof(*target)) == 0);
    socklen_t len = sizeof(*target);
    assert(getsockname(fd, (struct sockaddr*)target, &len) == 0);
    return fd;
}

static void publish_sr(IngressClient* c, int peer, int janus, struct sockaddr_in* sender) {
    uint8_t preamble[] = {'L','4','R','T',1,0,0,1,'A'};
    preamble[8] = (uint8_t)c->sn[0];
    uint8_t frame[] = {2,0,0,28,0x80,200,0,6,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
    assert(send(peer, preamble, 5, 0) == 5);
    process_ingress_data(g_epoll_fd, c);
    assert(c->state == STATE_PREAMBLE && c->rtcp_fd == -1);
    assert(send(peer, preamble + 5, sizeof(preamble) - 5, 0) == (ssize_t)sizeof(preamble) - 5);
    process_ingress_data(g_epoll_fd, c);
    assert(c->state == STATE_FRAMING && c->rtcp_fd == -1);
    assert(send(peer, frame, 3, 0) == 3);
    process_ingress_data(g_epoll_fd, c);
    assert(c->rtcp_fd == -1);
    assert(send(peer, frame + 3, sizeof(frame) - 3, 0) == (ssize_t)sizeof(frame) - 3);
    process_ingress_data(g_epoll_fd, c);
    assert(c->rtcp_fd >= 0);
    uint8_t buf[32];
    socklen_t len = sizeof(*sender);
    assert(recvfrom(janus, buf, sizeof(buf), 0, (struct sockaddr*)sender, &len) == 28);
    assert(memcmp(buf, frame + 4, 28) == 0);
}

static void feedback(int janus, struct sockaddr_in* sender, IngressClient* c, int peer, uint8_t id) {
    uint8_t pli[] = {0x81,206,0,2,0,0,0,1,0,0,0,id};
    assert(sendto(janus, pli, sizeof(pli), 0, (struct sockaddr*)sender, sizeof(*sender)) == sizeof(pli));
    assert(receive_feedback(c));
    uint8_t out[16];
    assert(recv(peer, out, sizeof(out), 0) == sizeof(out));
    assert(out[0] == 2 && out[1] == 0 && out[2] == 0 && out[3] == 12);
    assert(memcmp(out + 4, pli, 12) == 0);
}

int main(void) {
    g_epoll_fd = epoll_create1(0);
    assert(g_epoll_fd >= 0);
    g_janus_ip.s_addr = htonl(INADDR_LOOPBACK);
    int a[2], b[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, a) == 0);
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, b) == 0);
    struct sockaddr_in ja, jb, sa, sb;
    int ua = janus_socket(&ja), ub = janus_socket(&jb);
    g_routes.count = 2;
    strcpy(g_routes.entries[0].sn, "A");
    strcpy(g_routes.entries[1].sn, "B");
    g_routes.entries[0].rtcp_port = ntohs(ja.sin_port);
    g_routes.entries[1].rtcp_port = ntohs(jb.sin_port);
    IngressClient* ca = add_ingress_client(g_epoll_fd, a[0], "local", 1);
    IngressClient* cb = add_ingress_client(g_epoll_fd, b[0], "local", 2);
    assert(ca && cb);
    strcpy(ca->sn, "A"); strcpy(cb->sn, "B");
    publish_sr(ca, a[1], ua, &sa);
    publish_sr(cb, b[1], ub, &sb);
    assert(sa.sin_port != sb.sin_port);
    feedback(ua, &sa, ca, a[1], 11);
    feedback(ub, &sb, cb, b[1], 22);
    set_nonblocking(a[1]); set_nonblocking(b[1]);
    uint8_t buf[FEEDBACK_QUEUE_SIZE];
    assert(recv(a[1], buf, sizeof(buf), 0) < 0 && errno == EAGAIN);
    assert(recv(b[1], buf, sizeof(buf), 0) < 0 && errno == EAGAIN);

    /* Wrong-source UDP and malformed feedback never enter downstream framing. */
    uint8_t malformed[] = {0x81,206,0,9,0,0,0,1};
    assert(sendto(ua, malformed, sizeof(malformed), 0, (struct sockaddr*)&sa, sizeof(sa)) == sizeof(malformed));
    assert(sendto(ub, malformed, sizeof(malformed), 0, (struct sockaddr*)&sa, sizeof(sa)) == sizeof(malformed));
    assert(receive_feedback(ca));
    assert(recv(a[1], buf, sizeof(buf), 0) < 0 && errno == EAGAIN);

    /* Force partial TCP write/backpressure with a large valid RTCP packet. */
    int small = 1024;
    assert(setsockopt(ca->fd, SOL_SOCKET, SO_SNDBUF, &small, sizeof(small)) == 0);
    size_t frame_len = 60004;
    ca->tx_buf[0] = 2; ca->tx_buf[1] = 0; ca->tx_buf[2] = 60000 >> 8; ca->tx_buf[3] = (uint8_t)60000;
    memset(ca->tx_buf + 4, 0xAA, 60000);
    ca->tx_len = frame_len;
    assert(flush_feedback(ca));
    assert(ca->tx_off > 0 && ca->tx_off < ca->tx_len);
    memcpy(ca->tx_buf + frame_len, ca->tx_buf, 16);
    ca->tx_len = sizeof(ca->tx_buf); /* Saturated queue: no unbounded allocation. */
    uint8_t saturated_pli[] = {0x81,206,0,2,0,0,0,1,0,0,0,11};
    assert(sendto(ua, saturated_pli, sizeof(saturated_pli), 0, (struct sockaddr*)&sa, sizeof(sa)) == sizeof(saturated_pli));
    assert(receive_feedback(ca) && ca->feedback_dropped == 1);
    uint64_t old_udp_tag = (ca->rtcp_generation << 2) | 3;
    int changed = 0;
    assert(delete_route("A", &changed) && changed == 1);
    assert(ca->rtcp_fd == -1 && ca->rtcp_port == 0 && ca->tx_len == frame_len);
    assert(find_event_client(old_udp_tag) == NULL);
    size_t received = 0;
    for (int i = 0; i < 100 && (received < frame_len || ca->tx_len); i++) {
        ssize_t n = recv(a[1], buf + received, sizeof(buf) - received, 0);
        if (n > 0) received += (size_t)n;
        else assert(n < 0 && errno == EAGAIN);
        assert(flush_feedback(ca));
    }
    assert(received == frame_len && ca->tx_len == 0);
    for (size_t i = 4; i < frame_len; i++) assert(buf[i] == 0xAA);
    assert(ca->state == STATE_FRAMING); /* Route deletion keeps transport open. */
    update_client_targets_for_sn("A", 0, ntohs(jb.sin_port));
    assert(ensure_rtcp_socket(ca));
    assert(find_event_client(old_udp_tag) == NULL);
    assert(ca->rtcp_generation != (old_udp_tag >> 2));
    uint64_t old_tcp_tag = (ca->epoch << 2) | 2;
    remove_ingress_client(g_epoll_fd, ca);
    assert(find_event_client(old_tcp_tag) == NULL);
    int replacement[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, replacement) == 0);
    IngressClient* newer = add_ingress_client(g_epoll_fd, replacement[0], "local", 3);
    assert(newer && find_event_client(old_tcp_tag) == NULL);
    assert(find_event_client(old_udp_tag) == NULL);
    remove_ingress_client(g_epoll_fd, newer);
    remove_ingress_client(g_epoll_fd, cb);
    close(a[1]); close(b[1]); close(replacement[1]); close(ua); close(ub); close(g_epoll_fd);
    puts("Production feedback tests passed: isolation, partial frames/writes, delete, stale epochs.");
    return 0;
}
