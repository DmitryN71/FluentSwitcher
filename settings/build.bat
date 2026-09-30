@echo off
rem Builds FluentSwitcherSettings.exe, the settings window of FluentSwitcher: MinGW-w64 (GCC) against a
rem static release build of wxWidgets 3.3, the same toolchain as FluentClipper.
rem   WXDIR  - wxWidgets source tree, already built with (in %WXDIR%\build\msw):
rem              mingw32-make -f makefile.gcc SHELL=cmd.exe BUILD=release SHARED=0 UNICODE=1 setup_h
rem              mingw32-make -f makefile.gcc SHELL=cmd.exe BUILD=release SHARED=0 UNICODE=1 -j20
rem   MINGW  - MinGW-w64 bin folder (WinLibs GCC 16)
rem   OUT    - output file, build\FluentSwitcherSettings.exe by default
rem The defaults point at the copies next to FluentClipper on Dmitry's PC.
setlocal
cd /d "%~dp0"
if "%WXDIR%"=="" set WXDIR=..\..\..\..\ClipDiary Fluent\wx
if "%MINGW%"=="" set MINGW=..\..\..\..\ClipDiary Fluent\mingw64\bin
for %%I in ("%MINGW%") do set MINGW=%%~fI
set PATH=%MINGW%;%PATH%
set WXLIB=%WXDIR%\lib\gcc_lib
if not exist build mkdir build
if "%OUT%"=="" set OUT=build\FluentSwitcherSettings.exe

rem The resources need nothing from wxWidgets: the manifest (Common Controls 6, per-monitor DPI) is our own.
windres --use-temp-file -i settings.rc -o build\settings_rc.o || exit /b 1

g++ -specs=no-default-manifest.specs -O2 -std=c++17 -mthreads -D__WXMSW__ -DNDEBUG -D_UNICODE -DUNICODE -Wall -Wno-unused-parameter ^
  -I"%WXLIB%\mswu" -I"%WXDIR%\include" -Ifluentui -I..\extern ^
  src\main.cpp src\config.cpp src\engine.cpp src\pages.cpp src\hotkeys.cpp src\recorder.cpp src\i18n.cpp fluentui\fluent_ui.cpp fluentui\fluent_controls.cpp ^
  build\settings_rc.o -o %OUT% -mwindows -static -s -L"%WXLIB%" ^
  -lwxmsw33u_core -lwxbase33u -lwxpng -lwxzlib -lwxregexu -lwxexpat ^
  -lkernel32 -luser32 -lgdi32 -lgdiplus -lcomdlg32 -lwinspool -lwinmm -lshell32 -lshlwapi -lcomctl32 ^
  -lole32 -loleaut32 -luuid -lrpcrt4 -ladvapi32 -lversion -lws2_32 -loleacc -luxtheme ^
  -ldwmapi -lmsimg32 -limm32 || exit /b 1

echo Built %OUT%
