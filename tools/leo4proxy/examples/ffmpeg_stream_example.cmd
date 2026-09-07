@echo off
setlocal enabledelayedexpansion

echo ===============================================================================
echo  Leo4Proxy - Video Stream Forwarder Example (ffmpeg -^> plain TCP -^> mTLS)
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
set "STREAM_TARGET=tcp://127.0.0.1:8554"
set "VIDEO_DEV=video=USB Camera"

if not "%~1"=="" (
    set "VIDEO_DEV=video=%~1"
)

echo [INFO] Video source: "%VIDEO_DEV%"
echo [INFO] Target endpoint: %STREAM_TARGET% (Leo4Proxy Stream Forwarder)
echo [INFO] Video format: MPEG-TS over TCP, H.264 ultrafast zerolatency, 800k bitrate
echo [INFO] Starting streaming loop (auto-reconnect on disconnect)...
echo Press Ctrl+C to stop.
echo.

:loop
echo [%DATE% %TIME%] Starting ffmpeg video stream...
"%FFMPEG_BIN%" -hide_banner -f dshow -i "%VIDEO_DEV%" -c:v libx264 -preset ultrafast -tune zerolatency -b:v 800k -f mpegts "%STREAM_TARGET%"
set "EXIT_CODE=%ERRORLEVEL%"

echo [%DATE% %TIME%] ffmpeg exited (code: %EXIT_CODE%).
echo Reconnecting in 3 seconds...
timeout /t 3 /nobreak >nul
goto :loop
