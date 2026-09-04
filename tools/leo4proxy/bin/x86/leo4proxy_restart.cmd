@echo off
setlocal EnableExtensions

:: ============================================================================
:: Leo4Proxy - Restart Windows Service
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
echo              Leo4Proxy Service Restarter
echo =======================================================
echo.
echo [INFO] Working directory: %CD%

:: 3. Locate leo4proxy binary
set "EXE_PATH="
if exist "%~dp0leo4proxy.exe" (
    set "EXE_PATH=%~dp0leo4proxy.exe"
) else if exist "%~dp0bin\leo4proxy.exe" (
    set "EXE_PATH=%~dp0bin\leo4proxy.exe"
)

if not defined EXE_PATH goto :error_no_exe

echo [INFO] Binary executable: %EXE_PATH%
echo.

:: 4. Check if service is installed
sc query Leo4Proxy >nul 2>&1
if errorlevel 1 (
    echo [INFO] Leo4Proxy service is not installed in SCM. Installing and starting...
    "%EXE_PATH%" --install %*
    "%EXE_PATH%" --start
) else (
    echo [INFO] Restarting Leo4Proxy Windows Service...
    "%EXE_PATH%" --restart
)

:: Wait briefly and verify status
ping -n 3 127.0.0.1 >nul 2>&1
sc query Leo4Proxy | findstr /I "RUNNING START_PENDING" >nul 2>&1
if errorlevel 1 goto :error_restart

echo.
echo =======================================================
echo     [OK] Leo4Proxy Service RESTARTED successfully!
echo =======================================================
ping -n 2 127.0.0.1 >nul 2>&1
exit /b 0

:error_no_exe
echo.
echo *******************************************************
echo  [ERROR] Executable leo4proxy.exe not found!
echo *******************************************************
echo Please build the project (build.cmd) or place leo4proxy.exe in this folder.
echo.
pause
exit /b 1

:error_restart
echo.
echo *******************************************************
echo  [ERROR] Failed to restart Leo4Proxy Windows Service!
echo *******************************************************
echo Current service status:
sc query Leo4Proxy
echo.
echo Please check certificate in LocalMachine\MY and Windows Event Viewer.
echo.
pause
exit /b 1
