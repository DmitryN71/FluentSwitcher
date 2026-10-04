#include "fs_version.h"
#include "pages.h"

#include "engine.h"
#include "hotkeys.h"
#include "icons.h"

#include <wx/datetime.h>
#include <wx/dcbuffer.h>
#include <wx/dir.h>
#include <wx/filedlg.h>
#include <wx/filename.h>
#include <wx/utils.h>

#include "../../src/Update.h" // after wxWidgets: Windows headers of its own

namespace
{
const char* const kVersion = FS_VERSION; // fs_version.h, made by CMake: the same as the program's

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
    void SetHint(const wxString& hint) { m_text->SetHint(hint); }

private:
    wxTextCtrl* m_text;
};

// A list of words in a window of its own, one per line (the exceptions of ДВе ЗАглавные). True - "Готово":
// *words is the new list, without empty lines and repeats.
bool EditWordList(wxWindow* parent, const wxString& title, const wxString& description, wxArrayString* words)
{
    wxDialog dialog(parent, wxID_ANY, title, wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
    dialog.SetBackgroundColour(g.bg);
    ApplyDwm(&dialog, false, &g.bg);
    const int pad = dialog.FromDIP(20);
    wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
    wxStaticText* label = new wxStaticText(&dialog, wxID_ANY, wxString());
    label->SetFont(UiFont(10));
    label->SetForegroundColour(g.text);
    SetWrappedLabel(label, description, dialog.FromDIP(380));
    sizer->Add(label, 0, wxLEFT | wxRIGHT | wxTOP, pad);

    // The box of the list: drawn like the kit's text boxes, a multi-line text control inside.
    wxPanel* box = new wxPanel(&dialog);
    box->SetBackgroundStyle(wxBG_STYLE_PAINT);
    box->SetMinSize(dialog.FromDIP(wxSize(380, 260)));
    wxTextCtrl* text = new wxTextCtrl(box, wxID_ANY, wxJoin(*words, '\n'), wxDefaultPosition, wxDefaultSize,
                                      wxTE_MULTILINE | wxBORDER_NONE);
    text->SetFont(UiFont(10));
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
    buttons->AddStretchSpacer();
    buttons->Add(new FluentButton(&dialog, wxID_OK, T("Готово"), true));
    buttons->Add(new FluentButton(&dialog, wxID_CANCEL, T("Отмена")), 0, wxLEFT, dialog.FromDIP(8));
    sizer->Add(buttons, 0, wxEXPAND | wxALL, pad);
    dialog.SetSizerAndFit(sizer);
    // Esc - cancel; Enter is a new line here, Ctrl+Enter - done.
    dialog.Bind(wxEVT_CHAR_HOOK, [&dialog](wxKeyEvent& e) {
        if (e.GetKeyCode() == WXK_ESCAPE)
            dialog.EndModal(wxID_CANCEL);
        else if ((e.GetKeyCode() == WXK_RETURN || e.GetKeyCode() == WXK_NUMPAD_ENTER) && e.ControlDown())
            dialog.EndModal(wxID_OK);
        else
            e.Skip();
    });
    dialog.CentreOnParent();
    text->SetFocus();
    text->SetInsertionPointEnd();
    if (dialog.ShowModal() != wxID_OK)
        return false;
    wxArrayString result;
    for (wxString w : wxSplit(text->GetValue(), '\n'))
    {
        w.Trim(true).Trim(false);
        if (!w.empty() && result.Index(w) == wxNOT_FOUND)
            result.Add(w);
    }
    *words = result;
    return true;
}

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

    HotkeyEditor::s_doubleMs = (unsigned long)m_edit.GetInt("quick_press_ms", 280);
    RefreshEngine();
    BuildGeneral();
    BuildTyping();
    BuildHotkeys();
    BuildLayouts();
    BuildFlags();
    BuildCommands();
    BuildAdvanced();
    BuildAbout();

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
        if (Apply() && m_restarted)
            Close();
        else if (!m_restarted && !HasChanges())
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
        else
            HotkeyEditor::CancelRecording(); // the keyboard hook must not outlive the window's turn
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
        SetStatus(T("Не удалось прочитать FluentSwitcher.json: ") + loadError, true);
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
                                return SetStatus(T("Не нашёл FluentSwitcher.exe в папке программы"), true);
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
           T("FluentSwitcher тогда работает с правами администратора: Windows спросит разрешения один раз, дальше "
             "он запускается через планировщик заданий без вопросов"),
           "isMonitorAdmin", false);

    // Each language by its own name, in either language of the window.
    const wxArrayString langValues = { "English", "Russian" };
    const wxArrayString langNames = { wxString("English"), wxString::FromUTF8("Русский") };
    Choice(T("Язык"), T("Этого окна и меню у флага. Окно откроется на новом языке после сохранения"), langNames,
           IsEnglish() ? 0 : 1, [this, langValues](int i) { m_edit.SetString("gui_lang", langValues[i]); });

    // "" - as Windows; main.cpp reads it when the window starts.
    const wxArrayString themeValues = { wxString(), wxString("Light"), wxString("Dark") };
    const wxArrayString themeNames = { T("Как в Windows"), T("Светлая"), T("Тёмная") };
    const int theme = themeValues.Index(m_edit.GetString("ui_theme", wxString()));
    Choice(T("Тема"), T("Этого окна. Оно откроется в новой теме после сохранения"), themeNames,
           theme == wxNOT_FOUND ? 0 : theme, [this, themeValues](int i) { m_edit.SetString("ui_theme", themeValues[i]); });
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

    // ДВе ЗАглавные (the engine's TwoCaps.h): two_caps, and the words to leave alone, two_caps_exceptions (an array
    // in the file, words with spaces between them here).
    Toggle(T("Исправлять ДВе ЗАглавные"),
           T("«ДВух» станет «Двух» после пробела, Enter или Tab. PCs, IDs, GHz, eM, iPhone и слова из исключений не трогаются. "
             "Исправилось зря – сразу нажмите «Исправить последнее слово» (Shift дважды): слово вернётся. Тот же "
             "перевод раскладки исправляет и ДВе ЗАглавные: LDe[ – Двух"),
           "two_caps", false);
    // The exceptions: in a window of their own, one per line; the card says how many.
    auto exceptionWords = [this] {
        wxArrayString words;
        const nlohmann::json& file = std::as_const(m_edit).Json();
        if (auto list = file.find("two_caps_exceptions"); list != file.end() && list->is_array())
            for (const auto& w : *list)
                if (w.is_string())
                    words.Add(wxString::FromUTF8(w.get<std::string>()));
        return words;
    };
    auto exceptionsText = [](const wxArrayString& words) {
        const wxString about = T("Слова, которые так и пишутся. Слово закрывает и те, что с него начинаются: "
                                 "ИПшник – и ИПшники. Само слово попадает сюда после третьей отмены");
        return about + "\n" + (words.empty() ? T("Пока пусто") : wxString::Format(T("Слов в списке: %zu"), words.size()));
    };
    wxStaticText* exceptionsLabel = AddSettingsCard(
        m_page, m_column, T("Исключения для ДВух ЗАглавных"), exceptionsText(exceptionWords()),
        [this, exceptionWords, exceptionsText](wxWindow* card) {
            FluentButton* edit = new FluentButton(card, wxID_ANY, T("Изменить…"));
            edit->Bind(wxEVT_BUTTON, [this, exceptionWords, exceptionsText](wxCommandEvent&) {
                wxArrayString words = exceptionWords();
                if (!EditWordList(this, T("Исключения для ДВух ЗАглавных"),
                                  T("По слову в строке. Слово закрывает и те, что с него начинаются"), &words))
                    return;
                nlohmann::json list = nlohmann::json::array();
                for (const wxString& w : words)
                    list.push_back(w.utf8_string());
                m_edit.Json()["two_caps_exceptions"] = list;
                Changed();
                SetCardDescription(m_twoCapsExceptions, exceptionsText(words));
            });
            return edit;
        });
    m_twoCapsExceptions = exceptionsLabel;

    const bool alternative = m_edit.GetBool("AlternativeLayoutChange", false);
    Choice(T("Как переключать раскладку"),
           T("Если в какой-то программе раскладка после исправления не переключается, выберите второй способ: "
             "FluentSwitcher нажмёт то сочетание, которым раскладка переключается в Windows"),
           { T("Обычный"), T("Нажимать сочетание Windows") }, alternative ? 1 : 0,
           [this](int i) { m_edit.SetBool("AlternativeLayoutChange", i == 1); });

    HotkeyEditor* windows = nullptr;
    AddSettingsCard(m_page, m_column, T("Сочетание, которым раскладка переключается в Windows"),
                    T("FluentSwitcher нажимает его сам при втором способе. Обычно Alt + Shift или Win + Пробел"),
                    [&](wxWindow* card) {
                        return windows = new HotkeyEditor(card, m_edit.GetString("win_hotkey_cycle_lang", "LAlt + Shift"),
                                                          1, true, true);
                    }, true);
    windows->onChange = [this, windows] {
        m_edit.SetString("win_hotkey_cycle_lang", windows->Value());
        Changed();
    };
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
        AddSettingsCard(m_page, m_column, T(action.title), T(action.description), [&](wxWindow* card) {
            return editor = new HotkeyEditor(card, m_edit.GetHotkeys(key), 2, sides->IsOn());
        }, true);
        editor->onChange = [this, editor, key] {
            m_edit.SetHotkeys(key, editor->Value());
            Changed();
        };
        editor->sameAs = [this, key](const wxString& one) { return SameHotkey(one, key); };
        editors.push_back(editor);
    }
    sides->onChange = [this, sides, editors, save = sides->onChange] {
        save(); // record_sides, and Save / Apply light up
        for (HotkeyEditor* editor : editors)
            editor->SetSides(sides->IsOn());
        if (sides->IsOn())
            SetStatus(T("Запишите нужное сочетание заново: теперь левые и правые клавиши различаются"), false);
    };
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

void SettingsFrame::BuildLayouts()
{
    Section(kIconLayouts, T("Раскладки"));

    // Sounds (the engine's LayoutSound.h): sound_switch, sound_fix - per cent, 0 - none.
    auto sound = [this](const wxString& title, const wxString& description, const char* key) {
        const std::vector<int> volumes = { 0, 30, 60, 100 };
        const wxArrayString names = { T("Нет"), T("Тихий"), T("Средний"), T("Громкий") };
        const int now = m_edit.GetInt(key, 0);
        int index = 0;
        for (size_t i = 0; i < volumes.size(); i++)
            if (std::abs(volumes[i] - now) < std::abs(volumes[index] - now))
                index = (int)i;
        Choice(title, description, names, index, [this, volumes, key](int i) { m_edit.SetInt(key, volumes[i]); });
    };
    sound(T("Звук при переключении раскладки"),
          T("Сочетанием FluentSwitcher или Windows, щелчком по флагу. Звук – switch.wav в папке sounds рядом с "
            "программой; положите туда en.wav, ru.wav – и у каждого языка будет свой"),
          "sound_switch");
    sound(T("Звук при исправлении текста"),
          T("Когда FluentSwitcher исправляет слово или выделенный текст. Звук – fix.wav в папке sounds"), "sound_fix");

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
        const wxString own = layout.contains("hotkey") ? FromUtf8(layout["hotkey"].get<std::string>()) : wxString();
        ToggleSwitch* toggle = nullptr;
        HotkeyEditor* editor = nullptr;
        // The switch "takes part" on the right of the title; the layout's own hotkeys below.
        AddSettingsCard(m_page, m_column, LayoutName(hkl),
                        T("Участвует в переключении и исправлении. Своё сочетание включает сразу эту раскладку, "
                          "например левый Ctrl – английскую, правый – русскую"),
                        [&](wxWindow* card) {
                            wxPanel* panel = new wxPanel(card);
                            panel->SetBackgroundColour(card->GetBackgroundColour());
                            editor = new HotkeyEditor(panel, own, 2, true);
                            toggle = new ToggleSwitch(panel, layout.value("enabled", true));
                            wxBoxSizer* row = new wxBoxSizer(wxHORIZONTAL);
                            row->Add(editor, 0, wxALIGN_TOP);
                            row->AddStretchSpacer();
                            row->Add(toggle, 0, wxALIGN_TOP | wxTOP, card->FromDIP(4));
                            panel->SetSizer(row);
                            return panel;
                        }, true);
        toggle->onChange = [this, toggle, i] {
            m_edit.Json()["layouts_info"][i]["enabled"] = toggle->IsOn();
            Changed();
        };
        editor->onChange = [this, editor, i] {
            m_edit.Json()["layouts_info"][i]["hotkey"] = ToUtf8(editor->Value());
            Changed();
        };
    }
    FinishPage();
}

void SettingsFrame::BuildFlags()
{
    Section(kIconFlags, T("Флажки"));

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
        return a == "Glossy" ? -1 : b == "Glossy" ? 1 : a.CmpNoCase(b);
    });
    for (const wxString& v : values)
        names.Add(v == "Glossy" ? T("Глянцевые") : v == "Round" ? T("Круглые")
                  : v == "Square" ? T("Квадратные") : v);
    // Letters instead of a flag (the engine's LetterIcons.h): two, or three as Windows itself writes them.
    values.Add("Letters");
    names.Add(T("Буквы: EN, RU"));
    values.Add("Letters3");
    names.Add(T("Буквы: ENG, RUS"));
    values.Add("Application Icon");
    names.Add(T("Значок программы вместо флага"));
    values.Add("Nothing");
    names.Add(T("Не показывать значок у часов"));
    // A set that is gone (the old "Fluent") shows as the glossy one: the engine does the same.
    wxString flags = m_edit.GetString("flagsSet", "Glossy");
    if (values.Index(flags) == wxNOT_FOUND && values.Index("Glossy") != wxNOT_FOUND)
        flags = "Glossy";
    if (values.Index(flags) == wxNOT_FOUND)
    {
        values.Add(flags);
        names.Add(flags);
    }
    Choice(T("Флаг у часов"), T("Показывает текущую раскладку"), names, values.Index(flags),
           [this, values](int i) { m_edit.SetString("flagsSet", values[i]); });
    Toggle(T("Британский флаг для английского"), T("Вместо американского"), "useBritishFlag", false);

    // Clicks on the flag by the clock (the engine's TrayIcon.h): tray_click, tray_double_click.
    const wxArrayString clickValues = { wxString(), wxString("menu"), wxString("next_layout"), wxString("toggle"),
                                        wxString("settings") };
    const wxArrayString clickNames = { T("Ничего"), T("Меню"), T("Следующая раскладка"), T("Включить / выключить"),
                                       T("Открыть настройки") };
    auto clickIndex = [this, &clickValues](const char* key, const char* byDefault) {
        const int i = clickValues.Index(m_edit.GetString(key, byDefault));
        return i == wxNOT_FOUND ? clickValues.Index(byDefault) : i;
    };
    Choice(T("Щелчок по флагу у часов"),
           T("«Следующая раскладка» – у окна, где вы печатали, и курсор остаётся там. Если назначен и двойной "
             "щелчок, одиночный срабатывает чуть позже: ждёт, не будет ли второго"),
           clickNames, clickIndex("tray_click", ""),
           [this, clickValues](int i) { m_edit.SetString("tray_click", clickValues[i]); });
    Choice(T("Двойной щелчок по флагу у часов"), T("Правый щелчок всегда открывает меню"), clickNames,
           clickIndex("tray_double_click", "settings"),
           [this, clickValues](int i) { m_edit.SetString("tray_double_click", clickValues[i]); });

    // The flag at the text cursor (the engine's CaretFlag.h). Each choice is a number in the file; a number
    // that is not in the list shows as the nearest one.
    auto numbers = [this](const wxString& title, const wxString& description, const char* key, int byDefault,
                          const std::vector<int>& numbers, const wxArrayString& itemNames) {
        const int now = m_edit.GetInt(key, byDefault);
        int at = 0;
        for (size_t i = 0; i < numbers.size(); i++)
            if (std::abs(numbers[i] - now) < std::abs(numbers[at] - now))
                at = int(i);
        Choice(title, description, itemNames, at,
               [this, key, numbers](int i) { m_edit.SetInt(key, numbers[i]); });
    };
    numbers(T("Флажок у текстового курсора"), T("Показывает раскладку там, где вы печатаете"), "caret_flag", 1,
            { 1, 2, 0 }, { T("Всегда"), T("Ненадолго"), T("Не показывать") });
    numbers(T("Сколько показывать «ненадолго»"), T("После смены раскладки, окна или поля ввода"), "caret_flag_brief_ms",
            2000, { 1000, 2000, 3000, 5000, 10000 }, { T("1 секунду"), T("2 секунды"), T("3 секунды"), T("5 секунд"),
            T("10 секунд") });
    numbers(T("Где флажок"), T("Если у края экрана места нет – с другой стороны строки"), "caret_flag_place", 0,
            { 0, 1 }, { T("Под курсором"), T("Над курсором") });
    numbers(T("Размер флажка у курсора"), T("При масштабе 100 %; на экранах с большим масштабом он крупнее"),
            "caret_flag_size", 20, { 16, 20, 24, 32 }, { T("Маленький"), T("Обычный"), T("Крупный"), T("Очень крупный") });
    numbers(T("Прозрачность флажка у курсора"), T("Чтобы не отвлекал от текста"), "caret_flag_opacity", 60,
            { 100, 80, 60, 40, 25, 15 }, { T("Нет"), T("Слабая"), T("Средняя"), T("Сильная"), T("Очень сильная"),
            T("Максимальная") });
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
                    T("Запустить программу или вставить текст. В тексте @@(…) нажимает клавиши: "
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
        wxString title = snippet ? T("Вставить текст") : T("Запустить программу");
        if (!cmd.empty())
            title += ": " + (snippet ? cmd.Left(40) : wxFileName(cmd).GetFullName());

        AddSettingsCard(page, column, title,
                        snippet ? T("Текст печатается туда, где курсор")
                                : T("Программа, документ или папка; путь можно вставить или выбрать"),
                        [&](wxWindow* card) {
            wxPanel* panel = new wxPanel(card);
            panel->SetBackgroundColour(card->GetBackgroundColour());
            wxBoxSizer* rows = new wxBoxSizer(wxVERTICAL);
            auto label = [panel](const char* text) { return FluentText(panel, T(text), 9, g.text2); };

            // What it does, on or off, remove.
            wxBoxSizer* top = new wxBoxSizer(wxHORIZONTAL);
            FluentChoice* kind = new FluentChoice(panel, { T("Запустить программу"), T("Вставить текст") }, snippet ? 1 : 0);
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
                    if (HasChanges() && !Apply()) // the engine runs what is saved
                        return;
                    if (!m_engine || !Engine::RunCommand(m_engine, (int)i))
                        SetStatus(T("FluentSwitcher не запущен: команду выполнить некому"), true);
                });
                top->Add(run, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(8));
            }
            top->Add(remove, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(8));
            rows->Add(top, 0, wxEXPAND);

            // The program and its arguments, or the text.
            TextField* what = new TextField(panel, cmd, snippet ? 488 : 370);
            what->SetHint(snippet ? T("Текст, например: С уважением, Дмитрий") : T("Путь к программе"));
            what->onChange = [this, what, i] {
                m_edit.Json()["run_programs"][i]["cmd"] = ToUtf8(what->Value());
                Changed();
            };
            rows->Add(label(snippet ? "Текст" : "Программа"), 0, wxTOP, FromDIP(10));
            wxBoxSizer* whatRow = new wxBoxSizer(wxHORIZONTAL);
            whatRow->Add(what, 0, wxALIGN_CENTER_VERTICAL);
            if (!snippet)
            {
                FluentButton* browse = new FluentButton(panel, wxID_ANY, T("Выбрать…"));
                browse->Bind(wxEVT_BUTTON, [this, i](wxCommandEvent&) {
                    wxFileDialog dialog(this, T("Программа для команды"), wxString(), wxString(),
                                        T("Программы (*.exe;*.bat;*.cmd;*.lnk)|*.exe;*.bat;*.cmd;*.lnk|Все файлы (*.*)|*.*"),
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
                TextField* args = new TextField(panel, FromUtf8(command.value("args", std::string())), 370);
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
                more->Add(label("Аргументы"));
                more->Add(label("Пауза, мс"));
                more->Add(args);
                more->Add(delay);
                rows->Add(more, 0, wxTOP, FromDIP(10));
            }

            // Its hotkeys, and running it now.
            rows->Add(label("Сочетание"), 0, wxTOP, FromDIP(10));
            wxBoxSizer* keysRow = new wxBoxSizer(wxHORIZONTAL);
            HotkeyEditor* keys = new HotkeyEditor(panel, FromUtf8(command.value("hotkey", std::string())), 2, false);
            keys->onChange = [this, keys, i] {
                m_edit.Json()["run_programs"][i]["hotkey"] = ToUtf8(keys->Value());
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

    Toggle(T("Отключить залипание клавиш"),
           T("Пять нажатий Shift и другие сочетания специальных возможностей Windows не будут открывать их окна"),
           "disableAccessebility", false);
    Toggle(T("Не перехватывать клавиши, которые уходят на удалённый компьютер"),
           T("Для подключения к удалённому рабочему столу с этого компьютера"), "SkipLowLevelInjectKeys", false);
    Toggle(T("Сочетания с Ctrl + Alt в раскладках с AltGr"),
           T("Windows принимает Ctrl + Alt за правый Alt (AltGr) и печатает символ вместо сочетания: в немецкой, "
             "польской раскладке, в русской – ₽ на Ctrl + Alt + 8. FluentSwitcher на миг переключает раскладку, и "
             "программа получает сочетание"),
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

    // Switched at once, as in the old window; not a setting that is saved.
    ToggleSwitch* log = nullptr;
    AddSettingsCard(m_page, m_column, T("Журнал отладки"),
                    T("Сразу и до выхода из FluentSwitcher каждое нажатие клавиш пишется в log\\FluentSwitcher.exe.log "
                      "в папке программы. Пароли при этом не вводите; после проверки выключите и удалите журнал"),
                    [&](wxWindow* card) { return log = new ToggleSwitch(card, (m_state & Engine::StateLogging) != 0); });
    log->onChange = [this, log] {
        if (!m_engine || !Engine::SetLogging(m_engine, log->IsOn()))
        {
            log->SetOn(false);
            SetStatus(T("FluentSwitcher не запущен: журнал вести некому"), true);
        }
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
    Section(kIconAbout, T("О программе"));

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
           T("Раз в день программа спрашивает у GitHub номер последней версии, больше ничего не отправляет. "
             "Скачивать и ставить новую – решаете вы"),
           "check_updates", true);
    // "Проверить сейчас"; once a newer version is known, "Скачать" opens its page; when GitHub could not be
    // reached, "Открыть страницу загрузки" leaves it to the browser.
    m_updateLabel = AddSettingsCard(m_page, m_column, T("Обновления"), UpdateText(Update::Load(m_folder.ToStdWstring())),
                                    [this](wxWindow* card) {
                                        FluentButton* button = new FluentButton(card, wxID_ANY, T("Проверить сейчас"));
                                        button->Bind(wxEVT_BUTTON, [this, button](wxCommandEvent&) { CheckUpdateNow(button); });
                                        if (!m_updatePage.empty())
                                            button->SetText(m_updatePage == Update::kReleasesPage ? T("Открыть страницу загрузки")
                                                                                                : T("Скачать"));
                                        return button;
                                    });
    AddSettingsCard(m_page, m_column, T("Основан на SimpleSwitcher"),
                    T("Автор оригинала – Aegel5. FluentSwitcher – изменённая версия: окно настроек и флаги в стиле "
                      "Windows 11, флажок у курсора, исправление с начала строки, запуск от администратора без "
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
                    T("Программа бесплатная, исходный код открыт. Поставляется без каких-либо гарантий. Части "
                      "других авторов – под своими лицензиями: wxWidgets, оформление FluentClipper, значки Fluent "
                      "UI System Icons (Microsoft), флаги GoSquared и другие"),
                    [this](wxWindow* card) {
                        FluentButton* open = new FluentButton(card, wxID_ANY, T("Лицензии"));
                        open->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
                            const wxString notices = m_folder + "\\THIRD-PARTY-NOTICES.txt";
                            if (!wxFileName::FileExists(notices))
                                return SetStatus(T("Рядом с программой нет файла THIRD-PARTY-NOTICES.txt"), true);
                            OpenAsUser(notices.ToStdWstring());
                        });
                        return open;
                    });
    FinishPage();
}

wxString SettingsFrame::UpdateText(const Update::State& state)
{
    m_updatePage.clear();
    if (state.failed)
    {
        m_updatePage = Update::kReleasesPage;
        return T("Не удалось связаться с GitHub. Страница загрузки откроется в браузере");
    }
    if (!state.checkedAt || state.latest.empty())
        return T("Ещё не проверялось");
    const wxString when = wxString::Format(T("Проверено: %s"),
                                           wxDateTime((time_t)(state.checkedAt / 1000)).Format("%d.%m.%Y, %H:%M"));
    const wxString dot = wxString::FromUTF8(" \xC2\xB7 ");
    if (Update::NewerKnown(state))
    {
        m_updatePage = state.page.empty() ? wxString(Update::kReleasesPage) : wxString(state.page);
        return wxString::Format(T("Вышла версия %s"), wxString::FromUTF8(state.latest)) + dot + when;
    }
    return T("У вас последняя версия") + dot + when;
}

void SettingsFrame::CheckUpdateNow(FluentButton* button)
{
    if (!m_updatePage.empty())
    {
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
        SetStatus(T("Не удалось записать update.json в папку программы"), true);
    // The answer also comes as a note by the flag, as in FluentClipper.
    if (HWND engine = Engine::Find(m_folder, m_enginePid))
        Engine::UpdateChecked(engine);
    SetCardDescription(m_updateLabel, UpdateText(state));
    button->SetText(m_updatePage.empty()                   ? T("Проверить сейчас")
                    : m_updatePage == Update::kReleasesPage ? T("Открыть страницу загрузки")
                                                            : T("Скачать"));
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
    for (size_t i = 0; i < m_pages.size(); i++)
        m_pages[i]->Show((int)i == section);
    m_title->SetLabel(m_titles[section]);
    m_nav->Select(section);
    m_section = section;
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
    const wxString oldLanguage = m_saved.GetString("gui_lang", wxString());
    const wxString oldTheme = m_saved.GetString("ui_theme", wxString());
    if (m_edit != m_saved)
    {
        wxString error;
        if (!m_edit.Save(&error))
        {
            SetStatus(T("Не удалось сохранить: ") + error, true);
            return false;
        }
        m_saved = m_edit;
        if (m_edit.GetString("gui_lang", wxString()) != oldLanguage ||
            m_edit.GetString("ui_theme", wxString()) != oldTheme)
        {
            // The texts and colours are read when the window is built: a new window in the new language or
            // theme, on this section.
            wxString command = wxString(GetCommandLineW());
            command += wxString::Format(" --section=%d --wait-pid=%lu", m_section, GetCurrentProcessId());
            STARTUPINFOW si = { sizeof(si) };
            PROCESS_INFORMATION pi = {};
            std::wstring line = command.ToStdWstring();
            if (CreateProcessW(nullptr, line.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi))
            {
                CloseHandle(pi.hThread);
                CloseHandle(pi.hProcess);
                m_restarted = true;
            }
        }
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
    if (m_edit.GetBool("isMonitorAdmin", false) && state && !(state & Engine::StateElevated) && !m_enginePid)
        return RestartElevated();
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

bool SettingsFrame::RestartElevated()
{
    if (!AskFluent(this,
                   T("Чтобы работать в программах, запущенных от имени администратора, FluentSwitcher перезапустится "
                     "с правами администратора. Windows спросит разрешения один раз: дальше программа запускается "
                     "через планировщик заданий, без вопросов"),
                   T("Перезапустить"), T("Не сейчас")))
    {
        RefreshEngine();
        SetStatus(T("Без прав администратора FluentSwitcher выключен: перезапустите его или выключите работу "
                    "в программах администратора"), true);
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
    m_status->SetLabel(text);
    m_status->Wrap(m_status->GetSize().x > 0 ? m_status->GetSize().x : FromDIP(360));
    Layout();
}
