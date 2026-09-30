@echo off
rem Builds and runs the hotkey recording test (tools\test_recorder.cpp): no keyboard, no window.
setlocal
cd /d "%~dp0.."
if "%MINGW%"=="" set MINGW=..\..\..\..\ClipDiary Fluent\mingw64\bin
for %%I in ("%MINGW%") do set MINGW=%%~fI
set PATH=%MINGW%;%PATH%
if not exist build mkdir build
g++ -specs=no-default-manifest.specs -std=c++17 -Wall -static -Isrc tools\test_recorder.cpp src\recorder.cpp -o build\test_recorder.exe || exit /b 1
build\test_recorder.exe
