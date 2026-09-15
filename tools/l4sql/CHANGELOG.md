# Changelog: l4sql

All notable changes to the `l4sql` utility will be documented in this file.

## [1.7.6] — 2026-09-15

### Added
- **Adaptive Authentication Pipeline (`auth_adaptive.c` / `auth_adaptive.h`)**:
  - Implemented automatic fallback to active interactive console user impersonation (`WTSGetActiveConsoleSessionId`, `WTSQueryUserToken`, `ImpersonateLoggedOnUser`).
  - Added Windows 10 UAC Split-Token handling: automatic extraction of `TokenLinkedToken` (High Integrity Level) to ensure permissions granted to `BUILTIN\Administrators` are honored under UAC.
  - Resolves MS SQL Server Error 4060 ("Cannot open database requested by the login") when executed by `l4con` or background services under `NT AUTHORITY\SYSTEM`.
- **Self-Provisioning Option (`-G, --grant-system-access`)**:
  - Automatically provisions `NT AUTHORITY\SYSTEM` (resolved cross-language via well-known SID `S-1-5-18`) as a database user with `db_datareader` permissions in the active database.
  - Fully idempotent and compatible with MS SQL Server 2008+ (Windows 7 SP1 & Windows 10).

### Fixed
- **Complete Elimination of Mojibake (Encoding Distortion)**:
  - Switched ODBC error diagnostics from `SQLGetDiagRecA` to `SQLGetDiagRecW` with conversion to UTF-8 via `WideCharToMultiByte(CP_UTF8, ...)`.
  - Switched column metadata retrieval from `SQLDescribeColA` to `SQLDescribeColW`.
  - Switched row data fetching from `SQL_C_CHAR` to `SQL_C_WCHAR` with conversion to UTF-8.
  - Fixed table formatting in `output_formatter.c`: visual character width calculation (`utf8_char_count`) and boundary-safe copying (`utf8_safe_copy`) to prevent multi-byte UTF-8 sequence corruption.
