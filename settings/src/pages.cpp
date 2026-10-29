#include "fs_version.h"
#include "pages.h"

#include "engine.h"
#include "hotkeys.h"
#include "icons.h"
#include "textfield.h"
#include "wordlist.h"

#include <wx/clipbrd.h>
#include <wx/datetime.h>
#include <wx/dcbuffer.h>
#include <wx/dir.h>
#include <wx/ffile.h>
#include <wx/filedlg.h>
#include <wx/filename.h>
#include <wx/tokenzr.h>
#include <wx/tooltip.h>
#include <wx/utils.h>
#include <wx/wupdlock.h>

#include "../../src/Update.h" // after wxWidgets: Windows headers of its own
#include "../../src/UpdateInstall.h"
#include "../../src/CanElevate.h"
#include "../../src/ConfigLock.h"
#include "../../src/AutoSwitch.h" // one word - one list: the engine's rule (Covers)
#include "../../src/libtools/utils/LetterIcons.h" // the badge with the letters, as the engine draws it (BadgeColor.h)
#include <wx/display.h>
#include <shlwapi.h>         // SHLoadIndirectString: the names of keyboards
#pragma comment(lib, "shlwapi.lib")
#include <shlobj.h>           // the report to the developer: the desktop, Explorer
#include <tlhelp32.h>         // ... the running programs
#include <mapi.h>             // ... the letter with the file (Simple MAPI)
#include <map>
#include <set>
#include <thread>

namespace
{
const char* const kVersion = FS_VERSION; // fs_version.h, made by CMake: the same as the program's

// The engine learns words while this window is open - the third undo puts a word into "Не переключать" or the
// exceptions of ДВе ЗАглавные, the third fix by hand into "Переключать всегда" - and writes them to the file; the
// window's copy is older, and writing it whole would drop them. Before the window writes the file: the words the engine
// added since the window read it go into what it writes (a word the user removed here stays removed), and the engine's
// counts are taken as they are now (the window does not edit them). `now` - the file as the engine has it.
void MergeEngineLearned(Config& edit, const Config& saved, const nlohmann::json& now)
{
    const nlohmann::json& before = saved.Json();
    nlohmann::json& out = edit.Json();
    for (const char* key : { "autoswitch_exceptions", "autoswitch_force", "two_caps_exceptions" })
    {
        const auto d = now.find(key);
        if (d == now.end() || !d->is_array())
            continue;
        if (!out.contains(key) || !out[key].is_array())
        {
            out[key] = *d; // the window did not touch it: as the engine has it
            continue;
        }
        const auto b = before.find(key);
        for (const auto& word : *d)
        {
            const bool known = b != before.end() && b->is_array() && std::find(b->begin(), b->end(), word) != b->end();
            if (!known && std::find(out[key].begin(), out[key].end(), word) == out[key].end())
                out[key].push_back(word);
        }
        // And the words the engine took back since (the fix undone right after the third one, gui2 uncount): there when
        // the window read the file, not on the disk any more - out of what it writes too.
        if (b != before.end() && b->is_array())
            for (const auto& word : *b)
                if (std::find(d->begin(), d->end(), word) == d->end())
                {
                    auto& list = out[key];
                    if (const auto it = std::find(list.begin(), list.end(), word); it != list.end())
                        list.erase(it);
                }
    }
    // And on / off kept for the next start ("enabled": Win+F8, the menu, the switch here - the engine writes it).
    for (const char* key : { "autoswitch_undo", "autoswitch_fix", "two_caps_undo", "enabled" })
    {
        const auto d = now.find(key);
        if (d != now.end())
            out[key] = *d;
        else
            out.erase(key);
    }
    // The automatic switch on / off: the engine writes it too (the tray menu, its hotkey) - as on the disk, unless it was
    // changed here (review 11.10.2026: an unrelated Apply wrote the window's old value back, and it came back on).
    for (const char* key : { "autoswitch" })
    {
        const auto d = now.find(key), b = before.find(key);
        const auto o = std::as_const(out).find(key);
        const bool here = (o == out.cend()) != (b == before.end()) || (o != out.cend() && *o != *b);
        if (here)
            continue;
        if (d != now.end())
            out[key] = *d;
        else
            out.erase(key);
    }
    // The words the engine learned ("выучено" in the lists), as it has them: those still in the lists, and not the ones
    // removed here and added again by hand (the window had the mark, it has not now).
    auto marks = [](const nlohmann::json& json, const char* key) {
        const auto all = json.find("learned");
        if (all == json.end() || !all->is_object() || !all->contains(key) || !(*all)[key].is_array())
            return nlohmann::json::array();
        return (*all)[key];
    };
    auto has = [](const nlohmann::json& list, const nlohmann::json& word) {
        return list.is_array() && std::find(list.begin(), list.end(), word) != list.end();
    };
    nlohmann::json learned = nlohmann::json::object();
    for (const char* key : { "autoswitch_exceptions", "autoswitch_force", "two_caps_exceptions" })
    {
        const nlohmann::json engine = marks(now, key), was = marks(before, key), is = marks(out, key);
        for (const auto& word : engine)
            if (out.contains(key) && has(out[key], word) && (has(is, word) || !has(was, word)))
                learned[key].push_back(word);
    }
    if (learned.empty())
        out.erase("learned");
    else
        out["learned"] = learned;
}

void TakeEngineLearned(Config& edit, const Config& saved)
{
    Config disk;
    wxString error;
    if (disk.Load(edit.Path(), &error))
        MergeEngineLearned(edit, saved, std::as_const(disk).Json());
}

// ----- The report for the forum: the errors of the journal of the automatic switch -----
// The program sends nothing: the report is a text the user sees, edits and copies into a post (the beta testers' way to
// tell what the switch got wrong, forum/beta-invite.txt).

// "Windows 11 Pro 25H2 (26220)".
wxString WindowsVersion()
{
    auto read = [](const wchar_t* name) {
        wchar_t value[128] = {};
        DWORD size = sizeof(value);
        return RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", name, RRF_RT_REG_SZ,
                            nullptr, value, &size) == ERROR_SUCCESS
            ? wxString(value)
            : wxString();
    };
    wxString product = read(L"ProductName");
    const wxString display = read(L"DisplayVersion"), build = read(L"CurrentBuild");
    long number = 0;
    if (build.ToLong(&number) && number >= 22000)
        product.Replace("Windows 10", "Windows 11"); // Windows 11 still calls itself 10 there
    return wxString::Format("%s %s (%s)", product, display, build);
}

// The layouts of Windows: "en-US, ru-RU" (a variant of a language - with its number: "ru-RU (0419-f0a5)" is not usual).
wxString LayoutList()
{
    HKL list[32];
    const int n = GetKeyboardLayoutList(32, list);
    wxString names;
    for (int i = 0; i < n; i++)
    {
        const UINT_PTR value = (UINT_PTR)list[i];
        wchar_t locale[LOCALE_NAME_MAX_LENGTH] = {};
        wxString name = LCIDToLocaleName(MAKELCID(LOWORD(value), SORT_DEFAULT), locale, LOCALE_NAME_MAX_LENGTH, 0)
            ? wxString(locale)
            : wxString::Format("%04x", (unsigned)LOWORD(value));
        if (HIWORD(value) != LOWORD(value))
            name += wxString::Format(" (%04x-%04x)", (unsigned)LOWORD(value), (unsigned)HIWORD(value));
        names += (names.empty() ? "" : ", ") + name;
    }
    return names;
}

// The report: the version, Windows, the layouts, the switches; how many lines of each kind the journal has; and its
// errors - "switched back" (switched by mistake) and "by hand" (missed), at most the last 200. errors - how many there
// are; -1 - no journal.
// UTF-8 with broken bytes (a line cut off by a power loss, a file saved again in another encoding): the broken ones as
// replacement characters - wxConvUTF8 gave nothing for the whole file, and the report said there were no errors.
wxString FromUtf8Lenient(const std::string& bytes)
{
    const int n = MultiByteToWideChar(CP_UTF8, 0, bytes.data(), (int)bytes.size(), nullptr, 0);
    std::wstring text(n, L'\0');
    if (n > 0)
        MultiByteToWideChar(CP_UTF8, 0, bytes.data(), (int)bytes.size(), text.data(), n);
    return wxString(text);
}

wxString JournalReport(const wxString& folder, const Config& config, int* errors)
{
    // The journal in Russian or Ukrainian ("вручну" begins "вручную" too).
    const wxString back = wxString::FromUTF8("вернули"), backUk = wxString::FromUTF8("повернули"),
                   hand = wxString::FromUTF8("вручну");
    wxArrayString found;
    wxString first, last;
    int switched = 0, backs = 0, hands = 0;
    bool any = false;
    for (const char* name : { "autoswitch.old.log", "autoswitch.log" })
    {
        const wxString path = folder + "\\log\\" + name;
        if (!wxFileExists(path)) // the old one is there only after the journal grew over a megabyte
            continue;
        wxLogNull quiet; // a file being written by the engine: no message boxes of wxWidgets
        wxFFile file(path, "rb");
        if (!file.IsOpened())
            continue;
        std::string bytes((size_t)std::max<wxFileOffset>(file.Length(), 0), '\0');
        bytes.resize(file.Read(bytes.data(), bytes.size()));
        const wxString content = FromUtf8Lenient(bytes);
        any = true;
        // "06.10.2026 18:56:14  by hand    еру → the  (claude.exe)  [a short word alone]": the kind from the 22nd character.
        for (wxString line : wxSplit(content, '\n', '\0'))
        {
            line.Trim();
            if (line.length() < 22)
                continue;
            const wxString kind = line.Mid(21);
            const bool isBack = kind.StartsWith("switched back") || kind.StartsWith(back) || kind.StartsWith(backUk);
            const bool isHand = kind.StartsWith("by hand") || kind.StartsWith(hand);
            (isBack ? backs : isHand ? hands : switched)++;
            if (first.empty())
                first = line.Left(10);
            last = line.Left(10);
            if (isBack || isHand)
            {
                line.Replace(wxString::FromUTF8("→"), "->"); // the forum is in windows-1251, it has no arrow
                found.Add(line);
            }
        }
    }
    *errors = any ? (int)found.size() : -1;
    const size_t kMax = 200;
    const size_t skip = found.size() > kMax ? found.size() - kMax : 0;
    auto onOff = [&config](const char* key, bool def) { return config.GetBool(key, def) ? T("вкл.") : T("выкл."); };
    wxString text = wxString::Format("FluentSwitcher %s, %s\n", kVersion, WindowsVersion());
    text += T("Раскладки: ") + LayoutList() + "\n";
    text += wxString::Format(T("Автопереключение: %s, не ждать конца слова: %s, ДВе ЗАглавные: %s"),
                             onOff("autoswitch", false), onOff("autoswitch_early", true), onOff("two_caps", false)) + "\n";
    text += wxString::Format(T("Журнал с %s по %s: само – %d, вернули – %d, вручную – %d"), first, last, switched, backs,
                             hands) + "\n";
    if (skip)
        text += wxString::Format(T("Последние %zu ошибок из %zu"), kMax, found.size()) + "\n";
    text += "\n";
    for (size_t i = skip; i < found.size(); i++)
        text += found[i] + "\n";
    return text;
}

// ----- The report to the developer (1.5.2; Dmitry, 11.10.2026: a debug log people can turn on "не опасаясь, что пароли и
// т.д. в логе будут", sent by mail) -----
// The engine records what it does with no typed text (its safe log, Logger.h: log\FluentSwitcher-report.log); the window
// adds what is needed around it, saves it as a text file on the desktop, shows it in Explorer and opens a new letter to
// the developer. The user may read the file first and attaches it; nothing is sent by itself.

// "+fluentswitcher": the same mailbox, Gmail sorts the reports by it.
const wchar_t* const kReportMail = L"dmitry.novikov71+fluentswitcher@gmail.com";

// The report to the developer is in English whatever the window's language: a mail app may save the attached file
// again in its own code page, and Cyrillic came as "????" (Maz's reports, 11.10 and 14.10.2026; Dmitry: "Да, давай").

// Windows' own hotkeys for switching (HKCU\Keyboard Layout\Toggle): between languages and between the layouts of one.
wxString WindowsSwitchHotkeys()
{
    auto read = [](const wchar_t* name) -> wxString {
        wchar_t value[16] = {};
        DWORD size = sizeof(value);
        if (RegGetValueW(HKEY_CURRENT_USER, L"Keyboard Layout\\Toggle", name, RRF_RT_REG_SZ, nullptr, value, &size) !=
            ERROR_SUCCESS)
            return wxString("default");
        const wxString v(value);
        return v == "1" ? wxString("Alt + Shift") : v == "2" ? wxString("Ctrl + Shift") : v == "3" ? wxString("none")
             : v == "4" ? wxString("`") : v;
    };
    return wxString::Format("language %s, layout %s", read(L"Language Hotkey"), read(L"Layout Hotkey"));
}

// Running programs whose names contain one of `known` (lower case), by name; none - "none".
wxString RunningPrograms(std::initializer_list<const char*> known)
{
    wxArrayString found;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE)
        return wxString();
    PROCESSENTRY32W entry = { sizeof(entry) };
    for (BOOL ok = Process32FirstW(snap, &entry); ok; ok = Process32NextW(snap, &entry))
    {
        const wxString name = wxString(entry.szExeFile).Lower();
        for (const char* k : known)
            if (name.Contains(k) && found.Index(name) == wxNOT_FOUND)
            {
                found.Add(name);
                break;
            }
    }
    CloseHandle(snap);
    wxString list;
    for (const wxString& name : found)
        list += (list.empty() ? "" : ", ") + name;
    return list.empty() ? wxString("none") : list;
}

// Running programs that also take the keyboard (switchers, remappers, keyboard and mouse software): one of them may
// catch the keys first (forum, 10.10.2026: hotkeys did not work after Caramba).
wxString KeyboardPrograms()
{
    return RunningPrograms({ "caramba", "punto", "ps64ldr", "autohotkey", "everylang", "orfo", "keyboardninja",
                             "arumswitcher", "simpleswitcher", "langbar", "keyran", "sharpkeys", "keytweak",
                             "powertoys.keyboardmanager", "logioptions", "logi_", "lghub", "setpoint", "icue", "razer",
                             "steelseries", "x-mouse", "mahou" });
}

// Running antivirus programs other than Windows' own: a "secure keyboard input" of theirs hides the typed letters from
// every program that watches the keyboard (Maz, 14.10.2026: the letters typed in a search field of a shop never reached
// the program, the Backspaces did).
wxString AntivirusPrograms()
{
    return RunningPrograms({ "avp.exe", "avpui.exe", "ekrn.exe", "egui.exe", "dwservice.exe", "dwengine.exe",
                             "spideragent.exe", "bdagent.exe", "bdservicehost.exe", "avastui.exe", "avgui.exe",
                             "norton", "mcshield.exe", "mbamservice.exe", "sophos", "avira", "360tray.exe" });
}

// The settings without what is personal: the learned words and the lists of words, the commands' programs and texts,
// the place of the window; the user's folder in paths as %USERPROFILE%.
wxString SettingsForReport(const wxString& path)
{
    Config config;
    wxString error;
    if (!config.Load(path, &error))
        return "not read: " + error;
    nlohmann::json json = std::as_const(config).Json();
    for (const char* key : { "autoswitch_exceptions", "autoswitch_force", "autoswitch_fix", "autoswitch_undo",
                             "two_caps_exceptions", "two_caps_undo", "learned", "settings_window" })
        json.erase(key);
    if (json.contains("run_programs") && json["run_programs"].is_array())
        for (auto& command : json["run_programs"])
            if (command.is_object())
                for (const char* key : { "cmd", "args" })
                    if (command.contains(key))
                        command[key] = "...";
    wchar_t profile[MAX_PATH] = {};
    std::wstring home = GetEnvironmentVariableW(L"USERPROFILE", profile, MAX_PATH) ? std::wstring(profile) : std::wstring();
    CharLowerBuffW(home.data(), (DWORD)home.size());
    std::function<void(nlohmann::json&)> hide = [&](nlohmann::json& j) {
        if (j.is_string() && !home.empty())
        {
            std::wstring s = wxString::FromUTF8(j.get<std::string>()).ToStdWstring(), lower = s;
            CharLowerBuffW(lower.data(), (DWORD)lower.size());
            for (size_t at = lower.find(home); at != std::wstring::npos; at = lower.find(home))
            {
                s.replace(at, home.size(), L"%USERPROFILE%");
                lower.replace(at, home.size(), L"%userprofile%");
            }
            j = std::string(wxString(s).utf8_str());
        }
        else if (j.is_array() || j.is_object())
            for (auto& item : j)
                hide(item);
    };
    hide(json);
    // Letters of other alphabets as \u codes: a mail app may resave the attachment in its code page (Maz's report came in
    // Western, Cyrillic as "?") - the settings stay readable.
    return wxString::FromUTF8(json.dump(1, ' ', true));
}

// The report: what, where, the settings and the engine's recording (only its end if it is huge).
wxString DeveloperReport(const wxString& folder, const wxString& configPath)
{
    wxString log;
    {
        wxLogNull quiet;
        wxFFile file(folder + "\\log\\FluentSwitcher-report.log", "rb");
        if (file.IsOpened())
        {
            std::string bytes((size_t)std::max<wxFileOffset>(file.Length(), 0), '\0');
            bytes.resize(file.Read(bytes.data(), bytes.size()));
            const size_t kMax = 8 * 1024 * 1024;
            if (bytes.size() > kMax)
            {
                bytes.erase(0, bytes.size() - kMax);
                bytes.erase(0, bytes.find('\n') + 1); // from a whole line
            }
            if (bytes.starts_with("\xEF\xBB\xBF"))
                bytes.erase(0, 3);
            log = FromUtf8Lenient(bytes);
        }
    }
    if (log.Strip(wxString::both).empty())
        return wxString();
    wxString text = wxString::Format("Send this file to %s", kReportMail) + "\n\n";
    text += wxString::Format("FluentSwitcher %s - report to the developer, %s, window language %s\n", kVersion,
                             wxDateTime::Now().Format("%d.%m.%Y %H:%M"),
                             CurrentLanguage() == Language::Russian ? "Russian" : IsEnglish() ? "English" : "Ukrainian");
    text += "Windows: " + WindowsVersion() + ", account: " + (CanElevateSelf() ? "administrator" : "standard") + "\n";
    text += "Layouts: " + LayoutList() + "\n";
    text += "Windows hotkeys to switch: " + WindowsSwitchHotkeys() + "\n";
    text += "Other programs that work with the keyboard: " + KeyboardPrograms() + "\n";
    text += "Antivirus programs besides Windows' own: " + AntivirusPrograms() + "\n";
    text += "\n--- Settings (without the word lists and the commands' texts) ---\n" + SettingsForReport(configPath) + "\n";
    text += "\n--- Recording (no typed text: the number of letters instead) ---\n" + log;
    return text;
}

// The report as a file on the desktop. Its path; empty - not saved.
wxString SaveOnDesktop(const wxString& text)
{
    PWSTR desktop = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_Desktop, 0, nullptr, &desktop)))
        return wxString();
    const wxString path = wxString(desktop) + "\\" + wxDateTime::Now().Format("FluentSwitcher-report-%Y-%m-%d-%H%M.txt");
    CoTaskMemFree(desktop);
    wxLogNull quiet;
    wxFFile file(path, "wb");
    if (!file.IsOpened())
        return wxString();
    const wxScopedCharBuffer utf8 = text.utf8_str();
    file.Write("\xEF\xBB\xBF", 3); // UTF-8 for Notepad and mail
    file.Write(utf8.data(), utf8.length());
    return path;
}

// The mail app takes a letter with a file (Simple MAPI: Outlook, Thunderbird, eM Client - the one Windows has as the
// mail program, HKCU\Software\Clients\Mail, else HKLM). Not Mail of Windows ("PackagedMail": no Simple MAPI, Windows
// would tell an error). Not from a window run as administrator: the mail app would start so too.
bool MailAppTakesFiles()
{
    if (OpenAsUserDetails::IsElevated())
        return false;
    auto client = [](HKEY root) {
        wchar_t name[128] = {};
        DWORD size = sizeof(name);
        return RegGetValueW(root, L"Software\\Clients\\Mail", nullptr, RRF_RT_REG_SZ, nullptr, name, &size) ==
                       ERROR_SUCCESS
                   ? std::wstring(name)
                   : std::wstring();
    };
    std::wstring name = client(HKEY_CURRENT_USER);
    if (name.empty())
        name = client(HKEY_LOCAL_MACHINE);
    return !name.empty() && _wcsicmp(name.c_str(), L"PackagedMail") != 0;
}

// A new letter to the developer: the subject, what to tell, the report attached (Dmitry, 11.10.2026: "файл
// автоматически не вставляется" - mailto cannot attach). Through Simple MAPI, in a thread of its own (the mail app may
// hold it until the letter is sent or closed); the app does not take it - mailto, to attach the file by hand. The file
// is shown in Explorer either way (forum, 11.10.2026: a mail app said nothing, the letter had gone to spam - the report
// was there to send again). True - the letter goes with the file (as far as the mail app can).
bool OpenLetter(const wxString& path)
{
    const wxString name = wxFileName(path).GetFullName();
    const std::wstring subject = wxString::Format(T("FluentSwitcher %s – отчёт"), kVersion).ToStdWstring();
    const std::wstring ask = (T("Что делали:") + "\r\n\r\n" + T("Что ожидали:") + "\r\n\r\n" + T("Что получилось:") +
                              "\r\n\r\n").ToStdWstring();
    const std::wstring attachByHand =
        ask + wxString::Format(T("Отчёт – файл %s на рабочем столе, приложите его к письму."), name).ToStdWstring() + L"\r\n";
    const bool mapi = MailAppTakesFiles();
    std::thread([subject, ask, attachByHand, mapi, file = path.ToStdWstring(), fileName = name.ToStdWstring()] {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        if (PIDLIST_ABSOLUTE item = ILCreateFromPathW(file.c_str()))
        {
            SHOpenFolderAndSelectItems(item, 0, nullptr, 0);
            ILFree(item);
        }
        if (mapi)
        {
            ULONG result = MAPI_E_FAILURE;
            if (HMODULE lib = LoadLibraryW(L"mapi32.dll"))
            {
                if (auto send = (LPMAPISENDMAILW)GetProcAddress(lib, "MAPISendMailW"))
                {
                    const std::wstring address = std::wstring(L"SMTP:") + kReportMail;
                    MapiRecipDescW to{};
                    to.ulRecipClass = MAPI_TO;
                    to.lpszName = (PWSTR)kReportMail;
                    to.lpszAddress = (PWSTR)address.c_str();
                    MapiFileDescW attached{};
                    attached.nPosition = (ULONG)-1;
                    attached.lpszPathName = (PWSTR)file.c_str();
                    attached.lpszFileName = (PWSTR)fileName.c_str();
                    MapiMessageW letter{};
                    letter.lpszSubject = (PWSTR)subject.c_str();
                    letter.lpszNoteText = (PWSTR)ask.c_str();
                    letter.nRecipCount = 1;
                    letter.lpRecips = &to;
                    letter.nFileCount = 1;
                    letter.lpFiles = &attached;
                    // Not waiting for the letter where the app can; the others - as they can.
                    result = send(0, 0, &letter, MAPI_LOGON_UI | MAPI_DIALOG_MODELESS, 0);
                    if (result == MAPI_E_NOT_SUPPORTED)
                        result = send(0, 0, &letter, MAPI_LOGON_UI | MAPI_DIALOG, 0);
                }
                FreeLibrary(lib);
            }
            if (result == SUCCESS_SUCCESS || result == MAPI_E_USER_ABORT)
            {
                CoUninitialize();
                return;
            }
        }
        auto encode = [](const std::wstring& s) {
            std::wstring out;
            for (unsigned char c : std::string(wxString(s).utf8_str()))
                out += isalnum(c) || strchr("-_.~", c) ? std::wstring(1, (wchar_t)c) : std::format(L"%{:02X}", c);
            return out;
        };
        OpenAsUser(std::wstring(L"mailto:") + kReportMail + L"?subject=" + encode(subject) + L"&body=" +
                   encode(attachByHand));
        CoUninitialize();
    }).detach();
    return mapi;
}

// Windows switches the layout only with these (Settings - Time & language - Typing - Advanced keyboard settings - Input
// language hot keys; Win + Space always). The second way of switching presses the one set here, and a key Windows has no
// use for changed nothing (forum, 11.10.2026: the right Ctrl - the engine now switches the usual way then).
bool WindowsSwitches(const wxString& hotkey)
{
    if (hotkey.empty())
        return true;
    bool alt = false, ctrl = false, shift = false, win = false;
    wxString key;
    for (wxString part : wxSplit(hotkey, '+'))
    {
        part.Trim(true).Trim(false);
        if (part == "Alt" || part == "LAlt" || part == "RAlt")
            alt = true;
        else if (part == "Ctrl" || part == "LCtrl" || part == "RCtrl")
            ctrl = true;
        else if (part == "Shift" || part == "LShift" || part == "RShift")
            shift = true;
        else if (part == "Win" || part == "LWin" || part == "RWin")
            win = true;
        else if (key.empty())
            key = part;
        else
            return false;
    }
    if (key.empty())
        return shift && alt != ctrl && !win; // Alt + Shift, Ctrl + Shift
    if (key == "Space")
        return win && !alt && !ctrl;         // Win + Space, Win + Shift + Space
    return key == "OEM_3" && !alt && !ctrl && !shift && !win; // ` (Ё)
}

// The report in a window of its own: it can be edited; "Копировать" puts it into the clipboard as a spoiler for the
// forum ([more=...]), nothing goes anywhere by itself.
void ReportDialog(wxWindow* parent, const wxString& report)
{
    wxDialog dialog(parent, wxID_ANY, T("Отчёт для форума"), wxDefaultPosition, wxDefaultSize,
                    wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
    dialog.SetBackgroundColour(g.bg);
    ApplyDwm(&dialog, false, &g.bg);
    const int pad = dialog.FromDIP(20);
    wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
    wxStaticText* label = new wxStaticText(&dialog, wxID_ANY, wxString());
    label->SetFont(UiFont(10));
    label->SetForegroundColour(g.text);
    SetWrappedLabel(label,
                    T("Только ошибки из журнала: что вы вернули и что исправили вручную; в скобках – причина, она для "
                      "разработчика. Вычеркните то, что не хотите показывать. Приложение ничего не отправляет: «Копировать» "
                      "положит текст в буфер обмена – вставьте его в сообщение в теме FluentSwitcher на форуме"),
                    dialog.FromDIP(640));
    sizer->Add(label, 0, wxLEFT | wxRIGHT | wxTOP, pad);

    // The box of the text: drawn like the kit's text boxes, a multi-line text control inside (as EditWordList).
    wxPanel* box = new wxPanel(&dialog);
    box->SetBackgroundStyle(wxBG_STYLE_PAINT);
    box->SetMinSize(dialog.FromDIP(wxSize(640, 360)));
    wxTextCtrl* text = new wxTextCtrl(box, wxID_ANY, report, wxDefaultPosition, wxDefaultSize,
                                      wxTE_MULTILINE | wxBORDER_NONE);
    text->SetFont(UiFont(9));
    text->SetBackgroundColour(g.input);
    text->SetForegroundColour(g.text);
    text->Bind(wxEVT_SET_FOCUS, [box](wxFocusEvent& e) { box->Refresh(); e.Skip(); });
    text->Bind(wxEVT_KILL_FOCUS, [box](wxFocusEvent& e) { box->Refresh(); e.Skip(); });
    box->Bind(wxEVT_PAINT, [box, text](wxPaintEvent&) {
        wxAutoBufferedPaintDC dc(box);
        dc.SetBackground(wxBrush(g.bg));
        dc.Clear();
        FillInput(dc, wxRect(box->GetClientSize()), box->FromDIP(4), g.input, text->HasFocus() ? &g.accent : nullptr,
                  box->FromDIP(2));
    });
    box->Bind(wxEVT_SIZE, [box, text](wxSizeEvent& e) {
        const wxSize size = box->GetClientSize();
        text->SetSize(box->FromDIP(10), box->FromDIP(8), size.x - box->FromDIP(20), size.y - box->FromDIP(16));
        e.Skip();
    });
    sizer->Add(box, 1, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, pad);

    wxBoxSizer* buttons = new wxBoxSizer(wxHORIZONTAL);
    // The topic of FluentSwitcher on ru-board, its last page (glp): the report goes into a post at the end.
    FluentButton* topic = new FluentButton(&dialog, wxID_ANY, T("Открыть тему на форуме"));
    topic->Bind(wxEVT_BUTTON, [](wxCommandEvent&) {
        OpenAsUser(L"https://forum.ru-board.com/topic.cgi?forum=5&topic=51833&glp");
    });
    buttons->Add(topic);
    buttons->AddStretchSpacer();
    FluentButton* copy = new FluentButton(&dialog, wxID_ANY, T("Скопировано"), true);
    copy->SetText(T("Копировать")); // as wide as the longer of the two
    buttons->Add(copy);
    buttons->Add(new FluentButton(&dialog, wxID_CANCEL, T("Готово")), 0, wxLEFT, dialog.FromDIP(8));
    sizer->Add(buttons, 0, wxEXPAND | wxALL, pad);
    copy->Bind(wxEVT_BUTTON, [copy, text](wxCommandEvent&) {
        wxString body = text->GetValue();
        body.Trim();
        const wxString post = wxString::Format("[more=%s %s]\n", T("Отчёт FluentSwitcher"), kVersion) + body + "\n[/more]";
        if (wxTheClipboard->Open())
        {
            wxTheClipboard->SetData(new wxTextDataObject(post));
            wxTheClipboard->Close();
            copy->SetText(T("Скопировано"));
        }
    });
    text->Bind(wxEVT_TEXT, [copy](wxCommandEvent&) { copy->SetText(T("Копировать")); });
    dialog.SetSizerAndFit(sizer);
    dialog.Bind(wxEVT_CHAR_HOOK, [&dialog](wxKeyEvent& e) {
        if (e.GetKeyCode() == WXK_ESCAPE)
            dialog.EndModal(wxID_CANCEL);
        else
            e.Skip();
    });
    dialog.CentreOnParent();
    text->SetFocus();
    text->SetInsertionPoint(0);
    dialog.ShowModal();
}

// "English (United States)", "Русский (Россия)": a language in that language. Empty - Windows has no name for it.
wxString LanguageName(unsigned langid)
{
    wchar_t locale[LOCALE_NAME_MAX_LENGTH] = {};
    if (!LCIDToLocaleName(MAKELCID(langid, SORT_DEFAULT), locale, LOCALE_NAME_MAX_LENGTH, 0))
        return wxString();
    wchar_t name[256] = {};
    if (!GetLocaleInfoEx(locale, LOCALE_SNATIVEDISPLAYNAME, name, 256))
        return wxString(locale);
    CharUpperBuffW(name, 1); // wxString::Upper left "русский" as it was: the C library's towupper knows Latin only
    return wxString(name);
}

const wchar_t* const kKeyboardLayouts = L"SYSTEM\\CurrentControlSet\\Control\\Keyboard Layouts";

// The keyboard of a layout as the registry names it: "00000409", "00010419". The high word of the layout is the
// keyboard: a language's own ("0409", "0419"), an IME's (E...: the whole layout is its name) or, with F in front, the
// "Layout Id" of a variant - Dvorak, the typewriter, the layouts made in Microsoft Keyboard Layout Creator (Birman's).
// Empty - not found.
wxString KeyboardId(unsigned layout)
{
    const unsigned keyboard = HIWORD(layout);
    if ((keyboard & 0xF000) == 0xE000)
        return wxString::Format("%08X", layout);
    if ((keyboard & 0xF000) != 0xF000)
        return wxString::Format("%08X", keyboard);
    HKEY all = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, kKeyboardLayouts, 0, KEY_READ, &all) != ERROR_SUCCESS)
        return wxString();
    const wxString id = wxString::Format("%04X", keyboard & 0x0FFF);
    wxString found;
    wchar_t name[256];
    for (DWORD i = 0; found.empty(); i++)
    {
        DWORD length = 256;
        if (RegEnumKeyExW(all, i, name, &length, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS)
            break;
        wchar_t value[32] = {};
        DWORD size = sizeof(value);
        if (RegGetValueW(all, name, L"Layout Id", RRF_RT_REG_SZ, nullptr, value, &size) == ERROR_SUCCESS &&
            id.IsSameAs(value, false)) // "001A" and "00a8" both occur
            found = name;
    }
    RegCloseKey(all);
    return found;
}

// "США", "Русская (машинопись)": a keyboard by the name Windows shows, in the language of Windows. Empty - not found.
wxString KeyboardName(const wxString& id)
{
    HKEY key = nullptr;
    if (id.empty() || RegOpenKeyExW(HKEY_LOCAL_MACHINE, (wxString(kKeyboardLayouts) + "\\" + id).wc_str(), 0, KEY_READ,
                                    &key) != ERROR_SUCCESS)
        return wxString();
    // "@%SystemRoot%\system32\input.dll,-5056" - the name in the language of Windows; RegLoadMUIStringW does not find
    // it there (ERROR_FILE_NOT_FOUND), SHLoadIndirectString does. "Layout Text" - in English.
    wchar_t source[MAX_PATH] = {}, name[256] = {};
    DWORD size = sizeof(source);
    wxString found;
    if (RegGetValueW(key, nullptr, L"Layout Display Name", RRF_RT_REG_SZ, nullptr, source, &size) == ERROR_SUCCESS &&
        SUCCEEDED(SHLoadIndirectString(source, name, 256, nullptr)))
        found = name;
    size = sizeof(name);
    if (found.empty() && RegGetValueW(key, nullptr, L"Layout Text", RRF_RT_REG_SZ, nullptr, name, &size) == ERROR_SUCCESS)
        found = name;
    RegCloseKey(key);
    return found;
}

// The title of a layout's card: its language, and when its keyboard is not the language's own, the keyboard too -
// "English (United States) – США (Дворак)": two layouts of one language differ. A language Windows has no name for -
// the keyboard alone. 64-bit Windows widens a layout with the high bit set: the engine saves F0080419 (a variant) as
// 0xFFFFFFFFF0080419, and only the low half counts (read as 32 bits, it did not fit and showed as that number).
wxString LayoutName(const wxString& hkl)
{
    unsigned long long value = 0;
    if (!hkl.ToULongLong(&value, 16))
        return hkl;
    const unsigned layout = unsigned(value);
    const wxString language = LanguageName(LOWORD(layout));
    const wxString keyboard = KeyboardId(layout);
    if (!language.empty() && keyboard == wxString::Format("%08X", (unsigned)LOWORD(layout)))
        return language;
    const wxString name = KeyboardName(keyboard);
    if (language.empty())
        return name.empty() ? hkl : name;
    return name.empty() ? language : language + wxString::FromUTF8(" – ") + name;
}

void HideCard(wxWindow* card)
{
    if (card && card->IsShown())
    {
        card->Hide();
        card->GetParent()->Layout();
        static_cast<wxScrolledWindow*>(card->GetParent())->FitInside();
    }
}

void ShowCard(wxWindow* card)
{
    if (card && !card->IsShown())
    {
        card->Show();
        card->GetParent()->Layout();
        static_cast<wxScrolledWindow*>(card->GetParent())->FitInside();
    }
}

// ----- The search of the settings (1.5.9; Dmitry, 13.10.2026: "у нас уже столько настроек") -----

// A text as the search compares it: lower case (Cyrillic too - CharLowerBuffW; wxString::Lower knows Latin only), ё as
// е, the lines of a wrapped description as one.
wxString Folded(const wxString& text)
{
    std::wstring s = text.ToStdWstring();
    if (!s.empty())
        CharLowerBuffW(s.data(), (DWORD)s.size());
    for (wchar_t& c : s)
        if (c == L'ё')
            c = L'е';
        else if (c == L'\n' || c == L'\r' || c == L'\t')
            c = L' ';
    return wxString(s);
}

// The words of a query: its letters and digits.
std::vector<wxString> QueryWords(const wxString& folded)
{
    std::vector<wxString> words;
    wxString word;
    for (wxUniChar c : folded)
    {
        if (IsCharAlphaNumericW((WCHAR)c.GetValue()))
            word += c;
        else if (!word.empty())
        {
            words.push_back(word);
            word.clear();
        }
    }
    if (!word.empty())
        words.push_back(word);
    return words;
}

// A word of the query in a text: as typed, or a long one without its ending - "звуков" finds "звуки", "переключения" -
// "переключение".
bool HasWord(const wxString& text, const wxString& word)
{
    if (text.Find(word) != wxNOT_FOUND)
        return true;
    const size_t n = word.length();
    const size_t cut = n >= 6 ? 2 : n == 5 ? 1 : 0;
    return cut && text.Find(word.Left(n - cut)) != wxNOT_FOUND;
}

// A found card on the page of the search: its title, its description in one line, its section on the right with a
// chevron, as the cards of Windows' own Settings that lead to a page. A click, Enter or Space opens it; Up / Down go
// through the list (onArrow).
class SearchHit : public wxPanel
{
public:
    std::function<void()> onOpen;
    std::function<void(bool down)> onArrow;

    SearchHit(wxWindow* parent, const wxString& title, const wxString& about, const wxString& section)
        : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxWANTS_CHARS | wxFULL_REPAINT_ON_RESIZE),
          m_title(title), m_about(about), m_section(section), m_font(UiFont(10)), m_small(UiFont(9)),
          m_chevron(FluentIcon(kIconChevron, g.text2))
    {
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        SetBackgroundColour(g.card);
        wxClientDC dc(this);
        dc.SetFont(m_font);
        int height = dc.GetCharHeight();
        dc.SetFont(m_small);
        if (!m_about.empty())
            height += FromDIP(2) + dc.GetCharHeight();
        SetMinSize(wxSize(FromDIP(240), height + 2 * FromDIP(14)));
        SetCursor(wxCursor(wxCURSOR_HAND));
        Bind(wxEVT_PAINT, &SearchHit::OnPaint, this);
        Bind(wxEVT_ENTER_WINDOW, [this](wxMouseEvent&) { m_hover = true; Refresh(); });
        Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent&) { m_hover = m_pressed = false; Refresh(); });
        Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent&) {
            m_pressed = true;
            SetFocus();
        });
        Bind(wxEVT_LEFT_UP, [this](wxMouseEvent& e) {
            const bool click = m_pressed && wxRect(GetClientSize()).Contains(e.GetPosition());
            m_pressed = false;
            if (click && onOpen)
                onOpen();
        });
        Bind(wxEVT_KEY_DOWN, [this](wxKeyEvent& e) {
            const int key = e.GetKeyCode();
            if ((key == WXK_RETURN || key == WXK_NUMPAD_ENTER || key == WXK_SPACE) && onOpen)
                onOpen();
            else if ((key == WXK_UP || key == WXK_DOWN) && onArrow)
                onArrow(key == WXK_DOWN);
            else
                e.Skip();
        });
        Bind(wxEVT_SET_FOCUS, [this](wxFocusEvent& e) { Refresh(); e.Skip(); });
        Bind(wxEVT_KILL_FOCUS, [this](wxFocusEvent& e) { Refresh(); e.Skip(); });
    }

    bool AcceptsFocus() const override { return true; }

private:
    void OnPaint(wxPaintEvent&)
    {
        wxAutoBufferedPaintDC dc(this);
        dc.SetBackground(wxBrush(GetParent()->GetBackgroundColour()));
        dc.Clear();
        const wxRect r(GetClientSize());
        const double radius = FromDIP(6);
        FillRound(dc, r, radius, m_hover ? g.controlHover : g.card, &g.border);
        if (ShowsFocus(this))
            DrawFocusRing(this, dc, r, radius);
        const int pad = FromDIP(14);
        // The section and the chevron on the right.
        const wxBitmap chevron = m_chevron.GetBitmapFor(this);
        int right = r.GetRight() - pad - chevron.GetLogicalWidth();
        dc.DrawBitmap(chevron, right, (r.height - chevron.GetLogicalHeight()) / 2, true);
        dc.SetFont(m_small);
        dc.SetTextForeground(g.text2);
        const wxSize section = dc.GetTextExtent(m_section);
        right -= FromDIP(8) + section.x;
        dc.DrawText(m_section, right, (r.height - section.y) / 2);
        // The title and the description on the left, cut with "..." before the section.
        const int width = right - FromDIP(24) - pad;
        const int aboutHeight = m_about.empty() ? 0 : FromDIP(2) + dc.GetCharHeight();
        dc.SetFont(m_font);
        const int titleHeight = dc.GetCharHeight();
        const int y = (r.height - titleHeight - aboutHeight) / 2;
        dc.SetTextForeground(g.text);
        dc.DrawText(wxControl::Ellipsize(m_title, dc, wxELLIPSIZE_END, width, wxELLIPSIZE_FLAGS_NONE), pad, y);
        if (!m_about.empty())
        {
            dc.SetFont(m_small);
            dc.SetTextForeground(g.text2);
            dc.DrawText(wxControl::Ellipsize(m_about, dc, wxELLIPSIZE_END, width, wxELLIPSIZE_FLAGS_NONE), pad,
                        y + titleHeight + FromDIP(2));
        }
    }

    wxString m_title, m_about, m_section;
    wxFont m_font, m_small;
    wxBitmapBundle m_chevron;
    bool m_hover = false, m_pressed = false;
};

// The control that got the focus (Tab, a click, the arrows) - scrolled into sight (Dmitry, 11.10.2026: Tab went to a
// field out of sight, and the page stayed where it was).
void KeepFocusInSight(wxScrolledWindow* page)
{
    page->Bind(wxEVT_CHILD_FOCUS, [page](wxChildFocusEvent& e) {
        e.Skip(); // the page still remembers its last focused control
        wxWindow* focused = wxWindow::FindFocus();
        int unitX = 0, unitY = 0, viewX = 0, viewY = 0;
        page->GetScrollPixelsPerUnit(&unitX, &unitY);
        if (!focused || unitY <= 0)
            return;
        page->GetViewStart(&viewX, &viewY);
        const wxRect r = focused->GetScreenRect();
        const int top = page->ScreenToClient(r.GetTopLeft()).y, height = page->GetClientSize().y;
        const int margin = page->FromDIP(12);
        int y = viewY;
        if (top < margin)
            y = viewY + (top - margin) / unitY - 1;
        else if (top + r.height > height - margin)
            y = viewY + (top + r.height - height + margin + unitY - 1) / unitY;
        if (y != viewY)
            page->Scroll(-1, std::max(0, y));
    });
}
}

SettingsFrame::SettingsFrame(const Config& config, const wxString& folder, unsigned long enginePid,
                             const wxString& loadError, int section)
    : wxFrame(nullptr, wxID_ANY, "FluentSwitcher"),
      m_saved(config), m_edit(config), m_folder(folder), m_enginePid(enginePid), m_canSave(loadError.empty()),
      m_highlightTimer(this), m_savedTimer(this)
{
    SetIcon(wxICON(aaaa));
    SetBackgroundColour(g.bg);
    ApplyDwm(this, false, &g.bg);
    // The details of a card are in its tooltip (CardTip): wrapped, and up long enough to read.
    wxToolTip::SetMaxWidth(FromDIP(440));
    wxToolTip::SetAutoPop(30000);

    m_nav = new SectionNav(this);
    // The search above the sections, as in Windows' own Settings: the results replace the page while it has text.
    m_search = new TextField(this, wxString(), 160, true, 100);
    m_search->SetHint(T("Найти параметр"));
    m_search->AllowAutoSwitch(); // a query, not a word for a list (Dmitry, 26.10.2026)
    m_search->SetIcon(kIconSearch);
    m_search->onChange = [this] { Search(); };
    m_search->onEnter = [this] {
        if (!m_searching)
            Search();
        if (!m_searchHits.empty())
            static_cast<SearchHit*>(m_searchHits.front())->onOpen();
    };
    m_search->onKey = [this](int key) {
        if (key != WXK_DOWN || !m_searching || m_searchHits.empty())
            return false;
        m_searchHits.front()->SetFocus();
        return true;
    };
    // Back in the field after a card was opened: its results again (made anew - the cards may have changed).
    m_search->onFocus = [this] {
        CallAfter([this] {
            if (!m_searching && !m_search->Value().empty())
                Search();
        });
    };
    m_title = FluentText(this, wxString(), 20, g.text, true);
    m_pagesSizer = new wxBoxSizer(wxVERTICAL);

    HotkeyEditor::s_doubleMs = (unsigned long)m_edit.GetInt("quick_press_ms", 280);
    RefreshEngine();
    BuildGeneral();
    BuildAutoSwitch();
    BuildTyping();
    BuildHotkeys();
    BuildLayouts();
    BuildTray();
    BuildCaretFlag();
    BuildSounds();
    BuildCommands();
    BuildAdvanced();
    BuildAbout();
    m_searchPage = NewSettingsPage(this, &m_searchColumn);
    m_searchPage->Hide();
    KeepFocusInSight(m_searchPage);
    m_pagesSizer->Add(m_searchPage, 1, wxEXPAND);
    Bind(wxEVT_TIMER, [this](wxTimerEvent&) { EndHighlight(); }, m_highlightTimer.GetId());

    // The program's folder has no button of its own (FluentClipper's "database folder" is useful, ours is not):
    // the debug log card opens the log's folder.
    m_nav->AddAction(kIconQuit, T("Закрыть FluentSwitcher"));
    m_nav->onSelect = [this](int section) { ShowSection(section); };
    m_nav->onAction = [this](int) {
        if (!m_engine)
        {
            Close();
            return;
        }
        if (AskFluent(this, T("Закрыть FluentSwitcher? Исправление раскладки не будет работать до следующего запуска."),
                      T("Закрыть"), T("Отмена")))
        {
            Engine::Quit(m_engine);
            Close();
        }
    };

    // Bottom: a line for messages on the left, OK / Apply / Cancel on the right. OK, not "Save" (Dmitry, 25.10.2026):
    // "Save" lit up with nothing changed looked like unsaved changes (WorldTroll on the forum); OK and Cancel are always
    // on, as in the dialogs of Windows, and the three are as wide as the widest.
    wxBoxSizer* footer = new wxBoxSizer(wxHORIZONTAL);
    m_status = FluentText(this, wxString(), 9, g.text2);
    FluentButton* save = new FluentButton(this, wxID_OK, T("ОК"), true);
    m_apply = new FluentButton(this, wxID_APPLY, T("Сохранено"));
    m_apply->SetText(T("Применить")); // as wide as the longer of the two
    FluentButton* cancel = new FluentButton(this, wxID_CANCEL, T("Отмена"));
    int buttonWidth = 0;
    for (FluentButton* b : { save, m_apply, cancel })
        buttonWidth = std::max(buttonWidth, b->GetMinSize().x);
    for (FluentButton* b : { save, m_apply, cancel })
        b->SetMinSize(wxSize(buttonWidth, b->GetMinSize().y));
    footer->Add(m_status, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(12));
    footer->Add(save, 0, wxALIGN_CENTER_VERTICAL);
    footer->Add(m_apply, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(8));
    footer->Add(cancel, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(8));

    Bind(wxEVT_TIMER, [this](wxTimerEvent&) {
        m_savedShown = false;
        m_apply->SetText(T("Применить"));
    }, m_savedTimer.GetId());
    Bind(wxEVT_UPDATE_UI, [this](wxUpdateUIEvent& e) {
        const bool changed = m_canSave && HasChanges();
        if (changed && m_savedShown) // changed again: it is Apply again
        {
            m_savedShown = false;
            m_apply->SetText(T("Применить"));
        }
        e.Enable(changed);
    }, wxID_APPLY);
    m_apply->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        if (Apply() && m_restarted)
            return; // the new window closes this one once it is on the screen (main.cpp, --replace-pid)
        if (!m_restarted && !HasChanges())
        {
            m_savedShown = true;
            m_apply->SetText(T("Сохранено"));
            m_savedTimer.StartOnce(3000);
        }
    });
    save->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        // OK closes: no new window in a new language (Dmitry, 11.10.2026: "окно настроек не закрывается").
        if (!m_canSave || !HasChanges() || Apply(false))
            Close();
    });
    cancel->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Close(); });
    Bind(wxEVT_CHAR_HOOK, [this, save, cancel](wxKeyEvent& e) {
        if (m_gridKey && m_gridKey(e.GetKeyCode())) // the grid of colours is open: its keys, not the window's
            return;
        // Esc leaves the results (in the field it empties it first, TextField), otherwise closes the window.
        if (e.GetKeyCode() == WXK_ESCAPE && e.GetModifiers() == wxMOD_NONE)
        {
            if (!m_searching)
                return (void)Close();
            m_search->Clear();
            m_search->FocusText(false);
            return;
        }
        if (e.GetKeyCode() == 'F' && e.GetModifiers() == wxMOD_CONTROL)
            return m_search->FocusText(true);
        // Tab goes round the whole window, as in Windows' own Settings: the controls of the page shown (or of the search's
        // results), the buttons at the bottom, the search, the list of sections, the page again; Shift+Tab the other way.
        // Our own ring: Windows' navigation went
        // round the page only - the buttons and the list do not take the focus from the keyboard themselves (Dmitry,
        // 11.10.2026: "Tab ... в левую часть окна ... не переходит никогда").
        if (e.GetKeyCode() == WXK_TAB && !e.ControlDown() && !e.AltDown() && m_section >= 0 &&
            m_section < (int)m_pages.size())
        {
            std::vector<wxWindow*> ring;
            // A control that takes the focus and has none inside that would (a field, not the card it is on).
            std::function<bool(wxWindow*)> collect = [&](wxWindow* parent) {
                bool any = false;
                for (wxWindow* child : parent->GetChildren())
                {
                    if (!child->IsShownOnScreen() || !child->IsEnabled() || child->IsTopLevel())
                        continue;
                    if (collect(child))
                        any = true;
                    else if (child->AcceptsFocus())
                    {
                        ring.push_back(child);
                        any = true;
                    }
                }
                return any;
            };
            collect(m_searching ? m_searchPage : m_pages[m_section]);
            for (wxWindow* button : { (wxWindow*)save, (wxWindow*)m_apply, (wxWindow*)cancel })
                if (button->IsEnabled() && button->IsShownOnScreen())
                    ring.push_back(button);
            ring.push_back(m_search);
            ring.push_back(m_nav);
            int at = -1;
            for (wxWindow* w = wxWindow::FindFocus(); w && at < 0; w = w->GetParent())
                for (int i = 0; i < (int)ring.size(); i++)
                    if (ring[i] == w)
                        at = i;
            const int n = (int)ring.size();
            const int next = at < 0 ? (e.ShiftDown() ? n - 1 : 0) : (at + (e.ShiftDown() ? n - 1 : 1)) % n;
            if (ring[next] == m_search)
                m_search->FocusText(true);
            else
                ring[next]->SetFocus();
            return;
        }
        e.Skip();
    });
    // Back from another window: the engine may have been switched on or off from the tray meanwhile, and have learned
    // words (the note about one opens this window).
    Bind(wxEVT_ACTIVATE, [this](wxActivateEvent& e) {
        if (e.GetActive())
        {
            RefreshEngine();
            TakeLearnedNow();
        }
        else
            HotkeyEditor::CancelRecording(); // the keyboard hook must not outlive the window's turn
        e.Skip();
    });

    wxBoxSizer* right = new wxBoxSizer(wxVERTICAL);
    right->Add(m_title, 0, wxTOP | wxBOTTOM, FromDIP(12));
    right->Add(m_pagesSizer, 1, wxEXPAND);
    right->Add(footer, 0, wxEXPAND | wxTOP | wxBOTTOM | wxRIGHT, FromDIP(16));
    // The field as wide as the sections and level with the title.
    wxBoxSizer* left = new wxBoxSizer(wxVERTICAL);
    left->Add(m_search, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(4));
    left->Add(m_nav, 1, wxEXPAND | wxTOP, FromDIP(8));
    wxBoxSizer* sizer = new wxBoxSizer(wxHORIZONTAL);
    sizer->Add(left, 0, wxEXPAND | wxLEFT | wxTOP | wxBOTTOM, FromDIP(8));
    sizer->Add(right, 1, wxEXPAND | wxLEFT, FromDIP(20));
    SetSizer(sizer);

    if (!loadError.empty())
        SetStatus(T("Не удалось прочитать FluentSwitcher.json: ") + loadError, true);
    else if (m_canSave)
        if (const wxString dropped = DropCoveredForced(); !dropped.empty())
        {
            RefillWordLists();
            SetStatus(dropped, false); // Apply - to keep it
        }
    ShowSection(section >= 0 ? section : LastSection());
    // Not narrower than the widest page: the pages scroll only up and down, and a card cut at the right edge hid its
    // switch or button (Dmitry, 11.10.2026). The height - for the search, the eleven sections and "Закрыть FluentSwitcher"
    // under them.
    int widest = 0;
    for (wxWindow* page : m_pages)
        if (page->GetSizer())
            widest = std::max(widest, page->GetSizer()->CalcMin().x);
    const int client = m_nav->GetBestSize().x + FromDIP(8 + 20) + widest + wxSystemSettings::GetMetric(wxSYS_VSCROLL_X, this);
    const int tall = left->CalcMin().y + FromDIP(8 + 8);
    const wxSize least(std::max(FromDIP(720), ClientToWindowSize(wxSize(client, 1)).x),
                       std::max(FromDIP(560), ClientToWindowSize(wxSize(1, tall)).y));
    SetSize(wxSize(std::max(FromDIP(860), least.x), std::max(FromDIP(660), least.y)));
    SetMinSize(least);
    if (!RestorePlacement())
        CentreOnScreen();
    Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent& e) {
        SavePlacement();
        e.Skip();
    });
}

// The window's place and size (forum, 09.10.2026, AlexPORTrb: "чтобы окно настроек запоминало размер и положение"):
// "settings_window" in FluentSwitcher.json - its normal rectangle (GetWindowPlacement: workspace coordinates, screen
// points) and whether it was maximized, and the section it was on (Dmitry, 26.10.2026: as in FluentClipper, it opens
// where it was closed), "left,top,right,bottom,maximized,section" (until 1.5.19 without the section: the first one).
// Written on closing, this field alone, into the file as it is on the disk then: what the window did not save stays
// unsaved. The engine keeps it (Settings.h).
int SettingsFrame::LastSection() const
{
    wxStringTokenizer parts(m_saved.GetString("settings_window", wxString()), ",");
    long section = 0;
    for (int i = 0; i < 6; i++)
        if (!parts.HasMoreTokens() || !parts.GetNextToken().ToLong(&section))
            return 0;
    return (int)section; // ShowSection: one that is not there - the first
}

bool SettingsFrame::RestorePlacement()
{
    long v[5] = {};
    wxStringTokenizer parts(m_saved.GetString("settings_window", wxString()), ",");
    for (long& n : v)
        if (!parts.HasMoreTokens() || !parts.GetNextToken().ToLong(&n))
            return false;
    RECT rc{ v[0], v[1], v[2], v[3] };
    const wxSize min = GetMinSize();
    if (rc.right - rc.left < min.x || rc.bottom - rc.top < min.y || !MonitorFromRect(&rc, MONITOR_DEFAULTTONULL))
        return false; // smaller than it can be (another scale) or on a screen that is gone
    WINDOWPLACEMENT wp{ sizeof(wp) };
    if (!GetWindowPlacement(GetHWND(), &wp))
        return false;
    wp.rcNormalPosition = rc;
    wp.showCmd = SW_HIDE; // main.cpp shows it
    if (!SetWindowPlacement(GetHWND(), &wp))
        return false;
    if (v[4])
        Maximize();
    return true;
}

void SettingsFrame::SavePlacement()
{
    WINDOWPLACEMENT wp{ sizeof(wp) };
    if (!GetWindowPlacement(GetHWND(), &wp))
        return;
    const bool maximized = wp.showCmd == SW_SHOWMAXIMIZED ||
                           (wp.showCmd == SW_SHOWMINIMIZED && (wp.flags & WPF_RESTORETOMAXIMIZED));
    const RECT& r = wp.rcNormalPosition;
    const wxString value =
        wxString::Format("%ld,%ld,%ld,%ld,%d,%d", r.left, r.top, r.right, r.bottom, maximized ? 1 : 0, m_section);
    if (value == m_saved.GetString("settings_window", wxString()) || !wxFileExists(m_saved.Path()))
        return;
    ConfigLock lock;
    Config disk;
    wxString error;
    if (!disk.Load(m_saved.Path(), &error))
        return;
    disk.SetString("settings_window", value);
    disk.Save(&error);
}

// ---------------------------------------------------------------------------------------------
// Pages
// ---------------------------------------------------------------------------------------------

void SettingsFrame::Section(const char* icon, const wxString& title)
{
    m_page = NewSettingsPage(this, &m_column);
    m_pagesSizer->Add(m_page, 1, wxEXPAND);
    m_pages.push_back(m_page);
    KeepFocusInSight(m_page);
    m_titles.Add(title);
    m_nav->AddSection(icon, title);
}

void SettingsFrame::Group(const wxString& title)
{
    // Not a card: the search finds the cards only, and the heading stays out of its results. Space above it - after the
    // cards of the group before.
    if (!m_column->IsEmpty())
        m_column->AddSpacer(FromDIP(14));
    m_column->Add(FluentText(m_page, title, 10, g.text, true), 0, wxLEFT | wxBOTTOM, FromDIP(4));
}

wxWindow* SettingsFrame::Toggle(const wxString& title, const wxString& description, const char* key, bool def)
{
    ToggleSwitch* toggle = nullptr;
    AddSettingsCard(m_page, m_column, title, description, [&](wxWindow* card) {
        return toggle = new ToggleSwitch(card, m_edit.GetBool(key, def));
    });
    toggle->onChange = [this, toggle, key] {
        m_edit.SetBool(key, toggle->IsOn());
        Changed();
    };
    return toggle;
}

wxWindow* SettingsFrame::Choice(const wxString& title, const wxString& description, const wxArrayString& items,
                                int selection, std::function<void(int)> picked, bool below)
{
    FluentChoice* choice = nullptr;
    AddSettingsCard(m_page, m_column, title, description, [&](wxWindow* card) {
        return choice = new FluentChoice(card, items, selection);
    }, below);
    m_searchExtra[choice->GetParent()] = wxJoin(items, ' ', '\0');
    choice->onChange = [this, choice, picked] {
        picked(choice->GetSelection());
        Changed();
    };
    return choice;
}

wxWindow* SettingsFrame::Numbers(const wxString& title, const wxString& description, const char* key, int def,
                                 const std::vector<int>& numbers, const wxArrayString& names)
{
    const int now = m_edit.GetInt(key, def);
    int at = 0;
    for (size_t i = 0; i < numbers.size(); i++)
        if (std::abs(numbers[i] - now) < std::abs(numbers[at] - now))
            at = int(i);
    return Choice(title, description, names, at, [this, key, numbers](int i) { m_edit.SetInt(key, numbers[i]); });
}

wxWindow* SettingsFrame::FlagLook(const wxString& title, const wxString& description, const char* key, bool appIcon,
                                  const char* inherit, const wxString& follow)
{
    // The sets: the folders in "flags" next to the program - Flagpack (regular flags), Waving (the same with folds and a
    // shadow, as Punto's glossy ones; tools\make_flagpack_flags.py), or a folder of the user's own. Glossy, Fluent,
    // Round, Square - the sets before 1.5.0 (and SimpleSwitcher's), left by an older version unpacked over: the engine
    // takes Flagpack instead (Settings.h, NormalizeFlags), so they are not offered.
    wxArrayString values, names;
    wxDir dir(m_folder + "\\flags");
    if (dir.IsOpened())
    {
        wxString name;
        for (bool more = dir.GetFirst(&name, wxEmptyString, wxDIR_DIRS); more; more = dir.GetNext(&name))
            if (name != "Glossy" && name != "Fluent" && name != "Round" && name != "Square")
                values.Add(name);
    }
    if (values.Index("Flagpack") == wxNOT_FOUND)
        values.Add("Flagpack");
    values.Sort([](const wxString& a, const wxString& b) {
        const auto rank = [](const wxString& v) { return v == "Flagpack" ? 0 : v == "Waving" ? 1 : 2; };
        return rank(a) != rank(b) ? rank(a) - rank(b) : a.CmpNoCase(b);
    });
    for (const wxString& v : values)
        names.Add(v == "Flagpack" ? T("Обычные флаги") : v == "Waving" ? T("Флаги с переливом") : v);
    // Letters instead of a flag (the engine's LetterIcons.h); the tray may also show the app's icon.
    values.Add("Letters");
    names.Add(T("Буквы: EN, RU"));
    if (appIcon)
    {
        values.Add("Application Icon");
        names.Add(T("Значок приложения"));
    }
    // The flag on the mouse pointer: an empty look follows the flag at the cursor as long as it is empty (MouseFlag.h) -
    // its own item. A copy of the other look shown in its place went stale when that look changed, and picking it did
    // nothing (review 18.10.2026).
    if (!follow.empty())
    {
        values.Insert(wxString(), 0);
        names.Insert(follow, 0);
    }
    // As the engine reads the file (Settings.h, NormalizeFlags): the flag at the cursor without a look of its own - as
    // the tray icon (inherit; the engine soon writes it into the file); the framed letters (gone) - letters; a set that
    // is gone, and the tray hidden the old way ("Nothing") - the flags.
    wxString now = m_edit.GetString(key, wxString());
    if (!appIcon && now.empty() && follow.empty())
        now = m_edit.GetString(inherit, m_edit.GetString("flagsSet", "Flagpack"));
    if (now == "LettersFramed")
        now = "Letters";
    if (values.Index(now) == wxNOT_FOUND)
        now = "Flagpack";
    return Choice(title, description, names, values.Index(now),
                  [this, key, values](int i) { m_edit.SetString(key, values[i]); });
}

namespace
{
// The badge with a layout's letters as the flag at the cursor shows it (the engine's LetterIcons.h), in a colour of the list
// (BadgeColor.h). Drawn point for point: a bitmap given to wxStaticBitmap would be stretched on a screen with a bigger scale.
class BadgeSample : public wxPanel
{
public:
    BadgeSample(wxWindow* parent, const std::wstring& letters) : wxPanel(parent), m_letters(letters)
    {
        SetBackgroundColour(parent->GetBackgroundColour());
        SetMinSize(FromDIP(wxSize(kWidth, kWidth * 3 / 4)));
        Bind(wxEVT_PAINT, [this](wxPaintEvent&) {
            wxPaintDC dc(this);
            if (m_bitmap.IsOk())
                dc.DrawBitmap(m_bitmap, 0, (GetClientSize().y - m_bitmap.GetHeight()) / 2, true);
        });
    }
    // value: as in the file ("", "windows", "#rrggbb").
    void SetColour(const std::string& value)
    {
        const int w = FromDIP(kWidth);
        const LetterIcons::Picture pic = LetterIcons::Render(m_letters, w, (int)std::lround(w * 0.75),
                                                             LetterIcons::Style::Badge, true, false, BadgeColor::Resolve(value));
        m_bitmap = wxBitmap();
        if (!pic.rgba.empty())
        {
            wxImage image(pic.width, pic.height);
            image.InitAlpha();
            unsigned char* rgb = image.GetData();
            unsigned char* alpha = image.GetAlpha();
            for (int p = 0; p < pic.width * pic.height; p++)
            {
                rgb[p * 3] = pic.rgba[p * 4];
                rgb[p * 3 + 1] = pic.rgba[p * 4 + 1];
                rgb[p * 3 + 2] = pic.rgba[p * 4 + 2];
                alpha[p] = pic.rgba[p * 4 + 3];
            }
            m_bitmap = wxBitmap(image);
        }
        Refresh();
    }

private:
    static const int kWidth = 24; // DIPs, a bit bigger than the flag's usual 20
    std::wstring m_letters;
    wxBitmap m_bitmap;
};

// The letters of a layout ("0x...419" -> "RU"), as the engine names it (Utils::GetNameForHKL_simple, LetterIcons::Text).
std::wstring LayoutLetters(const wxString& hkl)
{
    unsigned long long value = 0;
    wchar_t locale[LOCALE_NAME_MAX_LENGTH] = {};
    if (!hkl.ToULongLong(&value, 16) || !LCIDToLocaleName(LOWORD(unsigned(value)), locale, LOCALE_NAME_MAX_LENGTH, 0))
        return L"?";
    return LetterIcons::Text(locale);
}
} // namespace

wxWindow* SettingsFrame::BadgeColors()
{
    // The forum's vote on https://dmitryn71.github.io/FluentSwitcher/badges/ (28.10.2026): most for "a colour of its own
    // for each language"; "as now" stays the first and the default (Maz). The list: the palette of the engine
    // (BadgeColor::kPalette), Windows' accent, and "Свой цвет…" - the grid of Windows' colours, as FluentClipper's accent.
    static const nlohmann::json kNoLayouts = nlohmann::json::array();
    const auto& file = std::as_const(m_edit).Json();
    const auto found = file.find("layouts_info");
    const nlohmann::json& layouts = found != file.end() && found->is_array() ? *found : kNoLayouts;
    const wxArrayString names = { T("Тёмная"), T("Серая"), T("Светлая"), T("Синяя"), T("Красная"), T("Жёлтая"),
                                  T("Зелёная"), T("Фиолетовая"), T("Цвет Windows"), T("Свой цвет…") };
    const int palette = (int)std::size(BadgeColor::kPalette), windows = palette, own = palette + 1;
    auto colour = [](uint32_t rgb) { return wxColour(rgb >> 16 & 0xff, rgb >> 8 & 0xff, rgb & 0xff); };

    wxPanel* rows = nullptr;
    AddSettingsCard(m_page, m_column, T("Цвет плашки с буквами"),
                    T("Когда вид флага – буквы: у каждой раскладки свой цвет. Буквы на плашке – белые или тёмные, какие "
                      "на этом цвете виднее. Буквы в трее – на плашке любого цвета, кроме тёмной"),
                    [&](wxWindow* card) {
        rows = new wxPanel(card);
        rows->SetBackgroundColour(card->GetBackgroundColour());
        wxFlexGridSizer* grid = new wxFlexGridSizer(3, card->FromDIP(8), card->FromDIP(12));
        grid->AddGrowableCol(1);
        for (size_t i = 0; i < layouts.size(); i++)
        {
            if (!layouts[i].is_object())
                continue;
            const wxString hkl = FromUtf8(layouts[i].value("layout", std::string()));
            const std::string value = layouts[i].value("badge", std::string());
            // What the file has: an item of the palette (any case of the letters), Windows' accent or a colour of one's
            // own; something else - the dark one, as the engine reads it.
            auto ownColour = std::make_shared<std::string>();
            int selection = 0;
            uint32_t rgb = 0;
            if (value == BadgeColor::kWindows)
                selection = windows;
            else if (BadgeColor::Parse(value, rgb))
            {
                selection = own;
                for (int k = 1; k < palette; k++)
                    if (BadgeColor::kPalette[k].rgb == rgb)
                        selection = k;
                if (selection == own)
                    *ownColour = wxString(value).Lower().ToStdString();
            }
            auto shown = std::make_shared<int>(selection); // the item before "Свой цвет…" was picked

            BadgeSample* sample = new BadgeSample(rows, LayoutLetters(hkl));
            sample->SetColour(value);
            FluentChoice* choice = new FluentChoice(rows, names, selection);
            auto swatches = [=] {
                std::vector<wxColour> dots;
                for (const BadgeColor::Entry& e : BadgeColor::kPalette)
                    dots.push_back(colour(e.rgb));
                dots.push_back(colour(BadgeColor::Resolve(BadgeColor::kWindows)));
                dots.push_back(ownColour->empty() ? wxColour() : colour(BadgeColor::Resolve(*ownColour)));
                choice->SetSwatches(dots);
            };
            swatches();
            auto save = [this, i, sample](const std::string& badge) {
                m_edit.Json()["layouts_info"][i]["badge"] = badge;
                sample->SetColour(badge);
                Changed();
            };
            // "Свой цвет…": the grid under the list. Closed with no colour picked - the list goes back to what it was.
            auto pick = [this, choice, ownColour, shown, swatches, save, own] {
                ColourGrid* colours = new ColourGrid(this, ownColour->empty() ? wxColour()
                                                                              : wxColour(wxString(*ownColour)));
                colours->onPick = [=](const wxColour& c) {
                    *ownColour = c.GetAsString(wxC2S_HTML_SYNTAX).Lower().ToStdString();
                    *shown = own;
                    swatches();
                    save(*ownColour);
                };
                colours->onClosed = [this, choice, shown] {
                    m_gridKey = nullptr; // the grid goes right after
                    choice->SetPopupKeys(nullptr);
                    choice->SetSelection(*shown);
                };
                // The focus stays where it was (a popup takes none): the keys go to the grid from there.
                m_gridKey = [colours](int key) { return colours->Key(key); };
                choice->SetPopupKeys(m_gridKey);
                // Under the list, its right edge at the list's right edge; on the screen.
                const wxSize size = colours->GetSize();
                wxPoint pos = choice->ClientToScreen(wxPoint(choice->GetSize().x - size.x, choice->GetSize().y + FromDIP(4)));
                const wxRect area = wxDisplay(choice).GetClientArea();
                pos.x = wxMax(area.x, wxMin(pos.x, area.GetRight() - size.x));
                if (pos.y + size.y > area.GetBottom()) // no room below: above
                    pos.y = choice->ClientToScreen(wxPoint(0, 0)).y - size.y - FromDIP(4);
                colours->OpenAt(pos);
            };
            choice->onChange = [this, choice, shown, save, pick, windows, own] {
                const int item = choice->GetSelection();
                if (item == own)
                    return CallAfter(pick); // once the list is gone
                *shown = item;
                save(item == windows ? std::string(BadgeColor::kWindows) : std::string(BadgeColor::kPalette[item].key));
            };
            choice->onReselect = [pick, own](int item) {
                if (item == own)
                    pick();
            };

            grid->Add(sample, 0, wxALIGN_CENTER_VERTICAL);
            grid->Add(FluentText(rows, LayoutName(hkl), 10, g.text), 0, wxALIGN_CENTER_VERTICAL);
            grid->Add(choice, 0, wxALIGN_CENTER_VERTICAL);
        }
        rows->SetSizer(grid);
        return rows;
    }, true);
    m_searchExtra[rows->GetParent()] = wxJoin(names, ' ', '\0');
    return rows;
}

void SettingsFrame::BuildGeneral()
{
    Section(kIconGeneral, T("Основные"));

    wxWindow* notRunning = nullptr;
    AddSettingsCard(m_page, m_column, T("FluentSwitcher не запущен"),
                    T("Настройки сохранятся и подействуют при запуске"), [&](wxWindow* card) {
                        notRunning = card;
                        FluentButton* start = new FluentButton(card, wxID_ANY, T("Запустить"), true);
                        start->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
                            if (!Engine::Start(m_folder))
                                return SetStatus(T("Не нашёл FluentSwitcher.exe в папке приложения"), true);
                            // It needs a moment to create its window.
                            for (int wait = 0; wait < 30 && !Engine::Find(m_folder, m_enginePid); wait++)
                                wxMilliSleep(100);
                            RefreshEngine();
                        });
                        return start;
                    });
    m_notRunningCard = notRunning;

    AddSettingsCard(m_page, m_column, T("FluentSwitcher включён"),
                    T("Выключенный не исправляет текст и не отвечает на сочетания, кроме «Включить / выключить»"),
                    [&](wxWindow* card) { return m_enabledSwitch = new ToggleSwitch(card, m_enabled); });
    m_enabledSwitch->onChange = [this] {
        m_enabled = m_enabledSwitch->IsOn();
        Changed();
    };
    AddSettingsCard(m_page, m_column, T("Запускать вместе с Windows"),
                    T("Приложение стартует при входе в Windows, видно только значок в трее"),
                    [&](wxWindow* card) { return m_autostartSwitch = new ToggleSwitch(card, m_autostart); });
    m_autostartSwitch->onChange = [this] {
        m_autostart = m_autostartSwitch->IsOn();
        Changed();
    };
    // Under a standard account "as administrator" is as another user, with that user's password each time and that
    // user's task (forum, WinnyS, 10.10.2026): it cannot be turned on there, only off if it is on (the engine takes it
    // as off anyway, Settings.h MonitorAdmin).
    const bool canElevate = CanElevateSelf();
    ToggleSwitch* admin = static_cast<ToggleSwitch*>(
        Toggle(T("Работать в приложениях, запущенных от имени администратора"),
               canElevate ? T("FluentSwitcher тогда работает с правами администратора: Windows спросит разрешения один "
                              "раз, дальше он запускается через планировщик заданий без вопросов")
                          : T("Нужна учётная запись администратора. Под обычной Windows запустила бы FluentSwitcher от "
                              "имени другого пользователя и спрашивала бы его пароль при каждом запуске"),
               "isMonitorAdmin", false));
    if (!canElevate)
        admin->onChange = [this, admin] {
            if (admin->IsOn())
            {
                admin->SetOn(false);
                return SetStatus(T("Под обычной учётной записью Windows этот режим не работает"), true);
            }
            m_edit.SetBool("isMonitorAdmin", false);
            Changed();
        };

    // Each language by its own name, in any language of the window; in the order of Language.
    const wxArrayString langValues = { "Russian", "English", "Ukrainian" };
    const wxArrayString langNames = { wxString::FromUTF8("Русский"), wxString("English"),
                                      wxString::FromUTF8("Українська") };
    Choice(T("Язык"), T("Этого окна и меню значка в трее. Окно откроется на новом языке после сохранения"), langNames,
           (int)CurrentLanguage(), [this, langValues](int i) { m_edit.SetString("gui_lang", langValues[i]); });

    // "" - as Windows; main.cpp reads it when the window starts.
    const wxArrayString themeValues = { wxString(), wxString("Light"), wxString("Dark") };
    const wxArrayString themeNames = { T("Как в Windows"), T("Светлая"), T("Тёмная") };
    const int theme = themeValues.Index(m_edit.GetString("ui_theme", wxString()));
    Choice(T("Тема"),
           T("Этого окна и меню значка в трее. «Как в Windows»: окно – как приложения, меню – как панель задач. "
             "Окно откроется в новой теме после сохранения"),
           themeNames,
           theme == wxNOT_FOUND ? 0 : theme, [this, themeValues](int i) { m_edit.SetString("ui_theme", themeValues[i]); });
    FinishPage();
}

// The details of a card as the tooltip of its title, where the "i" is (WithTip); `inCard` - anything on the card: its
// control or its description. Not over the whole card: it popped up whenever the mouse rested on the card on its way
// elsewhere, and lay over the cards below for half a minute (Дмитрий: the tooltip of "Переключать всегда" over the
// journal's card). The mouse leaves the title - the tooltip goes at once.
static void CardTip(wxWindow* inCard, const wxString& tip)
{
    wxWindow* card = inCard;
    while (card && !dynamic_cast<Card*>(card))
        card = card->GetParent();
    if (!card || card->GetChildren().empty())
        return;
    wxWindow* title = card->GetChildren().front(); // AddSettingsCard makes the title first
    title->SetToolTip(tip);
    title->Bind(wxEVT_LEAVE_WINDOW, [](wxMouseEvent& e) {
        wxToolTip::Enable(false); // hides the one shown
        wxToolTip::Enable(true);
        e.Skip();
    });
}

// A title that has details in its tooltip: with the "i" in a circle after it.
static wxString WithTip(const wxString& title)
{
    return title + wxString::FromUTF8(" \u24D8");
}

// The words of a list in the file; `defaults` while the file has none (the engine's own defaults, Settings.h).
static wxArrayString JsonWords(const nlohmann::json& file, const char* key, const wxArrayString& defaults)
{
    const auto it = file.find(key);
    if (it == file.end())
        return defaults;
    wxArrayString list;
    if (it->is_array())
        for (const auto& w : *it)
            if (w.is_string())
                list.Add(wxString::FromUTF8(w.get<std::string>()));
    return list;
}

// The words of a list that the engine added itself ("learned": { list: [...] }, Settings.h).
static wxArrayString LearnedWords(const nlohmann::json& file, const char* key)
{
    const auto all = file.find("learned");
    if (all == file.end() || !all->is_object())
        return wxArrayString();
    return JsonWords(*all, key, wxArrayString());
}

// The layouts that take part in the switch (layouts_info, as the engine saved them: 0xFFFFFFFFF0080419 is a 64-bit
// HKL as it is), for the other forms of the words; while the engine has not filled the list - the layouts of Windows.
static std::vector<HKL> SwitchLayouts(const Config& config)
{
    std::vector<HKL> layouts;
    const nlohmann::json& file = config.Json();
    const auto list = file.find("layouts_info");
    if (list != file.end() && list->is_array())
        for (const auto& layout : *list)
        {
            unsigned long long value = 0;
            if (layout.is_object() && layout.value("enabled", true) && layout.contains("layout") &&
                layout["layout"].is_string() && FromUtf8(layout["layout"].get<std::string>()).ToULongLong(&value, 16))
                layouts.push_back((HKL)(UINT_PTR)value);
        }
    if (layouts.size() < 2)
    {
        HKL all[32];
        const int n = GetKeyboardLayoutList(32, all);
        layouts.assign(all, all + wxMax(n, 0));
    }
    return layouts;
}

static wxString WordCountText(const wxString& about, WordKind kind, size_t n)
{
    return about + "\n" + WordListCount(kind, n);
}

void SettingsFrame::WordList(const char* key, WordKind kind, const wxString& title, const wxString& about,
                             const wxString& help, const wxString& tip, const wxArrayString& defaults)
{
    const nlohmann::json& now = std::as_const(m_edit).Json();
    wxStaticText* label = AddSettingsCard(
        m_page, m_column, WithTip(title), WordCountText(about, kind, JsonWords(now, key, defaults).size()),
        [this, key, kind, title, help, defaults](wxWindow* card) {
            FluentButton* edit = new FluentButton(card, wxID_ANY, T("Изменить…"));
            edit->Bind(wxEVT_BUTTON, [this, key, kind, title, help, defaults](wxCommandEvent&) {
                TakeLearnedNow();
                const Config base = m_saved; // the engine's lists when the window opens
                const nlohmann::json& file = std::as_const(m_edit).Json();
                WordListWords list{ JsonWords(file, key, defaults), LearnedWords(file, key) };
                const wxArrayString was = list.words;
                if (!EditWordList(this, kind, title, help, SwitchLayouts(m_edit), &list))
                    return;
                // The engine changed the lists while the window was open (learned a word, moved one between the lists),
                // and the window's copy would undo it: taken (the frame's activation on "Готово" has mostly taken it
                // already - wx/msw - not always), the window's list written, and then what the engine did since the window
                // opened - over it again, as Apply takes it (MergeEngineLearned: the window's own additions and removals
                // stay).
                TakeLearnedNow();
                nlohmann::json words = nlohmann::json::array(), learned = nlohmann::json::array();
                for (const wxString& w : list.words)
                    words.push_back(w.utf8_string());
                for (const wxString& w : list.learned)
                    learned.push_back(w.utf8_string());
                nlohmann::json& out = m_edit.Json();
                out[key] = words;
                // "learned" only while it has marks: the file stays as it was when the window changed nothing in it.
                if (!learned.empty())
                    out["learned"][key] = learned;
                else if (out.contains("learned") && out["learned"].is_object())
                {
                    out["learned"].erase(key);
                    if (out["learned"].empty())
                        out.erase("learned");
                }
                MergeEngineLearned(m_edit, base, std::as_const(m_saved).Json());
                const wxString left = LeaveOtherList(kind, was, list.words);
                Changed();
                RefillWordLists();
                if (!left.empty())
                    SetStatus(left, false);
            });
            return edit;
        });
    CardTip(label, tip);
    m_wordLists.push_back({ key, kind, defaults, about, label });
}

namespace
{
// The engine's "one word - one list" for the words of the lists (AutoSwitch::Covers): an exception stands in the way of an
// entry of "Переключать всегда" when it lets the entry through in none of the layouts it is typed in (OtherForms; and as
// it is - the same word in both lists). "ещё" and "to" can stay together: "ещё" in the other layout is "to`", and "ещ" is
// still switched to "to". The forms of each entry are made once (OtherForms asks the keyboard for every letter).
class Covering
{
public:
    explicit Covering(std::vector<HKL> layouts) : m_layouts(std::move(layouts)) {}

    bool operator()(const wxString& exception, const wxString& entry)
    {
        auto [it, fresh] = m_keys.try_emplace(entry);
        if (fresh)
        {
            it->second.push_back(entry.ToStdWstring());
            for (const wxString& form : OtherForms(entry, m_layouts))
                it->second.push_back(form.ToStdWstring());
        }
        const std::wstring x = exception.ToStdWstring(), f = entry.ToStdWstring();
        for (const std::wstring& keys : it->second)
            if (AutoSwitch::Covers(x, f, keys))
                return true;
        return false;
    }

private:
    std::vector<HKL> m_layouts;
    std::map<wxString, std::vector<std::wstring>> m_keys;
};

// «a», «b» - for the line at the bottom of the window.
wxString QuotedWords(const wxArrayString& words)
{
    wxString text;
    for (const wxString& w : words)
        text += (text.empty() ? wxString() : wxString(", ")) + wxString::Format(T("«%s»"), w);
    return text;
}
} // namespace

wxArrayString SettingsFrame::DefaultsOf(const char* key) const
{
    for (const WordListOnPage& list : m_wordLists)
        if (std::string_view(list.key) == key)
            return list.defaults;
    return wxArrayString();
}

void SettingsFrame::DropWords(const char* key, const wxArrayString& words)
{
    nlohmann::json& out = m_edit.Json();
    nlohmann::json kept = nlohmann::json::array();
    for (const wxString& w : JsonWords(out, key, DefaultsOf(key)))
        if (words.Index(w) == wxNOT_FOUND)
            kept.push_back(w.utf8_string());
    out[key] = kept;
    if (!out.contains("learned") || !out["learned"].is_object() || !out["learned"].contains(key) ||
        !out["learned"][key].is_array())
        return;
    nlohmann::json marks = nlohmann::json::array();
    for (const auto& m : out["learned"][key])
        if (!m.is_string() || words.Index(wxString::FromUTF8(m.get<std::string>())) == wxNOT_FOUND)
            marks.push_back(m);
    if (!marks.empty())
        out["learned"][key] = marks;
    else
    {
        out["learned"].erase(key);
        if (out["learned"].empty())
            out.erase("learned");
    }
}

// One word - one list (WinnyS on the forum, 26.10.2026: a word in both "Не переключать" and "Переключать всегда" - which
// wins? "Не переключать", and the other one was dead): a word just added to one of the two takes out of the other what
// stands in its way, as the engine decides it (Covering): "см" added to "Не переключать" takes "cv" out of "Переключать
// всегда", "cv" added there takes "см" out of "Не переключать". `was`, `now` - the list before and after its window (the
// window's own: a word the engine added meanwhile has done this by itself, gui2 WM_AutoSwitchLearn). Returns the line for
// the bottom of the window, or nothing.
wxString SettingsFrame::LeaveOtherList(WordKind kind, const wxArrayString& was, const wxArrayString& now)
{
    if (kind != WordKind::Never && kind != WordKind::Always)
        return wxString();
    const bool never = kind == WordKind::Never;
    const char* otherKey = never ? "autoswitch_force" : "autoswitch_exceptions";
    std::set<std::wstring> before;
    for (const wxString& w : was)
        before.insert(AutoSwitch::Lower(w.ToStdWstring()));
    wxArrayString added;
    for (const wxString& w : now)
        if (!before.contains(AutoSwitch::Lower(w.ToStdWstring())))
            added.Add(w);
    if (added.empty())
        return wxString();
    Covering covers(SwitchLayouts(m_edit));
    wxArrayString removed;
    for (const wxString& o : JsonWords(std::as_const(m_edit).Json(), otherKey, DefaultsOf(otherKey)))
        for (const wxString& w : added)
            if (never ? covers(w, o) : covers(o, w))
            {
                removed.Add(o);
                break;
            }
    if (removed.empty())
        return wxString();
    DropWords(otherKey, removed);
    return wxString::Format(T("Убрано из «%s»: %s"), never ? T("Переключать всегда") : T("Не переключать"),
                            QuotedWords(removed));
}

// One word - one list in the lists as they are (1.5.18 learned words into both; the file edited by hand; the engine
// learned an entry while this window held an exception not saved yet): an entry of "Переключать всегда" that a word of "Не
// переключать" covers was never switched - the exception wins (the engine's rule) - and goes, which changes nothing in the
// switch. Returns the line for the bottom of the window, or nothing.
wxString SettingsFrame::DropCoveredForced()
{
    const nlohmann::json& file = std::as_const(m_edit).Json();
    const wxArrayString never = JsonWords(file, "autoswitch_exceptions", DefaultsOf("autoswitch_exceptions"));
    if (never.empty())
        return wxString();
    Covering covers(SwitchLayouts(m_edit));
    wxArrayString dead;
    for (const wxString& f : JsonWords(file, "autoswitch_force", DefaultsOf("autoswitch_force")))
        for (const wxString& x : never)
            if (covers(x, f))
            {
                dead.Add(f);
                break;
            }
    if (dead.empty())
        return wxString();
    DropWords("autoswitch_force", dead);
    return wxString::Format(T("Убрано из «Переключать всегда» – эти слова есть в «Не переключать» и так не переключались: %s"),
                            QuotedWords(dead));
}

// A word put into "Не переключать" or "Переключать всегда" or taken out of them starts its count of switch-backs over
// (review of 1.5.19: two switch-backs from long ago, and one more by mistake put the word into "Не переключать" and took the
// user's own entry out of "Переключать всегда" with it). The count is kept by the word as typed: the word's own and those
// of its forms in the other layouts go. `saved` - the lists before. (The engine takes the counts from the file on the
// reload after Apply.)
void SettingsFrame::RestartMovedCounts(const Config& saved)
{
    nlohmann::json& out = m_edit.Json();
    const auto counts = out.find("autoswitch_undo");
    if (counts == out.end() || !counts->is_object() || counts->empty())
        return;
    const std::vector<HKL> layouts = SwitchLayouts(m_edit);
    for (const char* key : { "autoswitch_exceptions", "autoswitch_force" })
    {
        const wxArrayString defaults = DefaultsOf(key);
        const wxArrayString before = JsonWords(saved.Json(), key, defaults), after = JsonWords(out, key, defaults);
        auto restart = [&](const wxArrayString& from, const wxArrayString& to) {
            for (const wxString& w : from)
            {
                if (to.Index(w, false) != wxNOT_FOUND)
                    continue;
                wxArrayString forms = OtherForms(w, layouts);
                forms.Add(w);
                for (const wxString& form : forms)
                {
                    const std::wstring core = AutoSwitch::Lower(AutoSwitch::Letters(form.ToStdWstring()).core);
                    counts->erase(wxString(core).utf8_string());
                }
            }
        };
        restart(before, after);
        restart(after, before);
    }
}

// Forum, WinnyS, 11.10.2026: a note said a word went into "Переключать всегда", and the open window did not show it - it
// had read the lists before. Taken like Apply takes them (TakeEngineLearned), into m_saved too: Apply stays grey.
void SettingsFrame::TakeLearnedNow()
{
    if (!m_canSave || !wxFileExists(m_saved.Path()))
        return;
    const Config read = m_saved;
    TakeEngineLearned(m_saved, read);
    TakeEngineLearned(m_edit, read);
    RefillWordLists();
}

void SettingsFrame::RefillWordLists()
{
    const nlohmann::json& file = std::as_const(m_edit).Json();
    for (const WordListOnPage& list : m_wordLists)
        SetCardDescription(list.label, WordCountText(list.about, list.kind, JsonWords(file, list.key, list.defaults).size()));
}

void SettingsFrame::BuildAutoSwitch()
{
    Section(kIconAutoSwitch, T("Автопереключение"));

    // The automatic layout switch (the engine's AutoSwitch.h): autoswitch, and in the middle of a word, autoswitch_early;
    // the words never switched, autoswitch_exceptions, and always switched, autoswitch_force; the journal,
    // autoswitch_journal. Short texts on the cards, the details in their tooltips.
    CardTip(Toggle(WithTip(T("Автопереключение раскладки")),
                   T("Слово не в той раскладке исправляется само после пробела, Enter или Tab: ghbdtn – «привет»"),
                   "autoswitch", false),
            T("Переключает, когда набранного нет в словаре Windows своего языка, а те же клавиши в другой раскладке – "
              "слово. Короткие слова решает по соседям: f vj;yj – «а можно», ns ult – «ты где», а plan B и «5 шт» не "
              "трогает. Адрес и частые аббревиатуры, набранные не в той раскладке, исправляет: еюьу – t.me, ГЫИ – USB.\nНе трогает: слова с цифрами, "
              "другие аббревиатуры, адреса и почту, опечатки в английских словах, слово, "
              "перепечатанное после ручной смены раскладки сразу после исправления, пароли, "
              "консоль.\nИсправилось зря – сразу нажмите «Исправить "
              "последнее слово» (Shift дважды): слово вернётся, а на третий раз попадёт в «Не переключать»"));
    CardTip(Toggle(WithTip(T("Не ждать конца слова")), T("Переключать с четвёртой буквы: njkm станет «толь», ыщьу – some"),
                   "autoswitch_early", true),
            T("Переключает посреди слова, когда так не начинается ни одно слово своего языка, а те же клавиши в другой "
              "раскладке – начало слова. Начала слов – по спискам частых слов, встроенным в приложение (330 тысяч "
              "русских, 150 тысяч английских и 98 тысяч украинских форм), и по списку «Переключать всегда». В конце "
              "слова оно проверяется "
              "ещё раз по словарю.\nПереключилось зря – нажмите «Исправить последнее слово» (Shift дважды): слово вернётся, а на "
              "третий раз его начало попадёт в «Не переключать»"));
    WordList("autoswitch_exceptions", WordKind::Never, T("Не переключать"),
             T("Например, cv или см – в любой раскладке"),
             T("Слова, которые автопереключение не трогает. Одно слово – в обеих раскладках: cv закрывает и «см»"),
             T("Слово попадает сюда и само – после третьей отмены автопереключения, с отметкой «выучено»"));
    WordList("autoswitch_force", WordKind::Always, T("Переключать всегда"),
             T("Даже если словарь их не знает или это одна буква: the, a"),
             T("Слова, которые переключаются сразу, как набраны целиком, даже если словарь их не знает. Слева – что "
               "набрано, справа – что получится. Вводить можно любое из двух: приложение само поймёт, что из них "
               "слово; не так – ⇄ на строке меняет направление"),
             T("Пишите слово в том виде, какой нужен: the – и набранное «еру» станет the, a – и «ф» станет a. Слово в "
               "другом виде (еру) переключало бы правильно набранное. Слово попадает сюда и само – после третьего "
               "исправления вручную («Исправить последнее слово»), с отметкой «выучено»"),
             { wxString("the"), wxString("a") }); // as autoswitch_force in the engine's Settings.h
    // Apps (the engine's Settings.h): autoswitch_off - everything but the automatic switch works there (08.10.2026);
    // autoswitch_console - console apps where it works (ConsolePrograms.h; in "Дополнительно" before 08.10.2026).
    WordList("autoswitch_off", WordKind::Programs, T("Без автопереключения в приложениях"),
             T("Всё остальное там работает: исправление сочетанием, флаг у курсора, ДВе ЗАглавные кириллицей"),
             T("Приложения, где автопереключение не нужно: редакторы кода, приложения с командами. Исправление "
               "сочетанием и флаг у курсора там работают; латинские имена (ILogger) и i там не исправляются. Имя файла "
               "приложения (code.exe) или путь к нему"),
             T("Имя файла – как в Диспетчере задач на вкладке «Подробности». Путь – если нужно одно приложение из "
               "нескольких с тем же именем"));
    WordList("autoswitch_console", WordKind::Programs, T("Автопереключение в консоли"),
             T("Консольные приложения, где оно работает: far.exe. Пароль в консоли Windows от текста не отличает"),
             T("Консольные приложения, где автопереключение работает. В консоли его нет: там вводят команды и пароли, а "
               "пароль Windows от текста не отличает. Имя файла приложения (far.exe) или путь к нему"),
             T("В обычной консоли – и приложение, запущенное в ней: far.exe из cmd. В Windows Terminal и ConEmu "
               "приложение вкладки не узнать: добавьте WindowsTerminal.exe или ConEmu64.exe – и автопереключение будет "
               "во всех вкладках. Не вводите пароли там, где оно включено"));
    // The journal: on / off and "Открыть" (the file, in the folder of the debug log) on one card.
    {
        ToggleSwitch* journal = nullptr;
        wxStaticText* about = AddSettingsCard(
            m_page, m_column, WithTip(T("Журнал автопереключения")),
            T("Что переключилось само, что вернули и что исправили вручную"), [&](wxWindow* card) {
                // The card's colour: the button and the switch round their corners on the colour of their
                // parent, and the panel's own (the system's) showed as a dark frame around them.
                wxPanel* box = new wxPanel(card);
                box->SetBackgroundColour(card->GetBackgroundColour());
                wxBoxSizer* row = new wxBoxSizer(wxHORIZONTAL);
                FluentButton* open = new FluentButton(box, wxID_ANY, T("Открыть"));
                open->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
                    const wxString file = m_folder + "\\log\\autoswitch.log";
                    if (!wxFileExists(file))
                        return SetStatus(T("Журнала ещё нет: включите его и подождите первого переключения"), true);
                    OpenAsUser(file.ToStdWstring());
                });
                journal = new ToggleSwitch(box, m_edit.GetBool("autoswitch_journal", false));
                row->Add(open, 0, wxALIGN_CENTER_VERTICAL);
                row->Add(journal, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, box->FromDIP(12));
                box->SetSizer(row);
                return box;
            });
        journal->onChange = [this, journal] {
            m_edit.SetBool("autoswitch_journal", journal->IsOn());
            Changed();
        };
        CardTip(about, T("Файл autoswitch.log в папке log рядом с приложением: по нему видно, где автопереключение "
                         "ошибается и что пропускает. Пароли туда не попадают – в их полях оно не работает"));
    }
    // The report for the forum: the journal's errors in a window, to see, edit and copy (JournalReport, ReportDialog).
    AddSettingsCard(m_page, m_column, T("Отчёт об ошибках для форума"),
                    T("Что вы вернули и что исправили вручную – из журнала. Текст видно до отправки"),
                    [this](wxWindow* card) {
                        FluentButton* make = new FluentButton(card, wxID_ANY, T("Собрать…"));
                        make->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
                            int errors = 0;
                            const wxString report = JournalReport(m_folder, m_saved, &errors);
                            if (errors < 0)
                                return SetStatus(T("Журнала ещё нет: включите его выше и поработайте с автопереключением"),
                                                 true);
                            if (errors == 0)
                                return SetStatus(T("Ошибок в журнале нет: ничего не возвращали и не исправляли вручную"),
                                                 false);
                            ReportDialog(this, report);
                        });
                        return make;
                    });
    FinishPage();
}

void SettingsFrame::BuildTyping()
{
    Section(kIconTyping, T("Набор текста"));

    // "Где кончается слово" (separate_ext_mode) was not shown since 1.5.0-rc1 (Дмитрий 08.10.2026: nobody understands
    // it), "Считать буквами" (treat_as_letters, "_-") since 1.5.6 (Дмитрий 11.10.2026: without "-" the words with hyphens
    // fall apart); in 1.5.16 both left the engine too, with their one sensible values built in.

    // ДВе ЗАглавные (the engine's TwoCaps.h): two_caps, and the words to leave alone, two_caps_exceptions.
    CardTip(Toggle(WithTip(T("Исправлять ДВе ЗАглавные")), T("«ДВух» станет «Двух», «НЕт» – «Нет»: после пробела, знака препинания, Enter или Tab, слово через дефис – уже на дефисе"),
                   "two_caps", false),
            T("PCs, IDs, GHz, МГц, МВт, eM, iPhone и слова из исключений не трогаются; английские из трёх букв – только "
              "частые слова: THe, WAs; в редакторах кода латинские имена (ILogger) тоже. Исправилось зря – сразу нажмите «Исправить последнее слово» (Shift дважды): слово вернётся. "
              "Перевод раскладки тоже исправляет ДВе ЗАглавные: LDe[ – Двух"));
    // The exceptions: in a window of their own, one per line; the card says how many.
    WordList("two_caps_exceptions", WordKind::Caps, T("Исключения для ДВух ЗАглавных"),
             T("Слова, которые так и пишутся: VMware, IPsec"),
             T("Слова, которые так и пишутся: VMware, IPsec. Слово от четырёх букв закрывает и те, что с него "
               "начинаются: IPsec – и IPsecs. Регистр букв важен"),
             T("Слово закрывает и те, что с него начинаются: ИПшник – и ИПшники. Само слово попадает сюда после "
               "третьей отмены, с отметкой «выучено»"));
    // The English i alone - I (the engine's TwoCaps::LoneI): fix_lone_i.
    CardTip(Toggle(WithTip(T("Исправлять i на I")), T("Английское «i» отдельным словом станет «I»: i am – I am, i'm – I'm"),
                   "fix_lone_i", true),
            T("Только в английской раскладке и не в консоли или редакторе кода (VS Code, Visual Studio, JetBrains, "
              "Notepad++): там i – переменная. Исправилось зря – сразу нажмите «Исправить последнее слово» (Shift дважды): "
              "вернётся «i», а на третий раз оно попадёт в исключения ДВух ЗАглавных"));
    // CapsLock off (the engine's HookerKeyboard.cpp, capslock_off; Dmitry, 11.10.2026: without the registry).
    CardTip(Toggle(WithTip(T("Выключить CapsLock")),
                   T("Нажатие CapsLock не включает ПРОПИСНЫЕ – никаких «пРИВЕТ» от случайного нажатия. Сочетания с ним "
                     "работают: Shift + CapsLock, Ctrl + CapsLock"),
                   "capslock_off", false),
            T("ПРОПИСНЫЕ можно включить своим сочетанием «Нажать CapsLock» (раздел «Сочетания клавиш», например "
              "Alt + CapsLock). Пока FluentSwitcher выключен и в приложениях из «Не работать в приложениях», CapsLock "
              "работает как обычно"));
    FinishPage();
}

void SettingsFrame::BuildHotkeys()
{
    Section(kIconHotkeys, T("Сочетания клавиш"));

    // Recording keeps left and right Ctrl, Shift, Alt, Win apart only when asked: "Ctrl" fits either. A saved
    // setting (record_sides), so that it stays on; the hotkeys recorded before stay as they are.
    ToggleSwitch* sides = static_cast<ToggleSwitch*>(
        Toggle(T("Различать левые и правые Ctrl, Shift, Alt, Win"),
               T("При записи сочетания: включите и запишите сочетание заново – например, только левый Shift. "
                 "Выключено – годится любой"),
               "record_sides", false));
    std::vector<HotkeyEditor*> editors;
    for (const HotkeyAction& action : HotkeyActions())
    {
        HotkeyEditor* editor = nullptr;
        const char* key = action.key;
        wxStaticText* about = AddSettingsCard(m_page, m_column, T(action.title), T(action.description), [&](wxWindow* card) {
            return editor = new HotkeyEditor(card, m_edit.GetHotkeys(key), 2, sides->IsOn());
        }, true);
        m_doubleWaits.push_back({ about, T(action.description), [editor] { return editor->Value(); } });
        editor->onChange = [this, editor, key] {
            m_edit.SetHotkeys(key, editor->Value());
            RefreshDoubleWaits();
            Changed();
        };
        editor->sameAs = [this, key](const wxString& one) { return SameHotkey(one, key); };
        editors.push_back(editor);
        // The table of the transliteration (translit_table, the engine's Translit.h) - right under its action.
        if (strcmp(key, "hk_TranslitSelected") == 0)
        {
            const wxArrayString tableValues = { wxString(), wxString("wikipedia"), wxString("mosmetro"),
                                                wxString("passport") };
            const int table = tableValues.Index(m_edit.GetString("translit_table", wxString()));
            Choice(T("Таблица транслита"),
                   T("Для русского: обратимая возвращается в кириллицу без потерь; Википедия и метро – читаемо и без "
                     "апострофов; загранпаспорт – как пишет МВД. Украинский – всегда по официальной таблице"),
                   { T("Обратимая: Ob''yavlenie"), T("Как в Википедии: Obyavleniye"),
                     T("Как в московском метро: Obyavlenie"), T("Как в загранпаспорте: Obieiavlenie") },
                   table == wxNOT_FOUND ? 0 : table,
                   [this, tableValues](int i) { m_edit.SetString("translit_table", tableValues[i]); });
        }
    }
    sides->onChange = [this, sides, editors, save = sides->onChange] {
        save(); // record_sides, and Apply lights up
        for (HotkeyEditor* editor : editors)
            editor->SetSides(sides->IsOn());
        if (sides->IsOn())
            SetStatus(T("Запишите нужное сочетание заново: теперь левые и правые клавиши различаются"), false);
    };

    // How keys are pressed, below the hotkeys: the double press (the hotkeys "дважды"; the recording here counts by it
    // too) and the shortcuts of Windows that many presses of Shift open.
    NumberField* quick = nullptr;
    AddSettingsCard(m_page, m_column, T("Интервал двойного нажатия, мс"),
                    T("Два нажатия быстрее этого считаются двойным – для сочетаний «дважды». Обычно 250–350"),
                    [&](wxWindow* card) { return quick = new NumberField(card, m_edit.GetInt("quick_press_ms", 280)); });
    quick->onChange = [this, quick] {
        const int value = quick->Value();
        if (value > 0 && value <= 1000)
        {
            m_edit.SetInt("quick_press_ms", value);
            HotkeyEditor::s_doubleMs = (unsigned long)value;
            RefreshDoubleWaits();
            Changed();
        }
    };
    Toggle(T("Отключить залипание клавиш"),
           T("Пять нажатий Shift и другие сочетания специальных возможностей Windows не будут открывать их окна"),
           "disableAccessebility", false);
    FinishPage();
}

wxString SettingsFrame::SameHotkey(const wxString& one, const char* except) const
{
    for (const HotkeyAction& action : HotkeyActions())
    {
        if (strcmp(action.key, except) == 0)
            continue;
        for (const wxString& other : SplitHotkeys(m_edit.GetHotkeys(action.key)))
            if (other.IsSameAs(one, false))
                return T("Так же назначено: «") + T(action.title) + T("»");
    }
    return wxString();
}

namespace
{
// One key of a hotkey as the engine compares them (CHotKey::Compare, CompareKeys): "Shift" is either Shift, "LShift"
// only the left one.
bool SameKey(const wxString& a, const wxString& b)
{
    if (a == b)
        return true;
    for (const char* mod : { "Ctrl", "Shift", "Alt", "Win" })
    {
        const wxString left = wxString("L") + mod, right = wxString("R") + mod;
        if ((a == mod && (b == left || b == right)) || (b == mod && (a == left || a == right)))
            return true;
    }
    return false;
}

// The name of a command (run_programs) as its card shows it.
wxString CommandTitle(const nlohmann::json& command)
{
    const bool snippet = command.value("type", 0) == 1;
    const wxString cmd = FromUtf8(command.value("cmd", std::string()));
    wxString title = snippet ? T("Вставить текст") : T("Запустить приложение");
    if (!cmd.empty())
        title += ": " + (snippet ? cmd.Left(40) : wxFileName(cmd).GetFullName());
    return title;
}

// Two hotkeys on the same keys, in any order, "#up" and "#double" aside.
bool SameKeys(wxString a, wxString b)
{
    auto keys = [](wxString one) {
        one.Replace(" #up", "");
        one.Replace(" #double", "");
        std::vector<wxString> list;
        for (wxString part : wxSplit(one, '+'))
            if (!part.Trim(true).Trim(false).empty())
                list.push_back(part);
        return list;
    };
    std::vector<wxString> x = keys(a), y = keys(b);
    if (x.empty() || x.size() != y.size())
        return false;
    for (const wxString& k : x)
    {
        const auto it = std::find_if(y.begin(), y.end(), [&](const wxString& m) { return SameKey(k, m); });
        if (it == y.end())
            return false;
        y.erase(it);
    }
    return true;
}
}

wxString SettingsFrame::DoubleWait(const wxString& stored) const
{
    // The "twice" hotkeys the engine waits for - of the actions, the layouts and the commands (its All_hot_keys), with
    // their names, as the window has them now.
    std::vector<std::pair<wxString, wxString>> doubles; // hotkey, name
    auto add = [&doubles](const wxString& hotkeys, const wxString& name) {
        for (const wxString& one : SplitHotkeys(hotkeys))
            if (one.Contains("#double"))
                doubles.push_back({ one, name });
    };
    for (const HotkeyAction& action : HotkeyActions())
        add(m_edit.GetHotkeys(action.key), T(action.title));
    const nlohmann::json& file = std::as_const(m_edit).Json();
    if (const auto it = file.find("layouts_info"); it != file.end() && it->is_array())
        for (const auto& layout : *it)
            if (layout.is_object())
                add(FromUtf8(layout.value("hotkey", std::string())), LayoutName(FromUtf8(layout.value("layout", std::string()))));
    if (const auto it = file.find("run_programs"); it != file.end() && it->is_array())
        for (const auto& command : *it)
            if (command.is_object())
                add(FromUtf8(command.value("hotkey", std::string())), CommandTitle(command));
    // Which of them the single hotkeys of the card meet: each "twice" with its own names (two actions on LShift twice and
    // RShift twice are two keys, not one).
    std::vector<std::pair<wxString, wxArrayString>> found;
    size_t names = 0;
    for (const wxString& one : SplitHotkeys(stored))
    {
        if (one.Contains("#double"))
            continue;
        for (const auto& [other, name] : doubles)
        {
            if (!SameKeys(one, other))
                continue;
            const wxString twice = HotkeyDisplay(other);
            auto it = std::find_if(found.begin(), found.end(), [&twice](const auto& f) { return f.first == twice; });
            if (it == found.end())
                it = found.insert(found.end(), { twice, wxArrayString() });
            if (it->second.Index(name) == wxNOT_FOUND)
            {
                it->second.Add(name);
                names++;
            }
        }
    }
    if (found.empty())
        return wxString();
    wxString parts;
    for (const auto& [twice, list] : found)
    {
        wxString joined;
        for (const wxString& name : list)
            joined += (joined.empty() ? wxString() : T("», «")) + name;
        parts += (parts.empty() ? wxString() : wxString("; ")) + wxString::Format(T("«%s» есть у «%s»"), twice, joined);
    }
    const int ms = m_edit.GetInt("quick_press_ms", 280);
    return names == 1
        ? wxString::Format(T("Срабатывает на %d мс позже: ждёт, не нажмут ли второй раз, – %s. Смените то сочетание, и "
                             "задержки не будет"), ms, parts)
        : wxString::Format(T("Срабатывает на %d мс позже: ждёт, не нажмут ли второй раз, – %s. Смените те сочетания, и "
                             "задержки не будет"), ms, parts);
}

void SettingsFrame::RefreshDoubleWaits()
{
    for (DoubleWaitCard& card : m_doubleWaits)
    {
        const wxString note = DoubleWait(card.hotkeys());
        if (note == card.shown)
            continue;
        card.shown = note;
        const wxString text = card.about.empty() || note.empty() ? card.about + note : card.about + "\n" + note;
        // These cards have their control below (AddSettingsCard(..., true)): their text is wrapped wider, as the kit does
        // it for them (kDescriptionBelow in fluent_controls.cpp); SetCardDescription would wrap it at the narrow width.
        SetWrappedLabel(card.label, text, card.label->FromDIP(480));
        card.label->Show(!text.empty());
        card.label->GetParent()->Layout();
        // The card grows or shrinks: its page lays out again (it may be another page than the one being built).
        for (wxWindow* w = card.label; w; w = w->GetParent())
            if (wxScrolledWindow* page = wxDynamicCast(w, wxScrolledWindow))
            {
                page->Layout();
                page->FitInside();
                break;
            }
    }
}

void SettingsFrame::BuildLayouts()
{
    Section(kIconLayouts, T("Раскладки"));

    // The layouts of Windows first; how FluentSwitcher switches between them below. Read without operator[]: on a config
    // without the field it put "layouts_info": null into it, saved by the next Apply - and the engine stopped with
    // "Bad config" at every start (review 11.10.2026).
    static const nlohmann::json kNoLayouts = nlohmann::json::array();
    const auto& file = std::as_const(m_edit).Json();
    const auto found = file.find("layouts_info");
    const nlohmann::json& layouts = found != file.end() ? *found : kNoLayouts;
    if (!layouts.is_array() || layouts.empty())
        AddSettingsCard(m_page, m_column, T("Раскладок пока нет"),
                        T("FluentSwitcher заполнит список раскладками Windows при запуске"),
                        [](wxWindow*) { return nullptr; });
    // The switch "takes part in switching and fixing" - only with three layouts or more (Dmitry, 11.10.2026: with two,
    // turning one off left nothing to fix into), or when one is off already (then it must be seen to be turned on). The
    // text about the cards - once, above them, not under each.
    bool anyOff = false;
    for (size_t i = 0; layouts.is_array() && i < layouts.size(); i++)
        anyOff = anyOff || (layouts[i].is_object() && !layouts[i].value("enabled", true));
    const bool switches = layouts.is_array() && (layouts.size() >= 3 || anyOff);
    if (layouts.is_array() && !layouts.empty())
    {
        wxString about = T("Своё сочетание раскладки включает сразу её: например, левый Ctrl – английскую, правый – русскую");
        if (switches)
            about += ". " + T("Переключатель – участвует ли раскладка в переключении и исправлении: выключите ту, что "
                              "нужна редко");
        wxStaticText* intro = FluentText(m_page, about, 9, g.text2);
        intro->Wrap(FromDIP(560));
        m_column->Add(intro, 0, wxLEFT | wxBOTTOM, FromDIP(4));
    }
    for (size_t i = 0; layouts.is_array() && i < layouts.size(); i++)
    {
        const auto& layout = layouts[i];
        const wxString hkl = layout.contains("layout") ? FromUtf8(layout["layout"].get<std::string>()) : wxString();
        const wxString own = layout.contains("hotkey") ? FromUtf8(layout["hotkey"].get<std::string>()) : wxString();
        ToggleSwitch* toggle = nullptr;
        HotkeyEditor* editor = nullptr;
        // The layout's own hotkeys; the switch on the right of them. No text under the name (it is above the list): its
        // empty line is hidden.
        wxStaticText* noText = AddSettingsCard(m_page, m_column, LayoutName(hkl), wxString(),
                        [&](wxWindow* card) {
                            wxPanel* panel = new wxPanel(card);
                            panel->SetBackgroundColour(card->GetBackgroundColour());
                            editor = new HotkeyEditor(panel, own, 2, true);
                            wxBoxSizer* row = new wxBoxSizer(wxHORIZONTAL);
                            row->Add(editor, 0, wxALIGN_TOP);
                            if (switches)
                            {
                                toggle = new ToggleSwitch(panel, layout.value("enabled", true));
                                row->AddStretchSpacer();
                                row->Add(toggle, 0, wxALIGN_TOP | wxTOP, card->FromDIP(4));
                            }
                            panel->SetSizer(row);
                            return panel;
                        }, true);
        noText->Hide();
        m_doubleWaits.push_back({ noText, wxString(), [editor] { return editor->Value(); } });
        if (toggle)
            toggle->onChange = [this, toggle, i] {
                // At least two stay on: one alone, there is nothing to switch to and nothing to fix into.
                auto& all = m_edit.Json()["layouts_info"];
                int on = 0;
                for (size_t k = 0; all.is_array() && k < all.size(); k++)
                    on += k == i ? toggle->IsOn() : (all[k].is_object() && all[k].value("enabled", true));
                if (!toggle->IsOn() && on < 2)
                {
                    toggle->SetOn(true);
                    SetStatus(T("Включёнными должны остаться хотя бы две раскладки: иначе исправлять не на что"), true);
                    return;
                }
                all[i]["enabled"] = toggle->IsOn();
                Changed();
            };
        editor->onChange = [this, editor, i] {
            m_edit.Json()["layouts_info"][i]["hotkey"] = ToUtf8(editor->Value());
            RefreshDoubleWaits();
            Changed();
        };
    }
    // The hotkeys of both pages are there now: which of them wait for a second press (DoubleWait).
    RefreshDoubleWaits();

    const bool alternative = m_edit.GetBool("AlternativeLayoutChange", false);
    Choice(T("Как переключать раскладку"),
           T("Если в каком-то приложении раскладка после исправления не переключается, выберите второй способ: "
             "FluentSwitcher нажмёт то сочетание, которым раскладка переключается в Windows"),
           { T("Обычный"), T("Сочетанием Windows") }, alternative ? 1 : 0,
           [this](int i) { m_edit.SetBool("AlternativeLayoutChange", i == 1); });

    HotkeyEditor* windows = nullptr;
    AddSettingsCard(m_page, m_column, T("Сочетание, которым раскладка переключается в Windows"),
                    T("FluentSwitcher нажимает его сам при втором способе. Обычно Alt + Shift или Win + Пробел"),
                    [&](wxWindow* card) {
                        return windows = new HotkeyEditor(card, m_edit.GetString("win_hotkey_cycle_lang", "LAlt + Shift"),
                                                          1, true, true);
                    }, true);
    const wxString notWindows = T("Windows этим сочетанием раскладку не переключает");
    if (!WindowsSwitches(windows->Value()))
        windows->SetNote(notWindows, false);
    windows->onChange = [this, windows, notWindows] {
        m_edit.SetString("win_hotkey_cycle_lang", windows->Value());
        Changed();
        if (WindowsSwitches(windows->Value()))
            return;
        windows->SetNote(notWindows, false);
        SetStatus(T("Windows переключает раскладку только Alt + Shift, Ctrl + Shift, Win + Пробел или Ё. Свою клавишу, "
                    "например правый Ctrl, задайте в разделе «Сочетания клавиш» – «Следующая раскладка»"),
                  true);
    };

    // The flag of English in the tray and at the text cursor (the engine's IconManager.h): the US or the British one.
    Choice(T("Флаг английской раскладки"), T("В трее и у текстового курсора"), { T("Американский"), T("Британский") },
           m_edit.GetBool("useBritishFlag", false) ? 1 : 0, [this](int i) { m_edit.SetBool("useBritishFlag", i == 1); });
    FinishPage();
}

void SettingsFrame::BuildTray()
{
    // The icon in the notification area, by the clock (the engine's TrayIcon.h): whether it shows, which, its clicks.
    // Apart from the flag at the text cursor since 07.10.2026 (forum: hiding the tray icon hid that flag too).
    Section(kIconTray, T("Значок в трее"));
    ToggleSwitch* shown = nullptr;
    AddSettingsCard(m_page, m_column, T("Показывать значок в трее"),
                    T("В трее (области уведомлений Windows): раскладка, меню и уведомления FluentSwitcher. Без значка "
                      "настройки открывает сочетание «Открыть настройки»"),
                    [&](wxWindow* card) {
                        return shown = new ToggleSwitch(card, m_edit.GetBool("tray_icon", true) &&
                                                                  m_edit.GetString("flagsSet", "") != "Nothing");
                    });
    shown->onChange = [this, shown] {
        m_edit.SetBool("tray_icon", shown->IsOn());
        if (m_edit.GetString("flagsSet", "") == "Nothing") // hidden the old way (before 1.5.0): the flags again
            m_edit.SetString("flagsSet", "Flagpack");
        Changed();
    };
    FlagLook(T("Вид значка"), T("Флаг раскладки, её буквы или значок FluentSwitcher"), "flagsSet", true);

    // Clicks on the icon (tray_click, tray_double_click).
    const wxArrayString clickValues = { wxString(), wxString("menu"), wxString("next_layout"), wxString("toggle"),
                                        wxString("settings") };
    const wxArrayString clickNames = { T("Ничего"), T("Меню"), T("Следующая раскладка"), T("Включить / выключить"),
                                       T("Открыть настройки") };
    auto clickIndex = [this, &clickValues](const char* key, const char* byDefault) {
        const int i = clickValues.Index(m_edit.GetString(key, byDefault));
        return i == wxNOT_FOUND ? clickValues.Index(byDefault) : i;
    };
    Choice(T("Клик по значку"),
           T("«Следующая раскладка» – у окна, где вы печатали, и курсор остаётся там. Если назначен и двойной "
             "клик, одиночный срабатывает чуть позже: ждёт, не будет ли второго"),
           clickNames, clickIndex("tray_click", ""),
           [this, clickValues](int i) { m_edit.SetString("tray_click", clickValues[i]); });
    Choice(T("Двойной клик по значку"), T("Правый клик всегда открывает меню"), clickNames,
           clickIndex("tray_double_click", "settings"),
           [this, clickValues](int i) { m_edit.SetString("tray_double_click", clickValues[i]); });
    FinishPage();
}

void SettingsFrame::BuildCaretFlag()
{
    // The flag at the text cursor (the engine's CaretFlag.h), with its own look.
    Section(kIconFlags, T("Флаг у курсора"));
    // Three groups (gutasiho on the forum, 29.10.2026): the page is full, and the rows of both flags among the cursor's
    // ones looked like a heading for the rows under them.
    Group(T("У текстового курсора"));
    FluentChoice* caret = static_cast<FluentChoice*>(
        Numbers(T("Флаг у текстового курсора"), T("Показывает раскладку там, где вы печатаете"), "caret_flag", 1,
                { 1, 2, 0 }, { T("Всегда"), T("Ненадолго"), T("Не показывать") }));
    FluentChoice* caretLook = static_cast<FluentChoice*>(
        FlagLook(T("Вид флага"), T("Флаг раскладки или её буквы на плашке"), "caret_flag_set", false));
    const std::vector<wxWindow*> caretRows = {
        caretLook,
        Numbers(T("Где показывать"), T("Если у края экрана места нет – с другой стороны строки"), "caret_flag_place", 0,
                { 0, 1 }, { T("Под курсором"), T("Над курсором") }),
        Numbers(T("Размер"), T("При масштабе 100 %; на экранах с большим масштабом он крупнее"), "caret_flag_size", 20,
                { 16, 20, 24, 32 }, { T("Маленький"), T("Обычный"), T("Крупный"), T("Очень крупный") }),
        Numbers(T("Прозрачность"), T("Чтобы не отвлекал от текста"), "caret_flag_opacity", 60,
                { 100, 80, 60, 40, 25, 15 }, { T("Нет"), T("Слабая"), T("Средняя"), T("Сильная"), T("Очень сильная"),
                T("Максимальная") }),
    };
    // The flag on the mouse pointer over text (the engine's MouseFlag.h; Dmitry, 16.10.2026).
    Group(T("На указателе мыши"));
    FluentChoice* mouse = static_cast<FluentChoice*>(
        Numbers(T("Флаг на указателе мыши"),
                T("Над текстом указатель мыши показывает флаг раскладки: всё время или ненадолго – после смены "
                  "раскладки, окна или поля ввода"),
                "mouse_flag", 0, { 1, 2, 0 }, { T("Всегда"), T("Ненадолго"), T("Не показывать") }));
    FluentChoice* mouseLook = static_cast<FluentChoice*>(
        FlagLook(T("Вид флага на указателе"), T("Флаг раскладки или её буквы на плашке"), "mouse_flag_set",
                 false, "caret_flag_set", T("Как у текстового курсора")));
    const std::vector<wxWindow*> mouseRows = {
        mouseLook,
        Numbers(T("Прозрачность флага на указателе"), T("Чтобы не отвлекал от текста"), "mouse_flag_opacity", 100,
                { 100, 80, 60, 40, 25, 15 }, { T("Нет"), T("Слабая"), T("Средняя"), T("Сильная"), T("Очень сильная"),
                T("Максимальная") }),
    };
    // Both flags: "for a moment" (the pointer's takes that time too); the colour of the badge for each layout and
    // CapsLock on the flag (the forum's vote, 28.10.2026).
    Group(T("Для обоих флагов"));
    wxWindow* brief = Numbers(T("Сколько показывать «ненадолго»"), T("После смены раскладки, окна или поля ввода"),
                              "caret_flag_brief_ms", 2000, { 1000, 2000, 3000, 5000, 10000 },
                              { T("1 секунду"), T("2 секунды"), T("3 секунды"), T("5 секунд"), T("10 секунд") });
    wxWindow* badges = BadgeColors();
    wxWindow* caps = Toggle(T("CapsLock на флаге"),
                            T("Пока включён CapsLock, сверху на флаге – оранжевая полоса: у текстового курсора и на "
                              "указателе мыши. Нажали CapsLock – флаг показывается и в режиме «ненадолго»"),
                            "flag_capslock", false);
    // Pale while they mean nothing, as Windows 11 shows them (Dmitry, 16.10.2026: "если флаг не показываем, то и
    // настройки прозрачности и типа флагов ... должны быть grayed out"): a flag's own rows while it is not shown; "for a
    // moment" while neither flag shows for a moment - the pointer's takes that time too; CapsLock while neither flag is
    // shown; the badges' colours while no flag shown is letters (as FlagLook and the engine read the looks), nor the tray
    // icon - it takes a chosen colour too (its look is on another page: as it was when this one was built or changed).
    const auto pale = [this, brief, caretRows, mouseRows, badges, caps] {
        const int caretMode = m_edit.GetInt("caret_flag", 1), mouseMode = m_edit.GetInt("mouse_flag", 0);
        const auto letters = [](const wxString& look) { return look == "Letters" || look == "LettersFramed"; };
        const wxString caretSet = m_edit.GetString("caret_flag_set", m_edit.GetString("flagsSet", "Flagpack"));
        const wxString mouseSet = m_edit.GetString("mouse_flag_set", wxString());
        const bool trayLetters = m_edit.GetBool("tray_icon", true) && letters(m_edit.GetString("flagsSet", "Flagpack"));
        for (wxWindow* row : caretRows)
            EnableSettingsCard(row->GetParent(), caretMode != 0);
        for (wxWindow* row : mouseRows)
            EnableSettingsCard(row->GetParent(), mouseMode != 0);
        EnableSettingsCard(brief->GetParent(), caretMode == 2 || mouseMode == 2);
        EnableSettingsCard(caps->GetParent(), caretMode != 0 || mouseMode != 0);
        EnableSettingsCard(badges->GetParent(), trayLetters || (caretMode != 0 && letters(caretSet)) ||
                                                    (mouseMode != 0 && letters(mouseSet.empty() ? caretSet : mouseSet)));
    };
    for (FluentChoice* choice : { caret, mouse, caretLook, mouseLook })
        choice->onChange = [pale, save = choice->onChange] {
            save(); // the setting, and Apply lights up
            pale();
        };
    pale();
    FinishPage();
}

void SettingsFrame::BuildSounds()
{
    // The engine's LayoutSound.h: sound_switch, sound_fix - per cent, 0 - none.
    Section(kIconSounds, T("Звуки"));
    Numbers(T("Звук при переключении раскладки"),
            T("Сочетанием FluentSwitcher или Windows, кликом по значку в трее. Звук – switch.wav в папке sounds рядом "
              "с приложением; положите туда en.wav, ru.wav – и у каждого языка будет свой"),
            "sound_switch", 0, { 0, 30, 60, 100 }, { T("Нет"), T("Тихий"), T("Средний"), T("Громкий") });
    Numbers(T("Звук при исправлении текста"),
            T("Когда FluentSwitcher исправляет слово или выделенный текст. Звук – fix.wav в папке sounds"), "sound_fix", 0,
            { 0, 30, 60, 100 }, { T("Нет"), T("Тихий"), T("Средний"), T("Громкий") });
    // sound_bluetooth_ms (the engine's LayoutSound.h): 0,2 s is not enough for every pair (forum, WinnyS, 13.10.2026).
    Numbers(T("Пауза перед звуком в Bluetooth-наушниках"),
            T("Наушники засыпают в тишине и пропускают начало звука: не слышно щелчка – увеличьте паузу, щелчок "
              "запаздывает – уменьшите. Пока в них что-то играет, паузы нет"),
            "sound_bluetooth_ms", 200, { 0, 100, 200, 500, 1000, 2000 },
            { T("Без паузы"), T("0,1 секунды"), T("0,2 секунды"), T("0,5 секунды"), T("1 секунда"), T("2 секунды") });
    FinishPage();
}

void SettingsFrame::BuildCommands()
{
    Section(kIconCommands, T("Команды"));
    m_commandsPage = m_page;
    m_commandsColumn = m_column;
    FillCommands();
}

void SettingsFrame::FillCommands()
{
    HotkeyEditor::CancelRecording();
    m_commandsColumn->Clear(true);
    wxScrolledWindow* page = m_commandsPage;
    wxBoxSizer* column = m_commandsColumn;
    auto& list = m_edit.Json()["run_programs"];
    if (!list.is_array())
        list = nlohmann::json::array();

    AddSettingsCard(page, column, T("Команды по сочетанию клавиш"),
                    T("Запустить приложение или вставить текст. В тексте @@(…) нажимает клавиши: "
                      "@@(Ctrl + A) – выделить всё, @@(Enter) – новая строка"),
                    [this](wxWindow* card) {
                        FluentButton* add = new FluentButton(card, wxID_ANY, T("Добавить команду"), true);
                        add->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
                            m_edit.Json()["run_programs"].push_back({ { "args", "" }, { "cmd", "" }, { "delay", 0 },
                                                                       { "elevated", false }, { "enabled", true },
                                                                       { "hotkey", "" }, { "type", 0 } });
                            Changed();
                            CallAfter([this] { FillCommands(); });
                        });
                        return add;
                    });

    for (size_t i = 0; i < list.size(); i++)
    {
        const auto& command = list[i];
        const bool snippet = command.value("type", 0) == 1;
        const wxString cmd = FromUtf8(command.value("cmd", std::string()));
        const wxString title = CommandTitle(command);

        AddSettingsCard(page, column, title,
                        snippet ? T("Текст печатается туда, где курсор")
                                : T("Приложение, документ или папка; путь можно вставить или выбрать"),
                        [&](wxWindow* card) {
            wxPanel* panel = new wxPanel(card);
            panel->SetBackgroundColour(card->GetBackgroundColour());
            wxBoxSizer* rows = new wxBoxSizer(wxVERTICAL);
            auto label = [panel](const char* text) { return FluentText(panel, T(text), 9, g.text2); }; // text: N_("...")

            // What it does, on or off, remove.
            wxBoxSizer* top = new wxBoxSizer(wxHORIZONTAL);
            FluentChoice* kind = new FluentChoice(panel, { T("Запустить приложение"), T("Вставить текст") }, snippet ? 1 : 0);
            kind->onChange = [this, kind, i] {
                m_edit.Json()["run_programs"][i]["type"] = kind->GetSelection();
                Changed();
                CallAfter([this] { FillCommands(); });
            };
            ToggleSwitch* on = new ToggleSwitch(panel, command.value("enabled", true));
            on->onChange = [this, on, i] {
                m_edit.Json()["run_programs"][i]["enabled"] = on->IsOn();
                Changed();
            };
            FluentButton* remove = new FluentButton(panel, wxID_ANY, kIconDelete, T("Удалить команду"));
            remove->Bind(wxEVT_BUTTON, [this, i](wxCommandEvent&) {
                auto& all = m_edit.Json()["run_programs"];
                all.erase(all.begin() + (std::ptrdiff_t)i);
                Changed();
                CallAfter([this] { FillCommands(); });
            });
            top->Add(kind, 0, wxALIGN_CENTER_VERTICAL);
            top->Add(on, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(12));
            on->SetToolTip(T("Включена"));
            top->AddStretchSpacer();
            if (!snippet)
            {
                FluentButton* run = new FluentButton(panel, wxID_ANY, T("Выполнить сейчас"));
                run->Bind(wxEVT_BUTTON, [this, i](wxCommandEvent&) {
                    if (HasChanges() && !Apply(false)) // the engine runs what is saved
                        return;
                    if (!m_engine || !Engine::RunCommand(m_engine, (int)i))
                        SetStatus(T("FluentSwitcher не запущен: команду выполнить некому"), true);
                });
                top->Add(run, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(8));
            }
            top->Add(remove, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(8));
            rows->Add(top, 0, wxEXPAND);

            // The program and its arguments, or the text.
            TextField* what = new TextField(panel, cmd, snippet ? 488 : 370, false, 4096); // a path, a text
            what->SetHint(snippet ? T("Текст, например: С уважением, Дмитрий") : T("Путь к приложению"));
            what->onChange = [this, what, i] {
                m_edit.Json()["run_programs"][i]["cmd"] = ToUtf8(what->Value());
                Changed();
            };
            rows->Add(label(snippet ? N_("Текст") : N_("Приложение")), 0, wxTOP, FromDIP(10));
            wxBoxSizer* whatRow = new wxBoxSizer(wxHORIZONTAL);
            whatRow->Add(what, 0, wxALIGN_CENTER_VERTICAL);
            if (!snippet)
            {
                FluentButton* browse = new FluentButton(panel, wxID_ANY, T("Выбрать…"));
                browse->Bind(wxEVT_BUTTON, [this, i](wxCommandEvent&) {
                    wxFileDialog dialog(this, T("Приложение для команды"), wxString(), wxString(),
                                        T("Приложения (*.exe;*.bat;*.cmd;*.lnk)|*.exe;*.bat;*.cmd;*.lnk|Все файлы (*.*)|*.*"),
                                        wxFD_OPEN | wxFD_FILE_MUST_EXIST);
                    if (dialog.ShowModal() != wxID_OK)
                        return;
                    m_edit.Json()["run_programs"][i]["cmd"] = ToUtf8(dialog.GetPath());
                    Changed();
                    CallAfter([this] { FillCommands(); });
                });
                whatRow->Add(browse, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(8));
            }
            rows->Add(whatRow, 0, wxTOP, FromDIP(4));
            if (!snippet)
            {
                TextField* args = new TextField(panel, FromUtf8(command.value("args", std::string())), 370, false, 4096);
                args->SetHint(T("Необязательно"));
                args->onChange = [this, args, i] {
                    m_edit.Json()["run_programs"][i]["args"] = ToUtf8(args->Value());
                    Changed();
                };
                NumberField* delay = new NumberField(panel, command.value("delay", 0));
                delay->onChange = [this, delay, i] {
                    const int value = delay->Value();
                    if (value >= 0 && value <= 10000)
                    {
                        m_edit.Json()["run_programs"][i]["delay"] = value;
                        Changed();
                    }
                };
                wxFlexGridSizer* more = new wxFlexGridSizer(2, FromDIP(4), FromDIP(16));
                more->Add(label(N_("Аргументы")));
                more->Add(label(N_("Пауза, мс")));
                more->Add(args);
                more->Add(delay);
                rows->Add(more, 0, wxTOP, FromDIP(10));
            }

            // Its hotkeys, and running it now.
            rows->Add(label(N_("Сочетание")), 0, wxTOP, FromDIP(10));
            wxBoxSizer* keysRow = new wxBoxSizer(wxHORIZONTAL);
            HotkeyEditor* keys = new HotkeyEditor(panel, FromUtf8(command.value("hotkey", std::string())), 2, false);
            keys->onChange = [this, keys, i] {
                m_edit.Json()["run_programs"][i]["hotkey"] = ToUtf8(keys->Value());
                RefreshDoubleWaits(); // a command's "twice" makes the single presses of those keys wait too
                Changed();
            };
            keysRow->Add(keys, 0, wxALIGN_TOP);
            rows->Add(keysRow, 0, wxTOP, FromDIP(4));
            panel->SetSizer(rows);
            return panel;
        }, true);
    }
    page->Layout();
    page->FitInside();
}

void SettingsFrame::BuildAdvanced()
{
    Section(kIconAdvanced, T("Дополнительно"));

    // Apps where FluentSwitcher keeps quiet altogether (the engine's Settings.h, disableInPrograms - SimpleSwitcher's
    // list, now shown): no hotkeys, fixes, automatic switch, flag at the cursor. The lists of the automatic switch -
    // in its section.
    WordList("disableInPrograms", WordKind::Programs, T("Не работать в приложениях"),
             T("FluentSwitcher там молчит: ни сочетаний, ни исправлений, ни автопереключения, ни флага у курсора"),
             T("Приложения, где FluentSwitcher молчит совсем: игры, приложения со своими сочетаниями. Имя файла "
               "приложения (far.exe) или путь к нему. Где не нужно только автопереключение – «Автопереключение» – "
               "«Без автопереключения в приложениях»"),
             T("Имя файла – как в Диспетчере задач на вкладке «Подробности». Путь – если нужно одно приложение из "
               "нескольких с тем же именем"));
    // Windows of remote desktop clients and virtual machines (the engine's RemoteDesktop.h, work_in_remote): typed there
    // goes to the other computer. On - FluentSwitcher works there as anywhere (as before 1.5.0); off - as in the apps of
    // the list above, for those who run FluentSwitcher on both computers (Dmitry, 06.10.2026: both copies fixed a word).
    Toggle(T("Работать в окнах удалённого доступа"),
           T("Удалённый рабочий стол, AnyDesk, TeamViewer, виртуальные машины. Выключите, если FluentSwitcher стоит и "
             "на том компьютере – иначе слово исправят обе копии"),
           "work_in_remote", true);

    Toggle(T("Сочетания с Ctrl + Alt в раскладках с AltGr"),
           T("Windows принимает Ctrl + Alt за правый Alt (AltGr) и печатает символ вместо сочетания: в немецкой, "
             "польской раскладке, в русской – ₽ на Ctrl + Alt + 8. FluentSwitcher на миг переключает раскладку, и "
             "приложение получает сочетание"),
           "fixRAlt", false);
    Toggle(T("Перепечатывать исправленное клавишами"),
           T("Старый способ. Обычно исправленное слово вставляется готовыми символами: так новый Блокнот Windows 11 "
             "не теряет Shift. Включите, если какое-то приложение не принимает такую вставку"),
           "retype_keys", false);
    NumberField* delay = nullptr;
    AddSettingsCard(m_page, m_column, T("Пауза между символами при исправлении, мс"),
                    T("Исправленное слово печатается по одному символу с этой паузой: новый Блокнот Windows 11 "
                      "теряет и повторяет символы, отправленные разом. Обычно 8"),
                    [&](wxWindow* card) { return delay = new NumberField(card, m_edit.GetInt("retype_delay_ms", 8)); });
    delay->onChange = [this, delay] {
        const int value = delay->Value();
        if (value > 0 && value <= 100)
        {
            m_edit.SetInt("retype_delay_ms", value);
            Changed();
        }
    };

    // The report to the developer (above, DeveloperReport): the engine records with no typed text; at once, not by
    // Apply - it is not a setting.
    AddSettingsCard(m_page, m_column, T("Отчёт для разработчика"),
                    T("Если что-то работает не так: начните запись, повторите ошибку и сохраните отчёт. В записи нет "
                      "набранного текста – вместо букв только их число, пароли в неё не попадают. Отчёт появится на "
                      "рабочем столе – его можно прочитать, – и откроется письмо разработчику, приложите отчёт к нему"),
                    [this](wxWindow* card) {
                        wxPanel* box = new wxPanel(card);
                        box->SetBackgroundColour(card->GetBackgroundColour());
                        m_reportStart = new FluentButton(box, wxID_ANY, T("Остановить запись"));
                        m_reportStart->SetText(T("Начать запись")); // as wide as the longer of the two
                        FluentButton* save = new FluentButton(box, wxID_ANY, T("Сохранить отчёт"), true);
                        m_reportStart->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
                            RefreshEngine();
                            if (!m_state)
                                return SetStatus(T("FluentSwitcher не запущен: записывать некому"), true);
                            const bool recording = (m_state & Engine::StateReport) != 0;
                            if (!Engine::SetReport(m_engine, !recording))
                                return SetStatus(T("FluentSwitcher не ответил"), true);
                            RefreshEngine();
                            SetStatus(recording ? T("Запись остановлена: сохраните отчёт")
                                                : T("Идёт запись. Повторите ошибку и нажмите «Сохранить отчёт». Через "
                                                    "час запись остановится сама"),
                                      false);
                        });
                        save->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
                            RefreshEngine();
                            if (m_state & Engine::StateReport)
                                Engine::SetReport(m_engine, false); // the end of the recording is in the file then
                            RefreshEngine();
                            const wxString report = DeveloperReport(m_folder, m_saved.Path());
                            if (report.empty())
                                return SetStatus(T("Записи ещё нет: нажмите «Начать запись» и повторите ошибку"), true);
                            const wxString path = SaveOnDesktop(report);
                            if (path.empty())
                                return SetStatus(T("Не удалось сохранить отчёт на рабочем столе"), true);
                            const wxString name = wxFileName(path).GetFullName();
                            SetStatus(OpenLetter(path)
                                          ? wxString::Format(T("Отчёт на рабочем столе: %s. Открывается письмо на %s с ним; "
                                                               "если файла в письме нет – приложите его сами"),
                                                             name, kReportMail)
                                          : wxString::Format(T("Отчёт на рабочем столе: %s. Приложите его к письму на %s"),
                                                             name, kReportMail),
                                      false);
                        });
                        wxBoxSizer* row = new wxBoxSizer(wxHORIZONTAL);
                        row->Add(m_reportStart, 0, wxALIGN_CENTER_VERTICAL);
                        row->Add(save, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, box->FromDIP(8));
                        box->SetSizer(row);
                        return box;
                    }, true); // the two buttons under the text: beside it the card was the widest of all (window minimum)

    // Not a setting that is saved: the engine keeps it until it quits. Switched by Apply, as "FluentSwitcher
    // включён" (Дмитрий 06.10: switched at once, it left Apply grey, as if the switch had not taken).
    AddSettingsCard(m_page, m_column, T("Журнал отладки"),
                    T("До выхода из FluentSwitcher каждое нажатие клавиш пишется в log\\FluentSwitcher.exe.log "
                      "в папке приложения. Пароли при этом не вводите; после проверки выключите и удалите журнал"),
                    [&](wxWindow* card) { return m_loggingSwitch = new ToggleSwitch(card, m_logging); });
    m_loggingSwitch->onChange = [this] {
        if (!m_state)
        {
            m_loggingSwitch->SetOn(false);
            SetStatus(T("FluentSwitcher не запущен: журнал вести некому"), true);
            return;
        }
        m_logging = m_loggingSwitch->IsOn();
        Changed();
    };
    AddSettingsCard(m_page, m_column, T("Папка журнала"),
                    T("Там файл FluentSwitcher.exe.log – его можно приложить к сообщению об ошибке"),
                    [this](wxWindow* card) {
                        FluentButton* open = new FluentButton(card, wxID_ANY, T("Открыть"));
                        open->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
                            const wxString folder = m_folder + "\\log";
                            if (!wxDirExists(folder))
                                return SetStatus(T("Журнала ещё нет: включите его выше и повторите ошибку"), true);
                            OpenAsUser(folder.ToStdWstring());
                        });
                        return open;
                    });
    FinishPage();
}

void SettingsFrame::BuildAbout()
{
    Section(kIconAbout, T("О приложении"));

    AddSettingsCard(m_page, m_column, wxString::Format("FluentSwitcher %s", kVersion),
                    T("Исправляет текст, набранный не в той раскладке, и переключает раскладки"),
                    [](wxWindow* card) {
                        FluentButton* open = new FluentButton(card, wxID_ANY, T("Открыть на GitHub"));
                        open->Bind(wxEVT_BUTTON, [](wxCommandEvent&) {
                            OpenAsUser(L"https://github.com/DmitryN71/FluentSwitcher");
                        });
                        return open;
                    });
    Toggle(T("Проверять обновления"),
           T("Раз в день приложение спрашивает у GitHub номер последней версии, больше ничего не отправляет. Новую "
             "версию оно скачает и поставит только по вашему клику – по уведомлению или кнопке «Обновить»"),
           "check_updates", true);
    // "Проверить сейчас"; once a newer version is known, "Обновить" has the engine install it (UpdateInstall.h) or,
    // when it cannot (no engine, a release without the file), "Скачать" opens its page; when GitHub could not be
    // reached, "Открыть страницу загрузки" leaves it to the browser.
    m_updateLabel = AddSettingsCard(m_page, m_column, T("Обновления"), UpdateText(Update::Load(m_folder.ToStdWstring())),
                                    [this](wxWindow* card) {
                                        FluentButton* button = new FluentButton(card, wxID_ANY, T("Проверить сейчас"));
                                        button->Bind(wxEVT_BUTTON, [this, button](wxCommandEvent&) { CheckUpdateNow(button); });
                                        if (!m_updatePage.empty())
                                            button->SetText(UpdateButtonText());
                                        return button;
                                    });
    AddSettingsCard(m_page, m_column, T("Основан на SimpleSwitcher"),
                    T("Автор оригинала – Aegel5. FluentSwitcher – изменённая версия: окно настроек и флаги в стиле "
                      "Windows 11, флаг у курсора, исправление с начала строки, запуск от администратора без "
                      "вопросов и другие исправления"),
                    [this](wxWindow* card) {
                        FluentButton* open = new FluentButton(card, wxID_ANY, T("Открыть на GitHub"));
                        open->Bind(wxEVT_BUTTON, [](wxCommandEvent&) {
                            OpenAsUser(L"https://github.com/Aegel5/SimpleSwitcher");
                        });
                        return open;
                    });
    // THIRD-PARTY-NOTICES.txt lies next to the program (the installer and the zip put it there).
    AddSettingsCard(m_page, m_column, T("Лицензия GPL-3.0"),
                    T("Приложение бесплатное, исходный код открыт. Поставляется без каких-либо гарантий. Части "
                      "других авторов – под своими лицензиями: wxWidgets, оформление FluentClipper, значки Fluent "
                      "UI System Icons (Microsoft), флаги Flagpack и другие"),
                    [this](wxWindow* card) {
                        FluentButton* open = new FluentButton(card, wxID_ANY, T("Лицензии"));
                        open->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
                            const wxString notices = m_folder + "\\THIRD-PARTY-NOTICES.txt";
                            if (!wxFileName::FileExists(notices))
                                return SetStatus(T("Рядом с приложением нет файла THIRD-PARTY-NOTICES.txt"), true);
                            OpenAsUser(notices.ToStdWstring());
                        });
                        return open;
                    });
    FinishPage();
}

wxString SettingsFrame::UpdateButtonText() const
{
    // "Обновить" first: the click installs then whatever the page (CheckUpdateNow).
    return m_updatePage.empty()                    ? T("Проверить сейчас")
         : m_updateInstall                       ? T("Обновить")
         : m_updatePage == Update::kReleasesPage ? T("Открыть страницу загрузки")
                                                 : T("Скачать");
}

wxString SettingsFrame::UpdateText(const Update::State& state)
{
    m_updatePage.clear();
    m_updateInstall = false;
    // A failed check leaves the button "Проверить сейчас": it became "Открыть страницу загрузки" for good, and with the
    // automatic checks off the window could never ask GitHub again. A newer version known from an earlier answer is
    // still shown (review 11.10.2026).
    if (state.failed && !Update::NewerKnown(state))
        return T("Не удалось связаться с GitHub. Проверьте ещё раз позже");
    if (!state.checkedAt || state.latest.empty())
        return T("Ещё не проверялось");
    const wxString when = wxString::Format(T("Проверено: %s"),
                                           wxDateTime((time_t)(state.checkedAt / 1000)).Format("%d.%m.%Y, %H:%M"));
    const wxString dot = wxString::FromUTF8(" \xC2\xB7 ");
    if (Update::NewerKnown(state))
    {
        m_updatePage = state.page.empty() ? wxString(Update::kReleasesPage) : wxString(state.page);
        m_updateInstall = !(Update::Installed(m_folder.ToStdWstring()) ? state.setup.url : state.zip.url).empty();
        return wxString::Format(T("Вышла версия %s"), wxString::FromUTF8(state.latest)) + dot + when;
    }
    return T("У вас последняя версия") + dot + when;
}

void SettingsFrame::CheckUpdateNow(FluentButton* button)
{
    if (!m_updatePage.empty())
    {
        // "Обновить": the engine downloads, checks and installs the new version (UpdateInstall.h) and tells how it
        // goes by the clock; this window closes (the installer would close it anyway) - what was changed here is
        // saved first.
        if (m_updateInstall && (m_engine = Engine::Find(m_folder, m_enginePid)) != nullptr)
        {
            if (HasChanges() && !Apply(false))
                return;
            if (Engine::StartUpdate(m_engine))
            {
                Close();
                return;
            }
        }
        OpenAsUser(m_updatePage.ToStdWstring());
        return;
    }
    // Seen at once, before the wait for GitHub (some 10 s when there is no network).
    wxBusyCursor busy;
    button->Enable(false);
    SetCardDescription(m_updateLabel, T("Проверяю…"));
    m_updateLabel->GetParent()->Update();
    const Update::Result result = Update::Check();
    const std::wstring folder = m_folder.ToStdWstring();
    Update::State state = Update::Load(folder);
    Update::Apply(state, result, true);
    if (Update::NewerKnown(state))
        state.notified = state.latest; // seen here: the engine does not tell about it by the clock again
    if (!Update::Save(folder, state))
        SetStatus(T("Не удалось записать update.json в папку приложения"), true);
    // The answer also comes as a note by the flag, as in FluentClipper.
    if (HWND engine = Engine::Find(m_folder, m_enginePid))
        Engine::UpdateChecked(engine);
    SetCardDescription(m_updateLabel, UpdateText(state));
    button->SetText(UpdateButtonText());
    button->Enable(true);
    // The card may have grown: its page lays out again (m_page is the page being built, not this one).
    for (wxWindow* w = m_updateLabel; w; w = w->GetParent())
        if (wxScrolledWindow* page = wxDynamicCast(w, wxScrolledWindow))
        {
            page->Layout();
            page->FitInside();
            break;
        }
}

void SettingsFrame::FinishPage()
{
    m_page->FitInside();
}

void SettingsFrame::ShowSection(int section)
{
    if (section < 0 || section >= (int)m_pages.size())
        section = 0;
    if (m_searchPage)
        m_searchPage->Hide();
    m_searching = false;
    for (size_t i = 0; i < m_pages.size(); i++)
        m_pages[i]->Show((int)i == section);
    m_title->SetLabel(m_titles[section]);
    m_nav->Select(section);
    m_section = section;
    Layout();
}

// ---------------------------------------------------------------------------------------------
// The search
// ---------------------------------------------------------------------------------------------

std::vector<SettingsFrame::Found> SettingsFrame::FindCards(const std::vector<wxString>& words) const
{
    std::vector<Found> found[3]; // all the words in the title; in the visible texts; somewhere
    if (words.empty())
        return found[0];
    const wxString tipSign = WithTip(wxString());
    for (size_t section = 0; section < m_pages.size(); section++)
        for (wxWindow* child : m_pages[section]->GetChildren())
        {
            // A card: AddSettingsCard makes its title and description first. Hidden ones (FluentSwitcher running or not)
            // are not found.
            Card* card = dynamic_cast<Card*>(child);
            if (!card || !card->IsShown() || card->GetChildren().size() < 2)
                continue;
            auto part = card->GetChildren().begin();
            wxStaticText* titleLabel = dynamic_cast<wxStaticText*>(*part);
            wxStaticText* aboutLabel = dynamic_cast<wxStaticText*>(*++part);
            if (!titleLabel || !aboutLabel)
                continue;
            wxString title = titleLabel->GetLabelText();
            title.EndsWith(tipSign, &title);
            // The description in one line (Wrap put line breaks in it); a list's count after a dot: "VMware, IPsec ·
            // Пока пусто".
            wxString about = aboutLabel->IsShown() ? aboutLabel->GetLabelText() : wxString();
            const bool list = std::any_of(m_wordLists.begin(), m_wordLists.end(),
                                          [aboutLabel](const WordListOnPage& l) { return l.label == aboutLabel; });
            if (list && about.Contains("\n"))
                about = about.BeforeLast('\n') + wxString::FromUTF8(" \xC2\xB7 ") + about.AfterLast('\n');
            about.Replace("\n", " ");
            // Its hotkeys before it: the results show what is set ("break" finds the actions on Break).
            wxString keys;
            std::function<void(wxWindow*)> hotkeys = [&](wxWindow* parent) {
                for (wxWindow* w : parent->GetChildren())
                    if (HotkeyEditor* editor = dynamic_cast<HotkeyEditor*>(w))
                    {
                        const wxString shown = HotkeyDisplay(editor->Value());
                        if (!shown.empty())
                            keys += (keys.empty() ? "" : ", ") + shown;
                    }
                    else
                        hotkeys(w);
            };
            hotkeys(card);
            if (!keys.empty())
                about = about.empty() ? keys : keys + wxString::FromUTF8(" \xC2\xB7 ") + about;
            // Elsewhere: the details (the tooltip of the title), the section, the items of its list.
            wxString more = titleLabel->GetToolTipText() + " " + m_titles[section];
            if (const auto extra = m_searchExtra.find(card); extra != m_searchExtra.end())
                more += " " + extra->second;
            const wxString inTitle = Folded(title), visible = Folded(title + " " + about),
                           all = visible + " " + Folded(more);
            auto every = [&words](const wxString& text) {
                return std::all_of(words.begin(), words.end(), [&text](const wxString& w) { return HasWord(text, w); });
            };
            const int tier = every(inTitle) ? 0 : every(visible) ? 1 : every(all) ? 2 : -1;
            if (tier >= 0)
                found[tier].push_back({ (int)section, card, title, about });
        }
    for (int tier : { 1, 2 })
        found[0].insert(found[0].end(), found[tier].begin(), found[tier].end());
    return found[0];
}

void SettingsFrame::Search()
{
    const wxString query = Folded(m_search->Value()).Strip(wxString::both);
    if (query.empty())
    {
        if (m_searching)
            ShowSection(m_section);
        return;
    }
    // Nothing for the query as typed - the same keys in the other layouts: "pderb" finds "звуки" (the window's field
    // may be typed in the wrong layout too, and the automatic switch waits for the end of a word).
    std::vector<Found> found = FindCards(QueryWords(query));
    wxString typedAs;
    if (found.empty())
        for (const wxString& other : OtherForms(query, SwitchLayouts(m_edit)))
            if (!(found = FindCards(QueryWords(Folded(other)))).empty())
            {
                typedAs = other;
                break;
            }

    wxWindowUpdateLocker noFlicker(this);
    m_searchColumn->Clear(true);
    m_searchHits.clear();
    if (found.empty() || !typedAs.empty())
    {
        wxStaticText* note = FluentText(m_searchPage,
                                        found.empty() ? T("Ничего не нашлось. Попробуйте другое слово")
                                                      : wxString::Format(T("Найдено по «%s»: набрано в другой раскладке"),
                                                                         typedAs),
                                        9, g.text2);
        m_searchColumn->Add(note, 0, wxLEFT | wxBOTTOM, FromDIP(4));
    }
    for (const Found& f : found)
    {
        SearchHit* hit = new SearchHit(m_searchPage, f.title, f.about, m_titles[f.section]);
        const int at = (int)m_searchHits.size();
        hit->onOpen = [this, section = f.section, card = f.card] { OpenFound(section, card); };
        hit->onArrow = [this, at](bool down) {
            const int next = at + (down ? 1 : -1);
            if (next < 0)
                m_search->FocusText(false);
            else if (next < (int)m_searchHits.size())
                m_searchHits[next]->SetFocus();
        };
        m_searchColumn->Add(hit, 0, wxEXPAND | wxBOTTOM, FromDIP(4));
        m_searchHits.push_back(hit);
    }
    for (wxWindow* page : m_pages)
        page->Hide();
    m_searchPage->Show();
    m_title->SetLabel(T("Результаты поиска"));
    m_nav->Select(-1);
    m_searching = true;
    Layout();
    m_searchPage->Scroll(0, 0);
    m_searchPage->Layout();
    m_searchPage->FitInside();
}

void SettingsFrame::OpenFound(int section, wxWindow* card)
{
    if (section < 0 || section >= (int)m_pages.size())
        return;
    ShowSection(section);
    wxScrolledWindow* page = m_pages[section];
    const wxWindowList& cards = page->GetChildren();
    if (std::find(cards.begin(), cards.end(), card) == cards.end()) // gone meanwhile: the commands' cards are made anew
        return;
    // The card at the top of the page (positions as unscrolled: from the top).
    page->Scroll(0, 0);
    page->Layout();
    page->FitInside();
    int unitX = 0, unitY = 0;
    page->GetScrollPixelsPerUnit(&unitX, &unitY);
    if (unitY > 0)
        page->Scroll(-1, std::max(0, (card->GetPosition().y - FromDIP(8)) / unitY));
    // Its control takes the focus: Space or the arrows change it at once.
    std::function<wxWindow*(wxWindow*)> control = [&](wxWindow* parent) -> wxWindow* {
        for (wxWindow* child : parent->GetChildren())
        {
            if (!child->IsShown() || !child->IsEnabled())
                continue;
            if (wxWindow* inside = control(child))
                return inside;
            if (child->AcceptsFocus())
                return child;
        }
        return nullptr;
    };
    if (wxWindow* focus = control(card))
        focus->SetFocus();
    // Outlined in the accent color for a moment: where it is on the page. Its own painting is the kit's (Card); this
    // handler, bound later, comes first and leaves it the card while it is not outlined.
    EndHighlight();
    m_highlight = card;
    card->Bind(wxEVT_PAINT, &SettingsFrame::PaintHighlight, this);
    card->Refresh();
    m_highlightTimer.StartOnce(2500);
}

void SettingsFrame::PaintHighlight(wxPaintEvent& e)
{
    wxWindow* card = static_cast<wxWindow*>(e.GetEventObject());
    if (card != m_highlight.get())
        return e.Skip();
    wxAutoBufferedPaintDC dc(card);
    dc.SetBackground(wxBrush(card->GetParent()->GetBackgroundColour()));
    dc.Clear();
    const wxRect r(card->GetClientSize());
    const int ring = card->FromDIP(2);
    FillRound(dc, r, card->FromDIP(6), g.accent);
    FillRound(dc, wxRect(r).Deflate(ring), card->FromDIP(6) - ring, g.card);
}

void SettingsFrame::EndHighlight()
{
    m_highlightTimer.Stop();
    if (wxWindow* card = m_highlight.get())
    {
        card->Unbind(wxEVT_PAINT, &SettingsFrame::PaintHighlight, this);
        card->Refresh();
    }
    m_highlight.Release();
}

// ---------------------------------------------------------------------------------------------
// The engine and saving
// ---------------------------------------------------------------------------------------------

void SettingsFrame::RefreshEngine()
{
    m_engine = Engine::Find(m_folder, m_enginePid);
    const long state = m_engine ? Engine::GetState(m_engine) : 0;
    const bool running = state != 0;
    // The switches follow the engine, except one the user has just changed and not applied yet.
    const bool enabledPending = m_state && m_enabled != ((m_state & Engine::StateEnabled) != 0);
    const bool autostartPending = m_state && m_autostart != ((m_state & Engine::StateAutostart) != 0);
    const bool loggingPending = m_state && m_logging != ((m_state & Engine::StateLogging) != 0);
    m_state = state;
    if (!enabledPending)
        m_enabled = (state & Engine::StateEnabled) != 0 || !running;
    if (!autostartPending)
        m_autostart = (state & Engine::StateAutostart) != 0;
    if (!loggingPending || !running)
        m_logging = (state & Engine::StateLogging) != 0;
    if (m_loggingSwitch)
        m_loggingSwitch->SetOn(m_logging);
    if (m_reportStart)
        m_reportStart->SetText((state & Engine::StateReport) ? T("Остановить запись") : T("Начать запись"));
    if (m_enabledSwitch)
    {
        // Not running: on/off and autostart are the engine's to tell and to do, so their cards go.
        m_enabledSwitch->SetOn(m_enabled);
        m_autostartSwitch->SetOn(m_autostart);
        for (wxWindow* card : { m_enabledSwitch->GetParent(), m_autostartSwitch->GetParent() })
            running ? ShowCard(card) : HideCard(card);
    }
    if (m_notRunningCard)
        running ? HideCard(m_notRunningCard) : ShowCard(m_notRunningCard);
}

bool SettingsFrame::HasChanges() const
{
    if (m_edit != m_saved)
        return true;
    return m_state && (m_enabled != ((m_state & Engine::StateEnabled) != 0) ||
                       m_autostart != ((m_state & Engine::StateAutostart) != 0) ||
                       m_logging != ((m_state & Engine::StateLogging) != 0));
}

void SettingsFrame::Changed()
{
    SetStatus(wxString(), false);
}

bool SettingsFrame::Apply(bool reopen)
{
    if (!m_canSave)
        return false;
    const wxString oldLanguage = m_saved.GetString("gui_lang", wxString());
    const wxString oldTheme = m_saved.GetString("ui_theme", wxString());
    const bool oldAdmin = m_saved.GetBool("isMonitorAdmin", false);
    // A new window in the new language or theme - started when all of Apply is done. Started in the middle, it closed
    // this window's question about administrator rights (RestartElevated) half a second later, as "Не сейчас" (review
    // 11.10.2026).
    struct Later
    {
        std::function<void()> run;
        ~Later()
        {
            if (run)
                run();
        }
    } newWindow;
    if (m_edit != m_saved)
    {
        wxString error, dropped;
        bool saved;
        {
            // Taking what the engine learned and writing - together, while the engine does not write (it adds learned
            // words and its counts to the file too): its write in between would be lost.
            ConfigLock lock;
            TakeEngineLearned(m_edit, m_saved);
            dropped = DropCoveredForced(); // the engine learned an entry that an exception of this window covers
            RestartMovedCounts(m_saved);
            saved = m_edit.Save(&error);
        }
        RefillWordLists(); // with the words the engine learned meanwhile
        if (!saved)
        {
            SetStatus(T("Не удалось сохранить: ") + error, true);
            return false;
        }
        m_saved = m_edit;
        if (!dropped.empty())
            SetStatus(dropped, false);
        if (reopen && (m_edit.GetString("gui_lang", wxString()) != oldLanguage ||
                       m_edit.GetString("ui_theme", wxString()) != oldTheme))
        {
            newWindow.run = [this] {
                // The texts and colours are read when the window is built: a new window in the new language or
                // theme, on this section. This one stays on the screen until the new one is there and closes it
                // (main.cpp, --replace-pid): before, this one closed first, and for half a second to a second there was
                // no window at all (Dmitry, 11.10.2026: "так долго перерисовывается"). Its place now, for the new one.
                SavePlacement();
                wxString command = wxString(GetCommandLineW());
                command += wxString::Format(" --section=%d --replace-pid=%lu", m_section, GetCurrentProcessId());
                STARTUPINFOW si = { sizeof(si) };
                PROCESS_INFORMATION pi = {};
                std::wstring line = command.ToStdWstring();
                if (CreateProcessW(nullptr, line.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi))
                {
                    AllowSetForegroundWindow(pi.dwProcessId); // the new window comes up over this one
                    CloseHandle(pi.hThread);
                    CloseHandle(pi.hProcess);
                    m_restarted = true;
                }
            };
        }
    }
    m_engine = Engine::Find(m_folder, m_enginePid);
    if (!m_engine)
    {
        RefreshEngine();
        return true; // saved; the engine reads the file when it starts
    }
    wxString problem;
    if (!Engine::ReloadConfig(m_engine))
        problem = T("FluentSwitcher не смог прочитать настройки и работает с прежними: в FluentSwitcher.json ошибка. ");
    const long state = Engine::GetState(m_engine);
    if (m_edit.GetBool("isMonitorAdmin", false) && CanElevateSelf() && state && !(state & Engine::StateElevated) &&
        !m_enginePid)
        return RestartElevated();
    // Autostart again also when the administrator mode changed: the engine's state after the reload is already the new
    // mode's and looked right, and the other way (the scheduler task or the Run value) stayed - an elevated start at every
    // logon with autostart shown off (review 11.10.2026).
    const bool adminChanged = oldAdmin != m_edit.GetBool("isMonitorAdmin", false);
    if ((adminChanged || m_autostart != ((state & Engine::StateAutostart) != 0)) &&
        !Engine::SetAutostart(m_engine, m_autostart))
        problem += T("Автозапуск не изменился: в режиме «от имени администратора» для этого нужны права администратора. ");
    if (m_logging != ((state & Engine::StateLogging) != 0) && !Engine::SetLogging(m_engine, m_logging))
        problem += T("Журнал отладки не переключился. ");
    if (m_enabled != ((state & Engine::StateEnabled) != 0) && !Engine::SetEnabled(m_engine, m_enabled))
        problem += m_edit.GetBool("isMonitorAdmin", false) && !(state & Engine::StateElevated)
            ? T("Не включился: запустите FluentSwitcher от имени администратора или выключите работу в приложениях "
                "администратора")
            : T("Не включился: включена другая копия приложения");
    m_state = 0; // take the engine's state as it is now, pending nothing
    RefreshEngine();
    if (!problem.empty())
    {
        SetStatus(problem.Strip(wxString::trailing), true);
        return false;
    }
    return true;
}

bool SettingsFrame::RestartElevated()
{
    if (!AskFluent(this,
                   T("Чтобы работать в приложениях, запущенных от имени администратора, FluentSwitcher перезапустится "
                     "с правами администратора. Windows спросит разрешения один раз: дальше приложение запускается "
                     "через планировщик заданий, без вопросов"),
                   T("Перезапустить"), T("Не сейчас")))
    {
        RefreshEngine();
        SetStatus(T("Без прав администратора FluentSwitcher выключен: перезапустите его или выключите работу "
                    "в приложениях администратора"), true);
        return false;
    }
    // The engine goes; the new one, as administrator, takes the autostart wish with it: with the rights it
    // can make the scheduler task.
    DWORD pid = 0;
    GetWindowThreadProcessId(m_engine, &pid);
    HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, pid);
    Engine::Quit(m_engine);
    if (process)
    {
        WaitForSingleObject(process, 5000);
        CloseHandle(process);
    }
    bool elevated = Engine::Start(m_folder, m_autostart ? "/set-autostart=1" : "/set-autostart=0", true);
    if (!elevated)
        Engine::Start(m_folder, "/no-elevate"); // No to Windows: back as it was, without asking again
    for (int wait = 0; wait < 50 && !Engine::Find(m_folder, 0); wait++)
        wxMilliSleep(100);
    m_state = 0;
    RefreshEngine();
    if (!elevated)
    {
        SetStatus(T("Windows не дала прав администратора: FluentSwitcher запущен без них и выключен"), true);
        return false;
    }
    SetStatus(T("FluentSwitcher перезапущен с правами администратора"), false);
    return true;
}

void SettingsFrame::SetStatus(const wxString& text, bool warning)
{
    m_status->SetForegroundColour(warning ? WarningColour() : g.text2);
    // Wrapped to the width the footer leaves it, not to its own: SetLabel sizes the line to the text in one row, and a
    // long message was cut at the buttons (11.10.2026: "Windows switches the layout only with Alt + Shift, Ctrl").
    m_status->SetMinSize(wxSize(1, -1));
    m_status->SetLabel(text);
    Layout();
    const int width = m_status->GetSize().x;
    m_status->Wrap(width > 1 ? width : FromDIP(360));
    m_status->SetMinSize(wxSize(1, m_status->GetBestSize().y)); // all its rows: the footer grows
    Layout();
}
