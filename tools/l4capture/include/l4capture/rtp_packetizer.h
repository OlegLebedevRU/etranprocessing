#ifndef L4C_RTP_PACKETIZER_H
#define L4C_RTP_PACKETIZER_H
#include "types.h"
#include "encoder_backend.h"
#include "rtp_sender.h"

/* RFC 6184 packetization result callback.
 * Called once per produced RTP packet with assembled buffer and length.
 * Return true to continue, false to abort (e.g. socket error). */
typedef bool (*l4c_rtp_packet_cb_t)(const uint8_t *buf, uint32_t len,
                                     bool is_last_of_au, void *user_ctx);

/* Pack an entire Access Unit into RTP packets via callback.
 * Handles Single NAL (L <= 1200) and FU-A fragmentation (L > 1200).
 * All packets of one AU share the same rtp_timestamp.
 * Marker bit M=1 only on the very last packet.
 * Returns L4C_OK on success, L4C_ERR_NETWORK if callback aborted. */
l4c_status_t l4c_rtp_packetize_au(const l4c_access_unit_t *au,
                                   uint32_t rtp_timestamp,
                                   uint16_t seq_num_base,
                                   uint32_t ssrc,
                                   uint8_t payload_type,
                                   l4c_rtp_packet_cb_t callback,
                                   void *user_ctx);

/* Compute how many RTP packets one AU will produce (for stats). */
uint32_t l4c_rtp_count_packets(const l4c_access_unit_t *au);

#endif /* L4C_RTP_PACKETIZER_H */
