#ifndef L4SUPERV_SESSION_PROC_H
#define L4SUPERV_SESSION_PROC_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <stdbool.h>

bool sp_enable_system_privileges(void);
DWORD sp_get_active_console_session(void);
BOOL sp_start_in_session(DWORD session_id, const wchar_t* exe, const wchar_t* cmdline, const wchar_t* workdir, PROCESS_INFORMATION* out);
BOOL sp_is_alive(HANDLE hProcess);
void sp_stop(PROCESS_INFORMATION* pi, const wchar_t* stop_event_name, DWORD grace_ms);

#endif /* L4SUPERV_SESSION_PROC_H */
