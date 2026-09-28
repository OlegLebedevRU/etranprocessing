#ifndef L4C_IPC_PIPE_H
#define L4C_IPC_PIPE_H
#include "safety_gate.h"

typedef struct {
    uint8_t bytes[L4C_IPC_MAX_FRAME];
    size_t length;
    uint16_t type;
    uint64_t queued_ms;
} l4c_ipc_packet_t;

typedef struct {
    HANDLE input, output, writer, wake;
    SRWLOCK lock;
    l4c_safety_gate_t *gate;
    l4c_ipc_packet_t queue[L4C_STATUS_QUEUE_CAPACITY];
    size_t head, count;
    bool writing;
    uint64_t writing_since_ms;
    l4c_ipc_parser_t parser;
} l4c_ipc_pipe_t;

l4c_status_t l4c_parse_handle(const char *text, HANDLE *out);
l4c_status_t l4c_pipe_open(l4c_ipc_pipe_t *pipe, HANDLE input, HANDLE output, l4c_safety_gate_t *gate);
/* Ограниченная работа: не более одного фрагмента за вызов. */
l4c_status_t l4c_pipe_poll(l4c_ipc_pipe_t *pipe, uint64_t now);
l4c_status_t l4c_pipe_enqueue(l4c_ipc_pipe_t *pipe, const l4c_message_t *message, uint64_t now);
bool l4c_pipe_stalled(l4c_ipc_pipe_t *pipe, uint64_t now);
void l4c_pipe_close(l4c_ipc_pipe_t *pipe);
#endif