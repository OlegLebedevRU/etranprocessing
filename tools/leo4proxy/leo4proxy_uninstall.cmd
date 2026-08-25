@echo off
setlocal EnableExtensions

:: ============================================================================
:: Leo4Proxy - Stop and Uninstall Windows Service
:: ============================================================================

:: 1. Check and request Administrator privileges (UAC elevation)
net session >nul 2>&1
if errorlevel 1 (
    echo [INFO] Administrator privileges required. Requesting UAC elevation...
    powershell -NoProfile -ExecutionPolicy Bypass -Command "Start-Process -FilePath 'cmd.exe' -ArgumentList '/c \"\"%~f0\" %*\"' -Verb RunAs"
    exit /b 0
)

:: 2. Switch to script directory
cd /d "%~dp0"

echo =======================================================
echo          Leo4Proxy Service Uninstaller
echo =======================================================
echo.
echo [INFO] Working directory: %CD%

:: 3. Locate leo4proxy binary (optional)
set "EXE_PATH="
if exist "%~dp0leo4proxy.exe" (
    set "EXE_PATH=%~dp0leo4proxy.exe"
) else if exist "%~dp0bin\leo4proxy.exe" (
    set "EXE_PATH=%~dp0bin\leo4proxy.exe"
) else if exist "%~dp0bin\x86\leo4proxy.exe" (
    set "EXE_PATH=%~dp0bin\x86\leo4proxy.exe"
) else if exist "%~dp0bin\x64\leo4proxy.exe" (
    set "EXE_PATH=%~dp0bin\x64\leo4proxy.exe"
)

:: 4. Stop service and terminate processes
echo [1/3] Stopping Leo4Proxy service and terminating processes...
if defined EXE_PATH (
    "%EXE_PATH%" --stop >nul 2>&1
)
sc stop Leo4Proxy >nul 2>&1
taskkill /f /im leo4proxy.exe >nul 2>&1

:: 5. Uninstall service from Service Control Manager
echo [2/3] Deleting Leo4Proxy service from SCM...
if defined EXE_PATH (
    "%EXE_PATH%" --uninstall >nul 2>&1
)
sc delete Leo4Proxy >nul 2>&1

:: 6. Clean up firewall rules
echo [3/3] Removing Windows Defender Firewall rules...
netsh advfirewall firewall delete rule name="Leo4Proxy HTTPS" >nul 2>&1
netsh advfirewall firewall delete rule name="Leo4Proxy HTTP" >nul 2>&1
netsh advfirewall firewall delete rule name="Leo4Proxy mDNS" >nul 2>&1
netsh advfirewall firewall delete rule name="Leo4Proxy LLMNR" >nul 2>&1
netsh advfirewall firewall delete rule name="Leo4Proxy" >nul 2>&1

:: Verify deletion
ping -n 2 127.0.0.1 >nul 2>&1
sc query Leo4Proxy >nul 2>&1
if not errorlevel 1 (
    sc query Leo4Proxy | findstr /I "STOPPED" >nul 2>&1
    if errorlevel 1 goto :error_uninstall
)

echo.
echo =======================================================
echo   [OK] Leo4Proxy Service uninstalled successfully!
echo =======================================================
ping -n 2 127.0.0.1 >nul 2>&1
exit /b 0

:error_uninstall
echo.
echo *******************************************************
echo  [ERROR] Failed to completely remove Leo4Proxy service!
echo *******************************************************
echo Current service status:
sc query Leo4Proxy
echo.
pause
exit /b 1
