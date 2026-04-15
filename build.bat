@echo off
setlocal enabledelayedexpansion

rem Find vcvars64.bat automatically
set "VCVARS="
for %%v in (18 17 16) do (
    for %%e in ("Community" "Professional" "Enterprise" "BuildTools") do (
        set "CANDIDATE=C:\Program Files\Microsoft Visual Studio\%%v\%%e\VC\Auxiliary\Build\vcvars64.bat"
        if exist "!CANDIDATE!" set "VCVARS=!CANDIDATE!"
        set "CANDIDATE=C:\Program Files (x86)\Microsoft Visual Studio\%%v\%%e\VC\Auxiliary\Build\vcvars64.bat"
        if exist "!CANDIDATE!" set "VCVARS=!CANDIDATE!"
    )
)

if "%VCVARS%"=="" (
    echo ERROR: Visual Studio not found. Install VS 2019 or later with C++ Build Tools.
    exit /b 1
)

call "%VCVARS%"

set BUILD_DIR=%~dp0build
cmake -S "%~dp0" -B "%BUILD_DIR%" -G Ninja -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 (echo CMake configure failed & exit /b 1)

cmake --build "%BUILD_DIR%"
if errorlevel 1 (echo Build failed & exit /b 1)

echo.
echo Build successful: %BUILD_DIR%\PacketTestBundle.exe
