@echo off
setlocal

set "PRESET=%~1"
if "%PRESET%"=="" set "PRESET=windows-msvc-debug"

call "%~dp0scripts\windows_build_env.bat" "%PRESET%" || exit /b %ERRORLEVEL%
call "%~dp0build.bat" "%PRESET%" || exit /b %ERRORLEVEL%
ctest --preset "%PRESET%" || exit /b %ERRORLEVEL%
