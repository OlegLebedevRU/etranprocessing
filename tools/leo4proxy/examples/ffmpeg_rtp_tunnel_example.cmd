@echo off
setlocal enabledelayedexpansion

echo ===============================================================================
echo  Leo4Proxy - Primary Video RTP/RTCP Tunnel Example (ffmpeg -^> UDP -^> mTLS)
echo ===============================================================================
echo.

:: 1. Check if ffmpeg is available in PATH or standard paths
set "FFMPEG_BIN=ffmpeg"
where ffmpeg >nul 2>&1
if errorlevel 1 (
    if exist "D:\ffmpeg\bin\ffmpeg.exe" (
        set "FFMPEG_BIN=D:\ffmpeg\bin\ffmpeg.exe"
    ) else if exist "C:\ffmpeg\bin\ffmpeg.exe" (
        set "FFMPEG_BIN=C:\ffmpeg\bin\ffmpeg.exe"
    ) else (
        echo [WARNING] ffmpeg.exe was not found in PATH or standard paths.
        echo Please ensure ffmpeg is installed and available in PATH.
    )
)

echo [INFO] Using ffmpeg binary: %FFMPEG_BIN%
echo.

:: 2. List DirectShow video and audio devices
echo -------------------------------------------------------------------------------
echo Listing available DirectShow devices on this system:
echo -------------------------------------------------------------------------------
"%FFMPEG_BIN%" -list_devices true -f dshow -i dummy 2>&1
echo -------------------------------------------------------------------------------
echo.

:: 3. Configure streaming parameters
set "RTP_TARGET=rtp://127.0.0.1:5004?rtcpport=5005"
set "VIDEO_DEV=video=USB Camera"

if not "%~1"=="" (
    set "VIDEO_DEV=video=%~1"
)

echo [INFO] Video source: "%VIDEO_DEV%"
echo [INFO] Target endpoint: %RTP_TARGET% (Leo4Proxy RTP/RTCP UDP Tunnel)
echo [INFO] Video format: RTP/RTCP UDP, H.264 ultrafast zerolatency, 800k bitrate
echo [INFO] Remote ingress: dev.leo4.ru:8443 (L4RTP/1 framed mTLS, Lazy Connect)
echo [INFO] Starting streaming loop (auto-reconnect on disconnect)...
echo Press Ctrl+C to stop.
echo.

:loop
echo [%DATE% %TIME%] Starting ffmpeg RTP/RTCP video stream...
"%FFMPEG_BIN%" -hide_banner -f dshow -i "%VIDEO_DEV%" -c:v libx264 -preset ultrafast -tune zerolatency -b:v 800k -f rtp "%RTP_TARGET%"
set "EXIT_CODE=%ERRORLEVEL%"

echo [%DATE% %TIME%] ffmpeg exited (code: %EXIT_CODE%).
echo Reconnecting in 3 seconds...
timeout /t 3 /nobreak >nul
goto :loop
