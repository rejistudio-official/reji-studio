@echo off
REM Configure Reji Studio for Ninja build
REM Usage: scripts\configure.bat

setlocal enabledelayedexpansion

REM Detect Visual Studio installation
set VSWHERE="%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist %VSWHERE% (
    set VSWHERE="%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
)
if not exist %VSWHERE% (
    echo ERROR: vswhere.exe not found - check Visual Studio installation
    exit /b 1
)

for /f "usebackq tokens=*" %%A in (`%VSWHERE% -latest -property installationPath`) do set "VSINSTALL=%%A"

if "!VSINSTALL!"=="" (
    echo ERROR: Visual Studio installation not found
    exit /b 1
)

set VCVARS="!VSINSTALL!\VC\Auxiliary\Build\vcvars64.bat"
if not exist !VCVARS! (
    echo ERROR: vcvars64.bat not found at !VCVARS!
    exit /b 1
)

echo [*] Setting up MSVC environment from !VSINSTALL!...
call !VCVARS!

REM Ninja path (built into VS 2024)
set NINJA_PATH="!VSINSTALL!\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja"

if not exist !NINJA_PATH!\ninja.exe (
    echo ERROR: Ninja not found at !NINJA_PATH!
    echo Install: VS installer ^> C++ workload ^> CMake tools for Windows
    exit /b 1
)

cd /d C:\reji-studio

echo [*] Configuring with Ninja generator...
cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Release -DCMAKE_MAKE_PROGRAM=!NINJA_PATH!\ninja.exe

if %ERRORLEVEL% equ 0 (
    echo.
    echo [OK] Configuration complete. Next:
    echo      scripts\build.bat
    exit /b 0
) else (
    echo [ERROR] Configuration failed
    exit /b 1
)
