#include "auth_adaptive.h"
#include "db_odbc.h"

#include <windows.h>
#include <wtsapi32.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma comment(lib, "wtsapi32.lib")
#pragma comment(lib, "advapi32.lib")

#ifndef SECURITY_MANDATORY_HIGH_RID
#define SECURITY_MANDATORY_HIGH_RID (0x00003000L)
#endif

bool auth_is_running_as_system(void) {
    HANDLE hToken = NULL;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken)) {
        return false;
    }

    BYTE buf[256];
    DWORD len = 0;
    bool is_sys = false;

    if (GetTokenInformation(hToken, TokenUser, buf, sizeof(buf), &len)) {
        TOKEN_USER* pUser = (TOKEN_USER*)buf;
        SID_IDENTIFIER_AUTHORITY nt_auth = SECURITY_NT_AUTHORITY;
        PSID sys_sid = NULL;
        if (AllocateAndInitializeSid(&nt_auth, 1, SECURITY_LOCAL_SYSTEM_RID, 0, 0, 0, 0, 0, 0, 0, &sys_sid)) {
            if (EqualSid(pUser->User.Sid, sys_sid)) {
                is_sys = true;
            }
            FreeSid(sys_sid);
        }
    }

    CloseHandle(hToken);
    return is_sys;
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

bool auth_enable_system_privileges(void) {
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

HANDLE auth_acquire_active_user_token(DWORD* out_session_id, wchar_t* out_username, size_t max_username, bool* out_is_elevated) {
    if (out_is_elevated) *out_is_elevated = false;
    if (out_session_id) *out_session_id = 0;
    if (out_username && max_username > 0) out_username[0] = L'\0';

    DWORD sessionId = WTSGetActiveConsoleSessionId();
    if (sessionId == 0xFFFFFFFF || sessionId == 0) {
        PWTS_SESSION_INFOW pSessions = NULL;
        DWORD count = 0;
        if (WTSEnumerateSessionsW(WTS_CURRENT_SERVER_HANDLE, 0, 1, &pSessions, &count) && pSessions) {
            for (DWORD i = 0; i < count; i++) {
                if (pSessions[i].State == WTSActive && pSessions[i].SessionId != 0) {
                    sessionId = pSessions[i].SessionId;
                    break;
                }
            }
            WTSFreeMemory(pSessions);
        }
    }

    if (sessionId == 0xFFFFFFFF || sessionId == 0) {
        return NULL;
    }

    if (out_session_id) *out_session_id = sessionId;

    LPWSTR pUser = NULL;
    DWORD bytes = 0;
    if (WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE, sessionId, WTSUserName, &pUser, &bytes)) {
        if (pUser && pUser[0] != L'\0' && out_username && max_username > 0) {
            wcsncpy(out_username, pUser, max_username - 1);
            out_username[max_username - 1] = L'\0';
        }
        if (pUser) WTSFreeMemory(pUser);
    }

    HANDLE hUserToken = NULL;
    if (!WTSQueryUserToken(sessionId, &hUserToken)) {
        return NULL;
    }

    TOKEN_ELEVATION_TYPE elevType = TokenElevationTypeDefault;
    DWORD len = 0;
    if (GetTokenInformation(hUserToken, TokenElevationType, &elevType, sizeof(elevType), &len)) {
        if (elevType == TokenElevationTypeLimited) {
            TOKEN_LINKED_TOKEN linked = { 0 };
            if (GetTokenInformation(hUserToken, TokenLinkedToken, &linked, sizeof(linked), &len) && linked.LinkedToken) {
                CloseHandle(hUserToken);
                if (out_is_elevated) *out_is_elevated = true;
                return linked.LinkedToken;
            }
        } else if (elevType == TokenElevationTypeFull) {
            if (out_is_elevated) *out_is_elevated = true;
        }
    }

    return hUserToken;
}

bool auth_impersonate(HANDLE hToken) {
    if (!hToken || hToken == INVALID_HANDLE_VALUE) return false;
    return ImpersonateLoggedOnUser(hToken) != 0;
}

void auth_revert(void) {
    RevertToSelf();
}

bool db_odbc_connect_adaptive(const DBConfig* cfg, SQLHENV* out_hEnv, SQLHDBC* out_hDbc,
                              char* err_msg, size_t err_msg_max, bool verbose) {
    if (!cfg || !out_hEnv || !out_hDbc) return false;

    // 1. Try standard direct connection first
    char direct_err[1024] = {0};
    if (db_odbc_connect(cfg, out_hEnv, out_hDbc, direct_err, sizeof(direct_err), verbose)) {
        return true;
    }

    // If explicit SQL Authentication was configured, Windows user impersonation will not alter SQL Auth credentials
    if (cfg->auth_type != 0 && strlen(cfg->user) > 0) {
        if (err_msg && err_msg_max > 0) {
            strncpy(err_msg, direct_err, err_msg_max - 1);
            err_msg[err_msg_max - 1] = '\0';
        }
        return false;
    }

    // 2. Check if failure indicates permission issue (4060 = Cannot open DB, 18456 = Login failed) or running as SYSTEM
    bool is_sys = auth_is_running_as_system();
    bool is_access_err = (strstr(direct_err, "4060") != NULL || strstr(direct_err, "18456") != NULL);

    if (!is_sys && !is_access_err) {
        if (err_msg && err_msg_max > 0) {
            strncpy(err_msg, direct_err, err_msg_max - 1);
            err_msg[err_msg_max - 1] = '\0';
        }
        return false;
    }

    if (verbose) {
        printf("[auth] Direct connection failed (%s). Engaging adaptive user impersonation...\n",
               is_sys ? "running as NT AUTHORITY\\SYSTEM" : "access denied");
    }

    // 3. Acquire active console session token
    auth_enable_system_privileges();

    DWORD session_id = 0;
    wchar_t username[128] = {0};
    bool is_elevated = false;
    HANDLE hUserToken = auth_acquire_active_user_token(&session_id, username, sizeof(username)/sizeof(wchar_t), &is_elevated);

    if (!hUserToken) {
        if (verbose) {
            printf("[auth] No active interactive console session available for token acquisition.\n");
        }
        if (err_msg && err_msg_max > 0) {
            snprintf(err_msg, err_msg_max,
                     "%s\n[Adaptive Auth] Could not acquire active console user token for fallback.",
                     direct_err);
        }
        return false;
    }

    char u8_username[256] = {0};
    WideCharToMultiByte(CP_UTF8, 0, username, -1, u8_username, sizeof(u8_username), NULL, NULL);

    if (verbose) {
        printf("[auth] Acquired user token: '%s' in session %lu (Elevation: %s)\n",
               u8_username, session_id, is_elevated ? "High IL" : "Medium/Default IL");
    }

    // 4. Impersonate active user on current thread
    if (!auth_impersonate(hUserToken)) {
        CloseHandle(hUserToken);
        if (verbose) {
            printf("[auth] ImpersonateLoggedOnUser failed with Win32 Error %lu\n", GetLastError());
        }
        if (err_msg && err_msg_max > 0) {
            strncpy(err_msg, direct_err, err_msg_max - 1);
            err_msg[err_msg_max - 1] = '\0';
        }
        return false;
    }

    if (verbose) {
        printf("[auth] Thread impersonation active as '%s'. Retrying MS SQL connection...\n", u8_username);
    }

    // 5. Retry connection under impersonated context
    char retry_err[1024] = {0};
    bool retried = db_odbc_connect(cfg, out_hEnv, out_hDbc, retry_err, sizeof(retry_err), verbose);

    // 6. Revert impersonation immediately (authenticated ODBC session stays bound on server side)
    auth_revert();
    CloseHandle(hUserToken);

    if (retried) {
        if (verbose) {
            printf("[auth] Connected successfully using adaptive impersonation of '%s'!\n", u8_username);
        }
        return true;
    }

    if (verbose) {
        printf("[auth] Impersonated connection attempt failed: %s\n", retry_err);
    }

    if (err_msg && err_msg_max > 0) {
        snprintf(err_msg, err_msg_max,
                 "%s\n[Adaptive Auth] Impersonated connection attempt as '%s' failed:\n%s",
                 direct_err, u8_username, retry_err);
    }
    return false;
}

bool l4sql_grant_system_access(SQLHDBC hDbc, const char* database, char* err_msg, size_t err_msg_max, bool verbose) {
    if (!hDbc || !database) return false;

    // Use well-known SID S-1-5-18 (0x010100000000000512000000) to support both localized OS names:
    // Russian Windows: 'NT AUTHORITY\СИСТЕМА'
    // English Windows: 'NT AUTHORITY\SYSTEM'
    // Uses sp_addrolemember for universal compatibility with SQL Server 2008+ (Win7 SP1 & Win10).
    char sql_batch[2048];
    snprintf(sql_batch, sizeof(sql_batch),
        "DECLARE @sysName NVARCHAR(256); "
        "SET @sysName = SUSER_SNAME(0x010100000000000512000000); "
        "IF @sysName IS NOT NULL "
        "BEGIN "
        "    IF NOT EXISTS (SELECT 1 FROM sys.server_principals WHERE sid = 0x010100000000000512000000) "
        "    BEGIN "
        "        DECLARE @createLogin NVARCHAR(MAX); "
        "        SET @createLogin = N'CREATE LOGIN [' + REPLACE(@sysName, ']', ']]') + N'] FROM WINDOWS;'; "
        "        EXEC (@createLogin); "
        "    END; "
        "    DECLARE @sql NVARCHAR(MAX); "
        "    SET @sql = N'USE [' + REPLACE('%s', ']', ']]') + N']; ' + "
        "               N'DECLARE @dbUser NVARCHAR(256); ' + "
        "               N'SELECT @dbUser = name FROM sys.database_principals WHERE sid = 0x010100000000000512000000; ' + "
        "               N'IF @dbUser IS NULL ' + "
        "               N'BEGIN ' + "
        "               N'    SET @dbUser = N''SystemUser''; ' + "
        "               N'    DECLARE @createUser NVARCHAR(MAX); ' + "
        "               N'    SET @createUser = N''CREATE USER ['' + @dbUser + N''] FOR LOGIN ['' + REPLACE(N''' + REPLACE(@sysName, '''', '''''') + N''', '']'', '']]'') + N''];''; ' + "
        "               N'    EXEC (@createUser); ' + "
        "               N'END; ' + "
        "               N'EXEC sp_addrolemember ''db_datareader'', @dbUser;'; "
        "    EXEC (@sql); "
        "END;",
        database);

    if (verbose) {
        printf("[grant] Executing self-provisioning batch:\n%s\n", sql_batch);
    }

    SQLHSTMT hStmt = NULL;
    SQLRETURN ret = SQLAllocHandle(SQL_HANDLE_STMT, hDbc, &hStmt);
    if (!SQL_SUCCEEDED(ret)) {
        if (err_msg && err_msg_max > 0) snprintf(err_msg, err_msg_max, "Failed to allocate SQL statement handle");
        return false;
    }

    ret = SQLExecDirectA(hStmt, (SQLCHAR*)sql_batch, SQL_NTS);
    if (!SQL_SUCCEEDED(ret)) {
        SQLWCHAR sqlStateW[6];
        SQLINTEGER nativeError;
        SQLWCHAR msgW[512];
        SQLSMALLINT textLength;
        if (SQLGetDiagRecW(SQL_HANDLE_STMT, hStmt, 1, sqlStateW, &nativeError, msgW, 512, &textLength) == SQL_SUCCESS) {
            char state_u8[32] = {0};
            char msg_u8[1024] = {0};
            WideCharToMultiByte(CP_UTF8, 0, (LPCWCH)sqlStateW, -1, state_u8, sizeof(state_u8), NULL, NULL);
            WideCharToMultiByte(CP_UTF8, 0, (LPCWCH)msgW, -1, msg_u8, sizeof(msg_u8), NULL, NULL);
            if (err_msg && err_msg_max > 0) {
                snprintf(err_msg, err_msg_max, "[%s] (Error %ld): %s", state_u8, (long)nativeError, msg_u8);
            }
        } else {
            if (err_msg && err_msg_max > 0) snprintf(err_msg, err_msg_max, "SQLExecDirect failed");
        }
        SQLFreeHandle(SQL_HANDLE_STMT, hStmt);
        return false;
    }

    SQLFreeHandle(SQL_HANDLE_STMT, hStmt);
    return true;
}
