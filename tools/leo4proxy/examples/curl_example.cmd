@echo off
setlocal EnableDelayedExpansion
chcp 65001 >nul

echo ================================================================
echo   Leo4 IoT cURL HTTP Discovery + mTLS API Verification
echo   Local Proxy: 127.0.0.1:18443 (SChannel mTLS)
echo ================================================================
echo.

echo [1] Querying Device Metadata (JSON) from Leo4Proxy:
curl -s http://127.0.0.1:18443/_leo4/info
echo.
echo.

echo [2] Querying Device SN only (Plaintext):
curl -s http://127.0.0.1:18443/_leo4/sn
echo.
echo.

echo [3] Querying SN directly from CLI tool:
..\bin\leo4proxy.exe --get-sn 2>nul || echo (leo4proxy binary not found in ..\bin)
echo.

echo [4] Performing mTLS LicenseBilling check through Proxy:
curl -s -X POST http://127.0.0.1:18443/licensebilling/ -d "function=check&Signature=TUI9MSBCQkhFNTMyMUI0MDAxNDkwfENQVT0xIEJGRUJGQkZGMDAwODA2QzE=" -H "Content-Type: application/x-www-form-urlencoded"
echo.
echo.

echo [5] For publishing MQTT events in a 10-minute cycle, run:
echo     curl_mosquitto_pub.cmd  (or mosquitto_pub_example.cmd)
echo.
echo ================================================================
