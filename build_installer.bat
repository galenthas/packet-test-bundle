@echo off
setlocal

rem Version from argument or default
set "VERSION=%~1"
if "%VERSION%"=="" set "VERSION=0.1.0"

rem Find ISCC
set "ISCC="
if exist "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" set "ISCC=C:\Program Files (x86)\Inno Setup 6\ISCC.exe"
if exist "C:\Program Files\Inno Setup 6\ISCC.exe"       set "ISCC=C:\Program Files\Inno Setup 6\ISCC.exe"

if "%ISCC%"=="" (
    echo ERROR: Inno Setup 6 not found. Install from https://jrsoftware.org/isinfo.php
    exit /b 1
)

mkdir "%~dp0installer_out" 2>nul
"%ISCC%" /DMyAppVersion=%VERSION% "%~dp0installer.iss"
if errorlevel 1 (echo Installer build failed & exit /b 1)

echo.
echo Installer: %~dp0installer_out\PacketTestBundle_v%VERSION%_Setup.exe
