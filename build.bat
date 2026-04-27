@echo off
setlocal

set "PRESET=%~1"
if "%PRESET%"=="" set "PRESET=windows-msvc-debug"
set "BUILD_TARGET=%~2"

call "%~dp0scripts\windows_build_env.bat" "%PRESET%" || exit /b %ERRORLEVEL%

set "CMAKE_FRESH_ARG="
set "BUILD_DIR=%~dp0build\%PRESET%"
set "CACHE_FILE=%BUILD_DIR%\CMakeCache.txt"
if defined CMAKE_EXPECTED_GENERATOR if exist "%BUILD_DIR%\CMakeFiles" if not exist "%CACHE_FILE%" (
    set "CMAKE_FRESH_ARG=--fresh"
)
if defined CMAKE_EXPECTED_GENERATOR if exist "%CACHE_FILE%" (
    findstr /C:"CMAKE_GENERATOR:INTERNAL=%CMAKE_EXPECTED_GENERATOR%" "%CACHE_FILE%" >nul
    if errorlevel 1 set "CMAKE_FRESH_ARG=--fresh"
)

if defined CMAKE_MAKE_PROGRAM_ARG (
    cmake %CMAKE_FRESH_ARG% --preset "%PRESET%" "%CMAKE_MAKE_PROGRAM_ARG%" || exit /b %ERRORLEVEL%
) else (
    cmake %CMAKE_FRESH_ARG% --preset "%PRESET%" || exit /b %ERRORLEVEL%
)
if "%BUILD_TARGET%"=="" (
    cmake --build --preset "%PRESET%" --parallel || exit /b %ERRORLEVEL%
) else (
    cmake --build --preset "%PRESET%" --parallel --target "%BUILD_TARGET%" || exit /b %ERRORLEVEL%
)
