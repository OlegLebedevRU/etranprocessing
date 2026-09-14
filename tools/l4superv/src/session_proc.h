#ifndef L4SUPERV_SESSION_PROC_H
#define L4SUPERV_SESSION_PROC_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <stdbool.h>

#define L4_STOP_GRACE_DEFAULT_MS 8000

#ifndef SECURITY_MANDATORY_HIGH_RID
#define SECURITY_MANDATORY_HIGH_RID (0x00003000L)
#endif
#ifndef SECURITY_MANDATORY_MEDIUM_RID
#define SECURITY_MANDATORY_MEDIUM_RID (0x00002000L)
#endif

typedef enum {
    SP_TOKEN_OK = 0,
    SP_TOKEN_ERR_NO_SESSION = 1,
    SP_TOKEN_ERR_QUERY_USER_TOKEN = 2,
    SP_TOKEN_ERR_INSUFFICIENT_INTEGRITY = 3,
    SP_TOKEN_ERR_LINKED_TOKEN_FAILED = 4,
    SP_TOKEN_ERR_INVALID_SESSION = 5,
    SP_TOKEN_ERR_INVALID_SID = 6,
    SP_TOKEN_ERR_DUPLICATE_FAILED = 7,
    SP_TOKEN_ERR_PROCESS_CREATE_FAILED = 8,
    SP_TOKEN_ERR_PROCESS_VERIFY_FAILED = 9
} SpLaunchStatus;

const char* sp_launch_status_to_string(SpLaunchStatus status);

bool sp_enable_system_privileges(void);
DWORD sp_get_active_console_session(void);
DWORD sp_get_token_integrity_level(HANDLE hToken);

SpLaunchStatus sp_select_target_token(
    HANDLE hUserToken,
    DWORD expected_session_id,
    bool strict_high_il,
    HANDLE* out_token,
    DWORD* out_selected_il
);

BOOL sp_start_in_session_ex(
    DWORD session_id,
    const wchar_t* exe,
    const wchar_t* cmdline,
    const wchar_t* workdir,
    bool strict_high_il,
    PROCESS_INFORMATION* out_pi,
    HANDLE* out_job,
    SpLaunchStatus* out_status
);

BOOL sp_start_in_session(DWORD session_id, const wchar_t* exe, const wchar_t* cmdline, const wchar_t* workdir, PROCESS_INFORMATION* out_pi, HANDLE* out_job);
BOOL sp_is_alive(HANDLE hProcess);
void sp_stop(PROCESS_INFORMATION* pi, HANDLE* phJob, const wchar_t* stop_event_name, DWORD grace_ms);

#endif /* L4SUPERV_SESSION_PROC_H */
