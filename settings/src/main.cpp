// FluentSwitcher's settings window: a program of its own next to the engine (FluentSwitcher.exe).
// It edits FluentSwitcher.json and tells the running engine to read it again (engine.h).
//
//   FluentSwitcherSettings.exe                 the settings of the engine in this folder
//   --config=<FluentSwitcher.json>             another copy's settings (the engine looked for is in that folder)
//   --engine-pid=<pid>                         that engine process exactly (tests)
//   --section=<n>                              open on that section (0 = the first)
//   --wait-pid=<pid>                           first wait for that process to end (the window restarting itself)
//
// One window at a time: a second start brings the first one forward.
#include "pages.h"

#include <wx/app.h>
#include <wx/cmdline.h>
#include <wx/filename.h>
#include <wx/settings.h>
#include <wx/stdpaths.h>

#include <dwmapi.h>

namespace
{
// The settings window of the same exe that is open already, if any.
HWND OpenWindow()
{
    struct Search
    {
        wxString exe;
        HWND found = nullptr;
    } search{ wxStandardPaths::Get().GetExecutablePath() };
    EnumWindows([](HWND window, LPARAM param) -> BOOL {
        Search* s = reinterpret_cast<Search*>(param);
        wchar_t title[64] = {};
        GetWindowTextW(window, title, 64);
        if (wcscmp(title, L"FluentSwitcher") != 0 || !IsWindowVisible(window))
            return TRUE;
        DWORD pid = 0;
        GetWindowThreadProcessId(window, &pid);
        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!process)
            return TRUE;
        wchar_t path[MAX_PATH * 2] = {};
        DWORD size = MAX_PATH * 2;
        const bool same = QueryFullProcessImageNameW(process, 0, path, &size) && s->exe.IsSameAs(path, false);
        CloseHandle(process);
        if (same && pid != GetCurrentProcessId())
        {
            s->found = window;
            return FALSE;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&search));
    return search.found;
}
}

class App : public wxApp
{
public:
    bool OnInit() override
    {
        wxString configPath, enginePid, section, waitPid;
        for (int i = 1; i < argc; i++)
        {
            wxString value;
            if (argv[i].StartsWith("--config=", &value))
                configPath = value;
            else if (argv[i].StartsWith("--engine-pid=", &value))
                enginePid = value;
            else if (argv[i].StartsWith("--section=", &value))
                section = value;
            else if (argv[i].StartsWith("--wait-pid=", &value))
                waitPid = value;
        }
        unsigned long previous = 0;
        if (waitPid.ToULong(&previous) && previous)
        {
            // The window restarting itself (a new language): the old one is closing and still holds the mutex.
            if (HANDLE old = OpenProcess(SYNCHRONIZE, FALSE, previous))
            {
                WaitForSingleObject(old, 5000);
                CloseHandle(old);
            }
        }
        if (configPath.empty())
        {
            m_instance = CreateMutexW(nullptr, FALSE, L"Local\\FluentSwitcher.Settings");
            if (GetLastError() == ERROR_ALREADY_EXISTS)
            {
                if (HWND open = OpenWindow())
                {
                    if (IsIconic(open))
                        ShowWindow(open, SW_RESTORE);
                    SetForegroundWindow(open);
                }
                return false;
            }
            // SimpleSwitcher.json until 7.0.6 test19: the engine renames it when it starts, and so do we
            // when the window comes first.
            const wxString folder = wxFileName(wxStandardPaths::Get().GetExecutablePath()).GetPath();
            configPath = folder + "\\FluentSwitcher.json";
            if (!wxFileName::FileExists(configPath) && wxFileName::FileExists(folder + "\\SimpleSwitcher.json"))
                MoveFileExW((folder + "\\SimpleSwitcher.json").wc_str(), configPath.wc_str(), MOVEFILE_WRITE_THROUGH);
        }
        unsigned long pid = 0;
        enginePid.ToULong(&pid);
        long first = 0;
        section.ToLong(&first);

        SetAppDisplayName("FluentSwitcher"); // the title of AskFluent
        MSWEnableDarkMode(DarkMode_Auto);    // scroll bars and the frame follow Windows
        g = MakeTheme(wxSystemSettings::GetAppearance().IsDark(), "windows");

        Config config;
        wxString error;
        config.Load(configPath, &error);
        SetEnglish(EnglishFor(config.GetString("gui_lang", wxString())));
        SettingsFrame* frame = new SettingsFrame(config, wxFileName(configPath).GetPath(), pid, error, (int)first);
        // A new window is on the screen before it has painted anything, and for a moment it is a white
        // rectangle. Hidden from the screen (cloaked) until all of it has painted, it appears finished.
        HWND hwnd = (HWND)frame->GetHWND();
        BOOL cloak = TRUE;
        DwmSetWindowAttribute(hwnd, DWMWA_CLOAK, &cloak, sizeof(cloak));
        frame->Show();
        frame->Raise();
        SetForegroundWindow(hwnd); // the engine allowed it (AllowSetForegroundWindow) when it started us
        CallAfter([hwnd] {
            RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN | RDW_UPDATENOW);
            BOOL uncloak = FALSE;
            DwmSetWindowAttribute(hwnd, DWMWA_CLOAK, &uncloak, sizeof(uncloak));
        });
        return true;
    }

    int OnExit() override
    {
        if (m_instance)
            CloseHandle(m_instance);
        return wxApp::OnExit();
    }

    // The focus ring shows only while the keyboard is in use, as in Windows 11.
    int FilterEvent(wxEvent& e) override
    {
        const wxEventType type = e.GetEventType();
        if (type == wxEVT_CHAR_HOOK)
        {
            const int key = static_cast<wxKeyEvent&>(e).GetKeyCode();
            if (key == WXK_TAB || key == WXK_UP || key == WXK_DOWN || key == WXK_LEFT || key == WXK_RIGHT)
                SetKeyboardCues(true);
        }
        else if (type == wxEVT_LEFT_DOWN || type == wxEVT_RIGHT_DOWN || type == wxEVT_MIDDLE_DOWN)
            SetKeyboardCues(false);
        return Event_Skip;
    }

private:
    HANDLE m_instance = nullptr;
};

wxIMPLEMENT_APP(App);
