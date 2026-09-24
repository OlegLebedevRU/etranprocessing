#ifndef L4C_RTP_SENDER_H
#define L4C_RTP_SENDER_H

#include "types.h"
#include "encoder_backend.h"

#define L4C_RTP_DEFAULT_HOST         "127.0.0.1"
#define L4C_RTP_DEFAULT_PORT         5004
#define L4C_RTCP_DEFAULT_PORT        5005
#define L4C_RTP_PAYLOAD_TYPE_H264    96
#define L4C_RTP_CLOCK_RATE_HZ        90000
#define L4C_RTP_MAX_PAYLOAD_SIZE     1200
#define L4C_RTP_MAX_FU_PAYLOAD_SIZE  1198
#define L4C_RTP_HEADER_SIZE          12
#define L4C_RTCP_SR_INTERVAL_MS      1000
/* Live video: bounded send queue — drop stale rather than buffer seconds of RTP. */
#define L4C_RTP_SNDBUF_BYTES         (64 * 1024)
/* On WSOULDBLOCK: drop AU + force IDR; no per-packet Sleep retry. */
#define L4C_RTP_SEND_MAX_WOULDBLOCK_RETRIES 0

typedef struct l4c_rtp_config {
    const char *dest_ip;
    uint16_t rtp_port;
    uint16_t rtcp_port;
    uint8_t payload_type;
    uint32_t ssrc;
    const char *cname;
} l4c_rtp_config_t;

typedef struct l4c_rtp_stats {
    uint32_t packets_sent;
    uint64_t bytes_sent;         /* H.264 payload only, no RTP headers */
    uint32_t transport_drops;
    uint32_t rtcp_sr_sent;
    uint32_t last_rtp_timestamp;
    uint64_t last_send_tick_ms;
} l4c_rtp_stats_t;

typedef struct l4c_rtp_sender l4c_rtp_sender_t;

l4c_status_t l4c_rtp_sender_create(const l4c_rtp_config_t *config,
                                    l4c_rtp_sender_t **out_sender);

l4c_status_t l4c_rtp_send_au(l4c_rtp_sender_t *sender,
                              const l4c_access_unit_t *au,
                              l4c_encoder_backend_t *encoder);

l4c_status_t l4c_rtp_sender_poll_rtcp(l4c_rtp_sender_t *sender,
                                       bool *out_pli_received);

void l4c_rtp_sender_get_stats(const l4c_rtp_sender_t *sender,
                               l4c_rtp_stats_t *out_stats);

void l4c_rtp_sender_destroy(l4c_rtp_sender_t *sender);

#endif /* L4C_RTP_SENDER_H */
