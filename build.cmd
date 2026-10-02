@echo off
rem Builds FluentSwitcher.exe: Visual Studio 2026 or its Build Tools (C++ with ATL), CMake and Ninja (both come
rem with Visual Studio).
rem   WXDIR - a wxWidgets 3.3 source tree (optional): without it CMake downloads the 3.3.3 release.
rem Result: build\x64-release\FluentSwitcher.exe with flags\ next to it.
setlocal
cd /d "%~dp0"
set VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe
if not defined VSDIR for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set VSDIR=%%I
if not defined VSDIR (echo Visual Studio with C++ is not found & exit /b 1)
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
set WXARG=
if defined WXDIR for %%I in ("%WXDIR%") do set WXDIR=%%~fI
if defined WXDIR if exist "%WXDIR%\CMakeLists.txt" set WXARG=-DFLUENTSWITCHER_WX_DIR="%WXDIR:\=/%"
cmake --preset x64-release %WXARG% || exit /b 1
cmake --build build\x64-release || exit /b 1
xcopy /e /i /y /q bin_files\flags build\x64-release\flags >nul
echo Built %CD%\build\x64-release\FluentSwitcher.exe
