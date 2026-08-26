@echo off
setlocal EnableExtensions

net session >nul 2>&1
if errorlevel 1 (
    echo [INFO] Administrator privileges required. Requesting UAC elevation...
    powershell -NoProfile -ExecutionPolicy Bypass -Command "Start-Process -FilePath 'cmd.exe' -ArgumentList '/c \"\"%~f0\" %*\"' -Verb RunAs"
    exit /b 0
)

cd /d "%~dp0"

echo [INFO] Stopping Leo4SimpleSvcMqtt service...
sc stop Leo4SimpleSvcMqtt
ping -n 2 127.0.0.1 >nul 2>&1
sc query Leo4SimpleSvcMqtt
exit /b 0
