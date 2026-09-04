@echo off
setlocal EnableExtensions enabledelayedexpansion
cd /d "%~dp0"

echo ===============================================================
echo   Leo4 Suite Windows 7 SP1 (32-bit x86) Dist Builder
echo ===============================================================
echo.

set "DIST_WIN7=%~dp0dist_win7_sp1"
set "STAGING=%DIST_WIN7%\staging"
set "OUT_ZIP=%DIST_WIN7%\tools.zip"

:: 1. Build Mosquitto No-SSL with Windows 7 SP1 targeting
echo [1/4] Building Mosquitto No-SSL (Win32 / MT / Win7 SP1)...
if exist "D:\repo\mosquitto\build_win32_nossl.cmd" (
    cmd /c "D:\repo\mosquitto\build_win32_nossl.cmd"
    if errorlevel 1 (
        echo [ERROR] Mosquitto build failed!
        exit /b 1
    )
) else (
    echo [ERROR] D:\repo\mosquitto\build_win32_nossl.cmd not found!
    exit /b 1
)

:: 2. Build all 5 Leo4 tools for Windows 7 x86
echo.
echo [2/4] Compiling Leo4 Tools for Windows 7 SP1 x86 (/D_WIN32_WINNT=0x0601 /SUBSYSTEM:CONSOLE,6.01)...

echo   - Compiling leo4proxy...
cd /d "%~dp0leo4proxy"
call build.cmd win7
if errorlevel 1 (
    echo [ERROR] leo4proxy build failed!
    exit /b 1
)

echo   - Compiling l4con...
cd /d "%~dp0l4con"
call build.cmd win7
if errorlevel 1 (
    echo [ERROR] l4con build failed!
    exit /b 1
)

echo   - Compiling l4sql...
cd /d "%~dp0l4sql"
call build.cmd win7
if errorlevel 1 (
    echo [ERROR] l4sql build failed!
    exit /b 1
)

echo   - Compiling l4pin...
cd /d "%~dp0l4pin"
call build.cmd win7
if errorlevel 1 (
    echo [ERROR] l4pin build failed!
    exit /b 1
)

echo   - Compiling l4superv and l4install_x86...
cd /d "%~dp0l4superv"
call build.cmd win7
if errorlevel 1 (
    echo [ERROR] l4superv build failed!
    exit /b 1
)

cd /d "%~dp0"

:: 3. Stage files for tools.zip
echo.
echo [3/4] Staging files for Windows 7 SP1 package...

if exist "%STAGING%" rd /s /q "%STAGING%"
md "%STAGING%"
md "%STAGING%\leo4proxy"
md "%STAGING%\leo4proxy\x86"
md "%STAGING%\mosquitto"
md "%STAGING%\mosquitto\log"
md "%STAGING%\l4con"
md "%STAGING%\l4con\x86"
md "%STAGING%\l4sql"
md "%STAGING%\l4sql\x86"
md "%STAGING%\l4pin"
md "%STAGING%\l4pin\x86"
md "%STAGING%\l4superv"
md "%STAGING%\l4superv\x86"

:: Copy leo4proxy
copy /y "%~dp0leo4proxy\bin\x86\leo4proxy.exe" "%STAGING%\leo4proxy\x86\leo4proxy.exe" >nul
for %%f in ("%~dp0leo4proxy\leo4proxy_*.cmd") do (
    copy /y "%%f" "%STAGING%\leo4proxy\" >nul
)
if exist "%~dp0leo4proxy\README.md" (
    copy /y "%~dp0leo4proxy\README.md" "%STAGING%\leo4proxy\README.md" >nul
)

:: Copy mosquitto
if exist "%~dp0mosquitto" (
    xcopy /e /y /q "%~dp0mosquitto\*" "%STAGING%\mosquitto\" >nul
) else if exist "C:\l4tools\mosquitto" (
    xcopy /e /y /q "C:\l4tools\mosquitto\*" "%STAGING%\mosquitto\" >nul
)
copy /y "D:\repo\mosquitto\bin\win32_nossl\mosquitto.exe" "%STAGING%\mosquitto\mosquitto.exe" >nul
if exist "%STAGING%\mosquitto\mosquitto.conf" del /f /q "%STAGING%\mosquitto\mosquitto.conf" >nul 2>nul
if exist "%STAGING%\mosquitto\mosquitto.conf.bak" del /f /q "%STAGING%\mosquitto\mosquitto.conf.bak" >nul 2>nul
if exist "%STAGING%\mosquitto\install_mosquitto.ps1.bak" del /f /q "%STAGING%\mosquitto\install_mosquitto.ps1.bak" >nul 2>nul

:: Copy l4con
copy /y "%~dp0l4con\bin\x86\l4con.exe" "%STAGING%\l4con\x86\l4con.exe" >nul
for %%f in ("%~dp0l4con\l4con_*.cmd") do (
    copy /y "%%f" "%STAGING%\l4con\" >nul
)
if exist "%~dp0l4con\README.md" (
    copy /y "%~dp0l4con\README.md" "%STAGING%\l4con\README.md" >nul
)
if exist "%~dp0l4con\CHANGELOG.md" (
    copy /y "%~dp0l4con\CHANGELOG.md" "%STAGING%\l4con\CHANGELOG.md" >nul
)

:: Copy l4sql
copy /y "%~dp0l4sql\bin\x86\l4sql.exe" "%STAGING%\l4sql\x86\l4sql.exe" >nul
if exist "%~dp0l4sql\README.md" (
    copy /y "%~dp0l4sql\README.md" "%STAGING%\l4sql\README.md" >nul
)

:: Copy l4pin
copy /y "%~dp0l4pin\bin\x86\l4pin.exe" "%STAGING%\l4pin\x86\l4pin.exe" >nul
if exist "%~dp0l4pin\install_cert.cmd" (
    copy /y "%~dp0l4pin\install_cert.cmd" "%STAGING%\l4pin\install_cert.cmd" >nul
)
if exist "%~dp0l4pin\README.md" (
    copy /y "%~dp0l4pin\README.md" "%STAGING%\l4pin\README.md" >nul
)

:: Copy l4superv
copy /y "%~dp0l4superv\bin\x86\l4superv.exe" "%STAGING%\l4superv\x86\l4superv.exe" >nul
for %%f in ("%~dp0l4superv\l4superv_*.cmd") do (
    copy /y "%%f" "%STAGING%\l4superv\" >nul
)
if exist "%~dp0l4superv\l4install_run.cmd" (
    copy /y "%~dp0l4superv\l4install_run.cmd" "%STAGING%\l4superv\l4install_run.cmd" >nul
)
if exist "%~dp0l4superv\README.md" (
    copy /y "%~dp0l4superv\README.md" "%STAGING%\l4superv\README.md" >nul
)
if exist "%~dp0l4superv\CHANGELOG.md" (
    copy /y "%~dp0l4superv\CHANGELOG.md" "%STAGING%\l4superv\CHANGELOG.md" >nul
)

:: Copy User Guide
if exist "%~dp0l4superv\bin\terminal-tools-user-guide.md" (
    copy /y "%~dp0l4superv\bin\terminal-tools-user-guide.md" "%STAGING%\terminal-tools-user-guide.md" >nul
) else if exist "%~dp0..\docs\terminal-tools-user-guide.md" (
    copy /y "%~dp0..\docs\terminal-tools-user-guide.md" "%STAGING%\terminal-tools-user-guide.md" >nul
) else if exist "%~dp0terminal-tools-user-guide.md" (
    copy /y "%~dp0terminal-tools-user-guide.md" "%STAGING%\terminal-tools-user-guide.md" >nul
)

:: 4. Create tools.zip and populate dist_win7_sp1
echo.
echo [4/4] Creating tools.zip archive and populating dist_win7_sp1...

if not exist "%DIST_WIN7%" md "%DIST_WIN7%"
if exist "%OUT_ZIP%" del /f /q "%OUT_ZIP%"

powershell -NoProfile -ExecutionPolicy Bypass -Command "Compress-Archive -Path '%STAGING%\*' -DestinationPath '%OUT_ZIP%' -Force"

if not exist "%OUT_ZIP%" (
    echo [ERROR] Failed to create %OUT_ZIP%
    exit /b 1
)

:: Clean temporary staging
rd /s /q "%STAGING%"

:: Copy l4install_x86.exe and installer script into dist_win7_sp1
copy /y "%~dp0l4superv\bin\x86\l4install_x86.exe" "%DIST_WIN7%\l4install_x86.exe" >nul
copy /y "%~dp0l4superv\bin\x86\l4install_x86.exe" "%DIST_WIN7%\l4install.exe" >nul
if exist "%~dp0l4superv\bin\terminal-tools-user-guide.md" (
    copy /y "%~dp0l4superv\bin\terminal-tools-user-guide.md" "%DIST_WIN7%\terminal-tools-user-guide.md" >nul
)

echo ===============================================================
echo  [OK] Windows 7 SP1 32-bit Distribution Ready in:
echo       %DIST_WIN7%
echo    - %DIST_WIN7%\tools.zip
echo    - %DIST_WIN7%\l4install_x86.exe
echo    - %DIST_WIN7%\l4install_x86.cmd
echo    - %DIST_WIN7%\l4install.cmd
echo    - %DIST_WIN7%\README.md
echo ===============================================================
exit /b 0
