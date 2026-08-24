@echo off
setlocal enabledelayedexpansion

echo =======================================================
echo Building terminal-cert-installer (C / MSVC /MT Static)
echo =======================================================

set VCVARS="C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"

if not exist %VCVARS% (
    set VCVARS="d:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
)

if not exist %VCVARS% (
    echo Error: vcvars64.bat not found in standard paths.
    exit /b 1
)

call %VCVARS%

if not exist bin mkdir bin
if not exist obj mkdir obj

rc.exe /nologo /fo obj\app.res res\app.rc

cl.exe /nologo /O2 /MT /W4 /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS /I src /Foobj\ src\main.c src\http_client.c src\xml_utils.c src\cng_crypto.c src\cert_store.c obj\app.res /link /OUT:bin\terminal-cert-installer.exe ncrypt.lib crypt32.lib winhttp.lib advapi32.lib shell32.lib user32.lib

if %ERRORLEVEL% equ 0 (
    echo =======================================================
    echo Build SUCCESS: bin\terminal-cert-installer.exe
    echo =======================================================
) else (
    echo =======================================================
    echo Build FAILED with error code %ERRORLEVEL%
    echo =======================================================
    exit /b %ERRORLEVEL%
)
