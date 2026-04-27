@echo off
setlocal

set "PRESET=%~1"
if "%PRESET%"=="" set "PRESET=windows-msvc-debug"
set "RUN_TARGET=%~2"
if "%RUN_TARGET%"=="" set "RUN_TARGET=render_triangle_testbed"

call "%~dp0scripts\windows_build_env.bat" "%PRESET%" || exit /b %ERRORLEVEL%
call "%~dp0build.bat" "%PRESET%" "%RUN_TARGET%" || exit /b %ERRORLEVEL%

set "EXE=%~dp0build\%PRESET%\%RUN_TARGET%.exe"
if not exist "%EXE%" (
    echo Expected executable not found: "%EXE%"
    exit /b 1
)

"%EXE%"
