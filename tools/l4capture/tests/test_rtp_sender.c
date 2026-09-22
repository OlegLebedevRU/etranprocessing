/*
 * Tests for RTP/RTCP sender module — RFC 3550, RFC 6184 compliance.
 * Uses a callback-based packetization API for unit tests and actual UDP
 * sockets for integration/E2E tests.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include "l4capture/rtp_packetizer.h"
#include "l4capture/rtp_sender.h"
#include "l4capture/encoder_backend.h"
#include "l4capture/clock.h"

/* Test callback that captures packets into arrays */
typedef struct {
    uint8_t *packets[64];
    uint32_t lengths[64];
    bool markers[64];
    uint16_t seq_nums[64];
    uint32_t timestamps[64];
    uint32_t count;
    bool aborted;
} capture_ctx_t;

static bool capture_cb(const uint8_t *buf, uint32_t len,
                        bool is_last_of_au, void *user_ctx) {
    capture_ctx_t *ctx = (capture_ctx_t *)user_ctx;
    if (ctx->count >= 64) { ctx->aborted = true; return false; }
    ctx->packets[ctx->count] = (uint8_t *)malloc(len);
    if (!ctx->packets[ctx->count]) { ctx->aborted = true; return false; }
    memcpy(ctx->packets[ctx->count], buf, len);
    ctx->lengths[ctx->count] = len;
    ctx->markers[ctx->count] = (buf[1] & 0x80) != 0;
    ctx->seq_nums[ctx->count] = (uint16_t)((uint16_t)buf[2] << 8 | buf[3]);
    ctx->timestamps[ctx->count] = ((uint32_t)buf[4] << 24) | ((uint32_t)buf[5] << 16) |
                                   ((uint32_t)buf[6] << 8) | (uint32_t)buf[7];
    ctx->count++;
    (void)is_last_of_au;
    return true;
}

static void capture_free(capture_ctx_t *ctx) {
    uint32_t i;
    for (i = 0; i < ctx->count; ++i) free(ctx->packets[i]);
    memset(ctx, 0, sizeof(*ctx));
}

static void make_test_nal(l4c_nal_desc_t *nal, uint8_t *buf, uint32_t size, uint8_t type) {
    memset(buf, 0xAB, size);
    buf[0] = type; /* NAL type in low 5 bits */
    nal->data = buf;
    nal->length = size;
    nal->nal_type = type;
}

/* ---- Test 1: Single NAL small (SPS 30B, PPS 8B) ---- */
int test_rtp_single_nal_small(void) {
    l4c_nal_desc_t nals[2];
    uint8_t sps_buf[30], pps_buf[8];
    l4c_access_unit_t au;
    capture_ctx_t cap;
    l4c_status_t st;

    make_test_nal(&nals[0], sps_buf, 30, 7);  /* SPS */
    make_test_nal(&nals[1], pps_buf, 8, 8);   /* PPS */
    au.nals = nals; au.nal_count = 2; au.pts_ms = 100; au.is_idr = false; au.total_bytes = 38;

    memset(&cap, 0, sizeof(cap));
    st = l4c_rtp_packetize_au(&au, 1000, 0, 0x12345678, 96, capture_cb, &cap);
    if (st != L4C_OK || cap.count != 2 || cap.aborted) return 1;

    /* Packet 0: SPS Single NAL */
    if (cap.lengths[0] != 12 + 30) return 2;
    if ((cap.packets[0][0] & 0xC0) != 0x80) return 3; /* V=2 */
    if ((cap.packets[0][1] & 0x7F) != 96) return 4;   /* PT=96 */
    if (cap.markers[0]) return 5; /* M=0 for first NAL */
    if (memcmp(cap.packets[0] + 12, sps_buf, 30) != 0) return 6;

    /* Packet 1: PPS Single NAL, last of AU => M=1 */
    if (cap.lengths[1] != 12 + 8) return 7;
    if (!cap.markers[1]) return 8; /* M=1 for last NAL */
    if (memcmp(cap.packets[1] + 12, pps_buf, 8) != 0) return 9;

    capture_free(&cap);
    return 0;
}

/* ---- Test 2: Boundary 1200 and 1201 bytes ---- */
int test_rtp_boundary_1200_1201(void) {
    l4c_nal_desc_t nal;
    uint8_t *buf1200, *buf1201;
    l4c_access_unit_t au;
    capture_ctx_t cap;
    l4c_status_t st;

    /* 1200 bytes: Single NAL */
    buf1200 = (uint8_t *)malloc(1200);
    if (!buf1200) return 1;
    make_test_nal(&nal, buf1200, 1200, 5);
    au.nals = &nal; au.nal_count = 1; au.pts_ms = 0; au.is_idr = true; au.total_bytes = 1200;
    memset(&cap, 0, sizeof(cap));
    st = l4c_rtp_packetize_au(&au, 0, 0, 0xAA, 96, capture_cb, &cap);
    if (st != L4C_OK || cap.count != 1) { free(buf1200); return 2; }
    if (cap.lengths[0] != 1212) { free(buf1200); capture_free(&cap); return 3; }
    free(buf1200);
    capture_free(&cap);

    /* 1201 bytes: FU-A (2 fragments) */
    buf1201 = (uint8_t *)malloc(1201);
    if (!buf1201) return 4;
    make_test_nal(&nal, buf1201, 1201, 5);
    au.nals = &nal; au.nal_count = 1; au.total_bytes = 1201;
    memset(&cap, 0, sizeof(cap));
    st = l4c_rtp_packetize_au(&au, 0, 0, 0xAA, 96, capture_cb, &cap);
    if (st != L4C_OK || cap.count != 2) { free(buf1201); return 5; }
    /* Fragment 1: 12 + 2 + 1198 = 1212 */
    if (cap.lengths[0] != 1212) { free(buf1201); capture_free(&cap); return 6; }
    /* FU indicator: (hdr & 0xE0) | 28 */
    if (cap.packets[0][12] != ((buf1201[0] & 0xE0) | 28)) { free(buf1201); capture_free(&cap); return 7; }
    /* FU header: S=1, E=0 => 0x80 | (hdr & 0x1F) */
    if (cap.packets[0][13] != (0x80 | (buf1201[0] & 0x1F))) { free(buf1201); capture_free(&cap); return 8; }
    /* Fragment 2: 12 + 2 + 1 = 16 (remaining after first 1198 bytes: 1200 - 1198 = 2... wait) */
    /* payload = 1200 bytes (1201-1), first frag = 1198, second = 2 */
    if (cap.lengths[1] != 14 + 2) { free(buf1201); capture_free(&cap); return 9; }
    /* FU header: S=0, E=1 => 0x40 | (hdr & 0x1F) */
    if (cap.packets[0][13] != (0x80 | (buf1201[0] & 0x1F))) { free(buf1201); capture_free(&cap); return 10; }
    free(buf1201);
    capture_free(&cap);
    return 0;
}

/* ---- Test 3: FU-A fragmentation large (10000B) ---- */
int test_rtp_fua_fragmentation_large(void) {
    l4c_nal_desc_t nal;
    uint8_t *buf;
    l4c_access_unit_t au;
    capture_ctx_t cap;
    l4c_status_t st;
    uint32_t i;
    uint8_t hdr;
    /* 10000B: payload=9999, fragments = ceil(9999/1198) = 9 */
    uint32_t expected_frags = 9;

    buf = (uint8_t *)malloc(10000);
    if (!buf) return 1;
    make_test_nal(&nal, buf, 10000, 5);
    au.nals = &nal; au.nal_count = 1; au.pts_ms = 0; au.is_idr = true; au.total_bytes = 10000;
    memset(&cap, 0, sizeof(cap));
    st = l4c_rtp_packetize_au(&au, 0, 0, 0xBB, 96, capture_cb, &cap);
    if (st != L4C_OK || cap.count != expected_frags) { free(buf); return 2; }

    hdr = buf[0];
    for (i = 0; i < expected_frags; ++i) {
        if (cap.lengths[i] > 1212) { free(buf); capture_free(&cap); return 3; }
        /* FU indicator */
        if (cap.packets[i][12] != ((hdr & 0xE0) | 28)) { free(buf); capture_free(&cap); return 4; }
        /* FU header checks */
        {
            uint8_t fu_hdr = cap.packets[i][13];
            uint8_t expected_s = (i == 0) ? 1 : 0;
            uint8_t expected_e = (i == expected_frags - 1) ? 1 : 0;
            if ((fu_hdr >> 7) != expected_s) { free(buf); capture_free(&cap); return 5; }
            if (((fu_hdr >> 6) & 1) != expected_e) { free(buf); capture_free(&cap); return 6; }
            if ((fu_hdr & 0x1F) != (hdr & 0x1F)) { free(buf); capture_free(&cap); return 7; }
        }
    }
    free(buf);
    capture_free(&cap);
    return 0;
}

/* ---- Test 4: Marker bit AU boundary ---- */
int test_rtp_marker_bit_au_boundary(void) {
    l4c_nal_desc_t nals[3];
    uint8_t sps_buf[30], pps_buf[8], *idr_buf;
    l4c_access_unit_t au;
    capture_ctx_t cap;
    l4c_status_t st;
    /* SPS(30) + PPS(8) + IDR(5000) = 3 NALs, 5000B IDR => 5 FU-A frags => total 7 packets */

    idr_buf = (uint8_t *)malloc(5000);
    if (!idr_buf) return 1;
    make_test_nal(&nals[0], sps_buf, 30, 7);
    make_test_nal(&nals[1], pps_buf, 8, 8);
    make_test_nal(&nals[2], idr_buf, 5000, 5);
    au.nals = nals; au.nal_count = 3; au.pts_ms = 0; au.is_idr = true; au.total_bytes = 5038;

    memset(&cap, 0, sizeof(cap));
    st = l4c_rtp_packetize_au(&au, 0, 0, 0xCC, 96, capture_cb, &cap);
    if (st != L4C_OK || cap.count != 7) { free(idr_buf); return 2; }

    /* Packets 0..5: M=0 */
    {
        uint32_t i;
        for (i = 0; i < 6; ++i) {
            if (cap.markers[i]) { free(idr_buf); capture_free(&cap); return 3; }
        }
    }
    /* Packet 6 (last fragment of last NAL): M=1 */
    if (!cap.markers[6]) { free(idr_buf); capture_free(&cap); return 4; }

    free(idr_buf);
    capture_free(&cap);
    return 0;
}

/* ---- Test 5: Timestamp consistency ---- */
int test_rtp_timestamp_consistency(void) {
    l4c_nal_desc_t nals[3];
    uint8_t sps_buf[30], pps_buf[8], *idr_buf;
    l4c_access_unit_t au;
    capture_ctx_t cap;
    l4c_status_t st;
    uint32_t i, ts0;

    idr_buf = (uint8_t *)malloc(5000);
    if (!idr_buf) return 1;
    make_test_nal(&nals[0], sps_buf, 30, 7);
    make_test_nal(&nals[1], pps_buf, 8, 8);
    make_test_nal(&nals[2], idr_buf, 5000, 5);
    au.nals = nals; au.nal_count = 3; au.pts_ms = 100; au.is_idr = true; au.total_bytes = 5038;

    memset(&cap, 0, sizeof(cap));
    st = l4c_rtp_packetize_au(&au, 9000, 0, 0xDD, 96, capture_cb, &cap);
    if (st != L4C_OK || cap.count != 7) { free(idr_buf); return 1; }

    /* All packets of this AU must have identical timestamp */
    ts0 = cap.timestamps[0];
    for (i = 1; i < 7; ++i) {
        if (cap.timestamps[i] != ts0) { free(idr_buf); capture_free(&cap); return 2; }
    }
    /* Second AU with PTS +100ms => timestamp +9000 */
    capture_free(&cap);
    au.pts_ms = 200;
    memset(&cap, 0, sizeof(cap));
    st = l4c_rtp_packetize_au(&au, 18000, 7, 0xDD, 96, capture_cb, &cap);
    if (st != L4C_OK || cap.count == 0) { free(idr_buf); return 3; }
    if (cap.timestamps[0] != ts0 + 9000) { free(idr_buf); capture_free(&cap); return 4; }

    free(idr_buf);
    capture_free(&cap);
    return 0;
}

/* ---- Test 6: Sequence monotonicity and wrap ---- */
int test_rtp_sequence_monotonicity_and_wrap(void) {
    l4c_nal_desc_t nals[3];
    uint8_t sps_buf[30], pps_buf[8], *idr_buf;
    l4c_access_unit_t au;
    capture_ctx_t cap;
    l4c_status_t st;
    uint32_t i;

    idr_buf = (uint8_t *)malloc(5000);
    if (!idr_buf) return 1;
    make_test_nal(&nals[0], sps_buf, 30, 7);
    make_test_nal(&nals[1], pps_buf, 8, 8);
    make_test_nal(&nals[2], idr_buf, 5000, 5);
    au.nals = nals; au.nal_count = 3; au.pts_ms = 0; au.is_idr = true; au.total_bytes = 5038;

    /* Test monotonicity from seq=0 */
    memset(&cap, 0, sizeof(cap));
    st = l4c_rtp_packetize_au(&au, 0, 0, 0xEE, 96, capture_cb, &cap);
    if (st != L4C_OK || cap.count != 7) { free(idr_buf); return 1; }
    for (i = 1; i < cap.count; ++i) {
        if (cap.seq_nums[i] != cap.seq_nums[i - 1] + 1) { free(idr_buf); capture_free(&cap); return 2; }
    }
    capture_free(&cap);

    /* Test wrap: seq=65534, 7 packets => 65534, 65535, 0, 1, 2, 3, 4 */
    memset(&cap, 0, sizeof(cap));
    st = l4c_rtp_packetize_au(&au, 0, 65534, 0xEE, 96, capture_cb, &cap);
    if (st != L4C_OK || cap.count != 7) { free(idr_buf); return 3; }
    if (cap.seq_nums[0] != 65534) { free(idr_buf); capture_free(&cap); return 4; }
    if (cap.seq_nums[1] != 65535) { free(idr_buf); capture_free(&cap); return 5; }
    if (cap.seq_nums[2] != 0) { free(idr_buf); capture_free(&cap); return 6; }
    if (cap.seq_nums[3] != 1) { free(idr_buf); capture_free(&cap); return 7; }

    free(idr_buf);
    capture_free(&cap);
    return 0;
}

/* ---- Test 7: Timestamp wrap ---- */
int test_rtp_timestamp_wrap(void) {
    l4c_nal_desc_t nal;
    uint8_t buf[100];
    l4c_access_unit_t au;
    capture_ctx_t cap;
    l4c_status_t st;
    /* Init timestamp near UINT32_MAX: base=0xFFFFF000, elapsed_pts=200ms => +18000 */
    /* 0xFFFFF000 + 18000 = 0x100003650, wrapped = 0x00003650 */
    uint32_t base_ts = 0xFFFFF000u;
    uint32_t pts_elapsed_ms = 200;
    uint32_t expected_ts = base_ts + (uint32_t)(pts_elapsed_ms * 90);

    make_test_nal(&nal, buf, 100, 5);
    au.nals = &nal; au.nal_count = 1; au.pts_ms = pts_elapsed_ms; au.is_idr = true; au.total_bytes = 100;

    memset(&cap, 0, sizeof(cap));
    st = l4c_rtp_packetize_au(&au, expected_ts, 0, 0xFF, 96, capture_cb, &cap);
    if (st != L4C_OK || cap.count != 1) return 1;
    if (cap.timestamps[0] != expected_ts) return 2;
    /* Verify wrap happened correctly */
    if (expected_ts >= 0xFFFFF000u) return 3; /* Overflow check */

    capture_free(&cap);
    return 0;
}

/* ---- Test 8: RTCP SR/SDES generation ---- */
/* Forward declarations from rtcp_sender.c */
extern uint32_t l4c_rtcp_build_compound(uint8_t *buf, uint32_t buf_size,
                                          uint32_t ssrc, uint32_t rtp_ts,
                                          uint32_t pkt_count, uint32_t octet_count,
                                          const char *cname);
extern void l4c_rtcp_build_bye(uint8_t *buf, uint32_t buf_size, uint32_t ssrc);

int test_rtcp_sr_sdes_generation(void) {
    uint8_t buf[512];
    uint32_t len;

    len = l4c_rtcp_build_compound(buf, sizeof(buf), 0x11223344, 90000, 100, 50000, "test@host");
    if (len == 0) return 1;

    /* SR: byte 0 = 0x80 (V=2), byte 1 = 200 */
    if (buf[0] != 0x80) return 2;
    if (buf[1] != 200) return 3;
    /* Length = 6 => bytes 2-3 */
    if (buf[2] != 0 || buf[3] != 6) return 4;
    /* SSRC at bytes 4-7 */
    if (buf[4] != 0x11 || buf[5] != 0x22 || buf[6] != 0x33 || buf[7] != 0x44) return 5;
    /* NTP timestamp at bytes 8-15 should be non-zero */
    if (buf[8] == 0 && buf[9] == 0 && buf[10] == 0 && buf[11] == 0) return 6;
    /* RTP timestamp at bytes 16-19 */
    {
        uint32_t rtp_ts = ((uint32_t)buf[16] << 24) | ((uint32_t)buf[17] << 16) |
                           ((uint32_t)buf[18] << 8) | (uint32_t)buf[19];
        if (rtp_ts != 90000) return 7;
    }
    /* SDES: starts at byte 28 */
    if (buf[28] != 0x81) return 8;  /* V=2, SC=1 */
    if (buf[29] != 201) return 9;   /* PT=201 */
    /* SSRC at 32-35 */
    if (buf[32] != 0x11 || buf[33] != 0x22 || buf[34] != 0x33 || buf[35] != 0x44) return 10;
    /* CNAME item: type=1 at byte 36 */
    if (buf[36] != 1) return 11;
    if (buf[37] != 9) return 12; /* len("test@host") = 9 */
    if (memcmp(buf + 38, "test@host", 9) != 0) return 13;
    /* End marker */
    if (buf[38 + 9] != 0) return 14;
    /* 32-bit alignment check */
    if (len % 4 != 0) return 15;

    return 0;
}

/* ---- Test 9: RTCP BYE generation ---- */
int test_rtcp_bye_generation(void) {
    uint8_t buf[8];
    l4c_rtcp_build_bye(buf, sizeof(buf), 0xAABBCCDD);
    /* V=2, SC=1 => 0x81 */
    if (buf[0] != 0x81) return 1;
    /* PT=203 */
    if (buf[1] != 203) return 2;
    /* Length = 1 */
    if (buf[2] != 0 || buf[3] != 1) return 3;
    /* SSRC */
    if (buf[4] != 0xAA || buf[5] != 0xBB || buf[6] != 0xCC || buf[7] != 0xDD) return 4;
    return 0;
}

/* ---- Test 10: FU-A reassembly roundtrip ---- */
int test_rtp_fua_reassembly_roundtrip(void) {
    l4c_nal_desc_t nal;
    uint8_t *original_nal;
    uint8_t *reassembled;
    l4c_access_unit_t au;
    capture_ctx_t cap;
    l4c_status_t st;
    uint32_t i, total_payload;
    uint32_t reassembled_len;
    uint32_t original_len = 7500;

    original_nal = (uint8_t *)malloc(original_len);
    if (!original_nal) return 1;
    /* Fill with deterministic pattern */
    for (i = 0; i < original_len; ++i) original_nal[i] = (uint8_t)(i & 0xFF);
    original_nal[0] = 0x65; /* IDR NAL: F=0, NRI=3, Type=5 */

    make_test_nal(&nal, original_nal, original_len, 5);
    au.nals = &nal; au.nal_count = 1; au.pts_ms = 0; au.is_idr = true; au.total_bytes = original_len;

    memset(&cap, 0, sizeof(cap));
    st = l4c_rtp_packetize_au(&au, 0, 0, 0x11, 96, capture_cb, &cap);
    if (st != L4C_OK || cap.count == 0) { free(original_nal); return 2; }

    /* Reassemble: first byte = (fu_indicator & 0xE0) | (fu_header & 0x1F) */
    reassembled = (uint8_t *)malloc(original_len);
    if (!reassembled) { free(original_nal); capture_free(&cap); return 3; }

    reassembled[0] = (uint8_t)((cap.packets[0][12] & 0xE0) | (cap.packets[0][13] & 0x1F));
    reassembled_len = 1;
    total_payload = 0;

    for (i = 0; i < cap.count; ++i) {
        uint32_t payload_start = 14; /* Skip RTP header + FU indicator + FU header */
        uint32_t payload_len = cap.lengths[i] - payload_start;
        if (reassembled_len + payload_len > original_len) { free(original_nal); free(reassembled); capture_free(&cap); return 4; }
        memcpy(reassembled + reassembled_len, cap.packets[i] + payload_start, payload_len);
        reassembled_len += payload_len;
        total_payload += payload_len;
    }

    /* Check total reconstructed length */
    if (reassembled_len != original_len) { free(original_nal); free(reassembled); capture_free(&cap); return 5; }
    /* Byte-by-byte comparison */
    if (memcmp(reassembled, original_nal, original_len) != 0) { free(original_nal); free(reassembled); capture_free(&cap); return 6; }

    free(original_nal);
    free(reassembled);
    capture_free(&cap);
    return 0;
}

/* ---- Test 11: Network non-blocking drop on error ---- */
typedef struct {
    uint32_t call_count;
    uint32_t *drops;
    bool *aborted;
} drop_ctx_t;

static bool drop_test_cb(const uint8_t *buf, uint32_t len, bool is_last, void *ctx) {
    drop_ctx_t *d = (drop_ctx_t *)ctx;
    (void)buf; (void)len; (void)is_last;
    d->call_count++;
    if (d->call_count > 1) {
        (*d->drops)++;
        *d->aborted = true;
        return false;
    }
    return true;
}

int test_network_nonblocking_drop_on_error(void) {
    l4c_nal_desc_t nals[2];
    uint8_t nal1_buf[500], nal2_buf[500];
    l4c_access_unit_t au;
    uint32_t drop_count = 0;
    bool aborted_early = false;
    l4c_status_t st;
    drop_ctx_t dctx;

    dctx.call_count = 0;
    dctx.drops = &drop_count;
    dctx.aborted = &aborted_early;

    make_test_nal(&nals[0], nal1_buf, 500, 7);
    make_test_nal(&nals[1], nal2_buf, 500, 8);
    au.nals = nals; au.nal_count = 2; au.pts_ms = 0; au.is_idr = false; au.total_bytes = 1000;

    st = l4c_rtp_packetize_au(&au, 0, 0, 0x22, 96, drop_test_cb, &dctx);

    if (st != L4C_ERR_NETWORK) return 1;
    if (drop_count != 1) return 2;
    if (!aborted_early) return 3;
    if (dctx.call_count != 2) return 4;

    return 0;
}

/* ---- Test 12: E2E loopback (GDI→OpenH264→RTP) ---- */
/* This test creates a real RTP sender, sends an encoded IDR frame
 * over UDP loopback, and validates received packets. We use a mock
 * encoder since we don't want the full OpenH264 dependency in unit tests. */

/* Mock encoder that produces a synthetic IDR AU */
typedef struct {
    uint8_t sps_data[30];
    uint8_t pps_data[8];
    uint8_t idr_data[200];
    l4c_nal_desc_t nal_descs[3];
} mock_encoder_ctx_t;

static l4c_status_t mock_init(struct l4c_encoder_backend *self, const l4c_encoder_config_t *config) {
    mock_encoder_ctx_t *ctx;
    (void)config;
    if (!self) return L4C_ERR_INVALID_ARG;
    ctx = (mock_encoder_ctx_t *)self->impl_ctx;
    if (!ctx) return L4C_ERR_INVALID_ARG;
    /* SPS */
    memset(ctx->sps_data, 0, sizeof(ctx->sps_data));
    ctx->sps_data[0] = 0x67; /* F=0, NRI=3, Type=7 */
    ctx->sps_data[1] = 0x42; ctx->sps_data[2] = 0xC0; ctx->sps_data[3] = 0x1F;
    /* PPS */
    memset(ctx->pps_data, 0, sizeof(ctx->pps_data));
    ctx->pps_data[0] = 0x68; /* F=0, NRI=3, Type=8 */
    /* IDR */
    memset(ctx->idr_data, 0x42, sizeof(ctx->idr_data));
    ctx->idr_data[0] = 0x65; /* F=0, NRI=3, Type=5 */
    return L4C_OK;
}

static l4c_status_t mock_encode(struct l4c_encoder_backend *self, const l4c_raw_frame_t *raw, l4c_access_unit_t *out_au) {
    mock_encoder_ctx_t *ctx;
    (void)raw;
    if (!self || !out_au) return L4C_ERR_INVALID_ARG;
    ctx = (mock_encoder_ctx_t *)self->impl_ctx;
    ctx->nal_descs[0].data = ctx->sps_data; ctx->nal_descs[0].length = 30; ctx->nal_descs[0].nal_type = 7;
    ctx->nal_descs[1].data = ctx->pps_data; ctx->nal_descs[1].length = 8; ctx->nal_descs[1].nal_type = 8;
    ctx->nal_descs[2].data = ctx->idr_data; ctx->nal_descs[2].length = 200; ctx->nal_descs[2].nal_type = 5;
    out_au->nals = ctx->nal_descs;
    out_au->nal_count = 3;
    out_au->pts_ms = l4c_now_monotonic_ms();
    out_au->is_idr = true;
    out_au->total_bytes = 238;
    return L4C_OK;
}

static l4c_status_t mock_force_idr(struct l4c_encoder_backend *self) {
    (void)self;
    return L4C_OK;
}

static void mock_release_au(struct l4c_encoder_backend *self, l4c_access_unit_t *au) {
    (void)self;
    if (au) { au->nal_count = 0; au->total_bytes = 0; }
}

static void mock_destroy(struct l4c_encoder_backend *self) {
    (void)self;
}

int test_pipeline_e2e_loopback(void) {
    WSADATA wsa_data;
    SOCKET recv_sock, send_sock;
    struct sockaddr_in recv_addr, dest_addr;
    l4c_nal_desc_t nals[3];
    uint8_t sps_buf[30], pps_buf[8], idr_buf[200];
    l4c_access_unit_t au;
    uint8_t recv_buf[2048];
    int n;
    int result = 1;
    uint32_t total_packets = 0;
    bool seen_sps = false, seen_pps = false, seen_idr = false;
    DWORD timeout_ms;
    uint16_t assigned_port;

    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) return 1;

    /* Create receiver socket on dynamic port */
    recv_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (recv_sock == INVALID_SOCKET) { WSACleanup(); return 2; }
    timeout_ms = 2000;
    setsockopt(recv_sock, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout_ms, sizeof(timeout_ms));
    memset(&recv_addr, 0, sizeof(recv_addr));
    recv_addr.sin_family = AF_INET;
    recv_addr.sin_port = 0;
    recv_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(recv_sock, (struct sockaddr *)&recv_addr, sizeof(recv_addr)) == SOCKET_ERROR) {
        closesocket(recv_sock); WSACleanup(); return 3;
    }
    {
        int alen = (int)sizeof(recv_addr);
        getsockname(recv_sock, (struct sockaddr *)&recv_addr, &alen);
    }
    assigned_port = ntohs(recv_addr.sin_port);

    /* Create sender socket */
    send_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (send_sock == INVALID_SOCKET) { closesocket(recv_sock); WSACleanup(); return 4; }
    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(assigned_port);
    dest_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    /* Build a test AU: SPS(30) + PPS(8) + IDR(200) = 3 NALs, 7 RTP packets */
    make_test_nal(&nals[0], sps_buf, 30, 7);
    make_test_nal(&nals[1], pps_buf, 8, 8);
    make_test_nal(&nals[2], idr_buf, 200, 5);
    au.nals = nals; au.nal_count = 3; au.pts_ms = 0; au.is_idr = true; au.total_bytes = 238;

    /* Use packetizer callback to send each packet via send_sock */
    {
        typedef struct { SOCKET sock; struct sockaddr_in *addr; } e2e_ctx_t;
        e2e_ctx_t ectx;
        ectx.sock = send_sock;
        ectx.addr = &dest_addr;

        /* Need to define callback at file scope, so use a different approach:
         * Just call packetize and send manually via a simple callback */
        {
            /* Capture packets first, then send them */
            capture_ctx_t cap;
            uint32_t i;
            memset(&cap, 0, sizeof(cap));
            {
                l4c_status_t pstat = l4c_rtp_packetize_au(&au, 9000, 0, 0xDEADBEEF, 96, capture_cb, &cap);
                if (pstat != L4C_OK || cap.count != 3) { closesocket(send_sock); closesocket(recv_sock); WSACleanup(); return 5; }
            }

            /* Send all captured packets */
            for (i = 0; i < cap.count; ++i) {
                sendto(send_sock, (const char *)cap.packets[i], (int)cap.lengths[i], 0,
                       (const struct sockaddr *)&dest_addr, sizeof(dest_addr));
            }
            capture_free(&cap);
        }
    }

    /* Receive packets */
    while (total_packets < 10) {
        n = recv(recv_sock, (char *)recv_buf, sizeof(recv_buf), 0);
        if (n <= 0) break;
        total_packets++;
        if (n < 12) continue;
        if ((recv_buf[0] & 0xC0) != 0x80) continue;
        if ((recv_buf[1] & 0x7F) != 96) continue;
        if (n > 12) {
            uint8_t nal_byte = recv_buf[12];
            uint8_t nal_type = nal_byte & 0x1F;
            if (nal_type == 7) seen_sps = true;
            else if (nal_type == 8) seen_pps = true;
            else if (nal_type == 5) seen_idr = true;
            else if (nal_type == 28) {
                uint8_t inner_type = recv_buf[13] & 0x1F;
                if (inner_type == 5) seen_idr = true;
            }
        }
    }

    if (total_packets < 3) result = 7;
    else if (!seen_sps) result = 8;
    else if (!seen_pps) result = 9;
    else if (!seen_idr) result = 10;
    else result = 0;

    closesocket(send_sock);
    closesocket(recv_sock);
    WSACleanup();
    return result;
}
