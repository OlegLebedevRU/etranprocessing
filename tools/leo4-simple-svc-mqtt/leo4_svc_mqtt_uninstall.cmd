@echo off
setlocal EnableExtensions

:: 1. Check and request Administrator privileges (UAC elevation)
net session >nul 2>&1
if errorlevel 1 (
    echo [INFO] Administrator privileges required. Requesting UAC elevation...
    powershell -NoProfile -ExecutionPolicy Bypass -Command "Start-Process -FilePath 'cmd.exe' -ArgumentList '/c \"\"%~f0\" %*\"' -Verb RunAs"
    exit /b 0
)

cd /d "%~dp0"

echo =======================================================
echo    Leo4 Simple Extra Service MQTT Uninstaller
echo =======================================================
echo.

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

if defined EXE_PATH (
    "%EXE_PATH%" --uninstall
) else (
    sc stop Leo4SimpleSvcMqtt >nul 2>&1
    sc delete Leo4SimpleSvcMqtt >nul 2>&1
)

echo.
echo [OK] Service Leo4SimpleSvcMqtt uninstalled.
ping -n 2 127.0.0.1 >nul 2>&1
exit /b 0
