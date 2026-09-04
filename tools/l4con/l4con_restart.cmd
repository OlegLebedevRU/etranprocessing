@echo off
setlocal EnableExtensions

net session >nul 2>&1
if errorlevel 1 (
    echo [INFO] Administrator privileges required. Requesting UAC elevation...
    powershell -NoProfile -ExecutionPolicy Bypass -Command "Start-Process -FilePath 'cmd.exe' -ArgumentList '/c \"\"%~f0\" %*\"' -Verb RunAs"
    exit /b 0
)

cd /d "%~dp0"

set "EXE_PATH="
if exist "%~dp0l4con.exe" (
    set "EXE_PATH=%~dp0l4con.exe"
) else if exist "%~dp0bin\l4con.exe" (
    set "EXE_PATH=%~dp0bin\l4con.exe"
)

if defined EXE_PATH (
    "%EXE_PATH%" --restart
) else (
    sc stop L4Con >nul 2>&1
    ping -n 3 127.0.0.1 >nul 2>&1
    sc start L4Con
)
ping -n 2 127.0.0.1 >nul 2>&1
