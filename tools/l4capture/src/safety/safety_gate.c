#include <windows.h>
#include <wtsapi32.h>
#include <string.h>
#include <wchar.h>
#include "l4capture/safety_gate.h"

static void stop_locked(l4c_safety_gate_t *gate, l4c_status_t reason) {
    if (gate->stopped) return;
    gate->stopped = true;
    gate->reason = reason;
    gate->idr_pending = false;
    l4c_pipeline_stop(&gate->pipeline);
    SetEvent(gate->stop_event);
}

static l4c_status_t check_locked(l4c_safety_gate_t *gate, uint64_t now) {
    l4c_status_t status = L4C_OK;
    if (gate->stopped) return gate->reason;
    if (gate->started) {
        /* Конкурентные callers могут снять tick до входа в короткую блокировку. */
        if (now < gate->lease.last_tick_ms) now = gate->lease.last_tick_ms;
        status = l4c_deadline_check(&gate->lease, now);
        if (status != L4C_OK) stop_locked(gate, status);
    }
    return status;
}

l4c_status_t l4c_safety_init(l4c_safety_gate_t *gate) {
    if (!gate) return L4C_ERR_INVALID_ARG;
    memset(gate, 0, sizeof(*gate));
    InitializeSRWLock(&gate->lock);
    l4c_pipeline_init(&gate->pipeline);
    gate->stop_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    return gate->stop_event ? L4C_OK : L4C_ERR_FATAL;
}

void l4c_safety_destroy(l4c_safety_gate_t *gate) {
    if (gate->stop_event) CloseHandle(gate->stop_event);
    gate->stop_event = NULL;
}

void l4c_safety_stop(l4c_safety_gate_t *gate, l4c_status_t reason) {
    AcquireSRWLockExclusive(&gate->lock);
    stop_locked(gate, reason);
    ReleaseSRWLockExclusive(&gate->lock);
}

l4c_status_t l4c_safety_check(l4c_safety_gate_t *gate, uint64_t now, bool desktop_available) {
    l4c_status_t status;
    AcquireSRWLockExclusive(&gate->lock);
    if (!desktop_available) stop_locked(gate, L4C_ERR_SESSION_UNAVAILABLE);
    status = check_locked(gate, now);
    ReleaseSRWLockExclusive(&gate->lock);
    return status;
}

static bool nonzero_id(const uint8_t *id) {
    size_t i;
    for (i = 0; i < 16; ++i) if (id[i]) return true;
    return false;
}

l4c_status_t l4c_safety_command(l4c_safety_gate_t *gate, const l4c_message_t *message, uint64_t now) {
    l4c_status_t status = L4C_OK;
    bool applied;
    l4c_raster_layout_t layout;
    if (!gate || !message) return L4C_ERR_INVALID_ARG;
    AcquireSRWLockExclusive(&gate->lock);
    if (gate->stopped) {
        status = message->type == L4C_CMD_STOP && gate->started &&
            memcmp(message->body.stream_id, gate->stream_id, 16) == 0 ? gate->reason : L4C_ERR_PROTOCOL;
        goto done;
    }
    status = check_locked(gate, now);
    if (status != L4C_OK) goto done;
    if (gate->started && now < gate->lease.last_tick_ms) now = gate->lease.last_tick_ms;
    if (!message->request_seq) { status = L4C_ERR_PROTOCOL; goto done; }
    if (message->type == L4C_CMD_START) {
        const l4c_start_t *start = &message->body.start;
        if (gate->started || !nonzero_id(start->lease_id) || !nonzero_id(start->stream_id) ||
            start->profile_id < L4C_PROFILE_480P || start->profile_id > L4C_PROFILE_720P ||
            start->rtp_port != 5004 || start->rtcp_port != 5005) { status = L4C_ERR_PROTOCOL; goto done; }
        status = l4c_rect_layout(&start->source_rect, &layout);
        if (status != L4C_OK) goto done;
        status = l4c_deadline_start(&gate->lease, start->deadline_tick_ms, message->request_seq, now);
        if (status != L4C_OK) goto done;
        memcpy(gate->lease_id, start->lease_id, 16); memcpy(gate->stream_id, start->stream_id, 16);
        gate->started = true;
        gate->last_command_seq = message->request_seq;
        gate->last_start = *start;
    } else if (!gate->started) {
        status = L4C_ERR_PROTOCOL;
    } else if (message->type == L4C_CMD_RENEW_LEASE) {
        if (memcmp(message->body.renew.lease_id, gate->lease_id, 16) != 0) { status = L4C_ERR_PROTOCOL; goto done; }
        if (message->request_seq <= gate->last_command_seq) goto done;
        status = l4c_deadline_renew(&gate->lease, message->body.renew.new_deadline_tick_ms, message->request_seq, now, &applied);
        if (applied) gate->last_command_seq = message->request_seq;
    } else if (message->type == L4C_CMD_STOP || message->type == L4C_CMD_FORCE_IDR) {
        if (memcmp(message->body.stream_id, gate->stream_id, 16) != 0) { status = L4C_ERR_PROTOCOL; goto done; }
        if (message->type == L4C_CMD_STOP) stop_locked(gate, L4C_OK);
        else if (message->request_seq > gate->last_command_seq) {
            gate->last_command_seq = message->request_seq;
            gate->idr_pending = true;
        }
    } else status = L4C_ERR_PROTOCOL;
done:
    if (status != L4C_OK) stop_locked(gate, status);
    ReleaseSRWLockExclusive(&gate->lock);
    return status;
}

bool l4c_safety_can_send(l4c_safety_gate_t *gate, uint64_t now, bool desktop_available) {
    bool allowed;
    AcquireSRWLockExclusive(&gate->lock);
    if (!desktop_available) stop_locked(gate, L4C_ERR_SESSION_UNAVAILABLE);
    (void)check_locked(gate, now);
    allowed = gate->started && !gate->stopped;
    ReleaseSRWLockExclusive(&gate->lock);
    return allowed;
}

bool l4c_safety_take_idr(l4c_safety_gate_t *gate, uint64_t now) {
    bool take = false;
    AcquireSRWLockExclusive(&gate->lock);
    (void)check_locked(gate, now);
    if (!gate->stopped && gate->started && gate->idr_pending &&
        (!gate->idr_sent || (now >= gate->last_idr_ms && now - gate->last_idr_ms >= L4C_FORCE_IDR_MIN_INTERVAL_MS))) {
        gate->idr_sent = true; gate->idr_pending = false; gate->last_idr_ms = now; take = true;
    }
    ReleaseSRWLockExclusive(&gate->lock);
    return take;
}

l4c_status_t l4c_safety_reason(l4c_safety_gate_t *gate) {
    l4c_status_t status;
    AcquireSRWLockShared(&gate->lock);
    status = gate->reason;
    ReleaseSRWLockShared(&gate->lock);
    return status;
}

l4c_status_t l4c_check_job(void) {
    BOOL in_job = FALSE;
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits;
    memset(&limits, 0, sizeof(limits));
    if (!IsProcessInJob(GetCurrentProcess(), NULL, &in_job) || !in_job ||
        !QueryInformationJobObject(NULL, JobObjectExtendedLimitInformation, &limits, sizeof(limits), NULL) ||
        !(limits.BasicLimitInformation.LimitFlags & JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE)) return L4C_ERR_FATAL;
    return L4C_OK;
}

l4c_status_t l4c_session_open(l4c_session_probe_t *probe) {
    WCHAR path[MAX_PATH];
    UINT length;
    memset(probe, 0, sizeof(*probe));
    if (!ProcessIdToSessionId(GetCurrentProcessId(), &probe->session_id) || !probe->session_id)
        return L4C_ERR_SESSION_UNAVAILABLE;
    length = GetSystemDirectoryW(path, MAX_PATH);
    if (!length || length >= MAX_PATH - 14u) return L4C_ERR_SESSION_UNAVAILABLE;
    wcscat_s(path, MAX_PATH, L"\\wtsapi32.dll");
    probe->wts_module = LoadLibraryW(path);
    if (!probe->wts_module) return L4C_ERR_SESSION_UNAVAILABLE;
    probe->query_session = GetProcAddress(probe->wts_module, "WTSQuerySessionInformationW");
    probe->free_memory = GetProcAddress(probe->wts_module, "WTSFreeMemory");
    if (!probe->query_session || !probe->free_memory || !l4c_session_available(probe)) {
        l4c_session_close(probe);
        return L4C_ERR_SESSION_UNAVAILABLE;
    }
    return L4C_OK;
}

bool l4c_session_available(const l4c_session_probe_t *probe) {
    typedef BOOL (WINAPI *query_t)(HANDLE, DWORD, WTS_INFO_CLASS, LPWSTR *, DWORD *);
    typedef VOID (WINAPI *free_t)(PVOID);
    query_t query;
    free_t free_buffer;
    LPWSTR buffer = NULL;
    DWORD bytes = 0, session = 0;
    WCHAR input_name[128] = {0}, thread_name[128] = {0};
    HDESK desktop;
    bool available;
    if (!probe || !probe->query_session || !probe->free_memory ||
        !ProcessIdToSessionId(GetCurrentProcessId(), &session) || !session || session != probe->session_id) return false;
    memcpy(&query, &probe->query_session, sizeof(query));
    memcpy(&free_buffer, &probe->free_memory, sizeof(free_buffer));
    if (!query(WTS_CURRENT_SERVER_HANDLE, session, WTSConnectState, &buffer, &bytes)) return false;
    available = buffer && bytes >= sizeof(WTS_CONNECTSTATE_CLASS) && *(WTS_CONNECTSTATE_CLASS *)buffer == WTSActive;
    if (buffer) free_buffer(buffer);
    if (!available) return false;
    desktop = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS | DESKTOP_SWITCHDESKTOP);
    if (!desktop) return false;
    available = GetUserObjectInformationW(desktop, UOI_NAME, input_name, sizeof(input_name), &bytes) &&
        GetUserObjectInformationW(GetThreadDesktop(GetCurrentThreadId()), UOI_NAME, thread_name, sizeof(thread_name), &bytes) &&
        wcscmp(input_name, L"Default") == 0 && wcscmp(thread_name, input_name) == 0;
    CloseDesktop(desktop);
    return available;
}

void l4c_session_close(l4c_session_probe_t *probe) {
    if (probe->wts_module) FreeLibrary(probe->wts_module);
    memset(probe, 0, sizeof(*probe));
}