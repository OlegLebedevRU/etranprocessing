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

BOOL sp_start_in_session(DWORD session_id, const wchar_t* exe, const wchar_t* cmdline, const wchar_t* workdir, PROCESS_INFORMATION* out, HANDLE* out_job) {
    if (!out) return FALSE;
    memset(out, 0, sizeof(PROCESS_INFORMATION));
    if (out_job) *out_job = NULL;

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

    // 1. Create dedicated Job Object for process tree isolation
    HANDLE hJob = CreateJobObjectW(NULL, NULL);
    if (hJob) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli;
        memset(&jeli, 0, sizeof(jeli));
        jeli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        // JOB_OBJECT_LIMIT_BREAKAWAY_OK is intentionally NOT set (child processes cannot breakaway)
        SetInformationJobObject(hJob, JobObjectExtendedLimitInformation, &jeli, sizeof(jeli));
    }

    // 2. Prepare creation flags: create suspended so child is placed in Job Object before running
    DWORD creation_flags = CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT | CREATE_SUSPENDED;

    BOOL in_job = FALSE;
    if (IsProcessInJob(GetCurrentProcess(), NULL, &in_job) && in_job) {
        // Only break away from parent job if supervisor itself was launched inside a job
        creation_flags |= CREATE_BREAKAWAY_FROM_JOB;
    }

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

    if (!ret) {
        if (hJob) {
            CloseHandle(hJob);
        }
        return FALSE;
    }

    // 3. Assign process to Job Object before resuming
    if (hJob) {
        AssignProcessToJobObject(hJob, out->hProcess);
        if (out_job) {
            *out_job = hJob;
        }
    }

    // 4. Resume main thread to begin execution
    ResumeThread(out->hThread);

    return TRUE;
}

BOOL sp_is_alive(HANDLE hProcess) {
    if (!hProcess || hProcess == INVALID_HANDLE_VALUE) return FALSE;
    DWORD exitCode = 0;
    if (GetExitCodeProcess(hProcess, &exitCode)) {
        return (exitCode == STILL_ACTIVE);
    }
    return FALSE;
}

void sp_stop(PROCESS_INFORMATION* pi, HANDLE* phJob, const wchar_t* stop_event_name, DWORD grace_ms) {
    if (!pi || !pi->hProcess || pi->hProcess == INVALID_HANDLE_VALUE) {
        if (phJob && *phJob && *phJob != INVALID_HANDLE_VALUE) {
            CloseHandle(*phJob);
            *phJob = NULL;
        }
        return;
    }

    DWORD timeout = (grace_ms >= 8000) ? grace_ms : (grace_ms == 0 ? L4_STOP_GRACE_DEFAULT_MS : grace_ms);

    if (!sp_is_alive(pi->hProcess)) {
        CloseHandle(pi->hProcess);
        if (pi->hThread) CloseHandle(pi->hThread);
        memset(pi, 0, sizeof(PROCESS_INFORMATION));
        if (phJob && *phJob && *phJob != INVALID_HANDLE_VALUE) {
            CloseHandle(*phJob);
            *phJob = NULL;
        }
        return;
    }

    if (stop_event_name && stop_event_name[0] != L'\0') {
        HANDLE hEvent = OpenEventW(EVENT_MODIFY_STATE, FALSE, stop_event_name);
        if (hEvent) {
            SetEvent(hEvent);
            CloseHandle(hEvent);
        }
    }

    DWORD wait_res = WaitForSingleObject(pi->hProcess, timeout);
    if (wait_res != WAIT_OBJECT_0) {
        if (phJob && *phJob && *phJob != INVALID_HANDLE_VALUE) {
            TerminateJobObject(*phJob, 1);
        }
        TerminateProcess(pi->hProcess, 0);
        WaitForSingleObject(pi->hProcess, 1000);
    }

    CloseHandle(pi->hProcess);
    if (pi->hThread) CloseHandle(pi->hThread);
    memset(pi, 0, sizeof(PROCESS_INFORMATION));

    if (phJob && *phJob && *phJob != INVALID_HANDLE_VALUE) {
        CloseHandle(*phJob);
        *phJob = NULL;
    }
}
