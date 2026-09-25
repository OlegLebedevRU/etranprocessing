@echo off
setlocal enabledelayedexpansion

echo ===============================================================
echo  Packing Leo4 Tools Suite into unified tools.zip distribution
echo ===============================================================

set "REPO_TOOLS=%~dp0.."
set "STAGING=%~dp0obj\staging"
set "OUT_ZIP=%~dp0bin\tools.zip"
set "DIST_DIR=%REPO_TOOLS%\dist"
set "CANDIDATE_MODE=0"
if /i "%~1"=="--candidate" (
    set "CANDIDATE_MODE=1"
    set "STAGING=%~dp0obj\staging_candidate_1_8_1"
    set "DIST_DIR=%REPO_TOOLS%\dist\candidate-1.8.1"
    set "OUT_ZIP=%REPO_TOOLS%\dist\candidate-1.8.1\tools.zip"
)

if "%CANDIDATE_MODE%"=="1" (
    if exist "%STAGING%" (
        echo [ERROR] Candidate staging already exists: %STAGING%
        exit /b 1
    )
    if exist "%OUT_ZIP%" (
        echo [ERROR] Candidate archive already exists: %OUT_ZIP%
        exit /b 1
    )
)

if exist "%STAGING%" rd /s /q "%STAGING%"
md "%STAGING%"
md "%STAGING%\leo4proxy"
md "%STAGING%\leo4proxy\x86"
md "%STAGING%\leo4proxy\x64"

md "%STAGING%\mosquitto"
md "%STAGING%\mosquitto\log"

md "%STAGING%\l4con"
md "%STAGING%\l4con\x86"
md "%STAGING%\l4con\x64"

md "%STAGING%\l4desk"
md "%STAGING%\l4desk\x86"
md "%STAGING%\l4desk\x64"

md "%STAGING%\l4sql"
md "%STAGING%\l4sql\x86"
md "%STAGING%\l4sql\x64"

md "%STAGING%\l4pin"
md "%STAGING%\l4pin\x86"
md "%STAGING%\l4pin\x64"

md "%STAGING%\l4superv"
md "%STAGING%\l4superv\x86"
md "%STAGING%\l4superv\x64"

md "%STAGING%\l4capture"
md "%STAGING%\l4capture\bin"
md "%STAGING%\l4capture\bin\x86"
md "%STAGING%\l4capture\bin\x64"

:: 1. Copy leo4proxy
echo [1/7] Staging leo4proxy...
if exist "%REPO_TOOLS%\leo4proxy\bin\x86\leo4proxy.exe" (
    copy /y "%REPO_TOOLS%\leo4proxy\bin\x86\leo4proxy.exe" "%STAGING%\leo4proxy\x86\leo4proxy.exe" >nul
)
if exist "%REPO_TOOLS%\leo4proxy\bin\x64\leo4proxy.exe" (
    copy /y "%REPO_TOOLS%\leo4proxy\bin\x64\leo4proxy.exe" "%STAGING%\leo4proxy\x64\leo4proxy.exe" >nul
)
for %%f in ("%REPO_TOOLS%\leo4proxy\leo4proxy_*.cmd") do (
    copy /y "%%f" "%STAGING%\leo4proxy\" >nul
)
if exist "%REPO_TOOLS%\leo4proxy\README.md" (
    copy /y "%REPO_TOOLS%\leo4proxy\README.md" "%STAGING%\leo4proxy\README.md" >nul 2>nul
)

:: 2. Copy mosquitto
echo [2/7] Staging mosquitto...
if exist "%REPO_TOOLS%\mosquitto" (
    xcopy /e /y /q "%REPO_TOOLS%\mosquitto\*" "%STAGING%\mosquitto\" >nul
) else if exist "C:\l4tools\mosquitto" (
    xcopy /e /y /q "C:\l4tools\mosquitto\*" "%STAGING%\mosquitto\" >nul
) else if exist "D:\Platerra26\tools\mosquitto" (
    xcopy /e /y /q "D:\Platerra26\tools\mosquitto\*" "%STAGING%\mosquitto\" >nul
)
:: Clean any hardcoded config or backup files from development machine
if exist "%STAGING%\mosquitto\mosquitto.conf" del /f /q "%STAGING%\mosquitto\mosquitto.conf" >nul 2>nul
if exist "%STAGING%\mosquitto\mosquitto.conf.bak" del /f /q "%STAGING%\mosquitto\mosquitto.conf.bak" >nul 2>nul
if exist "%STAGING%\mosquitto\install_mosquitto.ps1.bak" del /f /q "%STAGING%\mosquitto\install_mosquitto.ps1.bak" >nul 2>nul

:: 3. Copy l4con
echo [3/7] Staging l4con...
if exist "%REPO_TOOLS%\l4con\bin\x86\l4con.exe" (
    copy /y "%REPO_TOOLS%\l4con\bin\x86\l4con.exe" "%STAGING%\l4con\x86\l4con.exe" >nul
)
if exist "%REPO_TOOLS%\l4con\bin\x64\l4con.exe" (
    copy /y "%REPO_TOOLS%\l4con\bin\x64\l4con.exe" "%STAGING%\l4con\x64\l4con.exe" >nul
)
for %%f in ("%REPO_TOOLS%\l4con\l4con_*.cmd") do (
    copy /y "%%f" "%STAGING%\l4con\" >nul
)
if exist "%REPO_TOOLS%\l4con\README.md" (
    copy /y "%REPO_TOOLS%\l4con\README.md" "%STAGING%\l4con\README.md" >nul 2>nul
)
if exist "%REPO_TOOLS%\l4con\CHANGELOG.md" (
    copy /y "%REPO_TOOLS%\l4con\CHANGELOG.md" "%STAGING%\l4con\CHANGELOG.md" >nul 2>nul
)

:: 3b. Copy l4desk
echo [3b/8] Staging l4desk...
if exist "%REPO_TOOLS%\l4desk\bin\x86\l4desk.exe" (
    copy /y "%REPO_TOOLS%\l4desk\bin\x86\l4desk.exe" "%STAGING%\l4desk\x86\l4desk.exe" >nul
)
if exist "%REPO_TOOLS%\l4desk\bin\x64\l4desk.exe" (
    copy /y "%REPO_TOOLS%\l4desk\bin\x64\l4desk.exe" "%STAGING%\l4desk\x64\l4desk.exe" >nul
)
for %%f in ("%REPO_TOOLS%\l4desk\l4desk_*.cmd") do (
    copy /y "%%f" "%STAGING%\l4desk\" >nul
)
if exist "%REPO_TOOLS%\l4desk\README.md" (
    copy /y "%REPO_TOOLS%\l4desk\README.md" "%STAGING%\l4desk\README.md" >nul 2>nul
)
if exist "%REPO_TOOLS%\l4desk\CHANGELOG.md" (
    copy /y "%REPO_TOOLS%\l4desk\CHANGELOG.md" "%STAGING%\l4desk\CHANGELOG.md" >nul 2>nul
)

:: 4. Copy l4sql
echo [4/7] Staging l4sql...
if exist "%REPO_TOOLS%\l4sql\bin\x86\l4sql.exe" (
    copy /y "%REPO_TOOLS%\l4sql\bin\x86\l4sql.exe" "%STAGING%\l4sql\x86\l4sql.exe" >nul
)
if exist "%REPO_TOOLS%\l4sql\bin\x64\l4sql.exe" (
    copy /y "%REPO_TOOLS%\l4sql\bin\x64\l4sql.exe" "%STAGING%\l4sql\x64\l4sql.exe" >nul
)
if exist "%REPO_TOOLS%\l4sql\README.md" (
    copy /y "%REPO_TOOLS%\l4sql\README.md" "%STAGING%\l4sql\README.md" >nul 2>nul
)

:: 5. Copy l4pin
echo [5/7] Staging l4pin...
if exist "%REPO_TOOLS%\l4pin\bin\x86\l4pin.exe" (
    copy /y "%REPO_TOOLS%\l4pin\bin\x86\l4pin.exe" "%STAGING%\l4pin\x86\l4pin.exe" >nul
)
if exist "%REPO_TOOLS%\l4pin\bin\x64\l4pin.exe" (
    copy /y "%REPO_TOOLS%\l4pin\bin\x64\l4pin.exe" "%STAGING%\l4pin\x64\l4pin.exe" >nul
)
if exist "%REPO_TOOLS%\l4pin\install_cert.cmd" (
    copy /y "%REPO_TOOLS%\l4pin\install_cert.cmd" "%STAGING%\l4pin\install_cert.cmd" >nul
)
if exist "%REPO_TOOLS%\l4pin\README.md" (
    copy /y "%REPO_TOOLS%\l4pin\README.md" "%STAGING%\l4pin\README.md" >nul 2>nul
)

:: 6. Copy l4superv
echo [6/7] Staging l4superv...
if exist "%~dp0bin\x86\l4superv.exe" (
    copy /y "%~dp0bin\x86\l4superv.exe" "%STAGING%\l4superv\x86\l4superv.exe" >nul
)
if exist "%~dp0bin\x64\l4superv.exe" (
    copy /y "%~dp0bin\x64\l4superv.exe" "%STAGING%\l4superv\x64\l4superv.exe" >nul
)
for %%f in ("%~dp0l4superv_*.cmd") do (
    copy /y "%%f" "%STAGING%\l4superv\" >nul
)
if exist "%~dp0l4install_run.cmd" (
    copy /y "%~dp0l4install_run.cmd" "%STAGING%\l4superv\l4install_run.cmd" >nul
)
if exist "%~dp0README.md" (
    copy /y "%~dp0README.md" "%STAGING%\l4superv\README.md" >nul 2>nul
)
if exist "%~dp0CHANGELOG.md" (
    copy /y "%~dp0CHANGELOG.md" "%STAGING%\l4superv\CHANGELOG.md" >nul 2>nul
)

:: 6b. Copy l4capture to the path consumed by l4desk after architecture filtering.
echo [6b/8] Staging l4capture...
for %%a in (x86 x64) do (
    if not exist "%REPO_TOOLS%\l4capture\bin\%%a\l4capture.exe" (
        echo [ERROR] Missing l4capture %%a binary.
        exit /b 1
    )
    copy /y "%REPO_TOOLS%\l4capture\bin\%%a\l4capture.exe" "%STAGING%\l4capture\bin\%%a\l4capture.exe" >nul
    if errorlevel 1 exit /b 1
)
for %%f in (OPENH264_LICENSE.txt NOTICE-OpenH264.txt SBOM.json ROLLBACK.md) do (
    if not exist "%REPO_TOOLS%\l4capture\%%f" (
        echo [ERROR] Missing l4capture release material %%f.
        exit /b 1
    )
    copy /y "%REPO_TOOLS%\l4capture\%%f" "%STAGING%\l4capture\%%f" >nul
    if errorlevel 1 exit /b 1
)

:: 7. Stage User Guide in package root
echo [7/8] Staging term_tool-user-guide.md in package root...
if exist "%REPO_TOOLS%\..\docs\term_tool-user-guide.md" (
    copy /y "%REPO_TOOLS%\..\docs\term_tool-user-guide.md" "%STAGING%\term_tool-user-guide.md" >nul
    copy /y "%REPO_TOOLS%\..\docs\term_tool-user-guide.md" "%~dp0bin\term_tool-user-guide.md" >nul 2>nul
) else if exist "%REPO_TOOLS%\..\docs\terminal-tools-user-guide.md" (
    copy /y "%REPO_TOOLS%\..\docs\terminal-tools-user-guide.md" "%STAGING%\terminal-tools-user-guide.md" >nul
    copy /y "%REPO_TOOLS%\..\docs\terminal-tools-user-guide.md" "%~dp0bin\terminal-tools-user-guide.md" >nul 2>nul
) else if exist "%~dp0bin\terminal-tools-user-guide.md" (
    copy /y "%~dp0bin\terminal-tools-user-guide.md" "%STAGING%\terminal-tools-user-guide.md" >nul
)

:: 8. Pack FFmpeg package
echo [8/8] Packing FFmpeg package via pack_ffmpeg.cmd...
if exist "%REPO_TOOLS%\ffmpeg\pack_ffmpeg.cmd" (
    if "%CANDIDATE_MODE%"=="1" (
        call "%REPO_TOOLS%\ffmpeg\pack_ffmpeg.cmd" --candidate
    ) else (
        call "%REPO_TOOLS%\ffmpeg\pack_ffmpeg.cmd"
    )
    if errorlevel 1 exit /b 1
)

:: Create zip archive via PowerShell Compress-Archive
echo Creating zip archive: %OUT_ZIP%...
powershell -Command "if (Test-Path '%OUT_ZIP%') { Remove-Item '%OUT_ZIP%' -Force }; Compress-Archive -Path '%STAGING%\*' -DestinationPath '%OUT_ZIP%' -Force"
if errorlevel 1 exit /b 1

if not exist "%OUT_ZIP%" (
    echo [ERROR] Failed to create %OUT_ZIP%
    exit /b 1
)

if "%CANDIDATE_MODE%"=="0" (
    copy /y "%OUT_ZIP%" "%~dp0tools.zip" >nul 2>nul
    copy /y "%OUT_ZIP%" "%REPO_TOOLS%\tools.zip" >nul 2>nul
)

:: Update tools\dist directory
if not exist "%DIST_DIR%" md "%DIST_DIR%"
copy /y "%OUT_ZIP%" "%DIST_DIR%\tools.zip" >nul 2>nul

if exist "%~dp0bin\x64\l4install_x64.exe" (
    copy /y "%~dp0bin\x64\l4install_x64.exe" "%DIST_DIR%\l4install_x64.exe" >nul
) else if exist "%~dp0bin\l4install_x64.exe" (
    copy /y "%~dp0bin\l4install_x64.exe" "%DIST_DIR%\l4install_x64.exe" >nul
)

if exist "%~dp0bin\x86\l4install_x86.exe" (
    copy /y "%~dp0bin\x86\l4install_x86.exe" "%DIST_DIR%\l4install_x86.exe" >nul
) else if exist "%~dp0bin\l4install_x86.exe" (
    copy /y "%~dp0bin\l4install_x86.exe" "%DIST_DIR%\l4install_x86.exe" >nul
)

echo ===============================================================
echo  [OK] Successfully packed:
echo    - %OUT_ZIP%
echo    - %REPO_TOOLS%\tools.zip
echo    - %DIST_DIR%\tools.zip
echo    - %DIST_DIR%\l4install_x64.exe
echo    - %DIST_DIR%\l4install_x86.exe
echo ===============================================================
exit /b 0
