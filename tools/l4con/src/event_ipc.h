#ifndef L4CON_EVENT_IPC_H
#define L4CON_EVENT_IPC_H
#include <windows.h>
#include <stdint.h>
#include <stdbool.h>

#define EVENT_PAYLOAD_MAX 1024
#ifndef EVENT_PIPE_NAME
#define EVENT_PIPE_NAME L"\\\\.\\pipe\\L4Con_UserEvents_v1"
#endif
enum { EVENT_OK = 0, EVENT_BAD_ARGS = 2, EVENT_DENIED = 3,
       EVENT_UNAVAILABLE = 4, EVENT_RATE_LIMIT = 5, EVENT_SEND_FAILED = 6 };
typedef struct {
    uint32_t version;
    int32_t code;
    int32_t exit_code;
    uint32_t has_payload;
    uint32_t payload_len;
    char payload[EVENT_PAYLOAD_MAX + 1];
    char correlation_id[37];
} UserEvent;
typedef bool (*UserEventPublisher)(const UserEvent* event, void* context);
int event_cli(int argc, wchar_t** argv);
bool event_parse_args(int argc, wchar_t** argv, UserEvent* event);
bool event_validate(const UserEvent* event);
int event_json(const UserEvent* event, const char* sn, unsigned id,
               const char* timestamp, const char* correlation, char* out, size_t size);
bool event_rate_take(ULONGLONG now, ULONGLONG* last, bool* used);
bool event_ipc_start(HANDLE stop, UserEventPublisher publish, void* context);
void event_ipc_stop(void);
void event_job_register(HANDLE job, volatile bool* cancelled, ULONGLONG deadline);
void event_job_revoke(HANDLE job);
#endif
