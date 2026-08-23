@echo off
echo ================================================================
echo   Leo4 IoT cURL Examples via Leo4Proxy (127.0.0.1:18443)
echo ================================================================
echo.

echo [1] Querying Device Metadata from Leo4Proxy:
curl -s http://127.0.0.1:18443/_leo4/info
echo.
echo.

echo [2] Querying Device SN only:
curl -s http://127.0.0.1:18443/_leo4/sn
echo.
echo.

echo [3] Querying SN directly from CLI tool:
..\bin\leo4proxy.exe --get-sn
echo.

echo [4] Performing mTLS LicenseBilling check through Proxy:
curl -s -X POST http://127.0.0.1:18443/licensebilling/ -d "function=check&Signature=TUI9MSBCQkhFNTMyMUI0MDAxNDkwfENQVT0xIEJGRUJGQkZGMDAwODA2QzE=" -H "Content-Type: application/x-www-form-urlencoded"
echo.
echo.
echo ================================================================
