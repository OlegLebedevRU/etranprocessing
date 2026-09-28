#include <string.h>
#include "l4capture/ipc_protocol.h"

uint16_t l4c_read_u16_le(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}
uint32_t l4c_read_u32_le(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
uint64_t l4c_read_u64_le(const uint8_t *p) {
    return l4c_read_u32_le(p) | ((uint64_t)l4c_read_u32_le(p + 4) << 32);
}
void l4c_write_u16_le(uint8_t *p, uint16_t value) {
    p[0] = (uint8_t)value; p[1] = (uint8_t)(value >> 8);
}
void l4c_write_u32_le(uint8_t *p, uint32_t value) {
    p[0] = (uint8_t)value; p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16); p[3] = (uint8_t)(value >> 24);
}
void l4c_write_u64_le(uint8_t *p, uint64_t value) {
    l4c_write_u32_le(p, (uint32_t)value); l4c_write_u32_le(p + 4, (uint32_t)(value >> 32));
}

static bool valid_length(uint16_t type, uint32_t length) {
    switch (type) {
    case L4C_CMD_START: return length == 70;
    case L4C_CMD_RENEW_LEASE: return length == 24;
    case L4C_CMD_STOP: case L4C_CMD_FORCE_IDR: return length == 16;
    case L4C_EVENT_READY: return length == 30;
    case L4C_EVENT_METRICS: return length == 30;
    case L4C_EVENT_DEGRADED: return length == 4;
    case L4C_EVENT_ERROR: return length >= 6 && length <= L4C_IPC_MAX_PAYLOAD;
    default: return false;
    }
}

static bool valid_utf8(const uint8_t *p, size_t length) {
    size_t i = 0;
    while (i < length) {
        uint32_t c = p[i++], minimum;
        unsigned extra, j;
        if (c < 0x80) { if (!c) return false; continue; }
        if (c >= 0xc2 && c <= 0xdf) { extra = 1; minimum = 0x80; c &= 0x1f; }
        else if (c >= 0xe0 && c <= 0xef) { extra = 2; minimum = 0x800; c &= 0x0f; }
        else if (c >= 0xf0 && c <= 0xf4) { extra = 3; minimum = 0x10000; c &= 7; }
        else return false;
        if (length - i < extra) return false;
        for (j = 0; j < extra; ++j) {
            if ((p[i] & 0xc0) != 0x80) return false;
            c = (c << 6) | (p[i++] & 0x3f);
        }
        if (c < minimum || c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff)) return false;
    }
    return true;
}

static int32_t signed_u32(uint32_t value) {
    return value <= INT32_MAX ? (int32_t)value : (int32_t)((int64_t)value - INT64_C(4294967296));
}

l4c_status_t l4c_ipc_encode(const l4c_message_t *message, uint8_t *out, size_t capacity, size_t *written) {
    uint32_t length;
    uint8_t *p;
    if (!message || !out || !written) return L4C_ERR_INVALID_ARG;
    *written = 0;
    switch (message->type) {
    case L4C_CMD_START: length = 70; break;
    case L4C_CMD_RENEW_LEASE: length = 24; break;
    case L4C_CMD_STOP: case L4C_CMD_FORCE_IDR: length = 16; break;
    case L4C_EVENT_READY: case L4C_EVENT_METRICS: length = 30; break;
    case L4C_EVENT_DEGRADED: length = 4; break;
    case L4C_EVENT_ERROR:
        if (message->body.error.message_len > L4C_ERROR_TEXT_MAX ||
            !valid_utf8(message->body.error.message_utf8, message->body.error.message_len)) return L4C_ERR_PROTOCOL;
        length = 6u + message->body.error.message_len; break;
    default: return L4C_ERR_PROTOCOL;
    }
    if (capacity < L4C_IPC_HEADER_SIZE + length) return L4C_ERR_OVERFLOW;
    l4c_write_u16_le(out, L4C_IPC_VERSION);
    l4c_write_u16_le(out + 2, message->type);
    l4c_write_u32_le(out + 4, length);
    l4c_write_u64_le(out + 8, message->request_seq);
    p = out + L4C_IPC_HEADER_SIZE;
    switch (message->type) {
    case L4C_CMD_START: {
        const l4c_start_t *s = &message->body.start;
        memcpy(p, s->lease_id, 16); memcpy(p + 16, s->stream_id, 16);
        l4c_write_u32_le(p + 32, (uint32_t)s->source_rect.left);
        l4c_write_u32_le(p + 36, (uint32_t)s->source_rect.top);
        l4c_write_u32_le(p + 40, (uint32_t)s->source_rect.right);
        l4c_write_u32_le(p + 44, (uint32_t)s->source_rect.bottom);
        l4c_write_u64_le(p + 48, s->geometry_gen);
        l4c_write_u16_le(p + 56, s->profile_id);
        l4c_write_u16_le(p + 58, s->rtp_port);
        l4c_write_u16_le(p + 60, s->rtcp_port);
        l4c_write_u64_le(p + 62, s->deadline_tick_ms); break;
    }
    case L4C_CMD_RENEW_LEASE:
        memcpy(p, message->body.renew.lease_id, 16);
        l4c_write_u64_le(p + 16, message->body.renew.new_deadline_tick_ms); break;
    case L4C_CMD_STOP: case L4C_CMD_FORCE_IDR:
        memcpy(p, message->body.stream_id, 16); break;
    case L4C_EVENT_READY: {
        const l4c_ready_t *r = &message->body.ready;
        memcpy(p, r->stream_id, 16);
        l4c_write_u32_le(p + 16, r->actual_width); l4c_write_u32_le(p + 20, r->actual_height);
        l4c_write_u16_le(p + 24, r->actual_fps); l4c_write_u16_le(p + 26, r->capture_backend);
        l4c_write_u16_le(p + 28, r->encoder_backend); break;
    }
    case L4C_EVENT_METRICS: {
        const l4c_metrics_t *m = &message->body.metrics;
        l4c_write_u16_le(p, m->fps); l4c_write_u32_le(p + 2, m->bitrate_kbps);
        l4c_write_u32_le(p + 6, m->raw_drops); l4c_write_u32_le(p + 10, m->encoder_drops);
        l4c_write_u32_le(p + 14, m->transport_drops); l4c_write_u16_le(p + 18, m->encode_p95_ms);
        l4c_write_u16_le(p + 20, m->queue_depth); l4c_write_u32_le(p + 22, m->private_bytes_kb);
        l4c_write_u32_le(p + 26, m->gdi_handles); break;
    }
    case L4C_EVENT_DEGRADED:
        l4c_write_u16_le(p, message->body.degraded.degrade_state);
        l4c_write_u16_le(p + 2, message->body.degraded.reason); break;
    case L4C_EVENT_ERROR:
        l4c_write_u32_le(p, message->body.error.error_code);
        l4c_write_u16_le(p + 4, message->body.error.message_len);
        memcpy(p + 6, message->body.error.message_utf8, message->body.error.message_len); break;
    default: return L4C_ERR_PROTOCOL;
    }
    *written = L4C_IPC_HEADER_SIZE + length;
    return L4C_OK;
}

l4c_status_t l4c_ipc_decode(const uint8_t *bytes, size_t length, l4c_message_t *out) {
    const uint8_t *p;
    uint16_t type;
    uint32_t payload;
    if (!bytes || !out) return L4C_ERR_INVALID_ARG;
    if (length < L4C_IPC_HEADER_SIZE || l4c_read_u16_le(bytes) != L4C_IPC_VERSION) return L4C_ERR_PROTOCOL;
    type = l4c_read_u16_le(bytes + 2); payload = l4c_read_u32_le(bytes + 4);
    if (!valid_length(type, payload) || length != L4C_IPC_HEADER_SIZE + (size_t)payload) return L4C_ERR_PROTOCOL;
    memset(out, 0, sizeof(*out));
    out->type = type; out->request_seq = l4c_read_u64_le(bytes + 8);
    p = bytes + L4C_IPC_HEADER_SIZE;
    switch (type) {
    case L4C_CMD_START: {
        l4c_start_t *s = &out->body.start;
        memcpy(s->lease_id, p, 16); memcpy(s->stream_id, p + 16, 16);
        s->source_rect.left = signed_u32(l4c_read_u32_le(p + 32));
        s->source_rect.top = signed_u32(l4c_read_u32_le(p + 36));
        s->source_rect.right = signed_u32(l4c_read_u32_le(p + 40));
        s->source_rect.bottom = signed_u32(l4c_read_u32_le(p + 44));
        s->geometry_gen = l4c_read_u64_le(p + 48);
        s->profile_id = l4c_read_u16_le(p + 56); s->rtp_port = l4c_read_u16_le(p + 58);
        s->rtcp_port = l4c_read_u16_le(p + 60); s->deadline_tick_ms = l4c_read_u64_le(p + 62); break;
    }
    case L4C_CMD_RENEW_LEASE:
        memcpy(out->body.renew.lease_id, p, 16);
        out->body.renew.new_deadline_tick_ms = l4c_read_u64_le(p + 16); break;
    case L4C_CMD_STOP: case L4C_CMD_FORCE_IDR:
        memcpy(out->body.stream_id, p, 16); break;
    case L4C_EVENT_READY: {
        l4c_ready_t *r = &out->body.ready;
        memcpy(r->stream_id, p, 16);
        r->actual_width = l4c_read_u32_le(p + 16); r->actual_height = l4c_read_u32_le(p + 20);
        r->actual_fps = l4c_read_u16_le(p + 24); r->capture_backend = l4c_read_u16_le(p + 26);
        r->encoder_backend = l4c_read_u16_le(p + 28); break;
    }
    case L4C_EVENT_METRICS: {
        l4c_metrics_t *m = &out->body.metrics;
        m->fps = l4c_read_u16_le(p); m->bitrate_kbps = l4c_read_u32_le(p + 2);
        m->raw_drops = l4c_read_u32_le(p + 6); m->encoder_drops = l4c_read_u32_le(p + 10);
        m->transport_drops = l4c_read_u32_le(p + 14); m->encode_p95_ms = l4c_read_u16_le(p + 18);
        m->queue_depth = l4c_read_u16_le(p + 20); m->private_bytes_kb = l4c_read_u32_le(p + 22);
        m->gdi_handles = l4c_read_u32_le(p + 26); break;
    }
    case L4C_EVENT_DEGRADED:
        out->body.degraded.degrade_state = l4c_read_u16_le(p);
        out->body.degraded.reason = l4c_read_u16_le(p + 2); break;
    case L4C_EVENT_ERROR:
        out->body.error.error_code = l4c_read_u32_le(p);
        out->body.error.message_len = l4c_read_u16_le(p + 4);
        if ((uint32_t)out->body.error.message_len != payload - 6u || !valid_utf8(p + 6, payload - 6u)) return L4C_ERR_PROTOCOL;
        memcpy(out->body.error.message_utf8, p + 6, payload - 6u); break;
    default: return L4C_ERR_PROTOCOL;
    }
    return L4C_OK;
}

void l4c_ipc_parser_init(l4c_ipc_parser_t *parser) {
    memset(parser, 0, sizeof(*parser));
    parser->expected = L4C_IPC_HEADER_SIZE;
}

l4c_status_t l4c_ipc_feed(l4c_ipc_parser_t *parser, const uint8_t *bytes, size_t length, size_t *consumed, l4c_message_t *out, bool *ready) {
    if (!parser || (!bytes && length) || !consumed || !out || !ready) return L4C_ERR_INVALID_ARG;
    *consumed = 0; *ready = false;
    if (parser->failed) return L4C_ERR_PROTOCOL;
    while (*consumed < length) {
        size_t take = parser->expected - parser->used;
        if (take > length - *consumed) take = length - *consumed;
        memcpy(parser->bytes + parser->used, bytes + *consumed, take);
        parser->used += take; *consumed += take;
        if (parser->used == L4C_IPC_HEADER_SIZE && parser->expected == L4C_IPC_HEADER_SIZE) {
            uint32_t payload = l4c_read_u32_le(parser->bytes + 4);
            if (l4c_read_u16_le(parser->bytes) != L4C_IPC_VERSION ||
                !valid_length(l4c_read_u16_le(parser->bytes + 2), payload)) {
                parser->failed = true; return L4C_ERR_PROTOCOL;
            }
            parser->expected += payload;
        }
        if (parser->used == parser->expected) {
            l4c_status_t status = l4c_ipc_decode(parser->bytes, parser->used, out);
            if (status != L4C_OK) { parser->failed = true; return status; }
            parser->used = 0; parser->expected = L4C_IPC_HEADER_SIZE;
            *ready = true; return L4C_OK;
        }
    }
    return L4C_OK;
}

l4c_status_t l4c_ipc_eof(const l4c_ipc_parser_t *parser) {
    if (!parser) return L4C_ERR_INVALID_ARG;
    return parser->failed || parser->used ? L4C_ERR_PROTOCOL : L4C_ERR_PIPE_BROKEN;
}