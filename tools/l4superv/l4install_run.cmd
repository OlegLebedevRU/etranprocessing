@echo off
setlocal enabledelayedexpansion

set "INSTALL_EXE="
if exist "%~dp0l4install.exe" set "INSTALL_EXE=%~dp0l4install.exe"
if not defined INSTALL_EXE if exist "%~dp0bin\l4install.exe" set "INSTALL_EXE=%~dp0bin\l4install.exe"
if not defined INSTALL_EXE if exist "%~dp0bin\x64\l4install.exe" set "INSTALL_EXE=%~dp0bin\x64\l4install.exe"
if not defined INSTALL_EXE if exist "%~dp0bin\x86\l4install.exe" set "INSTALL_EXE=%~dp0bin\x86\l4install.exe"

if not defined INSTALL_EXE (
    echo [ERROR] l4install.exe not found. Please run build.cmd first.
    pause
    exit /b 1
)

net session >nul 2>&1
if %errorlevel% neq 0 (
    echo Requesting Administrator privileges...
    powershell -Command "Start-Process '%INSTALL_EXE%' -Verb RunAs -Wait"
    exit /b %errorlevel%
)

"%INSTALL_EXE%"
pause
