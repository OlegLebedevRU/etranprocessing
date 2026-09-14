#include "session_proc.h"
#include <wtsapi32.h>
#include <userenv.h>
#include <shlwapi.h>
#include <stdio.h>
#include <stdlib.h>

#pragma comment(lib, "wtsapi32.lib")
#pragma comment(lib, "userenv.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "shlwapi.lib")

#ifndef SECURITY_MANDATORY_HIGH_RID
#define SECURITY_MANDATORY_HIGH_RID (0x00003000L)
#endif
#ifndef SECURITY_MANDATORY_MEDIUM_RID
#define SECURITY_MANDATORY_MEDIUM_RID (0x00002000L)
#endif

const char* sp_launch_status_to_string(SpLaunchStatus status) {
    switch (status) {
        case SP_TOKEN_OK: return "ok";
        case SP_TOKEN_ERR_NO_SESSION: return "no_active_console_session";
        case SP_TOKEN_ERR_QUERY_USER_TOKEN: return "query_user_token_failed";
        case SP_TOKEN_ERR_INSUFFICIENT_INTEGRITY: return "insufficient_integrity";
        case SP_TOKEN_ERR_LINKED_TOKEN_FAILED: return "linked_token_failed";
        case SP_TOKEN_ERR_INVALID_SESSION: return "invalid_session";
        case SP_TOKEN_ERR_INVALID_SID: return "invalid_sid";
        case SP_TOKEN_ERR_DUPLICATE_FAILED: return "token_duplicate_failed";
        case SP_TOKEN_ERR_PROCESS_CREATE_FAILED: return "process_create_failed";
        case SP_TOKEN_ERR_PROCESS_VERIFY_FAILED: return "process_verify_failed";
        default: return "unknown_error";
    }
}

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

DWORD sp_get_token_integrity_level(HANDLE hToken) {
    if (!hToken || hToken == INVALID_HANDLE_VALUE) return 0;
    DWORD dwLength = 0;
    GetTokenInformation(hToken, TokenIntegrityLevel, NULL, 0, &dwLength);
    if (dwLength == 0) return 0;

    BYTE* pBuffer = (BYTE*)malloc(dwLength);
    if (!pBuffer) return 0;

    DWORD dwIL = 0;
    if (GetTokenInformation(hToken, TokenIntegrityLevel, pBuffer, dwLength, &dwLength)) {
        TOKEN_MANDATORY_LABEL* pLabel = (TOKEN_MANDATORY_LABEL*)pBuffer;
        if (pLabel->Label.Sid) {
            UCHAR subAuthCount = *GetSidSubAuthorityCount(pLabel->Label.Sid);
            if (subAuthCount > 0) {
                dwIL = *GetSidSubAuthority(pLabel->Label.Sid, (DWORD)(subAuthCount - 1));
            }
        }
    }
    free(pBuffer);
    return dwIL;
}

static PSID get_token_user_sid(HANDLE hToken, BYTE* pBuf, DWORD bufLen) {
    DWORD dwLength = 0;
    if (GetTokenInformation(hToken, TokenUser, pBuf, bufLen, &dwLength)) {
        TOKEN_USER* pUser = (TOKEN_USER*)pBuf;
        return pUser->User.Sid;
    }
    return NULL;
}

DWORD sp_get_active_console_session(void) {
    DWORD sessionId = WTSGetActiveConsoleSessionId();
    if (sessionId == 0xFFFFFFFF || sessionId == 0) {
        return 0;
    }

    // Verify session connection state is WTSActive
    WTS_CONNECTSTATE_CLASS* pState = NULL;
    DWORD bytes = 0;
    if (WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE, sessionId, WTSConnectState, (LPWSTR*)&pState, &bytes)) {
        if (pState && bytes >= sizeof(WTS_CONNECTSTATE_CLASS)) {
            WTS_CONNECTSTATE_CLASS st = *pState;
            WTSFreeMemory(pState);
            if (st != WTSActive) {
                return 0;
            }
        } else {
            if (pState) WTSFreeMemory(pState);
            return 0;
        }
    } else {
        return 0;
    }

    // Verify a logged-in user is present (prevent running in Session 0 or unauthenticated logon screen)
    LPWSTR pUser = NULL;
    if (WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE, sessionId, WTSUserName, &pUser, &bytes)) {
        if (!pUser || pUser[0] == L'\0') {
            if (pUser) WTSFreeMemory(pUser);
            return 0;
        }
        WTSFreeMemory(pUser);
    } else {
        return 0;
    }

    return sessionId;
}

SpLaunchStatus sp_select_target_token(
    HANDLE hUserToken,
    DWORD expected_session_id,
    bool strict_high_il,
    HANDLE* out_token,
    DWORD* out_selected_il
) {
    if (!hUserToken || hUserToken == INVALID_HANDLE_VALUE || !out_token || !out_selected_il) {
        return SP_TOKEN_ERR_QUERY_USER_TOKEN;
    }
    *out_token = NULL;
    *out_selected_il = 0;

    // 1. Verify token session ID
    DWORD tokenSession = 0;
    DWORD len = 0;
    if (!GetTokenInformation(hUserToken, TokenSessionId, &tokenSession, sizeof(tokenSession), &len)) {
        return SP_TOKEN_ERR_QUERY_USER_TOKEN;
    }
    if (expected_session_id != 0 && tokenSession != expected_session_id) {
        return SP_TOKEN_ERR_INVALID_SESSION;
    }

    // 2. Query user SID from original token
    BYTE userBuf1[256] = { 0 };
    PSID sid1 = get_token_user_sid(hUserToken, userBuf1, sizeof(userBuf1));
    if (!sid1) {
        return SP_TOKEN_ERR_QUERY_USER_TOKEN;
    }

    // 3. Query TokenElevationType
    TOKEN_ELEVATION_TYPE elevType = TokenElevationTypeDefault;
    if (!GetTokenInformation(hUserToken, TokenElevationType, &elevType, sizeof(elevType), &len)) {
        elevType = TokenElevationTypeDefault;
    }

    switch (elevType) {
        case TokenElevationTypeLimited: {
            // Adaptive / best-effort: always attempt to extract TokenLinkedToken for split-token admin
            TOKEN_LINKED_TOKEN linked = { 0 };
            if (GetTokenInformation(hUserToken, TokenLinkedToken, &linked, sizeof(linked), &len) && linked.LinkedToken) {
                // Verify linked token session ID
                DWORD linkedSession = 0;
                if (GetTokenInformation(linked.LinkedToken, TokenSessionId, &linkedSession, sizeof(linkedSession), &len) &&
                    linkedSession == tokenSession) {
                    // Verify linked token user SID
                    BYTE userBuf2[256] = { 0 };
                    PSID sid2 = get_token_user_sid(linked.LinkedToken, userBuf2, sizeof(userBuf2));
                    if (sid2 && EqualSid(sid1, sid2)) {
                        // Verify linked token actual elevation / integrity level
                        DWORD linkedIL = sp_get_token_integrity_level(linked.LinkedToken);
                        TOKEN_ELEVATION elev = { 0 };
                        GetTokenInformation(linked.LinkedToken, TokenElevation, &elev, sizeof(elev), &len);
                        if (elev.TokenIsElevated || linkedIL >= SECURITY_MANDATORY_HIGH_RID) {
                            *out_token = linked.LinkedToken;
                            *out_selected_il = linkedIL ? linkedIL : SECURITY_MANDATORY_HIGH_RID;
                            return SP_TOKEN_OK;
                        }
                    }
                }
                CloseHandle(linked.LinkedToken);
            }

            if (strict_high_il) {
                return SP_TOKEN_ERR_INSUFFICIENT_INTEGRITY;
            } else {
                *out_token = hUserToken;
                *out_selected_il = sp_get_token_integrity_level(hUserToken);
                return SP_TOKEN_OK;
            }
        }
        case TokenElevationTypeFull: {
            // Already full elevated token. Do not switch to linked token!
            DWORD fullIL = sp_get_token_integrity_level(hUserToken);
            *out_token = hUserToken;
            *out_selected_il = fullIL ? fullIL : SECURITY_MANDATORY_HIGH_RID;
            return SP_TOKEN_OK;
        }
        case TokenElevationTypeDefault:
        default: {
            // No linked token exists. Check real rights and integrity level.
            DWORD defIL = sp_get_token_integrity_level(hUserToken);
            if (strict_high_il && defIL < SECURITY_MANDATORY_HIGH_RID) {
                // Standard user without elevation capability: cannot satisfy strict High IL requirement
                return SP_TOKEN_ERR_INSUFFICIENT_INTEGRITY;
            }
            *out_token = hUserToken;
            *out_selected_il = defIL;
            return SP_TOKEN_OK;
        }
    }
}

BOOL sp_start_in_session_ex(
    DWORD session_id,
    const wchar_t* exe,
    const wchar_t* cmdline,
    const wchar_t* workdir,
    bool strict_high_il,
    PROCESS_INFORMATION* out,
    HANDLE* out_job,
    SpLaunchStatus* out_status
) {
    if (out_status) *out_status = SP_TOKEN_ERR_PROCESS_CREATE_FAILED;
    if (!out) return FALSE;
    memset(out, 0, sizeof(PROCESS_INFORMATION));
    if (out_job) *out_job = NULL;

    if (session_id == 0) {
        if (out_status) *out_status = SP_TOKEN_ERR_NO_SESSION;
        return FALSE;
    }

    // Verify session did not change right before query
    DWORD current_console = WTSGetActiveConsoleSessionId();
    if (current_console != 0xFFFFFFFF && current_console != session_id) {
        if (out_status) *out_status = SP_TOKEN_ERR_NO_SESSION;
        return FALSE;
    }

    // Verify executable path is valid and absolute
    if (!exe || exe[0] == L'\0' || PathIsRelativeW(exe)) {
        if (out_status) *out_status = SP_TOKEN_ERR_PROCESS_CREATE_FAILED;
        return FALSE;
    }

    HANDLE hUserToken = NULL;
    if (!WTSQueryUserToken(session_id, &hUserToken)) {
        if (out_status) *out_status = SP_TOKEN_ERR_QUERY_USER_TOKEN;
        return FALSE;
    }

    // Save expected user SID for post-launch verification
    BYTE expectedUserBuf[256] = { 0 };
    PSID expectedSid = get_token_user_sid(hUserToken, expectedUserBuf, sizeof(expectedUserBuf));

    HANDLE hSelectedToken = NULL;
    DWORD selectedIL = 0;
    SpLaunchStatus selStatus = sp_select_target_token(hUserToken, session_id, strict_high_il, &hSelectedToken, &selectedIL);
    if (selStatus != SP_TOKEN_OK) {
        CloseHandle(hUserToken);
        if (out_status) *out_status = selStatus;
        return FALSE;
    }

    // Duplicate token with minimal required rights for CreateProcessAsUserW
    HANDLE hPrimaryToken = NULL;
    DWORD desired_access = TOKEN_ASSIGN_PRIMARY | TOKEN_DUPLICATE | TOKEN_QUERY | TOKEN_ADJUST_DEFAULT;
    if (!DuplicateTokenEx(hSelectedToken, desired_access, NULL, SecurityImpersonation, TokenPrimary, &hPrimaryToken)) {
        // Fallback to TOKEN_ALL_ACCESS if strict access mask fails
        if (!DuplicateTokenEx(hSelectedToken, TOKEN_ALL_ACCESS, NULL, SecurityImpersonation, TokenPrimary, &hPrimaryToken)) {
            if (hSelectedToken != hUserToken) CloseHandle(hSelectedToken);
            CloseHandle(hUserToken);
            if (out_status) *out_status = SP_TOKEN_ERR_DUPLICATE_FAILED;
            return FALSE;
        }
    }

    if (hSelectedToken != hUserToken) {
        CloseHandle(hSelectedToken);
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

    wchar_t cmdline_buf[2048] = { 0 };
    if (cmdline && cmdline[0] != L'\0') {
        wcsncpy_s(cmdline_buf, sizeof(cmdline_buf) / sizeof(wchar_t), cmdline, _TRUNCATE);
    } else {
        swprintf_s(cmdline_buf, sizeof(cmdline_buf) / sizeof(wchar_t), L"\"%ls\"", exe);
    }

    // 1. Create dedicated Job Object for process tree isolation
    HANDLE hJob = CreateJobObjectW(NULL, NULL);
    if (hJob) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli;
        memset(&jeli, 0, sizeof(jeli));
        jeli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(hJob, JobObjectExtendedLimitInformation, &jeli, sizeof(jeli));
    }

    // 2. Prepare creation flags: create suspended so child is placed in Job Object and validated before running
    DWORD creation_flags = CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT | CREATE_SUSPENDED;

    BOOL in_job = FALSE;
    if (IsProcessInJob(GetCurrentProcess(), NULL, &in_job) && in_job) {
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
        if (hJob) CloseHandle(hJob);
        if (out_status) *out_status = SP_TOKEN_ERR_PROCESS_CREATE_FAILED;
        return FALSE;
    }

    // 3. Post-launch verification: verify child process token SID, session, and IL
    HANDLE hChildToken = NULL;
    bool verify_ok = true;
    if (OpenProcessToken(out->hProcess, TOKEN_QUERY, &hChildToken)) {
        DWORD childSession = 0;
        DWORD len = 0;
        if (GetTokenInformation(hChildToken, TokenSessionId, &childSession, sizeof(childSession), &len)) {
            if (childSession != session_id) {
                verify_ok = false;
            }
        }
        if (expectedSid) {
            BYTE childUserBuf[256] = { 0 };
            PSID childSid = get_token_user_sid(hChildToken, childUserBuf, sizeof(childUserBuf));
            if (!childSid || !EqualSid(expectedSid, childSid)) {
                verify_ok = false;
            }
        }
        if (strict_high_il) {
            DWORD childIL = sp_get_token_integrity_level(hChildToken);
            if (childIL < SECURITY_MANDATORY_HIGH_RID) {
                verify_ok = false;
            }
        }
        CloseHandle(hChildToken);
    }

    if (!verify_ok) {
        TerminateProcess(out->hProcess, 1);
        CloseHandle(out->hProcess);
        CloseHandle(out->hThread);
        memset(out, 0, sizeof(PROCESS_INFORMATION));
        if (hJob) CloseHandle(hJob);
        if (out_status) *out_status = SP_TOKEN_ERR_PROCESS_VERIFY_FAILED;
        return FALSE;
    }

    // 4. Assign process to Job Object before resuming
    if (hJob) {
        AssignProcessToJobObject(hJob, out->hProcess);
        if (out_job) {
            *out_job = hJob;
        }
    }

    // 5. Resume main thread to begin execution
    ResumeThread(out->hThread);

    if (out_status) *out_status = SP_TOKEN_OK;
    return TRUE;
}

BOOL sp_start_in_session(DWORD session_id, const wchar_t* exe, const wchar_t* cmdline, const wchar_t* workdir, PROCESS_INFORMATION* out, HANDLE* out_job) {
    SpLaunchStatus status = SP_TOKEN_OK;
    return sp_start_in_session_ex(session_id, exe, cmdline, workdir, false, out, out_job, &status);
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
