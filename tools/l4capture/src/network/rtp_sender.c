#define _CRT_RAND_S
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include "l4capture/rtp_sender.h"
#include "l4capture/rtp_packetizer.h"
#include "l4capture/clock.h"

#ifndef SIO_UDP_CONNRESET
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
#endif

/* Forward declarations for RTCP builder (in rtcp_sender.c) */
extern uint32_t l4c_rtcp_build_compound(uint8_t *buf, uint32_t buf_size,
                                          uint32_t ssrc, uint32_t rtp_ts,
                                          uint32_t pkt_count, uint32_t octet_count,
                                          const char *cname);
extern void l4c_rtcp_build_bye(uint8_t *buf, uint32_t buf_size, uint32_t ssrc);

struct l4c_rtp_sender {
    SOCKET rtp_sock;
    SOCKET rtcp_sock;
    struct sockaddr_in rtp_addr;
    struct sockaddr_in rtcp_addr;
    uint32_t ssrc;
    uint16_t seq_num;
    uint8_t payload_type;
    uint32_t base_ts;
    uint64_t pts_start_ms;
    bool pts_start_set;
    l4c_rtp_stats_t stats;
    uint64_t last_rtcp_ms;
    char cname[64];
    bool wsa_initialized;
    /* PLI guard */
    uint64_t last_pli_ms;
};

/* Context passed to packet callback */
typedef struct {
    l4c_rtp_sender_t *sender;
    bool dropped;
    l4c_encoder_backend_t *encoder;
} send_ctx_t;

static bool rtp_packet_send_cb(const uint8_t *buf, uint32_t len,
                                bool is_last_of_au, void *user_ctx) {
    send_ctx_t *ctx = (send_ctx_t *)user_ctx;
    l4c_rtp_sender_t *s = ctx->sender;
    int rc;
    (void)is_last_of_au;

    rc = sendto(s->rtp_sock, (const char *)buf, (int)len, 0,
                (const struct sockaddr *)&s->rtp_addr, sizeof(s->rtp_addr));
    if (rc == SOCKET_ERROR) {
        int err = WSAGetLastError();
#if L4C_RTP_SEND_MAX_WOULDBLOCK_RETRIES > 0
        if (err == WSAEWOULDBLOCK) {
            /* At most one immediate re-send; never Sleep on the media path. */
            int retries;
            for (retries = 0; retries < L4C_RTP_SEND_MAX_WOULDBLOCK_RETRIES; retries++) {
                rc = sendto(s->rtp_sock, (const char *)buf, (int)len, 0,
                            (const struct sockaddr *)&s->rtp_addr, sizeof(s->rtp_addr));
                if (rc != SOCKET_ERROR) break;
                err = WSAGetLastError();
                if (err != WSAEWOULDBLOCK) break;
            }
            if (rc != SOCKET_ERROR) {
                s->stats.packets_sent++;
                s->stats.bytes_sent += (len > L4C_RTP_HEADER_SIZE) ? (len - L4C_RTP_HEADER_SIZE) : 0;
                s->stats.last_send_tick_ms = l4c_now_monotonic_ms();
                return true;
            }
        }
#else
        (void)err;
#endif
        /* Congestion / hard error: drop the AU immediately and request IDR.
         * Live policy — queueing old frames creates multi-second latency. */
        ctx->dropped = true;
        s->stats.transport_drops++;
        if (ctx->encoder && ctx->encoder->vtable && ctx->encoder->vtable->force_idr) {
            ctx->encoder->vtable->force_idr(ctx->encoder);
        }
        return false;
    }
    s->stats.packets_sent++;
    s->stats.bytes_sent += (len > L4C_RTP_HEADER_SIZE) ? (len - L4C_RTP_HEADER_SIZE) : 0;
    s->stats.last_send_tick_ms = l4c_now_monotonic_ms();
    return true;
}

l4c_status_t l4c_rtp_sender_create(const l4c_rtp_config_t *config,
                                    l4c_rtp_sender_t **out_sender) {
    l4c_rtp_sender_t *s;
    WSADATA wsa_data;
    int rc;
    u_long non_blocking;
    int sndbuf;
    BOOL bNewBehavior;
    DWORD dwBytesReturned;

    if (!config || !out_sender) return L4C_ERR_INVALID_ARG;

    s = (l4c_rtp_sender_t *)calloc(1, sizeof(l4c_rtp_sender_t));
    if (!s) return L4C_ERR_OUT_OF_MEMORY;

    /* Initialize Winsock */
    rc = WSAStartup(MAKEWORD(2, 2), &wsa_data);
    if (rc != 0) { free(s); return L4C_ERR_NETWORK; }
    s->wsa_initialized = true;

    /* Create RTP socket */
    s->rtp_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s->rtp_sock == INVALID_SOCKET) {
        WSACleanup(); free(s); return L4C_ERR_NETWORK;
    }

    /* Create RTCP socket */
    s->rtcp_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s->rtcp_sock == INVALID_SOCKET) {
        closesocket(s->rtp_sock); WSACleanup(); free(s); return L4C_ERR_NETWORK;
    }

    /* Non-blocking mode for both sockets */
    non_blocking = 1;
    ioctlsocket(s->rtp_sock, FIONBIO, &non_blocking);
    ioctlsocket(s->rtcp_sock, FIONBIO, &non_blocking);

    /* Socket buffer size */
    sndbuf = L4C_RTP_SNDBUF_BYTES;
    setsockopt(s->rtp_sock, SOL_SOCKET, SO_SNDBUF, (const char *)&sndbuf, sizeof(sndbuf));
    setsockopt(s->rtcp_sock, SOL_SOCKET, SO_SNDBUF, (const char *)&sndbuf, sizeof(sndbuf));

    /* Disable ICMP Port Unreachable reset on both sockets */
    bNewBehavior = FALSE;
    dwBytesReturned = 0;
    WSAIoctl(s->rtp_sock, SIO_UDP_CONNRESET, &bNewBehavior, sizeof(bNewBehavior),
             NULL, 0, &dwBytesReturned, NULL, NULL);
    WSAIoctl(s->rtcp_sock, SIO_UDP_CONNRESET, &bNewBehavior, sizeof(bNewBehavior),
             NULL, 0, &dwBytesReturned, NULL, NULL);

    /* Setup destination addresses */
    memset(&s->rtp_addr, 0, sizeof(s->rtp_addr));
    s->rtp_addr.sin_family = AF_INET;
    s->rtp_addr.sin_port = htons(config->rtp_port ? config->rtp_port : L4C_RTP_DEFAULT_PORT);
    inet_pton(AF_INET, config->dest_ip ? config->dest_ip : L4C_RTP_DEFAULT_HOST,
              &s->rtp_addr.sin_addr);

    memset(&s->rtcp_addr, 0, sizeof(s->rtcp_addr));
    s->rtcp_addr.sin_family = AF_INET;
    s->rtcp_addr.sin_port = htons(config->rtcp_port ? config->rtcp_port : L4C_RTCP_DEFAULT_PORT);
    inet_pton(AF_INET, config->dest_ip ? config->dest_ip : L4C_RTP_DEFAULT_HOST,
              &s->rtcp_addr.sin_addr);

    /* Connect UDP sockets — sets default destination, caches routing on Windows.
     * sendto() still works after connect(); this is purely an optimization. */
    connect(s->rtp_sock, (const struct sockaddr *)&s->rtp_addr, sizeof(s->rtp_addr));
    connect(s->rtcp_sock, (const struct sockaddr *)&s->rtcp_addr, sizeof(s->rtcp_addr));

    /* Initialize session state with crypto RNG */
    {
        unsigned int rng_val;
        rand_s(&rng_val);
        s->ssrc = config->ssrc ? config->ssrc : (uint32_t)rng_val;
        rand_s(&rng_val);
        s->seq_num = (uint16_t)(rng_val & 0xFFFF);
        rand_s(&rng_val);
        s->base_ts = (uint32_t)rng_val;
    }

    s->payload_type = config->payload_type ? config->payload_type : L4C_RTP_PAYLOAD_TYPE_H264;
    s->pts_start_set = false;
    s->last_rtcp_ms = l4c_now_monotonic_ms();
    s->last_pli_ms = 0;

    if (config->cname) {
        strncpy(s->cname, config->cname, sizeof(s->cname) - 1);
        s->cname[sizeof(s->cname) - 1] = '\0';
    } else {
        strncpy(s->cname, "l4capture@127.0.0.1", sizeof(s->cname) - 1);
    }

    memset(&s->stats, 0, sizeof(s->stats));
    *out_sender = s;
    return L4C_OK;
}

l4c_status_t l4c_rtp_send_au(l4c_rtp_sender_t *sender,
                              const l4c_access_unit_t *au,
                              l4c_encoder_backend_t *encoder) {
    uint32_t rtp_ts;
    send_ctx_t ctx;

    if (!sender || !au) return L4C_ERR_INVALID_ARG;
    if (au->nal_count == 0) return L4C_OK;

    /* Record PTS start on first AU */
    if (!sender->pts_start_set) {
        sender->pts_start_ms = au->pts_ms;
        sender->pts_start_set = true;
    }

    /* Compute RTP timestamp: base + elapsed_pts * 90.
     * Guard underflow: never cast a negative uint64 delta into uint32. */
    {
        uint64_t elapsed_ms = 0;
        if (au->pts_ms > sender->pts_start_ms) {
            elapsed_ms = au->pts_ms - sender->pts_start_ms;
        }
        rtp_ts = sender->base_ts + (uint32_t)(elapsed_ms * 90u);
    }
    sender->stats.last_rtp_timestamp = rtp_ts;

    ctx.sender = sender;
    ctx.dropped = false;
    ctx.encoder = encoder;

    {
        l4c_status_t pkt_status = l4c_rtp_packetize_au(au, rtp_ts, sender->seq_num, sender->ssrc,
                          sender->payload_type, rtp_packet_send_cb, &ctx);
        if (pkt_status != L4C_OK && !ctx.dropped) {
            ctx.dropped = true;
        }
    }

    if (ctx.dropped) {
        /* Advance seq_num past the dropped AU for correct continuity */
        uint32_t pkt_count = l4c_rtp_count_packets(au);
        sender->seq_num = (uint16_t)(sender->seq_num + (uint16_t)pkt_count);
        return L4C_ERR_NETWORK;
    }

    /* Advance seq_num by actual packets sent */
    {
        uint32_t pkt_count = l4c_rtp_count_packets(au);
        sender->seq_num = (uint16_t)(sender->seq_num + (uint16_t)pkt_count);
    }
    return L4C_OK;
}

l4c_status_t l4c_rtp_sender_poll_rtcp(l4c_rtp_sender_t *sender,
                                       bool *out_pli_received) {
    uint64_t now;
    uint8_t rtcp_buf[512];
    uint32_t rtcp_len;

    if (!sender) return L4C_ERR_INVALID_ARG;
    if (out_pli_received) *out_pli_received = false;

    now = l4c_now_monotonic_ms();

    /* Send compound RTCP SR/SDES every 1000ms */
    if (now - sender->last_rtcp_ms >= L4C_RTCP_SR_INTERVAL_MS) {
        rtcp_len = l4c_rtcp_build_compound(rtcp_buf, sizeof(rtcp_buf),
                                            sender->ssrc,
                                            sender->stats.last_rtp_timestamp,
                                            sender->stats.packets_sent,
                                            (uint32_t)(sender->stats.bytes_sent & 0xFFFFFFFF),
                                            sender->cname);
        if (rtcp_len > 0) {
            sendto(sender->rtcp_sock, (const char *)rtcp_buf, (int)rtcp_len, 0,
                   (const struct sockaddr *)&sender->rtcp_addr, sizeof(sender->rtcp_addr));
            sender->stats.rtcp_sr_sent++;
        }
        sender->last_rtcp_ms = now;
    }

    /* Check for incoming RTCP (PLI guard) - best effort read */
    {
        uint8_t recv_buf[256];
        int n = recv(sender->rtcp_sock, (char *)recv_buf, sizeof(recv_buf), 0);
        if (n >= 12 && recv_buf[1] == 206) {
            /* Payload-Specific Feedback, FMT=1 = PLI */
            uint8_t fmt = recv_buf[0] & 0x1F;
            if (fmt == 1 && out_pli_received) {
                /* Rate limit: at most 1 PLI per 500ms */
                if (now - sender->last_pli_ms >= 500) {
                    *out_pli_received = true;
                    sender->last_pli_ms = now;
                }
            }
        }
    }
    return L4C_OK;
}

void l4c_rtp_sender_get_stats(const l4c_rtp_sender_t *sender,
                               l4c_rtp_stats_t *out_stats) {
    if (sender && out_stats) *out_stats = sender->stats;
}

void l4c_rtp_sender_destroy(l4c_rtp_sender_t *sender) {
    if (!sender) return;
    /* Best-effort RTCP BYE */
    if (sender->rtcp_sock != INVALID_SOCKET) {
        uint8_t bye_buf[8];
        l4c_rtcp_build_bye(bye_buf, sizeof(bye_buf), sender->ssrc);
        sendto(sender->rtcp_sock, (const char *)bye_buf, 8, 0,
               (const struct sockaddr *)&sender->rtcp_addr, sizeof(sender->rtcp_addr));
    }
    if (sender->rtp_sock != INVALID_SOCKET) closesocket(sender->rtp_sock);
    if (sender->rtcp_sock != INVALID_SOCKET) closesocket(sender->rtcp_sock);
    if (sender->wsa_initialized) WSACleanup();
    free(sender);
}
