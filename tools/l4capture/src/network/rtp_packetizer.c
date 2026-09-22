#include <string.h>
#include "l4capture/rtp_packetizer.h"
#include <winsock2.h>

static void write_rtp_header(uint8_t *buf, bool marker, uint8_t pt,
                              uint16_t seq, uint32_t ts, uint32_t ssrc) {
    buf[0] = 0x80; /* V=2, P=0, X=0, CC=0 */
    buf[1] = (uint8_t)((marker ? 0x80 : 0x00) | (pt & 0x7F));
    buf[2] = (uint8_t)(seq >> 8);
    buf[3] = (uint8_t)(seq & 0xFF);
    buf[4] = (uint8_t)(ts >> 24);
    buf[5] = (uint8_t)((ts >> 16) & 0xFF);
    buf[6] = (uint8_t)((ts >> 8) & 0xFF);
    buf[7] = (uint8_t)(ts & 0xFF);
    buf[8] = (uint8_t)(ssrc >> 24);
    buf[9] = (uint8_t)((ssrc >> 16) & 0xFF);
    buf[10] = (uint8_t)((ssrc >> 8) & 0xFF);
    buf[11] = (uint8_t)(ssrc & 0xFF);
}

/* Flush accumulated STAP-A NALs as one RTP packet. */
static l4c_status_t flush_stap(uint8_t *pkt_buf, uint32_t stap_payload_bytes,
                                bool marker, uint8_t pt, uint16_t seq,
                                uint32_t ts, uint32_t ssrc,
                                l4c_rtp_packet_cb_t cb, void *ctx) {
    /* Prepend STAP-A header byte (0x78 = F=0, NRI=3, Type=24) */
    memmove(pkt_buf + L4C_RTP_HEADER_SIZE + 1,
            pkt_buf + L4C_RTP_HEADER_SIZE, stap_payload_bytes);
    pkt_buf[L4C_RTP_HEADER_SIZE] = 0x78;
    write_rtp_header(pkt_buf, marker, pt, seq, ts, ssrc);
    if (!cb(pkt_buf, L4C_RTP_HEADER_SIZE + 1 + stap_payload_bytes, marker, ctx))
        return L4C_ERR_NETWORK;
    return L4C_OK;
}

l4c_status_t l4c_rtp_packetize_au(const l4c_access_unit_t *au,
                                   uint32_t rtp_timestamp,
                                   uint16_t seq_num_base,
                                   uint32_t ssrc,
                                   uint8_t payload_type,
                                   l4c_rtp_packet_cb_t callback,
                                   void *user_ctx) {
    uint16_t seq;
    uint32_t k;
    /* Stack-allocated MTU buffer: 12 (RTP header) + 2 (FU-A hdr) + 1198 = 1212 max */
    uint8_t pkt_buf[L4C_RTP_HEADER_SIZE + 2 + L4C_RTP_MAX_FU_PAYLOAD_SIZE];
    /* STAP-A aggregation state: bytes accumulated in pkt_buf after RTP header */
    uint32_t stap_bytes = 0;

    if (!au || !callback) return L4C_ERR_INVALID_ARG;
    seq = seq_num_base;

    for (k = 0; k < au->nal_count; ++k) {
        const l4c_nal_desc_t *nal = &au->nals[k];
        uint32_t L = nal->length;
        const uint8_t *D = nal->data;
        bool last_nal = (k == au->nal_count - 1);

        if (!D || L == 0) continue;

        if (L <= L4C_RTP_MAX_PAYLOAD_SIZE) {
            /* Small NAL — candidate for STAP-A aggregation */
            uint32_t need = 2 + L; /* 2-byte length prefix + NAL data */
            bool fits = (stap_bytes + need) <= L4C_RTP_MAX_PAYLOAD_SIZE;

            if (fits) {
                /* Accumulate into STAP-A buffer */
                pkt_buf[L4C_RTP_HEADER_SIZE + stap_bytes]     = (uint8_t)(L >> 8);
                pkt_buf[L4C_RTP_HEADER_SIZE + stap_bytes + 1] = (uint8_t)(L & 0xFF);
                memcpy(pkt_buf + L4C_RTP_HEADER_SIZE + stap_bytes + 2, D, L);
                stap_bytes += need;

                if (last_nal) {
                    /* Last NAL — flush STAP-A with marker=1 */
                    l4c_status_t s = flush_stap(pkt_buf, stap_bytes, true,
                                                payload_type, seq, rtp_timestamp,
                                                ssrc, callback, user_ctx);
                    if (s != L4C_OK) return s;
                    seq++;
                    stap_bytes = 0;
                }
            } else {
                /* Doesn't fit — flush accumulated STAP-A first (marker=0) */
                if (stap_bytes > 0) {
                    l4c_status_t s = flush_stap(pkt_buf, stap_bytes, false,
                                                payload_type, seq, rtp_timestamp,
                                                ssrc, callback, user_ctx);
                    if (s != L4C_OK) return s;
                    seq++;
                    stap_bytes = 0;
                }
                if (last_nal) {
                    /* Single NAL packet */
                    uint32_t pkt_len = L4C_RTP_HEADER_SIZE + L;
                    write_rtp_header(pkt_buf, true, payload_type, seq, rtp_timestamp, ssrc);
                    memcpy(pkt_buf + L4C_RTP_HEADER_SIZE, D, L);
                    seq++;
                    if (!callback(pkt_buf, pkt_len, true, user_ctx))
                        return L4C_ERR_NETWORK;
                } else {
                    /* Start new STAP-A accumulation */
                    pkt_buf[L4C_RTP_HEADER_SIZE]     = (uint8_t)(L >> 8);
                    pkt_buf[L4C_RTP_HEADER_SIZE + 1] = (uint8_t)(L & 0xFF);
                    memcpy(pkt_buf + L4C_RTP_HEADER_SIZE + 2, D, L);
                    stap_bytes = need;
                }
            }
        } else {
            /* Large NAL — FU-A fragmentation. Flush STAP-A first. */
            if (stap_bytes > 0) {
                l4c_status_t s = flush_stap(pkt_buf, stap_bytes, false,
                                            payload_type, seq, rtp_timestamp,
                                            ssrc, callback, user_ctx);
                if (s != L4C_OK) return s;
                seq++;
                stap_bytes = 0;
            }

            /* FU-A fragmentation */
            {
                uint8_t hdr = D[0];
                const uint8_t *payload = D + 1;
                uint32_t payload_len = L - 1;
                uint32_t offset = 0;
                uint32_t frag_count = (payload_len + L4C_RTP_MAX_FU_PAYLOAD_SIZE - 1) / L4C_RTP_MAX_FU_PAYLOAD_SIZE;
                uint32_t i;
                uint8_t fu_indicator = (uint8_t)((hdr & 0xE0) | 28);

                for (i = 0; i < frag_count; ++i) {
                    uint32_t frag_size = payload_len - offset;
                    uint8_t fu_header;
                    bool is_last_frag;
                    bool is_first_frag;
                    uint32_t pkt_len;

                    if (frag_size > L4C_RTP_MAX_FU_PAYLOAD_SIZE) frag_size = L4C_RTP_MAX_FU_PAYLOAD_SIZE;
                    is_first_frag = (i == 0);
                    is_last_frag = (i == frag_count - 1);

                    fu_header = (uint8_t)(hdr & 0x1F);
                    if (is_first_frag) fu_header |= 0x80;
                    if (is_last_frag) fu_header |= 0x40;

                    write_rtp_header(pkt_buf, (is_last_frag && last_nal), payload_type,
                                      seq, rtp_timestamp, ssrc);
                    pkt_buf[L4C_RTP_HEADER_SIZE] = fu_indicator;
                    pkt_buf[L4C_RTP_HEADER_SIZE + 1] = fu_header;
                    memcpy(pkt_buf + L4C_RTP_HEADER_SIZE + 2, payload + offset, frag_size);

                    pkt_len = L4C_RTP_HEADER_SIZE + 2 + frag_size;
                    seq++;
                    if (!callback(pkt_buf, pkt_len, (is_last_frag && last_nal), user_ctx))
                        return L4C_ERR_NETWORK;
                    offset += frag_size;
                }
            }
        }
    }

    /* Safety: flush any remaining STAP-A (shouldn't happen if logic above is correct) */
    if (stap_bytes > 0) {
        l4c_status_t s = flush_stap(pkt_buf, stap_bytes, true,
                                    payload_type, seq, rtp_timestamp,
                                    ssrc, callback, user_ctx);
        if (s != L4C_OK) return s;
        seq++;
    }

    return L4C_OK;
}

uint32_t l4c_rtp_count_packets(const l4c_access_unit_t *au) {
    uint32_t count = 0;
    uint32_t k;
    uint32_t stap_bytes = 0;
    if (!au) return 0;

    for (k = 0; k < au->nal_count; ++k) {
        uint32_t L = au->nals[k].length;
        bool last = (k == au->nal_count - 1);
        if (L == 0) continue;

        if (L <= L4C_RTP_MAX_PAYLOAD_SIZE) {
            uint32_t need = 2 + L;
            if ((stap_bytes + need) <= L4C_RTP_MAX_PAYLOAD_SIZE) {
                stap_bytes += need;
                if (last) { count++; stap_bytes = 0; }
            } else {
                if (stap_bytes > 0) { count++; stap_bytes = 0; }
                if (last) { count++; }
                else { stap_bytes = need; }
            }
        } else {
            if (stap_bytes > 0) { count++; stap_bytes = 0; }
            {
                uint32_t payload_len = L - 1;
                count += (payload_len + L4C_RTP_MAX_FU_PAYLOAD_SIZE - 1) / L4C_RTP_MAX_FU_PAYLOAD_SIZE;
            }
        }
    }
    if (stap_bytes > 0) count++;
    return count;
}
