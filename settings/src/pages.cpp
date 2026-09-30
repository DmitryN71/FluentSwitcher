#include "pages.h"

#include "engine.h"
#include "hotkeys.h"
#include "icons.h"

#include <wx/dcbuffer.h>
#include <wx/dir.h>
#include <wx/filename.h>
#include <wx/utils.h>

namespace
{
const char* const kVersion = "7.0.6"; // the engine's (src/ver.h)

// A one-line text box in the Windows 11 look (the kit has one for numbers only).
class TextField : public wxPanel
{
public:
    std::function<void()> onChange;

    TextField(wxWindow* parent, const wxString& value, int width) : wxPanel(parent)
    {
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        SetMinSize(FromDIP(wxSize(width, 32)));
        m_text = new wxTextCtrl(this, wxID_ANY, value, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
        m_text->SetFont(UiFont(10));
        m_text->SetBackgroundColour(g.input);
        m_text->SetForegroundColour(g.text);
        m_text->SetMaxLength(64);
        m_text->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { if (onChange) onChange(); });
        m_text->Bind(wxEVT_SET_FOCUS, [this](wxFocusEvent& e) { Refresh(); e.Skip(); });
        m_text->Bind(wxEVT_KILL_FOCUS, [this](wxFocusEvent& e) { Refresh(); e.Skip(); });
        Bind(wxEVT_PAINT, [this](wxPaintEvent&) {
            wxAutoBufferedPaintDC dc(this);
            dc.SetBackground(wxBrush(GetParent()->GetBackgroundColour()));
            dc.Clear();
            FillInput(dc, wxRect(GetClientSize()), FromDIP(4), g.input, m_text->HasFocus() ? &g.accent : nullptr,
                      FromDIP(2));
        });
        Bind(wxEVT_SIZE, [this](wxSizeEvent& e) {
            const wxSize size = GetClientSize();
            const int h = m_text->GetBestSize().y;
            m_text->SetSize(FromDIP(10), (size.y - h) / 2, size.x - FromDIP(20), h);
            e.Skip();
        });
        Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent&) { m_text->SetFocus(); });
    }

    wxString Value() const { return m_text->GetValue(); }

private:
    wxTextCtrl* m_text;
};

// "English (United States)", "русский (Россия)": the language of a layout, in that language.
wxString LayoutName(const wxString& hkl)
{
    unsigned long value = 0;
    if (!hkl.ToULong(&value, 16))
        return hkl;
    wchar_t locale[LOCALE_NAME_MAX_LENGTH] = {};
    if (!LCIDToLocaleName(MAKELCID(LOWORD(value), SORT_DEFAULT), locale, LOCALE_NAME_MAX_LENGTH, 0))
        return hkl;
    wchar_t name[256] = {};
    if (!GetLocaleInfoEx(locale, LOCALE_SNATIVEDISPLAYNAME, name, 256))
        return wxString(locale);
    wxString s(name);
    return s.Left(1).Upper() + s.Mid(1);
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
}

SettingsFrame::SettingsFrame(const Config& config, const wxString& folder, unsigned long enginePid,
                             const wxString& loadError, int section)
    : wxFrame(nullptr, wxID_ANY, "FluentSwitcher"),
      m_saved(config), m_edit(config), m_folder(folder), m_enginePid(enginePid), m_canSave(loadError.empty()),
      m_savedTimer(this)
{
    SetIcon(wxICON(aaaa));
    SetBackgroundColour(g.bg);
    ApplyDwm(this, false, &g.bg);

    m_nav = new SectionNav(this);
    m_title = FluentText(this, wxString(), 20, g.text, true);
    m_pagesSizer = new wxBoxSizer(wxVERTICAL);

    RefreshEngine();
    BuildGeneral();
    BuildTyping();
    BuildHotkeys();
    BuildLayouts();
    BuildAdvanced();
    BuildAbout();

    m_nav->AddAction(kIconFolder, T("Папка программы"));
    m_nav->AddAction(kIconQuit, T("Закрыть FluentSwitcher"));
    m_nav->onSelect = [this](int section) { ShowSection(section); };
    m_nav->onAction = [this](int action) {
        if (action == 0)
        {
            wxLaunchDefaultApplication(m_folder);
            return;
        }
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

    // Bottom: a line for messages on the left, Save / Apply / Cancel on the right.
    wxBoxSizer* footer = new wxBoxSizer(wxHORIZONTAL);
    m_status = FluentText(this, wxString(), 9, g.text2);
    FluentButton* save = new FluentButton(this, wxID_OK, T("Сохранить"), true);
    m_apply = new FluentButton(this, wxID_APPLY, T("Сохранено"));
    m_apply->SetText(T("Применить")); // as wide as the longer of the two
    FluentButton* cancel = new FluentButton(this, wxID_CANCEL, T("Отмена"));
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
        if (Apply())
        {
            m_savedShown = true;
            m_apply->SetText(T("Сохранено"));
            m_savedTimer.StartOnce(3000);
        }
    });
    save->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        if (!m_canSave || !HasChanges() || Apply())
            Close();
    });
    cancel->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Close(); });
    Bind(wxEVT_CHAR_HOOK, [this](wxKeyEvent& e) {
        if (e.GetKeyCode() == WXK_ESCAPE && e.GetModifiers() == wxMOD_NONE)
            return (void)Close();
        e.Skip();
    });
    // Back from another window: the engine may have been switched on or off from the tray meanwhile.
    Bind(wxEVT_ACTIVATE, [this](wxActivateEvent& e) {
        if (e.GetActive())
            RefreshEngine();
        e.Skip();
    });

    wxBoxSizer* right = new wxBoxSizer(wxVERTICAL);
    right->Add(m_title, 0, wxTOP | wxBOTTOM, FromDIP(12));
    right->Add(m_pagesSizer, 1, wxEXPAND);
    right->Add(footer, 0, wxEXPAND | wxTOP | wxBOTTOM | wxRIGHT, FromDIP(16));
    wxBoxSizer* sizer = new wxBoxSizer(wxHORIZONTAL);
    sizer->Add(m_nav, 0, wxEXPAND | wxLEFT | wxTOP | wxBOTTOM, FromDIP(8));
    sizer->Add(right, 1, wxEXPAND | wxLEFT, FromDIP(20));
    SetSizer(sizer);

    if (!loadError.empty())
        SetStatus(T("Не удалось прочитать SimpleSwitcher.json: ") + loadError, true);
    ShowSection(section);
    SetSize(FromDIP(wxSize(860, 660)));
    SetMinSize(FromDIP(wxSize(720, 440)));
    CentreOnScreen();
}

// ---------------------------------------------------------------------------------------------
// Pages
// ---------------------------------------------------------------------------------------------

void SettingsFrame::Section(const char* icon, const wxString& title)
{
    m_page = NewSettingsPage(this, &m_column);
    m_pagesSizer->Add(m_page, 1, wxEXPAND);
    m_pages.push_back(m_page);
    m_titles.Add(title);
    m_nav->AddSection(icon, title);
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
    choice->onChange = [this, choice, picked] {
        picked(choice->GetSelection());
        Changed();
    };
    return choice;
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
                                return SetStatus(T("Не нашёл SimpleSwitcher.exe в папке программы"), true);
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
                    T("Программа стартует при входе в Windows, видно только флаг у часов"),
                    [&](wxWindow* card) { return m_autostartSwitch = new ToggleSwitch(card, m_autostart); });
    m_autostartSwitch->onChange = [this] {
        m_autostart = m_autostartSwitch->IsOn();
        Changed();
    };
    Toggle(T("Работать в программах, запущенных от имени администратора"),
           T("Для этого и сам FluentSwitcher нужно запускать от имени администратора. Автозапуск тогда идёт "
             "через планировщик заданий Windows"),
           "isMonitorAdmin", false);

    // The flag in the tray: the sets are the folders in "flags" next to the program.
    wxArrayString values, names;
    wxDir dir(m_folder + "\\flags");
    if (dir.IsOpened())
    {
        wxString name;
        for (bool more = dir.GetFirst(&name, wxEmptyString, wxDIR_DIRS); more; more = dir.GetNext(&name))
            values.Add(name);
    }
    values.Sort([](const wxString& a, const wxString& b) {
        return a == "Fluent" ? -1 : b == "Fluent" ? 1 : a.CmpNoCase(b);
    });
    for (const wxString& v : values)
        names.Add(v == "Fluent" ? T("Fluent – прямоугольные") : v == "Round" ? T("Круглые")
                  : v == "Square" ? T("Квадратные") : v);
    values.Add("Application Icon");
    names.Add(T("Значок программы вместо флага"));
    values.Add("Nothing");
    names.Add(T("Не показывать значок у часов"));
    const wxString flags = m_edit.GetString("flagsSet", "Fluent");
    if (values.Index(flags) == wxNOT_FOUND)
    {
        values.Add(flags);
        names.Add(flags);
    }
    Choice(T("Флаг у часов"), T("Показывает текущую раскладку"), names, values.Index(flags),
           [this, values](int i) { m_edit.SetString("flagsSet", values[i]); });

    const wxArrayString langValues = { "English", "Russian" };
    const wxArrayString langNames = { wxString("English"), T("Русский") };
    Choice(T("Язык меню у часов"), T("Меню по правому щелчку на флаге"), langNames,
           m_edit.GetString("gui_lang", "Russian") == "Russian" ? 1 : 0,
           [this, langValues](int i) { m_edit.SetString("gui_lang", langValues[i]); });
    FinishPage();
}

void SettingsFrame::BuildTyping()
{
    Section(kIconTyping, T("Набор текста"));

    // Saved as numbers (the engine's SeparateExtMode): Symbol 0, PossibleSymb_SeveralW 1, PossibleSymb_Always 2, Disabled 3.
    static const int modes[] = { 0, 3, 1, 2 };
    const wxArrayString names = { T("По пробелам и знакам препинания"), T("Только по пробелам"),
                                  T("По пробелам, знакам и «возможным знакам» – при исправлении нескольких слов"),
                                  T("По пробелам, знакам и «возможным знакам» – всегда") };
    const int mode = m_edit.GetInt("separate_ext_mode", 0);
    int selection = 0;
    for (int i = 0; i < 4; i++)
        if (modes[i] == mode)
            selection = i;
    Choice(T("Где кончается слово"),
           T("Что исправлять как последнее слово. Знаки в конце слова исправляются вместе с ним: «cnjg?» – «стоп,». "
             "«Возможный знак» – клавиша, которая в одной раскладке буква, а в другой знак, например б и ,"),
           names, selection, [this](int i) { m_edit.SetInt("separate_ext_mode", modes[i]); }, true);

    TextField* letters = nullptr;
    AddSettingsCard(m_page, m_column, T("Считать буквами"),
                    T("Эти знаки не разделяют слова: some_name, кто-то"), [&](wxWindow* card) {
                        return letters = new TextField(card, m_edit.GetString("treat_as_letters", "_-"), 120);
                    });
    letters->onChange = [this, letters] {
        m_edit.SetString("treat_as_letters", letters->Value());
        Changed();
    };

    const bool alternative = m_edit.GetBool("AlternativeLayoutChange", false);
    Choice(T("Как переключать раскладку"),
           T("Если в какой-то программе раскладка после исправления не переключается, выберите второй способ: "
             "FluentSwitcher нажмёт то сочетание, которым раскладка переключается в Windows"),
           { T("Обычный"), T("Нажимать сочетание Windows") }, alternative ? 1 : 0,
           [this](int i) { m_edit.SetBool("AlternativeLayoutChange", i == 1); });
    FinishPage();
}

void SettingsFrame::BuildHotkeys()
{
    Section(kIconHotkeys, T("Сочетания клавиш"));

    AddSettingsCard(m_page, m_column, T("Изменять сочетания – в следующей сборке"),
                    T("Пока здесь видно, что назначено. Поменять можно в старом окне: меню у часов → «Показать»"),
                    [](wxWindow*) { return nullptr; });
    auto shown = [](wxWindow* card, const wxString& stored) {
        HotkeyView* view = new HotkeyView(card);
        const wxString text = HotkeyDisplay(stored);
        view->SetText(text.empty() ? T("Нет") : text, text.empty());
        view->SetMinSize(wxSize(card->FromDIP(260), view->GetMinSize().y));
        return view;
    };
    for (const HotkeyAction& action : HotkeyActions())
    {
        const wxString stored = m_edit.GetHotkeys(action.key);
        AddSettingsCard(m_page, m_column, T(action.title), T(action.description),
                        [&](wxWindow* card) { return shown(card, stored); });
    }
    const wxString windows = m_edit.GetString("win_hotkey_cycle_lang", "LAlt + Shift");
    AddSettingsCard(m_page, m_column, T("Сочетание Windows для смены раскладки"),
                    T("FluentSwitcher нажимает его сам, когда выбрано «Нажимать сочетание Windows» (раздел «Набор текста»)"),
                    [&](wxWindow* card) { return shown(card, windows); });
    FinishPage();
}

void SettingsFrame::BuildLayouts()
{
    Section(kIconLayouts, T("Раскладки"));

    auto& layouts = m_edit.Json()["layouts_info"];
    if (!layouts.is_array() || layouts.empty())
    {
        AddSettingsCard(m_page, m_column, T("Раскладок пока нет"),
                        T("FluentSwitcher заполнит список раскладками Windows при запуске"),
                        [](wxWindow*) { return nullptr; });
        FinishPage();
        return;
    }
    for (size_t i = 0; i < layouts.size(); i++)
    {
        const auto& layout = layouts[i];
        const wxString hkl = layout.contains("layout") ? FromUtf8(layout["layout"].get<std::string>()) : wxString();
        const wxString own = layout.contains("hotkey") ? HotkeyDisplay(FromUtf8(layout["hotkey"].get<std::string>()))
                                                       : wxString();
        const wxString description = own.empty()
            ? T("Участвует в переключении и исправлении")
            : T("Своё сочетание: ") + own;
        ToggleSwitch* toggle = nullptr;
        AddSettingsCard(m_page, m_column, LayoutName(hkl), description, [&](wxWindow* card) {
            return toggle = new ToggleSwitch(card, layout.value("enabled", true));
        });
        toggle->onChange = [this, toggle, i] {
            m_edit.Json()["layouts_info"][i]["enabled"] = toggle->IsOn();
            Changed();
        };
    }
    FinishPage();
}

void SettingsFrame::BuildAdvanced()
{
    Section(kIconAdvanced, T("Дополнительно"));

    Toggle(T("Отключить залипание клавиш"),
           T("Пять нажатий Shift и другие сочетания специальных возможностей Windows не будут открывать их окна"),
           "disableAccessebility", false);
    Toggle(T("Убирать оформление при каждом копировании"),
           T("В буфере остаётся только простой текст. Не включайте вместе с FluentClipper: он хранит оформление"),
           "fClipboardClearFormat", false);
    Toggle(T("Не перехватывать клавиши, которые уходят на удалённый компьютер"),
           T("Для подключения к удалённому рабочему столу с этого компьютера"), "SkipLowLevelInjectKeys", false);
    Toggle(T("Ctrl + левый Alt – не правый Alt"),
           T("В раскладках с AltGr (немецкая, польская и др.) Windows путает эти сочетания. FluentSwitcher ненадолго "
             "переключает раскладку"),
           "fixRAlt", false);
    Toggle(T("Перепечатывать исправленное клавишами"),
           T("Старый способ. Обычно исправленное слово вставляется готовыми символами: так новый Блокнот Windows 11 "
             "не теряет Shift. Включите, если какая-то программа не принимает такую вставку"),
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
    Toggle(T("Британский флаг для английского"), T("Вместо американского"), "useBritishFlag", false);
    Toggle(T("Раскладки в меню у часов"), T("Щелчок по раскладке в меню переключает на неё"), "ShowLangsInTrayMenu", true);

    NumberField* quick = nullptr;
    AddSettingsCard(m_page, m_column, T("Интервал двойного нажатия, мс"),
                    T("Два нажатия быстрее этого считаются двойным – для сочетаний «дважды». Обычно 250–350"),
                    [&](wxWindow* card) { return quick = new NumberField(card, m_edit.GetInt("quick_press_ms", 280)); });
    quick->onChange = [this, quick] {
        const int value = quick->Value();
        if (value > 0 && value <= 1000)
        {
            m_edit.SetInt("quick_press_ms", value);
            Changed();
        }
    };
    FinishPage();
}

void SettingsFrame::BuildAbout()
{
    Section(kIconAbout, T("О программе"));

    AddSettingsCard(m_page, m_column, wxString::Format("FluentSwitcher %s", kVersion),
                    T("Исправляет текст, набранный не в той раскладке, и переключает раскладки"),
                    [](wxWindow*) { return nullptr; });
    AddSettingsCard(m_page, m_column, T("Основан на SimpleSwitcher"),
                    T("Автор оригинала – Aegel5. FluentSwitcher – изменённая версия: новые флаги и окно настроек, "
                      "исправлены Ctrl+Break, правый Ctrl, окно записи сочетаний и работа с буфером"),
                    [this](wxWindow* card) {
                        FluentButton* open = new FluentButton(card, wxID_ANY, T("Открыть на GitHub"));
                        open->Bind(wxEVT_BUTTON, [](wxCommandEvent&) {
                            wxLaunchDefaultBrowser("https://github.com/Aegel5/SimpleSwitcher");
                        });
                        return open;
                    });
    AddSettingsCard(m_page, m_column, T("Лицензия GPL-3.0"),
                    T("Программа бесплатная, исходный код открыт. Поставляется без каких-либо гарантий. "
                      "Оформление окна – из FluentClipper, лицензия MIT; иконки – Fluent UI System Icons (Microsoft, MIT)"),
                    [](wxWindow*) { return nullptr; });
    FinishPage();
}

void SettingsFrame::FinishPage()
{
    m_page->FitInside();
}

void SettingsFrame::ShowSection(int section)
{
    if (section < 0 || section >= (int)m_pages.size())
        section = 0;
    for (size_t i = 0; i < m_pages.size(); i++)
        m_pages[i]->Show((int)i == section);
    m_title->SetLabel(m_titles[section]);
    m_nav->Select(section);
    Layout();
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
    m_state = state;
    if (!enabledPending)
        m_enabled = (state & Engine::StateEnabled) != 0 || !running;
    if (!autostartPending)
        m_autostart = (state & Engine::StateAutostart) != 0;
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
                       m_autostart != ((m_state & Engine::StateAutostart) != 0));
}

void SettingsFrame::Changed()
{
    SetStatus(wxString(), false);
}

bool SettingsFrame::Apply()
{
    if (!m_canSave)
        return false;
    if (m_edit != m_saved)
    {
        wxString error;
        if (!m_edit.Save(&error))
        {
            SetStatus(T("Не удалось сохранить: ") + error, true);
            return false;
        }
        m_saved = m_edit;
    }
    m_engine = Engine::Find(m_folder, m_enginePid);
    if (!m_engine)
    {
        RefreshEngine();
        return true; // saved; the engine reads the file when it starts
    }
    Engine::ReloadConfig(m_engine);
    wxString problem;
    const long state = Engine::GetState(m_engine);
    if (m_autostart != ((state & Engine::StateAutostart) != 0) && !Engine::SetAutostart(m_engine, m_autostart))
        problem = T("Автозапуск не изменился: в режиме «от имени администратора» для этого нужны права администратора. ");
    if (m_enabled != ((state & Engine::StateEnabled) != 0) && !Engine::SetEnabled(m_engine, m_enabled))
        problem += m_edit.GetBool("isMonitorAdmin", false) && !(state & Engine::StateElevated)
            ? T("Не включился: запустите FluentSwitcher от имени администратора или выключите работу в программах "
                "администратора")
            : T("Не включился: включена другая копия программы");
    m_state = 0; // take the engine's state as it is now, pending nothing
    RefreshEngine();
    if (!problem.empty())
    {
        SetStatus(problem.Strip(wxString::trailing), true);
        return false;
    }
    return true;
}

void SettingsFrame::SetStatus(const wxString& text, bool warning)
{
    m_status->SetForegroundColour(warning ? WarningColour() : g.text2);
    m_status->SetLabel(text);
    m_status->Wrap(m_status->GetSize().x > 0 ? m_status->GetSize().x : FromDIP(360));
    Layout();
}
