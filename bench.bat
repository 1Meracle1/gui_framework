@echo off
setlocal

set "PRESET=%~1"
if "%PRESET%"=="" set "PRESET=windows-msvc-debug"

call "%~dp0scripts\windows_build_env.bat" "%PRESET%" || exit /b %ERRORLEVEL%
call "%~dp0build.bat" "%PRESET%" || exit /b %ERRORLEVEL%

set "EXE=%~dp0build\%PRESET%\gui_framework_benchmarks.exe"
if not exist "%EXE%" (
    echo Expected executable not found: "%EXE%"
    exit /b 1
)

"%EXE%"
