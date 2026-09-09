#include "session_proc.h"
#include <wtsapi32.h>
#include <userenv.h>
#include <stdio.h>
#include <stdlib.h>

#pragma comment(lib, "wtsapi32.lib")
#pragma comment(lib, "userenv.lib")
#pragma comment(lib, "advapi32.lib")

static bool set_privilege(HANDLE hToken, LPCWSTR privName, bool enable) {
    TOKEN_PRIVILEGES tp;
    LUID luid;

    if (!LookupPrivilegeValueW(NULL, privName, &luid)) {
        return false;
    }

    tp.PrivilegeCount = 1;
    tp.Privileges[0].Luid = luid;
    tp.Privileges[0].Attributes = enable ? SE_PRIVILEGE_ENABLED : 0;

    return AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(TOKEN_PRIVILEGES), NULL, NULL) &&
           (GetLastError() == ERROR_SUCCESS);
}

bool sp_enable_system_privileges(void) {
    HANDLE hToken = NULL;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) {
        return false;
    }

    set_privilege(hToken, SE_TCB_NAME, true);
    set_privilege(hToken, SE_ASSIGNPRIMARYTOKEN_NAME, true);
    set_privilege(hToken, SE_INCREASE_QUOTA_NAME, true);

    CloseHandle(hToken);
    return true;
}

DWORD sp_get_active_console_session(void) {
    DWORD sessionId = WTSGetActiveConsoleSessionId();
    if (sessionId == 0xFFFFFFFF || sessionId == 0) {
        return 0;
    }
    return sessionId;
}

BOOL sp_start_in_session(DWORD session_id, const wchar_t* exe, const wchar_t* cmdline, const wchar_t* workdir, PROCESS_INFORMATION* out) {
    if (!out) return FALSE;
    memset(out, 0, sizeof(PROCESS_INFORMATION));

    if (session_id == 0) {
        return FALSE;
    }

    HANDLE hUserToken = NULL;
    if (!WTSQueryUserToken(session_id, &hUserToken)) {
        return FALSE;
    }

    HANDLE hPrimaryToken = NULL;
    if (!DuplicateTokenEx(hUserToken, TOKEN_ALL_ACCESS, NULL, SecurityImpersonation, TokenPrimary, &hPrimaryToken)) {
        CloseHandle(hUserToken);
        return FALSE;
    }
    CloseHandle(hUserToken);

    LPVOID pEnv = NULL;
    CreateEnvironmentBlock(&pEnv, hPrimaryToken, FALSE);

    STARTUPINFOW si;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    si.lpDesktop = L"winsta0\\default";
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    wchar_t cmdline_buf[1024] = { 0 };
    if (cmdline && cmdline[0] != L'\0') {
        wcsncpy_s(cmdline_buf, sizeof(cmdline_buf) / sizeof(wchar_t), cmdline, _TRUNCATE);
    }

    DWORD creation_flags = CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT | CREATE_BREAKAWAY_FROM_JOB;

    BOOL ret = CreateProcessAsUserW(
        hPrimaryToken,
        exe,
        cmdline_buf[0] ? cmdline_buf : NULL,
        NULL,
        NULL,
        FALSE,
        creation_flags,
        pEnv,
        workdir,
        &si,
        out
    );

    if (pEnv) {
        DestroyEnvironmentBlock(pEnv);
    }
    CloseHandle(hPrimaryToken);

    return ret;
}

BOOL sp_is_alive(HANDLE hProcess) {
    if (!hProcess || hProcess == INVALID_HANDLE_VALUE) return FALSE;
    DWORD exitCode = 0;
    if (GetExitCodeProcess(hProcess, &exitCode)) {
        return (exitCode == STILL_ACTIVE);
    }
    return FALSE;
}

void sp_stop(PROCESS_INFORMATION* pi, const wchar_t* stop_event_name, DWORD grace_ms) {
    if (!pi || !pi->hProcess || pi->hProcess == INVALID_HANDLE_VALUE) return;

    if (!sp_is_alive(pi->hProcess)) {
        CloseHandle(pi->hProcess);
        if (pi->hThread) CloseHandle(pi->hThread);
        memset(pi, 0, sizeof(PROCESS_INFORMATION));
        return;
    }

    if (stop_event_name && stop_event_name[0] != L'\0') {
        HANDLE hEvent = OpenEventW(EVENT_MODIFY_STATE, FALSE, stop_event_name);
        if (hEvent) {
            SetEvent(hEvent);
            CloseHandle(hEvent);
        }
    }

    DWORD wait_res = WaitForSingleObject(pi->hProcess, grace_ms > 0 ? grace_ms : 3000);
    if (wait_res != WAIT_OBJECT_0) {
        TerminateProcess(pi->hProcess, 0);
        WaitForSingleObject(pi->hProcess, 1000);
    }

    CloseHandle(pi->hProcess);
    if (pi->hThread) CloseHandle(pi->hThread);
    memset(pi, 0, sizeof(PROCESS_INFORMATION));
}
