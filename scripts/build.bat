@echo off
REM Build Reji Studio with Ninja
REM Usage: scripts\build.bat [target]
REM Default target: reji_app
REM Examples: scripts\build.bat reji_pipeline
REM           scripts\build.bat reji_ui

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
    echo ERROR: vcvars64.bat not found
    exit /b 1
)

echo [*] Setting up MSVC environment...
call !VCVARS!

REM Set build target (default: reji_app)
set TARGET=reji_app
if not "%1"=="" set TARGET=%1

REM Ninja path
set NINJA_PATH="!VSINSTALL!\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja"

if not exist !NINJA_PATH!\ninja.exe (
    echo ERROR: Ninja not found at !NINJA_PATH!
    exit /b 1
)

cd /d C:\reji-studio

if not exist build (
    echo [*] build/ directory missing, running configure first...
    call scripts\configure.bat
    if !ERRORLEVEL! neq 0 exit /b 1
)

echo [*] Building target: !TARGET! with Ninja...
cmake --build build --target !TARGET! -- -j 8

if %ERRORLEVEL% equ 0 (
    echo.
    echo [OK] Build succeeded
    if "!TARGET!"=="reji_app" (
        echo     Output: build\src\ui\reji_app.exe
        echo     Run: build\src\ui\reji_app.exe
    )
    exit /b 0
) else (
    echo [ERROR] Build failed
    exit /b 1
)
