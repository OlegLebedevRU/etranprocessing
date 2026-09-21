#include <string.h>
#include "l4capture/clock.h"
#include "l4capture/ipc_pipe.h"

l4c_status_t l4c_parse_handle(const char *text, HANDLE *out) {
    uintptr_t value = 0;
    const char *p;
    if (!text || !*text || !out) return L4C_ERR_INVALID_ARG;
    for (p = text; *p; ++p) {
        unsigned digit;
        if (*p < '0' || *p > '9') return L4C_ERR_INVALID_ARG;
        digit = (unsigned)(*p - '0');
        if (value > (UINTPTR_MAX - digit) / 10u) return L4C_ERR_OVERFLOW;
        value = value * 10u + digit;
    }
    if (!value || value >= UINTPTR_MAX - 15u) return L4C_ERR_INVALID_ARG;
    *out = (HANDLE)value;
    return L4C_OK;
}

static DWORD WINAPI writer_main(LPVOID context) {
    l4c_ipc_pipe_t *pipe = context;
    HANDLE waits[2] = {pipe->gate->stop_event, pipe->wake};
    while (WaitForSingleObject(waits[0], 0) == WAIT_TIMEOUT) {
        l4c_ipc_packet_t packet;
        bool have_packet = false;
        size_t offset = 0;
        memset(&packet, 0, sizeof(packet));
        AcquireSRWLockExclusive(&pipe->lock);
        if (pipe->count) {
            packet = pipe->queue[pipe->head];
            pipe->head = (pipe->head + 1u) % L4C_STATUS_QUEUE_CAPACITY;
            --pipe->count;
            pipe->writing = true;
            pipe->writing_since_ms = packet.queued_ms;
            have_packet = true;
        }
        ReleaseSRWLockExclusive(&pipe->lock);
        if (!have_packet) {
            if (WaitForMultipleObjects(2, waits, FALSE, INFINITE) == WAIT_FAILED) {
                l4c_safety_stop(pipe->gate, L4C_ERR_FATAL); break;
            }
            continue;
        }
        while (offset < packet.length && WaitForSingleObject(waits[0], 0) == WAIT_TIMEOUT) {
            DWORD written = 0;
            DWORD chunk = (DWORD)(packet.length - offset);
            if (chunk > 1024u) chunk = 1024u;
            if (!WriteFile(pipe->output, packet.bytes + offset, chunk, &written, NULL) || !written) {
                l4c_safety_stop(pipe->gate, L4C_ERR_PIPE_BROKEN); break;
            }
            offset += written;
        }
        AcquireSRWLockExclusive(&pipe->lock);
        pipe->writing = false;
        ReleaseSRWLockExclusive(&pipe->lock);
    }
    return 0;
}

l4c_status_t l4c_pipe_open(l4c_ipc_pipe_t *pipe, HANDLE input, HANDLE output, l4c_safety_gate_t *gate) {
    DWORD flags, available;
    if (!pipe || !gate || !gate->stop_event || input == output ||
        GetFileType(input) != FILE_TYPE_PIPE || GetFileType(output) != FILE_TYPE_PIPE ||
        !GetNamedPipeInfo(input, &flags, NULL, NULL, NULL) || (flags & PIPE_TYPE_MESSAGE) ||
        !GetNamedPipeInfo(output, &flags, NULL, NULL, NULL) || (flags & PIPE_TYPE_MESSAGE) ||
        !PeekNamedPipe(input, NULL, 0, NULL, &available, NULL)) return L4C_ERR_INVALID_ARG;
    memset(pipe, 0, sizeof(*pipe));
    InitializeSRWLock(&pipe->lock);
    l4c_ipc_parser_init(&pipe->parser);
    pipe->input = input; pipe->output = output; pipe->gate = gate;
    if (!SetHandleInformation(input, HANDLE_FLAG_INHERIT, 0) || !SetHandleInformation(output, HANDLE_FLAG_INHERIT, 0)) return L4C_ERR_FATAL;
    pipe->wake = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (!pipe->wake) return L4C_ERR_FATAL;
    pipe->writer = CreateThread(NULL, 0, writer_main, pipe, 0, NULL);
    if (!pipe->writer) { CloseHandle(pipe->wake); pipe->wake = NULL; return L4C_ERR_FATAL; }
    return L4C_OK;
}

l4c_status_t l4c_pipe_poll(l4c_ipc_pipe_t *pipe, uint64_t now) {
    uint8_t bytes[256];
    DWORD available = 0, received = 0;
    size_t offset = 0;
    if (!PeekNamedPipe(pipe->input, NULL, 0, NULL, &available, NULL)) {
        l4c_status_t status = GetLastError() == ERROR_BROKEN_PIPE ? l4c_ipc_eof(&pipe->parser) : L4C_ERR_PIPE_BROKEN;
        l4c_safety_stop(pipe->gate, status == L4C_ERR_PIPE_BROKEN ? L4C_OK : status);
        return status;
    }
    if (!available) return L4C_OK;
    if (available > sizeof(bytes)) available = sizeof(bytes);
    if (!ReadFile(pipe->input, bytes, available, &received, NULL) || !received) {
        l4c_status_t status = l4c_ipc_eof(&pipe->parser);
        l4c_safety_stop(pipe->gate, status == L4C_ERR_PIPE_BROKEN ? L4C_OK : status);
        return status;
    }
    while (offset < received && WaitForSingleObject(pipe->gate->stop_event, 0) == WAIT_TIMEOUT) {
        l4c_message_t message;
        size_t consumed;
        bool ready;
        l4c_status_t status = l4c_ipc_feed(&pipe->parser, bytes + offset, received - offset, &consumed, &message, &ready);
        if (status == L4C_OK && ready) status = l4c_safety_command(pipe->gate, &message, now);
        if (status != L4C_OK) { l4c_safety_stop(pipe->gate, status); return status; }
        offset += consumed;
    }
    return L4C_OK;
}

l4c_status_t l4c_pipe_enqueue(l4c_ipc_pipe_t *pipe, const l4c_message_t *message, uint64_t now) {
    l4c_ipc_packet_t packet;
    l4c_status_t status;
    size_t i, index;
    if (!pipe || !message || message->type < L4C_EVENT_READY) return L4C_ERR_INVALID_ARG;
    status = l4c_ipc_encode(message, packet.bytes, sizeof(packet.bytes), &packet.length);
    if (status != L4C_OK) { l4c_safety_stop(pipe->gate, status); return status; }
    packet.type = message->type; packet.queued_ms = now;
    AcquireSRWLockExclusive(&pipe->lock);
    if (WaitForSingleObject(pipe->gate->stop_event, 0) != WAIT_TIMEOUT) {
        ReleaseSRWLockExclusive(&pipe->lock); return L4C_ERR_FATAL;
    }
    if (message->type == L4C_EVENT_METRICS) {
        for (i = 0; i < pipe->count; ++i) {
            index = (pipe->head + i) % L4C_STATUS_QUEUE_CAPACITY;
            if (pipe->queue[index].type == L4C_EVENT_METRICS) {
                packet.queued_ms = pipe->queue[index].queued_ms; /* Замена не сбрасывает таймаут. */
                pipe->queue[index] = packet;
                ReleaseSRWLockExclusive(&pipe->lock); SetEvent(pipe->wake); return L4C_OK;
            }
        }
    }
    if (pipe->count == L4C_STATUS_QUEUE_CAPACITY) {
        ReleaseSRWLockExclusive(&pipe->lock);
        l4c_safety_stop(pipe->gate, L4C_ERR_OVERFLOW); return L4C_ERR_OVERFLOW;
    }
    index = (pipe->head + pipe->count) % L4C_STATUS_QUEUE_CAPACITY;
    pipe->queue[index] = packet; ++pipe->count;
    ReleaseSRWLockExclusive(&pipe->lock);
    SetEvent(pipe->wake);
    return L4C_OK;
}

bool l4c_pipe_stalled(l4c_ipc_pipe_t *pipe, uint64_t now) {
    bool stalled = false;
    uint64_t oldest = 0;
    AcquireSRWLockShared(&pipe->lock);
    if (pipe->writing) oldest = pipe->writing_since_ms;
    else if (pipe->count) oldest = pipe->queue[pipe->head].queued_ms;
    if (pipe->writing || pipe->count) stalled = now < oldest || now - oldest >= L4C_WRITER_TIMEOUT_MS;
    ReleaseSRWLockShared(&pipe->lock);
    return stalled;
}

void l4c_pipe_close(l4c_ipc_pipe_t *pipe) {
    if (pipe->gate) l4c_safety_stop(pipe->gate, L4C_OK);
    if (pipe->writer) {
        uint64_t until = l4c_now_monotonic_ms() + 200u;
        /* Повторная отмена закрывает гонку между проверкой stop и входом в WriteFile. */
        do {
            CancelSynchronousIo(pipe->writer);
            if (WaitForSingleObject(pipe->writer, 10) == WAIT_OBJECT_0) break;
        } while (l4c_now_monotonic_ms() < until);
        if (WaitForSingleObject(pipe->writer, 0) != WAIT_OBJECT_0) {
            /* Нельзя освобождать память живого writer или убивать отдельный поток. */
            ExitProcess((UINT)L4C_ERR_FATAL);
        }
        CloseHandle(pipe->writer); pipe->writer = NULL;
    }
    pipe->count = 0;
    if (pipe->wake) CloseHandle(pipe->wake);
    pipe->wake = NULL;
    if (pipe->input) CloseHandle(pipe->input);
    if (pipe->output) CloseHandle(pipe->output);
    pipe->input = NULL; pipe->output = NULL;
}