@echo off
setlocal enabledelayedexpansion

REM Find VS installation
for /f "tokens=* usebackq" %%i in (`"C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath`) do set VS_PATH=%%i

REM Set up environment
call "!VS_PATH!\VC\Auxiliary\Build\vcvars64.bat"

REM Configure and build
cd /d C:\reji-studio
cmake -B build -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 (
    echo CMake configure failed
    exit /b 1
)

cd build
cmake --build . --target reji_app
if errorlevel 1 (
    echo Build failed
    exit /b 1
)

echo Build successful!
