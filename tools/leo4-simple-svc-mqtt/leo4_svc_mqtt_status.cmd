@echo off
setlocal

cd /d "%~dp0"

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
    "%EXE_PATH%" --status
) else (
    sc query Leo4SimpleSvcMqtt
)
