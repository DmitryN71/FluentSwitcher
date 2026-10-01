// The running engine (FluentSwitcher.exe) and its commands for this window: SettingsIpc.h in the
// engine's sources has the other side. The engine is the one whose exe lies in the given folder, so a
// second copy elsewhere (a test one, an old install) is never touched.
#pragma once

#include <wx/string.h>
#include <wx/msw/wrapwin.h>

namespace Engine
{
enum : long
{
    StateAnswered = 0x100, // the engine answered at all
    StateEnabled = 0x1,
    StateElevated = 0x2,   // it runs as administrator
    StateAutostart = 0x4,
    StateLogging = 0x8,    // the debug log is on
};

// The engine's window: of the process `pid` when it is not 0 (tests), else of FluentSwitcher.exe in
// `folder`. nullptr when it is not running.
HWND Find(const wxString& folder, unsigned long pid);

// 0 when it did not answer.
long GetState(HWND engine);
bool ReloadConfig(HWND engine);
// False: refused (switching on needs administrator rights with "work in programs run as administrator").
bool SetEnabled(HWND engine, bool on);
// False: failed (the scheduler task for administrator mode needs administrator rights).
bool SetAutostart(HWND engine, bool on);
bool Quit(HWND engine);
// Runs command `index` of run_programs as saved (as its hotkey would).
bool RunCommand(HWND engine, int index);
// The debug log (log\FluentSwitcher.exe.log next to the engine), until the engine quits.
bool SetLogging(HWND engine, bool on);

// Starts FluentSwitcher.exe from `folder` with `args`; `elevated`: as administrator (Windows asks).
// False if it is not there or does not start (also when the user says No to Windows).
bool Start(const wxString& folder, const wxString& args = wxString(), bool elevated = false);
}
