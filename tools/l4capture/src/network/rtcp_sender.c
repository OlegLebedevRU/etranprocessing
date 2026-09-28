#include <string.h>
#include "l4capture/rtp_sender.h"
#include <winsock2.h>

/* Seconds between FILETIME epoch (1601) and NTP epoch (1900). */
#define NTP_FILETIME_OFFSET 9435484800ULL
#define FILETIME_TICKS_PER_SEC 10000000ULL

/* Build RTCP SR (28 bytes) + SDES CNAME (variable) + optional BYE into buffer.
 * Returns total compound packet size. */
static uint32_t build_rtcp_compound(uint8_t *buf, uint32_t buf_size,
                                     uint32_t ssrc, uint32_t rtp_ts,
                                     uint32_t pkt_count, uint32_t octet_count,
                                     const char *cname, uint64_t filetime_100ns) {
    uint32_t pos = 0;
    uint32_t cname_len;
    uint32_t sdes_item_len;
    uint32_t sdes_total;
    uint32_t sdes_words;
    uint64_t ntp_sec;
    uint32_t ntp_frac;

    /* SR: 28 bytes */
    if (buf_size < 28) return 0;
    buf[0] = 0x80;             /* V=2, P=0, RC=0 */
    buf[1] = 200;              /* PT=200 (SR) */
    buf[2] = 0; buf[3] = 6;   /* Length = 6 (28/4 - 1) */
    buf[4] = (uint8_t)(ssrc >> 24);
    buf[5] = (uint8_t)((ssrc >> 16) & 0xFF);
    buf[6] = (uint8_t)((ssrc >> 8) & 0xFF);
    buf[7] = (uint8_t)(ssrc & 0xFF);

    /* NTP timestamp */
    ntp_sec = filetime_100ns / FILETIME_TICKS_PER_SEC - NTP_FILETIME_OFFSET;
    ntp_frac = (uint32_t)(((filetime_100ns % FILETIME_TICKS_PER_SEC) << 32) /
                          FILETIME_TICKS_PER_SEC);
    /* NTP MSW */
    buf[8]  = (uint8_t)(ntp_sec >> 24);
    buf[9]  = (uint8_t)((ntp_sec >> 16) & 0xFF);
    buf[10] = (uint8_t)((ntp_sec >> 8) & 0xFF);
    buf[11] = (uint8_t)(ntp_sec & 0xFF);
    /* NTP LSW */
    buf[12] = (uint8_t)(ntp_frac >> 24);
    buf[13] = (uint8_t)((ntp_frac >> 16) & 0xFF);
    buf[14] = (uint8_t)((ntp_frac >> 8) & 0xFF);
    buf[15] = (uint8_t)(ntp_frac & 0xFF);
    /* RTP timestamp */
    buf[16] = (uint8_t)(rtp_ts >> 24);
    buf[17] = (uint8_t)((rtp_ts >> 16) & 0xFF);
    buf[18] = (uint8_t)((rtp_ts >> 8) & 0xFF);
    buf[19] = (uint8_t)(rtp_ts & 0xFF);
    /* Sender's packet count */
    buf[20] = (uint8_t)(pkt_count >> 24);
    buf[21] = (uint8_t)((pkt_count >> 16) & 0xFF);
    buf[22] = (uint8_t)((pkt_count >> 8) & 0xFF);
    buf[23] = (uint8_t)(pkt_count & 0xFF);
    /* Sender's octet count */
    buf[24] = (uint8_t)(octet_count >> 24);
    buf[25] = (uint8_t)((octet_count >> 16) & 0xFF);
    buf[26] = (uint8_t)((octet_count >> 8) & 0xFF);
    buf[27] = (uint8_t)(octet_count & 0xFF);
    pos = 28;

    /* SDES: PT=201, SC=1 */
    if (!cname) cname = "l4capture@127.0.0.1";
    cname_len = (uint32_t)strlen(cname);
    if (cname_len > 255) cname_len = 255;
    /* Item: type(1) + len(1) + cname + END(1) = cname_len + 3, padded to 4-byte boundary */
    sdes_item_len = cname_len + 3; /* type + len + cname + END(0) */
    sdes_total = 4 + 4 + sdes_item_len; /* SDES header(4) + SSRC(4) + items */
    /* Pad to 32-bit boundary */
    while (sdes_total % 4 != 0) sdes_total++;
    sdes_words = (sdes_total / 4) - 1;

    if (pos + sdes_total > buf_size) return pos;
    buf[pos + 0] = 0x81; /* V=2, P=0, SC=1 */
    buf[pos + 1] = 201;  /* PT=201 */
    buf[pos + 2] = (uint8_t)(sdes_words >> 8);
    buf[pos + 3] = (uint8_t)(sdes_words & 0xFF);
    buf[pos + 4] = (uint8_t)(ssrc >> 24);
    buf[pos + 5] = (uint8_t)((ssrc >> 16) & 0xFF);
    buf[pos + 6] = (uint8_t)((ssrc >> 8) & 0xFF);
    buf[pos + 7] = (uint8_t)(ssrc & 0xFF);
    /* CNAME item */
    buf[pos + 8] = 1; /* Type = CNAME */
    buf[pos + 9] = (uint8_t)cname_len;
    memcpy(buf + pos + 10, cname, cname_len);
    buf[pos + 10 + cname_len] = 0; /* END */
    /* Zero-pad remaining bytes */
    {
        uint32_t pad_start = pos + 10 + cname_len + 1;
        uint32_t pad_end = pos + sdes_total;
        if (pad_end > pad_start) memset(buf + pad_start, 0, pad_end - pad_start);
    }
    pos += sdes_total;

    return pos;
}

uint32_t l4c_rtcp_build_compound(uint8_t *buf, uint32_t buf_size,
                                  uint32_t ssrc, uint32_t rtp_ts,
                                  uint32_t pkt_count, uint32_t octet_count,
                                  const char *cname) {
    FILETIME ft;
    uint64_t ticks;
    GetSystemTimeAsFileTime(&ft);
    ticks = ((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    return build_rtcp_compound(buf, buf_size, ssrc, rtp_ts, pkt_count, octet_count,
                               cname, ticks);
}

uint32_t l4c_rtcp_build_compound_at(uint8_t *buf, uint32_t buf_size,
                                    uint32_t ssrc, uint32_t rtp_ts,
                                    uint32_t pkt_count, uint32_t octet_count,
                                    const char *cname, uint64_t filetime_100ns) {
    return build_rtcp_compound(buf, buf_size, ssrc, rtp_ts, pkt_count, octet_count,
                               cname, filetime_100ns);
}

void l4c_rtcp_build_bye(uint8_t *buf, uint32_t buf_size, uint32_t ssrc) {
    if (buf_size < 8) return;
    buf[0] = 0x81; /* V=2, P=0, SC=1 */
    buf[1] = 203;  /* PT=203 (BYE) */
    buf[2] = 0; buf[3] = 1; /* Length = 1 (8/4 - 1) */
    buf[4] = (uint8_t)(ssrc >> 24);
    buf[5] = (uint8_t)((ssrc >> 16) & 0xFF);
    buf[6] = (uint8_t)((ssrc >> 8) & 0xFF);
    buf[7] = (uint8_t)(ssrc & 0xFF);
}
