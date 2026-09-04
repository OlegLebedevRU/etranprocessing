@echo off
setlocal EnableExtensions

:: ============================================================================
:: Leo4 Terminal Certificate Installer (l4pin) - Interactive UAC Setup
:: ============================================================================

:: 1. Check for Administrator privileges and request UAC elevation if needed
net session >nul 2>&1
if errorlevel 1 (
    echo [INFO] Administrator privileges required. Requesting UAC elevation...
    powershell -NoProfile -ExecutionPolicy Bypass -Command "Start-Process -FilePath 'cmd.exe' -ArgumentList '/c \"\"%~f0\" %*\"' -Verb RunAs"
    exit /b 0
)

:: 2. Switch to script directory
cd /d "%~dp0"

:: 3. Locate l4pin binary
set "EXE_PATH="
if exist "%~dp0l4pin.exe" (
    set "EXE_PATH=%~dp0l4pin.exe"
) else if exist "%~dp0bin\l4pin.exe" (
    set "EXE_PATH=%~dp0bin\l4pin.exe"
) else if exist "%~dp0..\l4pin.exe" (
    set "EXE_PATH=%~dp0..\l4pin.exe"
)

if not defined EXE_PATH goto :error_no_exe

echo =======================================================
echo     Leo4 Terminal Certificate Installer (l4pin)
echo =======================================================
echo.
echo [INFO] Working directory: %CD%
echo [INFO] Binary executable: %EXE_PATH%
echo [INFO] Target Store:      LocalMachine\MY (System)
echo.

:: 4. Check if direct flags (--help, --status, --list) were provided
if /i "%~1"=="--help"   goto :run_passthrough
if /i "%~1"=="-h"       goto :run_passthrough
if /i "%~1"=="/?"       goto :run_passthrough
if /i "%~1"=="--status" goto :run_passthrough
if /i "%~1"=="-l"       goto :run_passthrough
if /i "%~1"=="--list"   goto :run_passthrough

:: 5. Determine PIN from arguments or prompt interactively
set "PIN_ARG="
if not "%~1"=="" (
    set "ARG1=%~1"
    if /i "%~1"=="--pin" (
        set "PIN_ARG=%~2"
    ) else if /i "%~1"=="-pin" (
        set "PIN_ARG=%~2"
    ) else if /i "%~1"=="-p" (
        set "PIN_ARG=%~2"
    ) else if not "%~1"=="" (
        set "FIRST_CHAR=%ARG1:~0,1%"
        if not "%FIRST_CHAR%"=="-" (
            set "PIN_ARG=%~1"
        )
    )
)

if not defined PIN_ARG (
    :prompt_pin
    echo Please enter the 6-character terminal PIN code provided by the administrator:
    set /p "USER_INPUT=PIN Code: "
    echo.
    if not defined USER_INPUT (
        echo [ERROR] PIN code cannot be empty. Please try again.
        echo.
        goto :prompt_pin
    )
    :: Strip any accidental quotes or spaces
    set "PIN_ARG=%USER_INPUT:"=%"
    set "PIN_ARG=%PIN_ARG: =%"
    if "%PIN_ARG%"=="" (
        echo [ERROR] Invalid PIN code format. Please try again.
        echo.
        goto :prompt_pin
    )
)

:: 6. Run l4pin with PIN
echo -------------------------------------------------------
echo [INFO] Requesting and enrolling certificate for PIN: %PIN_ARG%...
echo -------------------------------------------------------
echo.

"%EXE_PATH%" --pin "%PIN_ARG%"
if errorlevel 1 goto :install_failed

echo.
echo =======================================================
echo    [OK] Certificate enrolled and installed successfully!
echo.
echo Target Store: LocalMachine\MY
echo Key Storage:  Microsoft Software Key Storage Provider (CNG)
echo Exportable:   NO (Private key is protected)
echo.
echo You may now proceed with starting Leo4Proxy service.
echo =======================================================
echo.
pause
exit /b 0

:install_failed
set "ERR_CODE=%ERRORLEVEL%"
echo.
echo =======================================================
echo    [ERROR] Certificate installation failed! (Code: %ERR_CODE%)
echo.
echo Troubleshooting tips:
echo  1. Ensure the 6-character PIN is valid, active, and not already used.
echo  2. Verify network connectivity to the certificate server (iot-processing.ru).
echo  3. Ensure this script was executed with Administrator privileges.
echo =======================================================
echo.
pause
exit /b %ERR_CODE%

:run_passthrough
"%EXE_PATH%" %*
set "EXIT_CODE=%ERRORLEVEL%"
echo.
pause
exit /b %EXIT_CODE%

:error_no_exe
echo.
echo *******************************************************
echo  [ERROR] l4pin.exe not found!
echo *******************************************************
echo Please build the project (build.cmd) or place
echo l4pin.exe in this folder.
echo.
pause
exit /b 1
