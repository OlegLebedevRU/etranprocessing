#ifndef L4C_IPC_PROTOCOL_H
#define L4C_IPC_PROTOCOL_H
#include "limits.h"
#include "telemetry.h"
#define L4C_IPC_VERSION 1u
#define L4C_IPC_HEADER_SIZE 16u
#define L4C_IPC_MAX_FRAME (L4C_IPC_HEADER_SIZE + L4C_IPC_MAX_PAYLOAD)
#define L4C_ERROR_TEXT_MAX (L4C_IPC_MAX_PAYLOAD - 6u)

typedef enum {
    L4C_CMD_START = 0x0001, L4C_CMD_RENEW_LEASE = 0x0002,
    L4C_CMD_STOP = 0x0003, L4C_CMD_FORCE_IDR = 0x0004,
    L4C_EVENT_READY = 0x0101, L4C_EVENT_METRICS = 0x0102,
    L4C_EVENT_DEGRADED = 0x0103, L4C_EVENT_ERROR = 0x0104
} l4c_message_type_t;

typedef struct {
    uint8_t lease_id[16], stream_id[16];
    l4c_rect_t source_rect;
    uint64_t geometry_gen;
    uint16_t profile_id, rtp_port, rtcp_port;
    uint64_t deadline_tick_ms;
} l4c_start_t;
typedef struct { uint8_t lease_id[16]; uint64_t new_deadline_tick_ms; } l4c_renew_t;
typedef struct {
    uint8_t stream_id[16];
    uint32_t actual_width, actual_height;
    uint16_t actual_fps, capture_backend, encoder_backend;
} l4c_ready_t;

typedef struct {
    uint16_t type;
    uint64_t request_seq;
    union {
        l4c_start_t start;
        l4c_renew_t renew;
        uint8_t stream_id[16];
        l4c_ready_t ready;
        l4c_metrics_t metrics;
        struct { uint16_t degrade_state, reason; } degraded;
        struct { uint32_t error_code; uint16_t message_len; uint8_t message_utf8[L4C_ERROR_TEXT_MAX]; } error;
    } body;
} l4c_message_t;

/* Парсер имеет фиксированный буфер, никаких выделений по wire length. */
typedef struct {
    uint8_t bytes[L4C_IPC_MAX_FRAME];
    size_t used, expected;
    bool failed;
} l4c_ipc_parser_t;

uint16_t l4c_read_u16_le(const uint8_t *p);
uint32_t l4c_read_u32_le(const uint8_t *p);
uint64_t l4c_read_u64_le(const uint8_t *p);
void l4c_write_u16_le(uint8_t *p, uint16_t value);
void l4c_write_u32_le(uint8_t *p, uint32_t value);
void l4c_write_u64_le(uint8_t *p, uint64_t value);
l4c_status_t l4c_ipc_encode(const l4c_message_t *message, uint8_t *out, size_t capacity, size_t *written);
l4c_status_t l4c_ipc_decode(const uint8_t *bytes, size_t length, l4c_message_t *out);
void l4c_ipc_parser_init(l4c_ipc_parser_t *parser);
l4c_status_t l4c_ipc_feed(l4c_ipc_parser_t *parser, const uint8_t *bytes, size_t length, size_t *consumed, l4c_message_t *out, bool *ready);
l4c_status_t l4c_ipc_eof(const l4c_ipc_parser_t *parser);
#endif