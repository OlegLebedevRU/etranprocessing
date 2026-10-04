#define _WIN32_WINNT 0x0601
#include "event_ipc.h"
#include <sddl.h>
#include <wchar.h>
#include <string.h>
#include <stdio.h>
#include "mqtt_protocol.h"

static SRWLOCK g_lock = SRWLOCK_INIT;
static HANDLE g_job, g_thread, g_stop;
static volatile bool* g_cancelled;
static volatile LONG* g_protection;
static ULONGLONG g_deadline, g_last;
static bool g_used;
static UserEventPublisher g_publish;
static void* g_context;

static bool valid_uuid(const char* value) {
    if (!memchr(value, 0, 37) || strlen(value) != 36) return false;
    for (unsigned i = 0; i < 36; ++i) {
        char c = value[i];
        if (i == 8 || i == 13 || i == 18 || i == 23) { if (c != '-') return false; }
        else if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return false;
    }
    return true;
}

static bool parse_int32(const wchar_t* text, int32_t* result) {
    bool negative = false;
    uint64_t value = 0;
    if (*text == L'-' || *text == L'+') negative = (*text++ == L'-');
    if (!*text) return false;
    for (; *text; ++text) {
        if (*text < L'0' || *text > L'9') return false;
        value = value * 10 + (unsigned)(*text - L'0');
        if (value > (negative ? 2147483648ULL : 2147483647ULL)) return false;
    }
    *result = negative ? (int32_t)(-(int64_t)value) : (int32_t)value;
    return true;
}

bool event_parse_args(int argc, wchar_t** argv, UserEvent* event) {
    bool code_seen = false, exit_seen = false, payload_seen = false, mode_seen = false, correlation_seen = false;
    const wchar_t* payload = NULL;
    memset(event, 0, sizeof(*event));
    event->version = 1;
    event->code = 999;
    for (int i = 1; i < argc; ++i) {
        const wchar_t* arg = argv[i];
        if (_wcsicmp(arg, L"--send-event") == 0 && !mode_seen) mode_seen = true;
        else if (_wcsnicmp(arg, L"-event-code=", 12) == 0 && !code_seen) {
            code_seen = true;
            if (!parse_int32(arg + 12, &event->code) || event->code < 900 || event->code > 999) return false;
        } else if (_wcsnicmp(arg, L"-event-exit-code=", 17) == 0 && !exit_seen) {
            exit_seen = true;
            if (!parse_int32(arg + 17, &event->exit_code)) event->exit_code = -1;
        } else if (_wcsnicmp(arg, L"-event-payload=", 15) == 0 && !payload_seen) {
            payload_seen = true;
            payload = arg + 15;
        } else if (_wcsnicmp(arg, L"-event-correlation-id=", 22) == 0 && !correlation_seen) {
            correlation_seen = true;
            if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, arg + 22, -1,
                    event->correlation_id, sizeof(event->correlation_id), NULL, NULL) ||
                !valid_uuid(event->correlation_id)) return false;
        } else return false;
    }
    if (!mode_seen) return false;
    if (payload) {
        int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, payload, -1, NULL, 0, NULL, NULL);
        if (length <= 0) return false;
        if (length - 1 > EVENT_PAYLOAD_MAX) event->exit_code = -2;
        else {
            event->has_payload = 1;
            event->payload_len = (uint32_t)(length - 1);
            if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, payload, -1,
                                     event->payload, sizeof(event->payload), NULL, NULL)) return false;
        }
    }
    return true;
}

bool event_validate(const UserEvent* event) {
    if (event->version != 1 || event->code < 900 || event->code > 999 ||
        event->has_payload > 1 || event->payload_len > EVENT_PAYLOAD_MAX ||
        (!event->has_payload && event->payload_len) ||
        event->payload[event->payload_len] != 0 ||
        strlen(event->payload) != event->payload_len ||
        (event->correlation_id[0] && !valid_uuid(event->correlation_id))) return false;
    return !event->payload_len || MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
             event->payload, (int)event->payload_len, NULL, 0) > 0;
}

int event_json(const UserEvent* event, const char* sn, unsigned id,
               const char* timestamp, const char* correlation, char* out, size_t size) {
    char escaped[6145], extra[6160], external[64];
    json_escape_string(event->payload, event->payload_len, escaped, sizeof(escaped));
    if (event->has_payload) snprintf(extra, sizeof(extra), ",\"446\":\"%s\"", escaped);
    else extra[0] = 0;
    if (event->correlation_id[0]) snprintf(external, sizeof(external), ",\"448\":\"%s\"", event->correlation_id);
    else external[0] = 0;
    int length = snprintf(out, size,
        "{\"101\":%u,\"102\":\"%s\",\"200\":%d,\"300\":[{\"324\":\"%s\",\"440\":\"l4con\"%s,\"447\":%d%s}],\"correlationData\":\"%s\"}",
        id, timestamp, event->code, sn, extra, event->exit_code, external, correlation);
    return length > 0 && (size_t)length < size ? length : -1;
}

bool event_rate_take(ULONGLONG now, ULONGLONG* last, bool* used) {
    if (*used && now - *last < 1000) return false;
    *last = now;
    *used = true;
    return true;
}

/* The job handle is borrowed. Revoke under the same lock before closing it. */
void event_job_register(HANDLE job, volatile bool* cancelled, ULONGLONG deadline) {
    AcquireSRWLockExclusive(&g_lock);
    g_job = job;
    g_cancelled = cancelled;
    g_deadline = deadline;
    ReleaseSRWLockExclusive(&g_lock);
}

void event_job_set_protection(HANDLE job, volatile LONG* protection) {
    AcquireSRWLockExclusive(&g_lock);
    if (g_job==job) g_protection=protection;
    ReleaseSRWLockExclusive(&g_lock);
}

bool event_job_cancel_replacement(volatile bool* cancelled, volatile LONG* protection) {
    AcquireSRWLockExclusive(&g_lock);
    bool allowed=!InterlockedCompareExchange(protection,0,0);
    if (allowed) *cancelled=true;
    ReleaseSRWLockExclusive(&g_lock);
    return allowed;
}

void event_job_revoke(HANDLE job) {
    AcquireSRWLockExclusive(&g_lock);
    if (g_job == job) {
        g_job = NULL;
        g_protection = NULL;
        g_cancelled = NULL;
    }
    ReleaseSRWLockExclusive(&g_lock);
}

static bool overlapped_done(HANDLE pipe, OVERLAPPED* operation, BOOL immediate,
                            HANDLE stop, DWORD timeout, DWORD* bytes) {
    if (!immediate && GetLastError() != ERROR_IO_PENDING) return false;
    HANDLE handles[2] = { operation->hEvent, stop };
    DWORD result = immediate ? WAIT_OBJECT_0 :
        WaitForMultipleObjects(stop ? 2 : 1, handles, FALSE, timeout);
    if (result != WAIT_OBJECT_0) {
        CancelIoEx(pipe, operation);
        GetOverlappedResult(pipe, operation, bytes, TRUE);
        return false;
    }
    return GetOverlappedResult(pipe, operation, bytes, FALSE) != FALSE;
}

static int dispatch_event(HANDLE pipe, const UserEvent* event) {
    ULONG pid = 0;
    if (!GetNamedPipeClientProcessId(pipe, &pid) || !pid) return EVENT_DENIED;
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, pid);
    if (!process) return EVENT_DENIED;
    int result = EVENT_DENIED;
    AcquireSRWLockExclusive(&g_lock);
    BOOL member = FALSE;
    ULONGLONG now = GetTickCount64();
    if (g_job && g_cancelled && !*g_cancelled && now < g_deadline &&
        WaitForSingleObject(g_stop, 0) != WAIT_OBJECT_0 &&
        WaitForSingleObject(process, 0) == WAIT_TIMEOUT &&
        IsProcessInJob(process, g_job, &member) && member) {
        if (event->version==2) {
            wchar_t actual[MAX_PATH], expected[MAX_PATH]; DWORD size=MAX_PATH;
            DWORD length=GetModuleFileNameW(NULL,expected,MAX_PATH);
            wchar_t* slash=wcsrchr(expected,L'\\');
            if (length && length<MAX_PATH && slash) {
                *slash=0; slash=wcsrchr(expected,L'\\');
                if (slash) {
                    *slash=0;
                    if (wcscat_s(expected,MAX_PATH,L"\\l4pin\\l4pin.exe")==0 &&
                        QueryFullProcessImageNameW(process,0,actual,&size) &&
                        !_wcsicmp(actual,expected) && g_protection && g_deadline-now>=100000) {
                        InterlockedExchange(g_protection,1); result=EVENT_OK;
                    }
                }
            }
        } else if (!event_rate_take(now, &g_last, &g_used)) result = EVENT_RATE_LIMIT;
        else result = g_publish(event, g_context) ? EVENT_OK : EVENT_SEND_FAILED;
    }
    ReleaseSRWLockExclusive(&g_lock);
    CloseHandle(process);
    return result;
}

static DWORD WINAPI pipe_thread(void* context) {
    HANDLE pipe = (HANDLE)context;
    OVERLAPPED operation = { 0 };
    operation.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!operation.hEvent) { CloseHandle(pipe); return 1; }
    while (WaitForSingleObject(g_stop, 0) != WAIT_OBJECT_0) {
        ResetEvent(operation.hEvent);
        BOOL connected = ConnectNamedPipe(pipe, &operation);
        DWORD error = connected ? ERROR_SUCCESS : GetLastError(), bytes = 0;
        if (error != ERROR_PIPE_CONNECTED &&
            !overlapped_done(pipe, &operation, connected, g_stop, INFINITE, &bytes)) break;
        UserEvent event;
        memset(&event, 0, sizeof(event));
        ResetEvent(operation.hEvent);
        BOOL read = ReadFile(pipe, &event, sizeof(event), NULL, &operation);
        bool received = overlapped_done(pipe, &operation, read, g_stop, 1000, &bytes);
        int32_t result = EVENT_BAD_ARGS;
        if (received && bytes == sizeof(event) && (event_validate(&event) || (event.version==2 && event.code==7011 &&
            event.exit_code==0 && !event.has_payload && !event.payload_len &&
            !event.payload[0] && !event.correlation_id[0])))
            result = dispatch_event(pipe, &event);
        if (received) {
            ResetEvent(operation.hEvent);
            BOOL written = WriteFile(pipe, &result, sizeof(result), NULL, &operation);
            if (overlapped_done(pipe, &operation, written, g_stop, 1000, &bytes)) {
                /* Wait for the reader's receipt before disconnecting the buffered pipe. */
                unsigned char receipt = 0;
                ResetEvent(operation.hEvent);
                BOOL acknowledged = ReadFile(pipe, &receipt, 1, NULL, &operation);
                overlapped_done(pipe, &operation, acknowledged, g_stop, 1000, &bytes);
            }
        }
        DisconnectNamedPipe(pipe);
    }
    CloseHandle(operation.hEvent);
    CloseHandle(pipe);
    return 0;
}

bool event_ipc_start(HANDLE stop, UserEventPublisher publish, void* context) {
    PSECURITY_DESCRIPTOR descriptor = NULL;
    /* Interactive users can reach the endpoint; only active job members may publish. */
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
          L"D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;GRGW;;;IU)", SDDL_REVISION_1, &descriptor, NULL)) return false;
    SECURITY_ATTRIBUTES attributes = { sizeof(attributes), descriptor, FALSE };
    HANDLE pipe = CreateNamedPipeW(EVENT_PIPE_NAME,
        PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
        PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
        1, sizeof(int32_t), sizeof(UserEvent), 0, &attributes);
    LocalFree(descriptor);
    if (pipe == INVALID_HANDLE_VALUE) return false;
    g_stop = stop;
    g_publish = publish;
    g_context = context;
    g_used = false;
    g_thread = CreateThread(NULL, 0, pipe_thread, pipe, 0, NULL);
    if (!g_thread) CloseHandle(pipe);
    return g_thread != NULL;
}

void event_ipc_stop(void) {
    if (!g_thread) return;
    /* Caller has signalled the shared stop event. */
    WaitForSingleObject(g_thread, INFINITE);
    CloseHandle(g_thread);
    g_thread = NULL;
}

int event_cli(int argc, wchar_t** argv) {
    UserEvent event;
    if (!event_parse_args(argc, argv, &event)) return EVENT_BAD_ARGS;
    HANDLE pipe = CreateFileW(EVENT_PIPE_NAME, GENERIC_READ | GENERIC_WRITE, 0, NULL,
                               OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
    if (pipe == INVALID_HANDLE_VALUE) return EVENT_UNAVAILABLE;
    DWORD mode = PIPE_READMODE_MESSAGE;
    OVERLAPPED operation = { 0 };
    operation.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    DWORD bytes = 0;
    int32_t result = EVENT_UNAVAILABLE;
    if (operation.hEvent && SetNamedPipeHandleState(pipe, &mode, NULL, NULL)) {
        BOOL sent = WriteFile(pipe, &event, sizeof(event), NULL, &operation);
        if (overlapped_done(pipe, &operation, sent, NULL, 2000, &bytes) && bytes == sizeof(event)) {
            ResetEvent(operation.hEvent);
            BOOL received = ReadFile(pipe, &result, sizeof(result), NULL, &operation);
            if (!overlapped_done(pipe, &operation, received, NULL, 12000, &bytes) || bytes != sizeof(result))
                result = EVENT_UNAVAILABLE;
            else {
                unsigned char receipt = 1;
                ResetEvent(operation.hEvent);
                BOOL written = WriteFile(pipe, &receipt, 1, NULL, &operation);
                overlapped_done(pipe, &operation, written, NULL, 1000, &bytes);
            }
        }
    }
    if (operation.hEvent) CloseHandle(operation.hEvent);
    CloseHandle(pipe);
    return result >= EVENT_OK && result <= EVENT_SEND_FAILED ? result : EVENT_UNAVAILABLE;
}
