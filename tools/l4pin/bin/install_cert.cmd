@echo off
setlocal EnableExtensions DisableDelayedExpansion

rem Resolve the executable before leaving a possibly locked installation directory.
set "L4PIN_EXE=%~dp0l4pin.exe"
if not exist "%L4PIN_EXE%" set "L4PIN_EXE=%~dp0bin\l4pin.exe"
if not exist "%L4PIN_EXE%" (
    echo [ERROR] l4pin.exe was not found next to install_cert.cmd.
    endlocal & exit /b 1
)

rem Do not keep cmd.exe in the directory that l4setup may replace.
if exist "%TEMP%\" cd /d "%TEMP%"
"%L4PIN_EXE%" %*
set "L4PIN_RESULT=%ERRORLEVEL%"
endlocal & exit /b %L4PIN_RESULT%
