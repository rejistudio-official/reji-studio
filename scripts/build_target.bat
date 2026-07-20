@echo off
rem Tek bir CMake hedefini vcvars ortaminda derle: build_target.bat <hedef>
rem (scripts/build.py'nin vcvars kalibinin hedef-parametreli hafif esi;
rem  test hedefleri build.py --target listesinde olmadigi icin gerekli.)
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" x64
if errorlevel 1 exit /b 1
cmake "C:\reji-studio\build"
if errorlevel 1 exit /b 1
cmake --build "C:\reji-studio\build" --target %1
