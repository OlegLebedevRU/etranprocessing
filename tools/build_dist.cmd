@echo off
setlocal
if /i "%~1"=="legacy" goto :legacy
set "RELEASE_VERSION=%~1"
if not defined RELEASE_VERSION set /p RELEASE_VERSION=<"%~dp0version.txt"
pushd "%~dp0.."
if not defined UV_CACHE_DIR set "UV_CACHE_DIR=%CD%\.uv-cache"
uv run --locked python -m l4release prepare --version "%RELEASE_VERSION%"
set "RELEASE_RESULT=%errorlevel%"
popd
exit /b %RELEASE_RESULT%

:legacy
pushd "%~dp0l4superv"
call build.cmd all
if errorlevel 1 goto :legacy_failed
call pack_zip.cmd
set "RELEASE_RESULT=%errorlevel%"
popd
exit /b %RELEASE_RESULT%
:legacy_failed
popd
exit /b 1
