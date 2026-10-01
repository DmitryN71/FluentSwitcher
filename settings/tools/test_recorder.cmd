@echo off
rem Builds and runs the hotkey recording test (tools\test_recorder.cpp): no keyboard, no window.
rem MSVC from Visual Studio 2026 Build Tools, as the program itself.
setlocal
cd /d "%~dp0.."
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
if not exist build mkdir build
cl /nologo /std:c++17 /EHsc /utf-8 /O2 /MT /Isrc tools\test_recorder.cpp src\recorder.cpp /Fobuild\ /Fe:build\test_recorder.exe >build\test_recorder.log 2>&1 || (type build\test_recorder.log & exit /b 1)
build\test_recorder.exe