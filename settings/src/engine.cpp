#include "engine.h"

#include <wx/filename.h>

#include <shellapi.h>

namespace
{
LRESULT Send(HWND engine, const wchar_t* name, WPARAM wParam, bool* answered)
{
    static_assert(sizeof(LRESULT) >= sizeof(long), "");
    DWORD_PTR result = 0;
    const UINT msg = RegisterWindowMessageW(name);
    *answered = engine && SendMessageTimeoutW(engine, msg, wParam, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, 3000, &result);
    return *answered ? (LRESULT)result : 0;
}

bool Command(HWND engine, const wchar_t* name, WPARAM wParam = 0)
{
    bool answered = false;
    return Send(engine, name, wParam, &answered) == 1 && answered;
}

wxString ProcessPath(DWORD pid)
{
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process)
        return wxString();
    wchar_t path[MAX_PATH * 2] = {};
    DWORD size = (DWORD)(sizeof(path) / sizeof(path[0]));
    const bool ok = QueryFullProcessImageNameW(process, 0, path, &size) != FALSE;
    CloseHandle(process);
    return ok ? wxString(path, size) : wxString();
}
}

namespace Engine
{
HWND Find(const wxString& folder, unsigned long pid)
{
    // FluentSwitcher.exe, or SimpleSwitcher.exe as it was called before 7.0.6 test18.
    const wxString want = wxFileName(folder, "FluentSwitcher.exe").GetFullPath();
    const wxString old = wxFileName(folder, "SimpleSwitcher.exe").GetFullPath();
    HWND window = nullptr;
    while ((window = FindWindowExW(HWND_MESSAGE, window, L"SimpleSwitcher_Timer_001", nullptr)) != nullptr)
    {
        DWORD owner = 0;
        GetWindowThreadProcessId(window, &owner);
        const wxString path = pid ? wxString() : ProcessPath(owner);
        if (pid ? owner == pid : path.IsSameAs(want, false) || path.IsSameAs(old, false))
            return window;
    }
    return nullptr;
}

long GetState(HWND engine)
{
    bool answered = false;
    const LRESULT state = Send(engine, L"SimpleSwitcher.GetState", 0, &answered);
    return answered && (state & StateAnswered) ? (long)state : 0;
}

bool ReloadConfig(HWND engine)
{
    return Command(engine, L"SimpleSwitcher.ReloadConfig");
}

bool SetEnabled(HWND engine, bool on)
{
    return Command(engine, L"SimpleSwitcher.SetEnabled", on);
}

bool SetAutostart(HWND engine, bool on)
{
    return Command(engine, L"SimpleSwitcher.SetAutostart", on);
}

bool Quit(HWND engine)
{
    return Command(engine, L"SimpleSwitcher.Quit");
}

bool RunCommand(HWND engine, int index)
{
    return Command(engine, L"SimpleSwitcher.RunCommand", (WPARAM)index);
}

bool SetLogging(HWND engine, bool on)
{
    return Command(engine, L"SimpleSwitcher.SetLogging", on);
}

bool UpdateChecked(HWND engine)
{
    return Command(engine, L"SimpleSwitcher.UpdateChecked");
}

bool Start(const wxString& folder, const wxString& args, bool elevated)
{
    const wxString exe = wxFileName(folder, "FluentSwitcher.exe").GetFullPath();
    if (!wxFileName::FileExists(exe))
        return false;
    SHELLEXECUTEINFOW sei = { sizeof(sei) };
    sei.lpVerb = elevated ? L"runas" : L"open";
    sei.lpFile = exe.wc_str();
    sei.lpParameters = args.empty() ? nullptr : args.wc_str();
    sei.lpDirectory = folder.wc_str();
    sei.nShow = SW_SHOWNORMAL;
    return ShellExecuteExW(&sei) != FALSE;
}
}
