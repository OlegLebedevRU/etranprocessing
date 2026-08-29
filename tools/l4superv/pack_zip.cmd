@echo off
setlocal enabledelayedexpansion

echo ===============================================================
echo  Packing Leo4 Tools Suite into tools.zip distribution
echo ===============================================================

set "REPO_TOOLS=%~dp0.."
set "STAGING=%~dp0obj\staging"
set "OUT_ZIP=%~dp0bin\tools.zip"

if exist "%STAGING%" rd /s /q "%STAGING%"
md "%STAGING%"
md "%STAGING%\leo4proxy"
md "%STAGING%\mosquitto"
md "%STAGING%\mosquitto\log"
md "%STAGING%\l4con"
md "%STAGING%\l4pin"
md "%STAGING%\l4superv"

:: 1. Copy leo4proxy
echo [1/5] Staging leo4proxy...
if exist "%REPO_TOOLS%\leo4proxy\bin" (
    xcopy /e /y /q "%REPO_TOOLS%\leo4proxy\bin\*" "%STAGING%\leo4proxy\" >nul
)
if exist "%REPO_TOOLS%\leo4proxy\leo4proxy.exe" (
    copy /y "%REPO_TOOLS%\leo4proxy\leo4proxy.exe" "%STAGING%\leo4proxy\" >nul
)

:: 2. Copy mosquitto
echo [2/5] Staging mosquitto...
if exist "D:\Platerra26\tools\mosquitto" (
    xcopy /e /y /q "D:\Platerra26\tools\mosquitto\*" "%STAGING%\mosquitto\" >nul
) else if exist "%REPO_TOOLS%\mosquitto" (
    xcopy /e /y /q "%REPO_TOOLS%\mosquitto\*" "%STAGING%\mosquitto\" >nul
)
:: Clean any hardcoded config from development machine
if exist "%STAGING%\mosquitto\mosquitto.conf" del /f /q "%STAGING%\mosquitto\mosquitto.conf" >nul 2>nul
if exist "%STAGING%\mosquitto\mosquitto.conf.bak" del /f /q "%STAGING%\mosquitto\mosquitto.conf.bak" >nul 2>nul
if exist "%STAGING%\mosquitto\install_mosquitto.ps1.bak" del /f /q "%STAGING%\mosquitto\install_mosquitto.ps1.bak" >nul 2>nul

:: 3. Copy l4con
echo [3/6] Staging l4con...
if exist "%REPO_TOOLS%\l4con\bin" (
    xcopy /e /y /q "%REPO_TOOLS%\l4con\bin\*" "%STAGING%\l4con\" >nul
)
if exist "%REPO_TOOLS%\l4con\CHANGELOG.md" (
    copy /y "%REPO_TOOLS%\l4con\CHANGELOG.md" "%STAGING%\l4con\CHANGELOG.md" >nul 2>nul
)

:: 4. Copy l4pin
echo [4/6] Staging l4pin...
if exist "%REPO_TOOLS%\l4pin\bin" (
    xcopy /e /y /q "%REPO_TOOLS%\l4pin\bin\*" "%STAGING%\l4pin\" >nul
)

:: 5. Copy l4superv and l4install
echo [5/6] Staging l4superv and l4install...
if exist "%~dp0bin" (
    xcopy /e /y /q "%~dp0bin\*.exe" "%STAGING%\l4superv\" >nul 2>nul
    xcopy /e /y /q "%~dp0bin\*.cmd" "%STAGING%\l4superv\" >nul 2>nul
    copy /y "%~dp0bin\l4install.exe" "%STAGING%\l4install.exe" >nul 2>nul
)
if exist "%~dp0CHANGELOG.md" (
    copy /y "%~dp0CHANGELOG.md" "%STAGING%\l4superv\CHANGELOG.md" >nul 2>nul
)

:: 6. Stage User Guide in package root
echo [6/6] Staging terminal-tools-user-guide.md in package root...
if exist "%REPO_TOOLS%\..\docs\terminal-tools-user-guide.md" (
    copy /y "%REPO_TOOLS%\..\docs\terminal-tools-user-guide.md" "%STAGING%\terminal-tools-user-guide.md" >nul
    copy /y "%REPO_TOOLS%\..\docs\terminal-tools-user-guide.md" "%~dp0bin\terminal-tools-user-guide.md" >nul 2>nul
)

:: Create zip archive via PowerShell Compress-Archive
echo Creating zip archive: %OUT_ZIP%...
powershell -Command "if (Test-Path '%OUT_ZIP%') { Remove-Item '%OUT_ZIP%' -Force }; Compress-Archive -Path '%STAGING%\*' -DestinationPath '%OUT_ZIP%' -Force"

if exist "%OUT_ZIP%" (
    copy /y "%OUT_ZIP%" "%~dp0tools.zip" >nul 2>nul
    echo ===============================================================
    echo  [OK] Successfully packed:
    echo    - %OUT_ZIP%
    echo    - %~dp0tools.zip
    echo ===============================================================
) else (
    echo [ERROR] Failed to create %OUT_ZIP%
    exit /b 1
)

exit /b 0
