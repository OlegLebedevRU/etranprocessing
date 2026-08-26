@echo off
setlocal EnableExtensions

:: ============================================================================
:: Leo4SimpleSvcMqtt - Install and Start Windows Service
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
echo     Leo4 Simple Extra Service MQTT Installer
echo =======================================================
echo.
echo [INFO] Working directory: %CD%

:: 3. Locate executable binary
set "EXE_PATH="
if exist "%~dp0leo4-simple-svc-mqtt.exe" (
    set "EXE_PATH=%~dp0leo4-simple-svc-mqtt.exe"
) else if exist "%~dp0bin\leo4-simple-svc-mqtt.exe" (
    set "EXE_PATH=%~dp0bin\leo4-simple-svc-mqtt.exe"
) else if exist "%~dp0bin\x86\leo4-simple-svc-mqtt.exe" (
    set "EXE_PATH=%~dp0bin\x86\leo4-simple-svc-mqtt.exe"
) else if exist "%~dp0bin\x64\leo4-simple-svc-mqtt.exe" (
    set "EXE_PATH=%~dp0bin\x64\leo4-simple-svc-mqtt.exe"
)

if not defined EXE_PATH goto :error_no_exe

echo [INFO] Binary executable: %EXE_PATH%
echo.

:: 4. Stop any previous instance
echo [1/3] Stopping previous instances if running...
sc stop Leo4SimpleSvcMqtt >nul 2>&1
taskkill /f /im leo4-simple-svc-mqtt.exe >nul 2>&1

:: 5. Register Windows Service in SCM
echo [2/3] Installing and registering Windows Service...
"%EXE_PATH%" --install %*
if errorlevel 1 (
    echo [WARNING] Direct CLI registration failed, trying SCM fallback...
    sc create Leo4SimpleSvcMqtt binPath= "\"%EXE_PATH%\" --service" start= auto DisplayName= "Leo4 Simple Extra Service MQTT Client" >nul 2>&1
    sc description Leo4SimpleSvcMqtt "Leo4 lightweight MQTT presence and LWT client (extra_service) for local Mosquitto bridge." >nul 2>&1
)

:: Verify service exists in SCM
sc query Leo4SimpleSvcMqtt >nul 2>&1
if errorlevel 1 goto :error_install

:: 6. Start Windows Service
echo [3/3] Starting Leo4SimpleSvcMqtt Windows Service...
"%EXE_PATH%" --start
if errorlevel 1 (
    echo [INFO] Retrying start via SCM directly...
    sc start Leo4SimpleSvcMqtt >nul 2>&1
)

:: Wait briefly and verify status
ping -n 3 127.0.0.1 >nul 2>&1
sc query Leo4SimpleSvcMqtt | findstr /I "RUNNING START_PENDING" >nul 2>&1
if errorlevel 1 goto :error_start

echo.
echo =======================================================
echo    [OK] Leo4SimpleSvcMqtt Service installed and started!
echo =======================================================
ping -n 2 127.0.0.1 >nul 2>&1
exit /b 0

:error_no_exe
echo.
echo *******************************************************
echo  [ERROR] Executable leo4-simple-svc-mqtt.exe not found!
echo *******************************************************
echo Please build the project (build.cmd) first.
echo.
pause
exit /b 1

:error_install
echo.
echo *******************************************************
echo  [ERROR] Failed to register Leo4SimpleSvcMqtt Windows Service!
echo *******************************************************
echo Please verify Administrator permissions.
echo.
pause
exit /b 1

:error_start
echo.
echo *******************************************************
echo  [ERROR] Leo4SimpleSvcMqtt Windows Service failed to start!
echo *******************************************************
echo Current service status:
sc query Leo4SimpleSvcMqtt
echo.
pause
exit /b 1
