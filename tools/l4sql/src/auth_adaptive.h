#ifndef AUTH_ADAPTIVE_H
#define AUTH_ADAPTIVE_H

#include <windows.h>
#include <stdbool.h>
#include <sql.h>
#include <sqlext.h>
#include "xml_parser.h"

// Check if current process is running under NT AUTHORITY\SYSTEM (LocalSystem)
bool auth_is_running_as_system(void);

// Enable required privileges (SE_TCB_NAME, SE_ASSIGNPRIMARYTOKEN_NAME, SE_INCREASE_QUOTA_NAME)
bool auth_enable_system_privileges(void);

// Acquire active console user token (supports Windows 10 UAC Split-Token / TokenLinkedToken)
HANDLE auth_acquire_active_user_token(DWORD* out_session_id, wchar_t* out_username, size_t max_username, bool* out_is_elevated);

// Impersonate logged-on user on current thread
bool auth_impersonate(HANDLE hToken);

// Revert thread impersonation back to process security context
void auth_revert(void);

// Adaptive connection pipeline: attempts direct connection, and on access error (4060/18456 or SYSTEM context)
// automatically falls back to active console user impersonation
bool db_odbc_connect_adaptive(const DBConfig* cfg, SQLHENV* out_hEnv, SQLHDBC* out_hDbc,
                              char* err_msg, size_t err_msg_max, bool verbose);

// Maintenance action: Grant NT AUTHORITY\SYSTEM db_datareader rights in target database
bool l4sql_grant_system_access(SQLHDBC hDbc, const char* database, char* err_msg, size_t err_msg_max, bool verbose);

#endif // AUTH_ADAPTIVE_H
