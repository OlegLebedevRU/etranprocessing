@echo off
setlocal EnableExtensions EnableDelayedExpansion
chcp 65001 >nul

set "ARG1=%~1"
set "ONCE=0"
if /i "%ARG1%"=="--once" set "ONCE=1"
if /i "%ARG1%"=="-1" set "ONCE=1"
if /i "%ARG1%"=="-once" set "ONCE=1"

echo ================================================================
echo   Leo4 IoT MQTTX CLI Event Publisher (extra_service role)
echo   Target: Mosquitto Bridge (127.0.0.1:1883, Plain TCP No-SSL)
echo   Interval: 10 minutes (600s), Topic: dev/{SN}/evt (QoS 1, Retain 0)
echo ================================================================
echo:

:: 1. Discover Device SN from local proxy
set "SN=a3b1234567c10221d290825"
for /f "usebackq tokens=*" %%A in (`curl -s -m 2 "http://127.0.0.1:18443/_leo4/sn"`) do (
    if not "%%A"=="" set "SN=%%A"
)
echo [INFO] Active Device SN: %SN%

:: 2. Check if mqttx CLI is installed
where mqttx.cmd >nul 2>&1
if %errorlevel% neq 0 (
    where mqttx >nul 2>&1
    if !errorlevel! neq 0 (
        echo [ERROR] MQTTX CLI 'mqttx' not found in PATH.
        echo To install MQTTX CLI globally, run: npm install -g @emqx/mqttx-cli
        echo Or use 'uv run python_client.py' / 'powershell_example.ps1' / 'curl_mosquitto_pub.cmd'.
        goto :eof
    )
)

set "TOPIC=dev/%SN%/evt"
set "EVENT_ID=36823"
set "ITERATION=0"

echo [PRESENCE] Publishing status: dev/%SN%/svc = svc_online (retain=true)
mqttx pub -h 127.0.0.1 -p 1883 -v 5 -u extra_service -i "%SN%_extra_pres" -t "dev/%SN%/svc" -m "svc_online" -r -q 1

:loop
set /a ITERATION+=1
set /a CURR_EVENT_ID=EVENT_ID + ITERATION - 1

:: Generate ISO timestamp, Unix epoch timestamp, and UUID via python one-liner
set "ISO_TIME=2026-08-03T12:41:33+03:00"
set "UNIX_TS=1740984093"
set "CORR_ID=d9afcbfa-3d2c-4304-8e69-f644fef29f1f"

for /f "usebackq tokens=1,2,3 delims=|" %%A in (`python -c "import datetime,time,uuid; now=datetime.datetime.now(datetime.timezone(datetime.timedelta(hours=3))); print(now.strftime('%%Y-%%m-%%dT%%H:%%M:%%S+03:00') + '|' + str(int(time.time())) + '|' + str(uuid.uuid4()))"`) do (
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

mqttx pub -h 127.0.0.1 -p 1883 -v 5 -u extra_service -i "%SN%_extra_mqttx" -t "%TOPIC%" -q 1 -m "!PAYLOAD!" --will-topic "dev/%SN%/svc" --will-message "svc_offline" --will-retain --will-qos 1 --user-properties "event_type_code:888" --user-properties "dev_event_id:!CURR_EVENT_ID!" --user-properties "dev_timestamp:!UNIX_TS!" --user-properties "correlation_id:!CORR_ID!"
if !errorlevel! equ 0 (
    echo     [ACK] Event delivered successfully via MQTTX CLI
) else (
    echo     [WARN] MQTTX CLI returned code !errorlevel!
)

if "%ONCE%"=="1" (
    echo:
    echo [INFO] Single event sent [--once]. Exiting.
    goto :shutdown
)

echo:
echo [SLEEP] Waiting 600 seconds (10 minutes) until next event publication...
echo Press Ctrl+C to stop.
timeout /t 600 /nobreak >nul
goto :loop

:shutdown
echo:
echo [PRESENCE] Publishing shutdown status: dev/%SN%/svc = svc_offline (retain=true)
mqttx pub -h 127.0.0.1 -p 1883 -v 5 -u extra_service -i "%SN%_extra_pres" -t "dev/%SN%/svc" -m "svc_offline" -r -q 1
echo [SUCCESS] MQTTX Extra Service terminated.
goto :eof
