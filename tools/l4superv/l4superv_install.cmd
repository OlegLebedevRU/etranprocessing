@echo off
setlocal enabledelayedexpansion

:: Auto-detect binary location
set "SUPERV_EXE="
if exist "%~dp0l4superv.exe" set "SUPERV_EXE=%~dp0l4superv.exe"
if not defined SUPERV_EXE if exist "%~dp0bin\l4superv.exe" set "SUPERV_EXE=%~dp0bin\l4superv.exe"

if not defined SUPERV_EXE (
    echo [ERROR] l4superv.exe not found. Please run build.cmd first.
    pause
    exit /b 1
)

:: Run with UAC elevation if needed
net session >nul 2>&1
if %errorlevel% neq 0 (
    echo Requesting Administrator privileges...
    powershell -Command "Start-Process '%SUPERV_EXE%' -ArgumentList '--install' -Verb RunAs -Wait"
    exit /b %errorlevel%
)

"%SUPERV_EXE%" --install
pause
