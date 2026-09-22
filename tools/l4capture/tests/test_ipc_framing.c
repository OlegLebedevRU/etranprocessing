#include <string.h>
#include "l4capture/ipc_protocol.h"

/* Helper: build a minimal CMD_STOP message for testing. */
static void make_stop(l4c_message_t *msg, uint64_t seq) {
    memset(msg, 0, sizeof(*msg));
    msg->type = L4C_CMD_STOP;
    msg->request_seq = seq;
    memset(msg->body.stream_id, 0xAB, 16);
}

int test_ipc_pack_unpack(void) {
    l4c_message_t orig, decoded;
    uint8_t buf[L4C_IPC_MAX_FRAME];
    size_t written;
    l4c_status_t s;
    make_stop(&orig, 42);
    s = l4c_ipc_encode(&orig, buf, sizeof(buf), &written);
    if (s != L4C_OK) return 1;
    if (written != L4C_IPC_HEADER_SIZE + 16) return 2;
    s = l4c_ipc_decode(buf, written, &decoded);
    if (s != L4C_OK) return 3;
    if (decoded.type != L4C_CMD_STOP || decoded.request_seq != 42) return 4;
    if (memcmp(decoded.body.stream_id, orig.body.stream_id, 16) != 0) return 5;
    return 0;
}

int test_ipc_fragmentation(void) {
    l4c_message_t orig, decoded;
    uint8_t buf[L4C_IPC_MAX_FRAME];
    l4c_ipc_parser_t parser;
    size_t written, consumed;
    bool ready;
    l4c_status_t s;
    int offsets[] = {1, 3, 7};
    int i;
    ready = false;
    make_stop(&orig, 100);
    s = l4c_ipc_encode(&orig, buf, sizeof(buf), &written);
    if (s != L4C_OK) return 1;
    /* Feed in fragments of 1, 3, 7 bytes. */
    l4c_ipc_parser_init(&parser);
    for (i = 0; i < 3; ++i) {
        size_t off = 0;
        while (off < written) {
            size_t chunk = (size_t)offsets[i];
            if (chunk > written - off) chunk = written - off;
            ready = false;
            s = l4c_ipc_feed(&parser, buf + off, chunk, &consumed, &decoded, &ready);
            if (s != L4C_OK) return 2 + i;
            off += consumed;
            if (ready) {
                if (decoded.type != L4C_CMD_STOP || decoded.request_seq != 100) return 10 + i;
            }
        }
        if (!ready) return 20 + i;
        l4c_ipc_parser_init(&parser);
    }
    return 0;
}

int test_ipc_bad_version(void) {
    l4c_message_t orig, decoded;
    uint8_t buf[L4C_IPC_MAX_FRAME];
    size_t written;
    l4c_status_t s;
    make_stop(&orig, 1);
    s = l4c_ipc_encode(&orig, buf, sizeof(buf), &written);
    if (s != L4C_OK) return 1;
    /* Corrupt version field. */
    buf[0] = 0x02; buf[1] = 0x00;
    s = l4c_ipc_decode(buf, written, &decoded);
    if (s != L4C_ERR_PROTOCOL) return 2;
    return 0;
}

int test_ipc_overflow_length(void) {
    l4c_message_t decoded;
    uint8_t buf[L4C_IPC_HEADER_SIZE];
    l4c_status_t s;
    /* Craft header with payload_length > L4C_IPC_MAX_PAYLOAD. */
    l4c_write_u16_le(buf, L4C_IPC_VERSION);
    l4c_write_u16_le(buf + 2, L4C_CMD_STOP);
    l4c_write_u32_le(buf + 4, L4C_IPC_MAX_PAYLOAD + 1);
    l4c_write_u64_le(buf + 8, 1);
    s = l4c_ipc_decode(buf, sizeof(buf), &decoded);
    if (s != L4C_ERR_PROTOCOL) return 1;
    return 0;
}

int test_ipc_eof(void) {
    l4c_ipc_parser_t parser;
    l4c_status_t s;
    l4c_ipc_parser_init(&parser);
    s = l4c_ipc_eof(&parser);
    if (s != L4C_ERR_PIPE_BROKEN) return 1;
    parser.failed = true;
    s = l4c_ipc_eof(&parser);
    if (s != L4C_ERR_PROTOCOL) return 2;
    return 0;
}

int test_ipc_roundtrip_all_types(void) {
    uint8_t buf[L4C_IPC_MAX_FRAME];
    size_t written;
    l4c_message_t decoded;
    l4c_status_t s;
    /* CMD_START */
    l4c_message_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = L4C_CMD_START; msg.request_seq = 1;
    memset(msg.body.start.lease_id, 1, 16);
    memset(msg.body.start.stream_id, 2, 16);
    msg.body.start.source_rect.left = 0; msg.body.start.source_rect.top = 0;
    msg.body.start.source_rect.right = 1920; msg.body.start.source_rect.bottom = 1080;
    msg.body.start.geometry_gen = 7;
    msg.body.start.profile_id = 1; msg.body.start.rtp_port = 5004; msg.body.start.rtcp_port = 5005;
    msg.body.start.deadline_tick_ms = 100000;
    s = l4c_ipc_encode(&msg, buf, sizeof(buf), &written);
    if (s != L4C_OK) return 1;
    s = l4c_ipc_decode(buf, written, &decoded);
    if (s != L4C_OK || decoded.type != L4C_CMD_START) return 2;
    if (decoded.body.start.profile_id != 1) return 3;
    /* CMD_RENEW_LEASE */
    memset(&msg, 0, sizeof(msg));
    msg.type = L4C_CMD_RENEW_LEASE; msg.request_seq = 2;
    memset(msg.body.renew.lease_id, 1, 16);
    msg.body.renew.new_deadline_tick_ms = 200000;
    s = l4c_ipc_encode(&msg, buf, sizeof(buf), &written);
    if (s != L4C_OK) return 4;
    s = l4c_ipc_decode(buf, written, &decoded);
    if (s != L4C_OK || decoded.type != L4C_CMD_RENEW_LEASE) return 5;
    /* EVENT_READY */
    memset(&msg, 0, sizeof(msg));
    msg.type = L4C_EVENT_READY; msg.request_seq = 3;
    memset(msg.body.ready.stream_id, 3, 16);
    msg.body.ready.actual_width = 640; msg.body.ready.actual_height = 480;
    msg.body.ready.actual_fps = 30; msg.body.ready.capture_backend = 1; msg.body.ready.encoder_backend = 1;
    s = l4c_ipc_encode(&msg, buf, sizeof(buf), &written);
    if (s != L4C_OK) return 6;
    s = l4c_ipc_decode(buf, written, &decoded);
    if (s != L4C_OK || decoded.type != L4C_EVENT_READY) return 7;
    if (decoded.body.ready.actual_width != 640) return 8;
    /* EVENT_DEGRADED */
    memset(&msg, 0, sizeof(msg));
    msg.type = L4C_EVENT_DEGRADED; msg.request_seq = 4;
    msg.body.degraded.degrade_state = 2; msg.body.degraded.reason = 1;
    s = l4c_ipc_encode(&msg, buf, sizeof(buf), &written);
    if (s != L4C_OK) return 9;
    s = l4c_ipc_decode(buf, written, &decoded);
    if (s != L4C_OK || decoded.type != L4C_EVENT_DEGRADED) return 10;
    /* EVENT_ERROR */
    memset(&msg, 0, sizeof(msg));
    msg.type = L4C_EVENT_ERROR; msg.request_seq = 5;
    msg.body.error.error_code = 42;
    memcpy(msg.body.error.message_utf8, "test", 4);
    msg.body.error.message_len = 4;
    s = l4c_ipc_encode(&msg, buf, sizeof(buf), &written);
    if (s != L4C_OK) return 11;
    s = l4c_ipc_decode(buf, written, &decoded);
    if (s != L4C_OK || decoded.type != L4C_EVENT_ERROR) return 12;
    if (decoded.body.error.error_code != 42 || decoded.body.error.message_len != 4) return 13;
    return 0;
}
