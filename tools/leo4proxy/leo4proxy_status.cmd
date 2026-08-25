@echo off
setlocal EnableExtensions

:: ============================================================================
:: Leo4Proxy - Check Windows Service and Certificate Status
:: ============================================================================

cd /d "%~dp0"

echo =======================================================
echo          Leo4Proxy Service and Component Status
echo =======================================================
echo.

set "EXE_PATH="
if exist "%~dp0leo4proxy.exe" (
    set "EXE_PATH=%~dp0leo4proxy.exe"
) else if exist "%~dp0bin\leo4proxy.exe" (
    set "EXE_PATH=%~dp0bin\leo4proxy.exe"
) else if exist "%~dp0bin\x86\leo4proxy.exe" (
    set "EXE_PATH=%~dp0bin\x86\leo4proxy.exe"
) else if exist "%~dp0bin\x64\leo4proxy.exe" (
    set "EXE_PATH=%~dp0bin\x64\leo4proxy.exe"
)

echo --- [1] Windows Service Status (SCM) ---
sc query Leo4Proxy
echo.

if defined EXE_PATH (
    echo --- [2] Service Status via CLI ---
    "%EXE_PATH%" --status
    echo.
    echo --- [3] Terminal Certificate and SChannel Test ---
    "%EXE_PATH%" --test-cert
) else (
    echo [WARNING] Binary leo4proxy.exe not found for detailed diagnostics.
)

echo.
echo =======================================================
echo Press any key to close this window...
pause >nul
exit /b 0
