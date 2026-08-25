@echo off
setlocal EnableExtensions EnableDelayedExpansion
chcp 65001 >nul

set "ARG1=%~1"
set "ONCE=0"
if /i "%ARG1%"=="--once" set "ONCE=1"
if /i "%ARG1%"=="-1" set "ONCE=1"
if /i "%ARG1%"=="-once" set "ONCE=1"

echo ================================================================
echo   Leo4 IoT cURL + Mosquitto_pub Event Publisher (extra_service)
echo   Target: Mosquitto Bridge (127.0.0.1:1883, Plain TCP No-SSL)
echo   Interval: 10 minutes (600s), Topic: dev/{SN}/evt (QoS 1, Retain 0)
echo ================================================================
echo:

:: 1. Discover Device SN via cURL from local proxy
set "SN=a3b1234567c10221d290825"
for /f "usebackq tokens=*" %%A in (`curl -s -m 2 "http://127.0.0.1:18443/_leo4/sn"`) do (
    if not "%%A"=="" set "SN=%%A"
)
echo [INFO] Active Device SN: %SN%

:: 2. Locate mosquitto_pub.exe
set "PUB_EXE="
if exist "D:\Platerra26\tools\mosquitto\mosquitto_pub.exe" set "PUB_EXE=D:\Platerra26\tools\mosquitto\mosquitto_pub.exe"
if not defined PUB_EXE if exist "C:\Program Files\mosquitto\mosquitto_pub.exe" set "PUB_EXE=C:\Program Files\mosquitto\mosquitto_pub.exe"
if not defined PUB_EXE (
    where mosquitto_pub.exe >nul 2>&1
    if !errorlevel! equ 0 set "PUB_EXE=mosquitto_pub.exe"
)

if not defined PUB_EXE (
    echo [ERROR] mosquitto_pub.exe not found!
    echo Fallback: Use 'uv run python_client.py' or 'powershell_example.ps1'.
    goto :eof
)

set "TOPIC=dev/%SN%/evt"
set "EVENT_ID=36823"
set "ITERATION=0"

:loop
set /a ITERATION+=1
set /a CURR_EVENT_ID=EVENT_ID + ITERATION - 1

:: Generate ISO timestamp, Unix epoch timestamp, and UUID via python one-liner
set "ISO_TIME=2026-08-03T12:41:33+03:00"
set "UNIX_TS=1740984093"
set "CORR_ID=d9afcbfa-3d2c-4304-8e69-f644fef29f1f"

for /f "usebackq tokens=1,2,3 delims=|" %%A in (`python -c "from datetime import datetime,timezone,timedelta; from time import time; from uuid import uuid4; now=datetime.now(timezone(timedelta(hours=3))); print(now.strftime('%%Y-%%m-%%dT%%H:%%M:%%S+03:00') + '|' + str(int(time())) + '|' + str(uuid4()))"`) do (
    set "ISO_TIME=%%A"
    set "UNIX_TS=%%B"
    set "CORR_ID=%%C"
)

set "PAYLOAD={\"101\":%CURR_EVENT_ID%,\"102\":\"%ISO_TIME%\",\"200\":888,\"300\":[{\"301\":\"044AFE42C76781\",\"302\":6,\"303\":0}]}"

echo:
echo [%ISO_TIME%] Publishing Event #!ITERATION!:
echo   Topic:           %TOPIC%
echo   QoS:             1 (Retain: 0)
echo   Payload:         !PAYLOAD!
echo   User Properties: event_type_code=888, dev_event_id=!CURR_EVENT_ID!, dev_timestamp=!UNIX_TS!, correlation_id=!CORR_ID!

"!PUB_EXE!" -h 127.0.0.1 -p 1883 -V 5 -u extra_service -i "%SN%_extra_cmd" -t "%TOPIC%" -q 1 -m "!PAYLOAD!" -D publish user-property event_type_code 888 -D publish user-property dev_event_id !CURR_EVENT_ID! -D publish user-property dev_timestamp !UNIX_TS! -D publish user-property correlation_id !CORR_ID!
if !errorlevel! equ 0 (
    echo     [ACK] Event delivered successfully to Mosquitto Bridge!
) else (
    echo     [WARN] mosquitto_pub exited with error code !errorlevel!
)

if "%ONCE%"=="1" (
    echo:
    echo [INFO] Single event sent (--once). Exiting.
    goto :eof
)

echo:
echo [SLEEP] Waiting 600 seconds (10 minutes) until next event publication...
echo Press Ctrl+C to stop.
timeout /t 600 /nobreak >nul
goto :loop
