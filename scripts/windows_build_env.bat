@echo off

set "PRESET=%~1"
if "%PRESET%"=="" set "PRESET=windows-msvc-debug"

set "CMAKE_MAKE_PROGRAM_ARG="
set "CMAKE_EXPECTED_GENERATOR="
set "VSCMD_SKIP_SENDTELEMETRY=1"
set "ORIGINAL_PATH=%PATH%"
set PATH=
set "Path=%ORIGINAL_PATH%"
set "PRESET_PREFIX=%PRESET:~0,8%"
if /I "%PRESET_PREFIX%"=="windows-" goto configure_windows
exit /b 0

:configure_windows
call :find_visual_studio
if defined VS_INSTALL goto have_visual_studio

echo Could not find a Visual Studio installation with MSVC tools.
echo Install Visual Studio Build Tools or run from a configured Developer Command Prompt.
exit /b 1

:have_visual_studio
set "MSVC_PREFIX=%PRESET:~0,12%"
if /I "%MSVC_PREFIX%"=="windows-msvc" set "WANT_MSVC_GENERATOR=1"

where cl.exe >nul 2>nul
if not errorlevel 1 goto have_compiler_env

set "VCVARS=%VS_INSTALL%\VC\Auxiliary\Build\vcvars64.bat"
if exist "%VCVARS%" goto call_vcvars

echo Could not find "%VCVARS%".
exit /b 1

:call_vcvars
call "%VCVARS%" >nul
if errorlevel 1 exit /b %ERRORLEVEL%

:have_compiler_env
if defined WANT_MSVC_GENERATOR (
    set "CMAKE_EXPECTED_GENERATOR=Visual Studio 17 2022"
    exit /b 0
)

:configure_ninja
set "NINJA_EXE=%VS_INSTALL%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
set "CMAKE_EXPECTED_GENERATOR=Ninja"
if not exist "%NINJA_EXE%" exit /b 0

for %%I in ("%NINJA_EXE%") do set "PATH=%%~dpI;%PATH%"
set "CMAKE_MAKE_PROGRAM_ARG=-DCMAKE_MAKE_PROGRAM=%NINJA_EXE%"
exit /b 0

:find_visual_studio
if not defined VSINSTALLDIR goto find_visual_studio_paths

set "VS_INSTALL=%VSINSTALLDIR%"
exit /b 0

:find_visual_studio_paths
call :try_visual_studio "%ProgramFiles%\Microsoft Visual Studio\2022\Community"
if defined VS_INSTALL exit /b 0
call :try_visual_studio "%ProgramFiles%\Microsoft Visual Studio\2022\Professional"
if defined VS_INSTALL exit /b 0
call :try_visual_studio "%ProgramFiles%\Microsoft Visual Studio\2022\Enterprise"
if defined VS_INSTALL exit /b 0
call :try_visual_studio "%ProgramFiles%\Microsoft Visual Studio\2022\BuildTools"
if defined VS_INSTALL exit /b 0
call :try_visual_studio "%ProgramFiles(x86)%\Microsoft Visual Studio\2019\Community"
if defined VS_INSTALL exit /b 0
call :try_visual_studio "%ProgramFiles(x86)%\Microsoft Visual Studio\2019\Professional"
if defined VS_INSTALL exit /b 0
call :try_visual_studio "%ProgramFiles(x86)%\Microsoft Visual Studio\2019\Enterprise"
if defined VS_INSTALL exit /b 0
call :try_visual_studio "%ProgramFiles(x86)%\Microsoft Visual Studio\2019\BuildTools"
if defined VS_INSTALL exit /b 0

exit /b 0

:try_visual_studio
if exist "%~1\VC\Auxiliary\Build\vcvars64.bat" set "VS_INSTALL=%~1"
exit /b 0
