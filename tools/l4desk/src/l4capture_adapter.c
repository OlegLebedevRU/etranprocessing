#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "l4capture_adapter.h"
#include "log.h"
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* IPC wire format: 16-byte LE header + payload <= 4096 */
#define IPC_HEADER_SIZE   16u
#define IPC_MAX_PAYLOAD   4096u
#define IPC_MAX_FRAME     (IPC_HEADER_SIZE + IPC_MAX_PAYLOAD)
#define IPC_VERSION       1u

/* Message types (matching l4capture ipc_protocol.h) */
#define CMD_START         0x0001
#define CMD_RENEW_LEASE   0x0002
#define CMD_STOP          0x0003
#define CMD_FORCE_IDR     0x0004
#define EVENT_READY       0x0101
#define EVENT_METRICS     0x0102
#define EVENT_DEGRADED    0x0103
#define EVENT_ERROR       0x0104

/* Recovery limits */
#define MAX_RESTARTS      5
#define RESTART_WINDOW_MS 600000u
#define MAX_BACKOFF_MS    16000u
#define JITTER_MAX_MS     500u

/* Profile IDs */
#define PROFILE_ID_480P   1
#define PROFILE_ID_540P   2
#define PROFILE_ID_720P   3

typedef struct {
    /* Process handles */
    HANDLE hProcess;
    HANDLE hJob;
    HANDLE hStdinWrite;
    HANDLE hStdoutRead;
    DWORD  child_pid;

    /* IPC read buffer */
    uint8_t read_buf[IPC_MAX_FRAME];
    size_t  read_buf_used;

    /* Stream state */
    l4d_adapter_state_t state;
    char lease_id[64];
    char stream_id[64];
    uint64_t current_deadline_tick_ms;
    uint64_t last_renew_seq;

    /* Metrics from l4capture */
    l4d_backend_metrics_t metrics;
    bool metrics_valid;
    uint64_t last_event_tick;
    uint64_t request_seq;

    /* Recovery */
    uint32_t recovery_count;
    uint64_t restart_timestamps[MAX_RESTARTS];
    uint32_t restart_timestamp_idx;
    uint64_t next_restart_tick;
    bool restart_pending;

    /* Stream params (saved for restart) */
    l4d_stream_params_t saved_params;
    bool has_saved_params;

    /* Config */
    l4d_l4capture_adapter_cfg_t cfg;
} adapter_ctx_t;

static void cleanup_process(adapter_ctx_t *ctx) {
    if (ctx->hStdinWrite) { CloseHandle(ctx->hStdinWrite); ctx->hStdinWrite = NULL; }
    if (ctx->hStdoutRead) { CloseHandle(ctx->hStdoutRead); ctx->hStdoutRead = NULL; }
    if (ctx->hProcess) {
        if (WaitForSingleObject(ctx->hProcess, 0) == WAIT_TIMEOUT) {
            TerminateProcess(ctx->hProcess, 1);
            WaitForSingleObject(ctx->hProcess, 2000);
        }
        CloseHandle(ctx->hProcess);
        ctx->hProcess = NULL;
    }
    if (ctx->hJob) { CloseHandle(ctx->hJob); ctx->hJob = NULL; }
    ctx->child_pid = 0;
}

static bool is_session_interactive(void) {
    DWORD session_id = 0;
    if (!ProcessIdToSessionId(GetCurrentProcessId(), &session_id)) return false;
    return (session_id != 0);
}

static void write_u16_le(uint8_t *p, uint16_t v) { p[0]=(uint8_t)(v&0xFF); p[1]=(uint8_t)((v>>8)&0xFF); }
static void write_u32_le(uint8_t *p, uint32_t v) { p[0]=(uint8_t)(v&0xFF); p[1]=(uint8_t)((v>>8)&0xFF); p[2]=(uint8_t)((v>>16)&0xFF); p[3]=(uint8_t)((v>>24)&0xFF); }
static void write_u64_le(uint8_t *p, uint64_t v) { for(int i=0;i<8;i++) p[i]=(uint8_t)((v>>(i*8))&0xFF); }
static uint16_t read_u16_le(const uint8_t *p) { return (uint16_t)(p[0]|((uint16_t)p[1]<<8)); }
static uint32_t read_u32_le(const uint8_t *p) { return (uint32_t)(p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24)); }
static uint64_t read_u64_le(const uint8_t *p) { uint64_t v=0; for(int i=0;i<8;i++) v|=((uint64_t)p[i])<<(i*8); return v; }

static bool ipc_send(HANDLE hWrite, uint16_t type, const uint8_t *payload, uint32_t payload_len, uint64_t seq) {
    uint8_t frame[IPC_MAX_FRAME];
    if (payload_len > IPC_MAX_PAYLOAD) return false;
    write_u16_le(frame + 0, IPC_VERSION);
    write_u16_le(frame + 2, type);
    write_u32_le(frame + 4, payload_len);
    write_u64_le(frame + 8, seq);
    if (payload_len > 0 && payload) memcpy(frame + IPC_HEADER_SIZE, payload, payload_len);
    DWORD written = 0;
    DWORD to_write = IPC_HEADER_SIZE + payload_len;
    return WriteFile(hWrite, frame, to_write, &written, NULL) && written == to_write;
}

static bool ipc_try_read(adapter_ctx_t *ctx, uint16_t *out_type, uint8_t *out_payload, uint32_t *out_payload_len, uint64_t *out_seq) {
    *out_type = 0; *out_payload_len = 0; *out_seq = 0;

    /* Peek available bytes */
    DWORD avail = 0;
    if (!PeekNamedPipe(ctx->hStdoutRead, NULL, 0, NULL, &avail, NULL) || avail == 0) return false;

    /* Read available bytes into buffer */
    DWORD bytes_read = 0;
    size_t space = sizeof(ctx->read_buf) - ctx->read_buf_used;
    if (space == 0) { ctx->read_buf_used = 0; return false; }
    DWORD to_read = (avail < space) ? avail : (DWORD)space;
    if (!ReadFile(ctx->hStdoutRead, ctx->read_buf + ctx->read_buf_used, to_read, &bytes_read, NULL) || bytes_read == 0) return false;
    ctx->read_buf_used += bytes_read;

    /* Need at least a header */
    if (ctx->read_buf_used < IPC_HEADER_SIZE) return false;

    uint32_t payload_len = read_u32_le(ctx->read_buf + 4);
    if (payload_len > IPC_MAX_PAYLOAD) { ctx->read_buf_used = 0; return false; }
    uint32_t frame_size = IPC_HEADER_SIZE + payload_len;
    if (ctx->read_buf_used < frame_size) return false;

    /* Decode complete frame */
    uint16_t version = read_u16_le(ctx->read_buf + 0);
    if (version != IPC_VERSION) { ctx->read_buf_used = 0; return false; }
    *out_type = read_u16_le(ctx->read_buf + 2);
    *out_payload_len = payload_len;
    *out_seq = read_u64_le(ctx->read_buf + 8);
    if (payload_len > 0) memcpy(out_payload, ctx->read_buf + IPC_HEADER_SIZE, payload_len);

    /* Remove consumed frame from buffer */
    size_t remaining = ctx->read_buf_used - frame_size;
    if (remaining > 0) memmove(ctx->read_buf, ctx->read_buf + frame_size, remaining);
    ctx->read_buf_used = remaining;

    return true;
}

static bool launch_process(adapter_ctx_t *ctx, const l4d_stream_params_t *params) {
    (void)params;
    if (!is_session_interactive()) {
        log_error("l4capture_adapter: cannot launch in Session 0");
        return false;
    }

    /* Create anonymous pipes */
    SECURITY_ATTRIBUTES sa = { sizeof(SECURITY_ATTRIBUTES), NULL, TRUE };
    HANDLE hStdinRead = NULL, hStdinWrite = NULL;
    HANDLE hStdoutRead = NULL, hStdoutWrite = NULL;

    if (!CreatePipe(&hStdinRead, &hStdinWrite, &sa, 0)) {
        log_error("l4capture_adapter: CreatePipe stdin failed");
        return false;
    }
    if (!CreatePipe(&hStdoutRead, &hStdoutWrite, &sa, 0)) {
        CloseHandle(hStdinRead); CloseHandle(hStdinWrite);
        log_error("l4capture_adapter: CreatePipe stdout failed");
        return false;
    }

    /* Configure handle inheritance: only child-side ends inherit */
    SetHandleInformation(hStdinRead, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
    SetHandleInformation(hStdoutWrite, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
    SetHandleInformation(hStdinWrite, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(hStdoutRead, HANDLE_FLAG_INHERIT, 0);

    /* Create Job Object */
    HANDLE hJob = CreateJobObject(NULL, NULL);
    if (!hJob) {
        CloseHandle(hStdinRead); CloseHandle(hStdinWrite);
        CloseHandle(hStdoutRead); CloseHandle(hStdoutWrite);
        log_error("l4capture_adapter: CreateJobObject failed");
        return false;
    }
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli = {0};
    jeli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(hJob, JobObjectExtendedLimitInformation, &jeli, sizeof(jeli))) {
        CloseHandle(hJob);
        CloseHandle(hStdinRead); CloseHandle(hStdinWrite);
        CloseHandle(hStdoutRead); CloseHandle(hStdoutWrite);
        log_error("l4capture_adapter: SetInformationJobObject failed");
        return false;
    }

    /* Build command line */
    wchar_t cmdline[1024];
    _snwprintf_s(cmdline, sizeof(cmdline)/sizeof(wchar_t), _TRUNCATE,
        L"\"%s\" --pipe-in=%lu --pipe-out=%lu",
        ctx->cfg.exe_path,
        (unsigned long)(uintptr_t)hStdinRead,
        (unsigned long)(uintptr_t)hStdoutWrite);

    /* Setup STARTUPINFOEX with handle allowlist */
    SIZE_T attr_size = 0;
    InitializeProcThreadAttributeList(NULL, 1, 0, &attr_size);
    LPPROC_THREAD_ATTRIBUTE_LIST attr_list = (LPPROC_THREAD_ATTRIBUTE_LIST)HeapAlloc(GetProcessHeap(), 0, attr_size);
    if (!attr_list) {
        CloseHandle(hJob);
        CloseHandle(hStdinRead); CloseHandle(hStdinWrite);
        CloseHandle(hStdoutRead); CloseHandle(hStdoutWrite);
        return false;
    }
    if (!InitializeProcThreadAttributeList(attr_list, 1, 0, &attr_size)) {
        HeapFree(GetProcessHeap(), 0, attr_list);
        CloseHandle(hJob);
        CloseHandle(hStdinRead); CloseHandle(hStdinWrite);
        CloseHandle(hStdoutRead); CloseHandle(hStdoutWrite);
        return false;
    }
    HANDLE inherit_handles[2] = { hStdinRead, hStdoutWrite };
    if (!UpdateProcThreadAttribute(attr_list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                   inherit_handles, sizeof(inherit_handles), NULL, NULL)) {
        DeleteProcThreadAttributeList(attr_list);
        HeapFree(GetProcessHeap(), 0, attr_list);
        CloseHandle(hJob);
        CloseHandle(hStdinRead); CloseHandle(hStdinWrite);
        CloseHandle(hStdoutRead); CloseHandle(hStdoutWrite);
        return false;
    }

    STARTUPINFOEXW si_ex = {0};
    si_ex.StartupInfo.cb = sizeof(si_ex);
    si_ex.StartupInfo.dwFlags = STARTF_USESHOWWINDOW;
    si_ex.StartupInfo.wShowWindow = SW_HIDE;
    si_ex.lpAttributeList = attr_list;

    PROCESS_INFORMATION pi = {0};
    BOOL ok = CreateProcessW(ctx->cfg.exe_path, cmdline, NULL, NULL, TRUE,
                             CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT | CREATE_NO_WINDOW,
                             NULL, NULL, &si_ex.StartupInfo, &pi);

    DeleteProcThreadAttributeList(attr_list);
    HeapFree(GetProcessHeap(), 0, attr_list);

    /* Close child-side ends in parent immediately */
    CloseHandle(hStdinRead);
    CloseHandle(hStdoutWrite);

    if (!ok) {
        CloseHandle(hJob);
        CloseHandle(hStdinWrite); CloseHandle(hStdoutRead);
        log_error("l4capture_adapter: CreateProcess failed (err=%lu)", GetLastError());
        return false;
    }

    /* Assign to Job Object */
    if (!AssignProcessToJobObject(hJob, pi.hProcess)) {
        TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess, 2000);
        CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
        CloseHandle(hJob); CloseHandle(hStdinWrite); CloseHandle(hStdoutRead);
        log_error("l4capture_adapter: AssignProcessToJobObject failed");
        return false;
    }

    /* Resume child */
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);

    ctx->hProcess = pi.hProcess;
    ctx->hJob = hJob;
    ctx->hStdinWrite = hStdinWrite;
    ctx->hStdoutRead = hStdoutRead;
    ctx->child_pid = pi.dwProcessId;
    ctx->read_buf_used = 0;

    log_info("l4capture_adapter: launched l4capture.exe (pid=%lu)", (unsigned long)pi.dwProcessId);
    return true;
}

static bool wait_for_ready(adapter_ctx_t *ctx, uint32_t timeout_ms) {
    uint64_t deadline = GetTickCount64() + timeout_ms;
    while (GetTickCount64() < deadline) {
        uint16_t type; uint8_t payload[IPC_MAX_PAYLOAD]; uint32_t plen; uint64_t seq;
        if (ipc_try_read(ctx, &type, payload, &plen, &seq)) {
            if (type == EVENT_READY && plen >= 28) {
                /* Parse EVENT_READY: stream_id(16) + w(4) + h(4) + fps(2) + cap_be(2) + enc_be(2) */
                ctx->metrics.actual_width = (uint16_t)read_u32_le(payload + 16);
                ctx->metrics.actual_height = (uint16_t)read_u32_le(payload + 20);
                ctx->metrics.fps = read_u16_le(payload + 24);
                ctx->last_event_tick = GetTickCount64();
                return true;
            }
            if (type == EVENT_ERROR) {
                return false;
            }
        }
        /* Check if process exited */
        if (WaitForSingleObject(ctx->hProcess, 0) == WAIT_OBJECT_0) {
            DWORD exit_code = 0;
            GetExitCodeProcess(ctx->hProcess, &exit_code);
            log_error("l4capture_adapter: child exited during wait_for_ready (code=%lu)", exit_code);
            return false;
        }
        Sleep(10);
    }
    return false;
}

static uint64_t get_current_tick(void) {
    return GetTickCount64();
}

/* Convert a string identifier to a 16-byte non-zero ID for IPC */
static void str_to_id16(const char *str, uint8_t out[16]) {
    size_t len = str ? strlen(str) : 0;
    memset(out, 0, 16);
    if (len > 0) {
        size_t copy = len < 16 ? len : 16;
        memcpy(out, str, copy);
    }
    /* Ensure non-zero: if all zeros, set first byte to 0x01 */
    bool all_zero = true;
    for (int i = 0; i < 16; i++) { if (out[i]) { all_zero = false; break; } }
    if (all_zero) out[0] = 0x01;
}

static uint64_t calc_backoff_ms(uint32_t attempt) {
    uint32_t shift = (attempt > 4) ? 4 : attempt;
    uint32_t base = 1000u << shift;
    if (base > MAX_BACKOFF_MS) base = MAX_BACKOFF_MS;
    uint32_t jitter = (uint32_t)(rand() % (JITTER_MAX_MS + 1));
    return (uint64_t)(base + jitter);
}

static void record_restart_timestamp(adapter_ctx_t *ctx) {
    ctx->restart_timestamps[ctx->restart_timestamp_idx % MAX_RESTARTS] = get_current_tick();
    ctx->restart_timestamp_idx++;
    ctx->recovery_count++;
}

static bool is_restart_budget_available(adapter_ctx_t *ctx) {
    uint64_t now = get_current_tick();
    uint32_t count = 0;
    for (uint32_t i = 0; i < MAX_RESTARTS && i < ctx->restart_timestamp_idx; i++) {
        uint32_t idx = (ctx->restart_timestamp_idx - 1 - i) % MAX_RESTARTS;
        if (now - ctx->restart_timestamps[idx] < RESTART_WINDOW_MS) count++;
    }
    return count < MAX_RESTARTS;
}

static uint16_t profile_to_id(const char *profile) {
    if (strcmp(profile, "low") == 0) return PROFILE_ID_480P;
    if (strcmp(profile, "540p") == 0) return PROFILE_ID_540P;
    return PROFILE_ID_720P;
}

bool l4d_adapter_init(l4d_media_backend_t *backend, const l4d_l4capture_adapter_cfg_t *cfg) {
    if (!backend || !cfg) return false;
    adapter_ctx_t *ctx = (adapter_ctx_t *)calloc(1, sizeof(adapter_ctx_t));
    if (!ctx) return false;
    memcpy(&ctx->cfg, cfg, sizeof(ctx->cfg));
    ctx->state = L4D_ADAPTER_IDLE;
    backend->impl_ctx = ctx;
    return true;
}

void l4d_adapter_destroy(l4d_media_backend_t *backend) {
    if (!backend || !backend->impl_ctx) return;
    adapter_ctx_t *ctx = (adapter_ctx_t *)backend->impl_ctx;
    if (ctx->state != L4D_ADAPTER_IDLE) {
        /* Send CMD_STOP if running */
        if (ctx->hStdinWrite && ctx->stream_id[0]) {
            uint8_t payload[16] = {0};
            /* stream_id as raw bytes — use zeroed since we track it as string */
            ipc_send(ctx->hStdinWrite, CMD_STOP, payload, 16, ++ctx->request_seq);
        }
        cleanup_process(ctx);
    }
    free(ctx);
    backend->impl_ctx = NULL;
}

bool l4d_adapter_start(l4d_media_backend_t *self, const l4d_stream_params_t *params) {
    if (!self || !self->impl_ctx || !params) return false;
    adapter_ctx_t *ctx = (adapter_ctx_t *)self->impl_ctx;

    if (ctx->state == L4D_ADAPTER_RUNNING || ctx->state == L4D_ADAPTER_STARTING) {
        /* Same stream → already running, return success */
        if (params->stream_id && ctx->stream_id[0] &&
            strcmp(ctx->stream_id, params->stream_id) == 0) {
            log_info("l4capture_adapter: stream already running (%s)", ctx->stream_id);
            return true;
        }
        /* Different stream → stop current, then start new */
        log_info("l4capture_adapter: switching stream (stopping %s)", ctx->stream_id);
        l4d_adapter_stop(self, ctx->stream_id);
    }

    /* Save params for potential recovery */
    memcpy(&ctx->saved_params, params, sizeof(ctx->saved_params));
    ctx->has_saved_params = true;

    /* Copy string fields */
    if (params->lease_id) strcpy_s(ctx->lease_id, sizeof(ctx->lease_id), params->lease_id);
    if (params->stream_id) strcpy_s(ctx->stream_id, sizeof(ctx->stream_id), params->stream_id);
    ctx->current_deadline_tick_ms = params->deadline_tick_ms;
    ctx->last_renew_seq = 0;

    ctx->state = L4D_ADAPTER_STARTING;
    ctx->request_seq = 0;

    if (!launch_process(ctx, params)) {
        ctx->state = L4D_ADAPTER_FAILED;
        return false;
    }

    /* Build CMD_START payload */
    uint8_t payload[80];
    size_t off = 0;
    /* lease_id: uuid(16) */
    str_to_id16(ctx->lease_id, payload + off); off += 16;
    /* stream_id: uuid(16) */
    str_to_id16(ctx->stream_id, payload + off); off += 16;
    /* source_rect: rect(16) — left, top, right, bottom as u32 LE */
    write_u32_le(payload + off, (uint32_t)params->source_rect.left); off += 4;
    write_u32_le(payload + off, (uint32_t)params->source_rect.top); off += 4;
    write_u32_le(payload + off, (uint32_t)params->source_rect.right); off += 4;
    write_u32_le(payload + off, (uint32_t)params->source_rect.bottom); off += 4;
    /* geometry_gen: u64 */
    write_u64_le(payload + off, params->geometry_gen); off += 8;
    /* profile_id: u16 */
    write_u16_le(payload + off, profile_to_id(params->profile)); off += 2;
    /* rtp_port: u16 */
    write_u16_le(payload + off, params->rtp_port); off += 2;
    /* rtcp_port: u16 */
    write_u16_le(payload + off, params->rtcp_port); off += 2;
    /* deadline_tick_ms: u64 */
    write_u64_le(payload + off, params->deadline_tick_ms); off += 8;

    if (!ipc_send(ctx->hStdinWrite, CMD_START, payload, (uint32_t)off, ++ctx->request_seq)) {
        log_error("l4capture_adapter: failed to send CMD_START");
        cleanup_process(ctx);
        ctx->state = L4D_ADAPTER_FAILED;
        return false;
    }

    /* Wait for EVENT_READY */
    uint32_t ready_timeout = ctx->cfg.ready_timeout_ms > 0 ? ctx->cfg.ready_timeout_ms : 10000;
    if (!wait_for_ready(ctx, ready_timeout)) {
        log_error("l4capture_adapter: EVENT_READY not received within timeout");
        cleanup_process(ctx);
        ctx->state = L4D_ADAPTER_FAILED;
        return false;
    }

    ctx->state = L4D_ADAPTER_RUNNING;
    ctx->metrics_valid = true;
    log_info("l4capture_adapter: stream running (%ux%u @ %u fps)",
             ctx->metrics.actual_width, ctx->metrics.actual_height, ctx->metrics.fps);
    return true;
}

bool l4d_adapter_stop(l4d_media_backend_t *self, const char *stream_id) {
    if (!self || !self->impl_ctx) return false;
    adapter_ctx_t *ctx = (adapter_ctx_t *)self->impl_ctx;
    (void)stream_id;

    if (ctx->state == L4D_ADAPTER_IDLE || ctx->state == L4D_ADAPTER_STOPPING) return true;

    /* Cancel any pending restart */
    ctx->restart_pending = false;
    ctx->state = L4D_ADAPTER_STOPPING;

    if (ctx->hStdinWrite && ctx->stream_id[0]) {
        uint8_t payload[16];
        str_to_id16(ctx->stream_id, payload);
        ipc_send(ctx->hStdinWrite, CMD_STOP, payload, 16, ++ctx->request_seq);
    }

    /* Wait briefly for process to exit gracefully */
    if (ctx->hProcess) {
        WaitForSingleObject(ctx->hProcess, 500);
    }
    cleanup_process(ctx);

    ctx->state = L4D_ADAPTER_IDLE;
    ctx->stream_id[0] = '\0';
    ctx->lease_id[0] = '\0';
    ctx->current_deadline_tick_ms = 0;
    ctx->metrics_valid = false;
    ctx->has_saved_params = false;

    log_info("l4capture_adapter: stream stopped");
    return true;
}

bool l4d_adapter_renew_lease(l4d_media_backend_t *self, const char *lease_id, uint64_t new_deadline_tick_ms) {
    if (!self || !self->impl_ctx) return false;
    adapter_ctx_t *ctx = (adapter_ctx_t *)self->impl_ctx;

    if (ctx->state != L4D_ADAPTER_RUNNING) return false;

    /* Validate lease_id matches */
    if (lease_id && ctx->lease_id[0] && strcmp(ctx->lease_id, lease_id) != 0) {
        log_warn("l4capture_adapter: renew rejected — lease mismatch");
        return false;
    }

    /* Accept if deadline advances. Also accept if it shrinks: the CMD_START
     * fallback (120s) may exceed the server's actual lease TTL, and the
     * server's lease is the authority.  Only skip if truly identical (dedup). */
    if (new_deadline_tick_ms == ctx->current_deadline_tick_ms) return true;

    if (new_deadline_tick_ms < ctx->current_deadline_tick_ms) {
        log_info("l4capture_adapter: renew accepted — deadline shrinks (new=%llu < cur=%llu, server lease is authority)",
                 (unsigned long long)new_deadline_tick_ms, (unsigned long long)ctx->current_deadline_tick_ms);
    }

    ctx->current_deadline_tick_ms = new_deadline_tick_ms;

    /* Send CMD_RENEW_LEASE */
    uint8_t payload[24];
    str_to_id16(ctx->lease_id, payload);
    write_u64_le(payload + 16, new_deadline_tick_ms);

    if (ctx->hStdinWrite) {
        ipc_send(ctx->hStdinWrite, CMD_RENEW_LEASE, payload, 24, ++ctx->request_seq);
        ctx->last_renew_seq = ctx->request_seq;
    }

    return true;
}

bool l4d_adapter_force_idr(l4d_media_backend_t *self, const char *stream_id) {
    if (!self || !self->impl_ctx) return false;
    adapter_ctx_t *ctx = (adapter_ctx_t *)self->impl_ctx;

    if (ctx->state != L4D_ADAPTER_RUNNING) return false;
    if (!ctx->hStdinWrite) return false;

    (void)stream_id;
    uint8_t payload[16];
    str_to_id16(ctx->stream_id, payload);
    return ipc_send(ctx->hStdinWrite, CMD_FORCE_IDR, payload, 16, ++ctx->request_seq);
}

void l4d_adapter_poll(l4d_media_backend_t *self) {
    if (!self || !self->impl_ctx) return;
    adapter_ctx_t *ctx = (adapter_ctx_t *)self->impl_ctx;

    if (ctx->state == L4D_ADAPTER_IDLE) return;

    /* Process events from l4capture */
    uint16_t type; uint8_t payload[IPC_MAX_PAYLOAD]; uint32_t plen; uint64_t seq;
    while (ipc_try_read(ctx, &type, payload, &plen, &seq)) {
        ctx->last_event_tick = get_current_tick();
        switch (type) {
        case EVENT_METRICS:
            if (plen >= 24) {
                ctx->metrics.fps = read_u16_le(payload + 0);
                ctx->metrics.bitrate_kbps = read_u32_le(payload + 2);
                ctx->metrics.raw_drops = read_u32_le(payload + 6);
                ctx->metrics.encoder_drops = read_u32_le(payload + 10);
                ctx->metrics.transport_drops = read_u32_le(payload + 14);
                ctx->metrics.encode_p95_ms = read_u16_le(payload + 18);
                ctx->metrics.queue_depth = read_u16_le(payload + 20);
                if (plen >= 28) {
                    ctx->metrics.private_bytes_kb = read_u32_le(payload + 22);
                }
                if (plen >= 32) {
                    ctx->metrics.gdi_handles = read_u32_le(payload + 26);
                }
                ctx->metrics_valid = true;
            }
            break;
        case EVENT_DEGRADED:
            if (plen >= 4) {
                ctx->metrics.degradation_state = read_u16_le(payload + 0);
            }
            break;
        case EVENT_ERROR:
            log_warn("l4capture_adapter: EVENT_ERROR code=%u", plen >= 4 ? read_u32_le(payload) : 0);
            break;
        default:
            break;
        }
    }

    /* Check deadline expiry — STRICT, no grace period */
    if (ctx->state == L4D_ADAPTER_RUNNING && ctx->current_deadline_tick_ms > 0) {
        uint64_t now = get_current_tick();
        if (now >= ctx->current_deadline_tick_ms) {
            log_warn("l4capture_adapter: deadline expired (now=%llu >= deadline=%llu), fail-closed stop",
                     (unsigned long long)now, (unsigned long long)ctx->current_deadline_tick_ms);
            l4d_adapter_stop(self, ctx->stream_id);
            return;
        }
    }

    /* Check if process exited unexpectedly */
    if (ctx->state == L4D_ADAPTER_RUNNING && ctx->hProcess) {
        DWORD exit_code = 0;
        if (GetExitCodeProcess(ctx->hProcess, &exit_code) && exit_code != STILL_ACTIVE) {
            log_warn("l4capture_adapter: child exited unexpectedly (code=%lu)", exit_code);
            CloseHandle(ctx->hProcess); ctx->hProcess = NULL;
            if (ctx->hStdinWrite) { CloseHandle(ctx->hStdinWrite); ctx->hStdinWrite = NULL; }
            if (ctx->hStdoutRead) { CloseHandle(ctx->hStdoutRead); ctx->hStdoutRead = NULL; }
            ctx->child_pid = 0;

            /* Schedule recovery if lease active and budget available */
            if (ctx->has_saved_params && ctx->current_deadline_tick_ms > get_current_tick() &&
                is_restart_budget_available(ctx)) {
                uint64_t delay = calc_backoff_ms(ctx->recovery_count);
                ctx->next_restart_tick = get_current_tick() + delay;
                ctx->restart_pending = true;
                ctx->state = L4D_ADAPTER_RESTARTING;
                log_info("l4capture_adapter: scheduling restart in %llu ms (attempt %u)",
                         (unsigned long long)delay, ctx->recovery_count + 1);
            } else {
                ctx->state = L4D_ADAPTER_FAILED;
            }
        }
    }

    /* Execute pending restart */
    if (ctx->state == L4D_ADAPTER_RESTARTING && ctx->restart_pending) {
        /* Cancel if stop or lease expired */
        if (ctx->current_deadline_tick_ms > 0 && get_current_tick() >= ctx->current_deadline_tick_ms) {
            ctx->restart_pending = false;
            ctx->state = L4D_ADAPTER_IDLE;
            log_info("l4capture_adapter: restart cancelled — lease expired");
            return;
        }

        if (get_current_tick() >= ctx->next_restart_tick) {
            ctx->restart_pending = false;
            record_restart_timestamp(ctx);

            if (!launch_process(ctx, &ctx->saved_params)) {
                ctx->state = L4D_ADAPTER_FAILED;
                return;
            }

            uint8_t payload2[80]; size_t off2 = 0;
            memset(payload2 + off2, 0, 16); off2 += 16;
            memset(payload2 + off2, 0, 16); off2 += 16;
            write_u32_le(payload2 + off2, (uint32_t)ctx->saved_params.source_rect.left); off2 += 4;
            write_u32_le(payload2 + off2, (uint32_t)ctx->saved_params.source_rect.top); off2 += 4;
            write_u32_le(payload2 + off2, (uint32_t)ctx->saved_params.source_rect.right); off2 += 4;
            write_u32_le(payload2 + off2, (uint32_t)ctx->saved_params.source_rect.bottom); off2 += 4;
            write_u64_le(payload2 + off2, ctx->saved_params.geometry_gen); off2 += 8;
            write_u16_le(payload2 + off2, profile_to_id(ctx->saved_params.profile)); off2 += 2;
            write_u16_le(payload2 + off2, ctx->saved_params.rtp_port); off2 += 2;
            write_u16_le(payload2 + off2, ctx->saved_params.rtcp_port); off2 += 2;
            write_u16_le(payload2 + off2, 0); off2 += 2;
            write_u64_le(payload2 + off2, ctx->saved_params.deadline_tick_ms); off2 += 8;

            ctx->request_seq = 0;
            if (!ipc_send(ctx->hStdinWrite, CMD_START, payload2, (uint32_t)off2, ++ctx->request_seq)) {
                cleanup_process(ctx);
                ctx->state = L4D_ADAPTER_FAILED;
                return;
            }

            uint32_t ready_timeout2 = ctx->cfg.ready_timeout_ms > 0 ? ctx->cfg.ready_timeout_ms : 10000;
            if (wait_for_ready(ctx, ready_timeout2)) {
                ctx->state = L4D_ADAPTER_RUNNING;
                log_info("l4capture_adapter: stream recovered");
            } else {
                cleanup_process(ctx);
                ctx->state = L4D_ADAPTER_FAILED;
            }
        }
    }
}

bool l4d_adapter_get_metrics(l4d_media_backend_t *self, l4d_backend_metrics_t *out_metrics) {
    if (!self || !self->impl_ctx || !out_metrics) return false;
    adapter_ctx_t *ctx = (adapter_ctx_t *)self->impl_ctx;
    if (!ctx->metrics_valid) return false;
    memcpy(out_metrics, &ctx->metrics, sizeof(ctx->metrics));
    return true;
}

bool l4d_adapter_is_running(l4d_media_backend_t *self) {
    if (!self || !self->impl_ctx) return false;
    adapter_ctx_t *ctx = (adapter_ctx_t *)self->impl_ctx;
    return ctx->state == L4D_ADAPTER_RUNNING;
}

void l4d_adapter_get_status(const l4d_media_backend_t *self, l4d_adapter_status_t *out_status) {
    if (!self || !self->impl_ctx || !out_status) return;
    const adapter_ctx_t *ctx = (const adapter_ctx_t *)self->impl_ctx;
    memset(out_status, 0, sizeof(*out_status));
    out_status->state = ctx->state;
    out_status->child_pid = ctx->child_pid;
    out_status->deadline_tick_ms = ctx->current_deadline_tick_ms;
    strcpy_s(out_status->lease_id, sizeof(out_status->lease_id), ctx->lease_id);
    strcpy_s(out_status->stream_id, sizeof(out_status->stream_id), ctx->stream_id);
    memcpy(&out_status->metrics, &ctx->metrics, sizeof(ctx->metrics));
    out_status->metrics_valid = ctx->metrics_valid;
    out_status->last_event_tick = ctx->last_event_tick;
    out_status->recovery_count = ctx->recovery_count;
}
