@echo off
setlocal EnableExtensions

:: ============================================================================
:: L4Con - Install and Start Windows Service
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
echo     L4Con Remote Diagnostics Service Installer
echo =======================================================
echo.
echo [INFO] Working directory: %CD%

:: 3. Locate executable binary
set "EXE_PATH="
if exist "%~dp0l4con.exe" (
    set "EXE_PATH=%~dp0l4con.exe"
) else if exist "%~dp0bin\l4con.exe" (
    set "EXE_PATH=%~dp0bin\l4con.exe"
) else if exist "%~dp0bin\x86\l4con.exe" (
    set "EXE_PATH=%~dp0bin\x86\l4con.exe"
) else if exist "%~dp0bin\x64\l4con.exe" (
    set "EXE_PATH=%~dp0bin\x64\l4con.exe"
)

if not defined EXE_PATH goto :error_no_exe

echo [INFO] Binary executable: %EXE_PATH%
echo.

:: 4. Stop any previous instance
echo [1/3] Stopping previous instances if running...
sc stop L4Con >nul 2>&1
taskkill /f /im l4con.exe >nul 2>&1

:: 5. Register Windows Service in SCM
echo [2/3] Installing and registering Windows Service...
"%EXE_PATH%" --install %*
if errorlevel 1 (
    echo [WARNING] Direct CLI registration failed, trying SCM fallback...
    sc create L4Con binPath= "\"%EXE_PATH%\" --service" start= auto DisplayName= "Leo4 Remote Diagnostics and Console Agent (l4con)" >nul 2>&1
    sc description L4Con "Leo4 lightweight MQTT diagnostic console client (extra_service) executing Windows commands and streaming output." >nul 2>&1
)

:: Verify service exists in SCM
sc query L4Con >nul 2>&1
if errorlevel 1 goto :error_install

:: 6. Start Windows Service
echo [3/3] Starting L4Con Windows Service...
"%EXE_PATH%" --start
if errorlevel 1 (
    echo [INFO] Retrying start via SCM directly...
    sc start L4Con >nul 2>&1
)

:: Wait briefly and verify status
ping -n 3 127.0.0.1 >nul 2>&1
sc query L4Con | findstr /I "RUNNING START_PENDING" >nul 2>&1
if errorlevel 1 goto :error_start

echo.
echo =======================================================
echo    [OK] L4Con Service installed and started!
echo =======================================================
ping -n 2 127.0.0.1 >nul 2>&1
exit /b 0

:error_no_exe
echo.
echo *******************************************************
echo  [ERROR] Executable l4con.exe not found!
echo *******************************************************
echo Please build the project (build.cmd) first.
echo.
pause
exit /b 1

:error_install
echo.
echo *******************************************************
echo  [ERROR] Failed to register L4Con Windows Service!
echo *******************************************************
echo Please verify Administrator permissions.
echo.
pause
exit /b 1

:error_start
echo.
echo *******************************************************
echo  [ERROR] L4Con Windows Service failed to start!
echo *******************************************************
echo Current service status:
sc query L4Con
echo.
pause
exit /b 1
