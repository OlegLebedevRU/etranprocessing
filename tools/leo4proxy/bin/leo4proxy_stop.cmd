@echo off
setlocal EnableExtensions

:: ============================================================================
:: Leo4Proxy - Stop Windows Service
:: ============================================================================

:: 1. Check and request Administrator privileges (UAC elevation)
net session >nul 2>&1
if errorlevel 1 (
    echo [INFO] Administrator privileges required. Requesting UAC elevation...
    powershell -NoProfile -ExecutionPolicy Bypass -Command "Start-Process -FilePath 'cmd.exe' -ArgumentList '/c \"\"%~f0\" %*\"' -Verb RunAs"
    exit /b 0
)

:: 2. Switch to script directory
cd /d "%~dp0"

echo =======================================================
echo              Leo4Proxy Service Stopper
echo =======================================================
echo.
echo [INFO] Working directory: %CD%

:: 3. Locate leo4proxy binary (optional)
set "EXE_PATH="
if exist "%~dp0leo4proxy.exe" (
    set "EXE_PATH=%~dp0leo4proxy.exe"
) else if exist "%~dp0bin\leo4proxy.exe" (
    set "EXE_PATH=%~dp0bin\leo4proxy.exe"
)

:: 4. Stop service if installed
sc query Leo4Proxy >nul 2>&1
if not errorlevel 1 (
    echo [INFO] Sending stop signal to Leo4Proxy service...
    if defined EXE_PATH (
        "%EXE_PATH%" --stop >nul 2>&1
    )
    sc stop Leo4Proxy >nul 2>&1
) else (
    echo [INFO] Leo4Proxy service is not registered in SCM.
)

:: 5. Terminate any remaining background processes
taskkill /f /im leo4proxy.exe >nul 2>&1

:: Wait briefly and verify stopped state
ping -n 2 127.0.0.1 >nul 2>&1
sc query Leo4Proxy >nul 2>&1
if not errorlevel 1 (
    sc query Leo4Proxy | findstr /I "RUNNING" >nul 2>&1
    if not errorlevel 1 goto :error_stop
)

echo.
echo =======================================================
echo        [OK] Leo4Proxy Service is STOPPED!
echo =======================================================
ping -n 2 127.0.0.1 >nul 2>&1
exit /b 0

:error_stop
echo.
echo *******************************************************
echo  [ERROR] Failed to stop Leo4Proxy Windows Service!
echo *******************************************************
echo Current service status:
sc query Leo4Proxy
echo.
pause
exit /b 1
