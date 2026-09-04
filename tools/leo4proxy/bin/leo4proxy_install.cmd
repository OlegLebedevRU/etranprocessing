@echo off
setlocal EnableExtensions

:: ============================================================================
:: Leo4Proxy - Install and Start Windows Service
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
echo          Leo4Proxy Windows Service Installer
echo =======================================================
echo.
echo [INFO] Working directory: %CD%

:: 3. Locate leo4proxy binary
set "EXE_PATH="
if exist "%~dp0leo4proxy.exe" (
    set "EXE_PATH=%~dp0leo4proxy.exe"
) else if exist "%~dp0bin\leo4proxy.exe" (
    set "EXE_PATH=%~dp0bin\leo4proxy.exe"
)

if not defined EXE_PATH goto :error_no_exe

echo [INFO] Binary executable: %EXE_PATH%
echo.

:: 4. Stop any previous instances or services
echo [1/4] Stopping previous instances of Leo4Proxy...
sc stop Leo4Proxy >nul 2>&1
taskkill /f /im leo4proxy.exe >nul 2>&1

:: 5. Configure Windows Defender Firewall rules
echo [2/4] Configuring Windows Defender Firewall rules...
netsh advfirewall firewall delete rule name="Leo4Proxy HTTPS" >nul 2>&1
netsh advfirewall firewall add rule name="Leo4Proxy HTTPS" dir=in action=allow protocol=TCP localport=443 program="%EXE_PATH%" enable=yes profile=any description="Allow inbound HTTPS for Leo4Proxy" >nul 2>&1

netsh advfirewall firewall delete rule name="Leo4Proxy HTTP" >nul 2>&1
netsh advfirewall firewall add rule name="Leo4Proxy HTTP" dir=in action=allow protocol=TCP localport=80 program="%EXE_PATH%" enable=yes profile=any description="Allow inbound HTTP for Leo4Proxy" >nul 2>&1

netsh advfirewall firewall delete rule name="Leo4Proxy mDNS" >nul 2>&1
netsh advfirewall firewall add rule name="Leo4Proxy mDNS" dir=in action=allow protocol=UDP localport=5353 program="%EXE_PATH%" enable=yes profile=any description="Allow inbound mDNS Discovery for Leo4Proxy" >nul 2>&1

netsh advfirewall firewall delete rule name="Leo4Proxy LLMNR" >nul 2>&1
netsh advfirewall firewall add rule name="Leo4Proxy LLMNR" dir=in action=allow protocol=UDP localport=5355 program="%EXE_PATH%" enable=yes profile=any description="Allow inbound LLMNR Resolution for Leo4Proxy" >nul 2>&1

netsh advfirewall firewall delete rule name="Leo4Proxy" >nul 2>&1
netsh advfirewall firewall add rule name="Leo4Proxy" dir=in action=allow program="%EXE_PATH%" enable=yes profile=any description="Allow all network traffic for Leo4Proxy" >nul 2>&1

:: 6. Register Windows Service in SCM
echo [3/4] Installing and registering Windows Service...
"%EXE_PATH%" --install %*
if errorlevel 1 (
    echo [WARNING] Direct CLI registration failed, trying SCM fallback...
    sc create Leo4Proxy binPath= "\"%EXE_PATH%\" --service" start= auto DisplayName= "Leo4Proxy SChannel TLS Service" >nul 2>&1
    sc description Leo4Proxy "Leo4Proxy SChannel TLS and Reverse HTTPS Gateway Service" >nul 2>&1
)

:: Verify service exists in SCM
sc query Leo4Proxy >nul 2>&1
if errorlevel 1 goto :error_install

:: 7. Start Windows Service
echo [4/4] Starting Leo4Proxy Windows Service...
"%EXE_PATH%" --start
if errorlevel 1 (
    echo [INFO] Retrying start via SCM directly...
    sc start Leo4Proxy >nul 2>&1
)

:: Wait briefly and verify status
ping -n 3 127.0.0.1 >nul 2>&1
sc query Leo4Proxy | findstr /I "RUNNING START_PENDING" >nul 2>&1
if errorlevel 1 goto :error_start

echo.
echo =======================================================
echo    [OK] Leo4Proxy Service installed and started!
echo =======================================================
ping -n 2 127.0.0.1 >nul 2>&1
exit /b 0

:error_no_exe
echo.
echo *******************************************************
echo  [ERROR] Executable leo4proxy.exe not found!
echo *******************************************************
echo Please build the project (build.cmd) or place leo4proxy.exe in this folder.
echo.
pause
exit /b 1

:error_install
echo.
echo *******************************************************
echo  [ERROR] Failed to register Leo4Proxy Windows Service!
echo *******************************************************
echo Please verify Administrator permissions and check system logs.
echo.
pause
exit /b 1

:error_start
echo.
echo *******************************************************
echo  [ERROR] Leo4Proxy Windows Service failed to start!
echo *******************************************************
echo Current service status:
sc query Leo4Proxy
echo.
echo Please check certificate in LocalMachine\MY and Windows Event Viewer.
echo.
pause
exit /b 1
