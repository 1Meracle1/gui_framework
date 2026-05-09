@echo off
setlocal

set "SCRIPT_DIR=%~dp0"
set "PRESET=%~1"
if "%PRESET%"=="" set "PRESET=windows-msvc-debug"
set "RUN_TARGET=%~2"
if "%RUN_TARGET%"=="" set "RUN_TARGET=render_triangle_testbed"
set "CONFIG="
set "RUN_ARGS="
if "%~3"=="--" (
    shift
    shift
    shift
    goto collect_run_args
)

set "CONFIG=%~3"
if "%~4"=="--" (
    shift
    shift
    shift
    shift
    goto collect_run_args
)

:run_args_collected
if "%CONFIG%"=="" set "CONFIG=Debug"
if /I "%PRESET:~-7%"=="release" set "CONFIG=Release"

call "%SCRIPT_DIR%scripts\windows_build_env.bat" "%PRESET%" || exit /b %ERRORLEVEL%
call "%SCRIPT_DIR%build.bat" "%PRESET%" "%RUN_TARGET%" || exit /b %ERRORLEVEL%

set "EXE=%SCRIPT_DIR%build\%PRESET%\%CONFIG%\%RUN_TARGET%.exe"
if not exist "%EXE%" set "EXE=%SCRIPT_DIR%build\%PRESET%\%RUN_TARGET%.exe"
if not exist "%EXE%" (
    echo Expected executable not found: "%EXE%"
    exit /b 1
)

if defined RUN_ARGS (
    "%EXE%" %RUN_ARGS%
) else (
    "%EXE%"
)
exit /b %ERRORLEVEL%

:collect_run_args
if "%~1"=="" goto run_args_collected
call :append_run_arg "%~1"
shift
goto collect_run_args

:append_run_arg
set "RUN_ARG=%~1"
set "RUN_ESCAPED=%RUN_ARG%"
:append_trailing_backslash_escape
if not "%RUN_ARG:~-1%"=="\" goto run_arg_escaped
set "RUN_ESCAPED=%RUN_ESCAPED%\"
set "RUN_ARG=%RUN_ARG:~0,-1%"
goto append_trailing_backslash_escape

:run_arg_escaped
if defined RUN_ARGS (
    set "RUN_ARGS=%RUN_ARGS% "%RUN_ESCAPED%""
) else (
    set "RUN_ARGS="%RUN_ESCAPED%""
)
set "RUN_ARG="
set "RUN_ESCAPED="
exit /b 0
