#include "hotkeys.h"

#include "i18n.h"

#include <wx/tokenzr.h>

const std::vector<HotkeyAction>& HotkeyActions()
{
    static const std::vector<HotkeyAction> actions = {
        { "hk_RevertLastWord", N_("Исправить последнее слово"),
          N_("Набранное не в той раскладке: стирает последнее слово, печатает его в другой раскладке и переключает её") },
        { "hk_RevertLine", N_("Исправить текст с начала строки"),
          N_("Выделяет от курсора до начала строки (Shift+Home) и исправляет, как выделенный текст") },
        { "hk_RevertSeveralWords", N_("Исправить несколько слов"),
          N_("Каждое следующее нажатие захватывает ещё одно слово назад. Способ SimpleSwitcher, в новом Блокноте "
          "путает текст – лучше «с начала строки»") },
        { "hk_RevertAllRecentText", N_("Исправить весь недавний текст"),
          N_("Всё, что набрано подряд в этом окне: до Enter, стрелок или смены окна") },
        { "hk_RevertSelelected", N_("Исправить выделенный текст"),
          N_("Выделенное в любой программе печатается в другой раскладке") },
        { "hk_toUpperSelected", N_("Выделенное ПРОПИСНЫМИ / строчными"),
          N_("Если выделенное уже прописными – строчными") },
        { "hk_InvertCaseSelected", N_("Выделенное иНВЕРСИЕЙ рЕГИСТРА"), N_("Для текста, набранного с нажатым CapsLock") },
        { "hk_CycleSwitchLayout", N_("Следующая раскладка"), N_("Переключает раскладку без исправления текста") },
        { "hk_EmulateCapsLock", N_("Нажать CapsLock"), N_("Если CapsLock занят под сочетание, включить его можно так") },
        { "hk_ToggleEnabled", N_("Включить / выключить FluentSwitcher"), N_("Работает и когда программа выключена") },
        { "hk_ShowMainWindow", N_("Открыть настройки"), N_("Это окно") },
    };
    return actions;
}

wxString HotkeyDisplay(const wxString& stored)
{
    wxString shown;
    wxStringTokenizer parts(stored, ",");
    while (parts.HasMoreTokens())
    {
        wxString one = parts.GetNextToken().Strip(wxString::both);
        if (one.empty())
            continue;
        wxString suffix;
        if (one.Replace("#double", "") > 0)
            suffix = T(" дважды");
        if (one.Replace("#up", "") > 0)
            suffix = T(", при отпускании");
        one = one.Strip(wxString::both) + suffix;
        if (!shown.empty())
            shown += T("   или   ");
        shown += one;
    }
    return shown;
}

std::vector<wxString> SplitHotkeys(const wxString& stored)
{
    std::vector<wxString> list;
    wxStringTokenizer parts(stored, ",");
    while (parts.HasMoreTokens())
    {
        wxString one = parts.GetNextToken().Strip(wxString::both);
        if (!one.empty())
            list.push_back(one);
    }
    return list;
}

// ---------------------------------------------------------------------------------------------
// HotkeyEditor
// ---------------------------------------------------------------------------------------------

HotkeyEditor* HotkeyEditor::s_recording = nullptr;
HHOOK HotkeyEditor::s_hook = nullptr;
unsigned long HotkeyEditor::s_doubleMs = 280;

namespace
{
const int kMaxRecordMs = 10000; // no keys for this long: the recording gives up (the hook holds all keys)

wxString Placeholder(int slot)
{
    return slot == 0 ? T("Не назначено") : T("Ещё одно сочетание");
}
}

HotkeyEditor::HotkeyEditor(wxWindow* parent, const wxString& stored, int slots, bool sides, bool plain)
    : wxPanel(parent), m_sides(sides), m_plain(plain), m_timer(this)
{
    SetBackgroundColour(parent->GetBackgroundColour());
    m_values = SplitHotkeys(stored);
    m_values.resize(slots);

    wxArrayString notes;
    // The longest notes it shows, for the width of the line.
    notes.Add(T("Нажмите сочетание или дважды одну клавишу. Esc – отмена"));
    notes.Add(T("Так же назначено: «") + T("Исправить текст с начала строки") + T("»"));
    int noteWidth = 0;
    m_note = NoteLine(this, notes, &noteWidth);

    wxBoxSizer* row = new wxBoxSizer(wxHORIZONTAL);
    for (int slot = 0; slot < slots; slot++)
    {
        HotkeyView* view = new HotkeyView(this);
        view->SetMinSize(wxSize(FromDIP(240), view->GetMinSize().y));
        view->onRecord = [this, slot] { Record(slot); };
        view->onClear = [this, slot] {
            if (m_recordingSlot >= 0)
                Stop();
            m_values[slot].clear();
            ShowSlot(slot);
            SetNoteLine(m_note, wxString(), true);
            if (onChange)
                onChange();
        };
        view->Bind(wxEVT_KILL_FOCUS, [this, slot](wxFocusEvent& e) {
            if (m_recordingSlot == slot)
                Stop();
            e.Skip();
        });
        m_views.push_back(view);
        row->Add(view, 0, slot ? wxLEFT : 0, FromDIP(8));
        ShowSlot(slot);
    }
    wxBoxSizer* column = new wxBoxSizer(wxVERTICAL);
    column->Add(row);
    column->Add(m_note, 0, wxTOP, FromDIP(4));
    SetSizer(column);

    Bind(wxEVT_TIMER, [this](wxTimerEvent&) {
        if (m_recordingSlot < 0)
            return;
        const unsigned long now = GetTickCount();
        m_recorder.Tick(now);
        if (m_recorder.Done())
            Finish(m_recorder.Result());
        else if (now - m_recordingSince > kMaxRecordMs)
            Stop();
    }, m_timer.GetId());
}

HotkeyEditor::~HotkeyEditor()
{
    if (s_recording == this)
        Stop();
}

wxString HotkeyEditor::Value() const
{
    wxString stored;
    for (const wxString& one : m_values)
    {
        if (one.empty())
            continue;
        if (!stored.empty())
            stored += ", ";
        stored += one;
    }
    return stored;
}

void HotkeyEditor::ShowSlot(int slot)
{
    HotkeyView* view = m_views[slot];
    const wxString& value = m_values[slot];
    if (m_recordingSlot == slot)
    {
        view->SetText(T("Нажмите сочетание…"), true);
        view->SetClearable(false);
        return;
    }
    view->SetText(value.empty() ? Placeholder(slot) : HotkeyDisplay(value), value.empty());
    view->SetClearable(!value.empty(), T("Убрать сочетание"));
}

void HotkeyEditor::Record(int slot)
{
    CancelRecording();
    m_recordingSlot = slot;
    m_recordingSince = GetTickCount();
    m_recorder.Start(m_sides, s_doubleMs);
    s_recording = this;
    if (!s_hook)
        s_hook = SetWindowsHookExW(WH_KEYBOARD_LL, HookProc, GetModuleHandleW(nullptr), 0);
    m_views[slot]->SetFocus();
    m_views[slot]->SetRecording(true);
    ShowSlot(slot);
    SetNoteLine(m_note, T("Нажмите сочетание или дважды одну клавишу. Esc – отмена"), true);
    m_timer.Start(30);
}

void HotkeyEditor::Stop()
{
    const int slot = m_recordingSlot;
    m_recordingSlot = -1;
    m_timer.Stop();
    if (s_recording == this)
    {
        s_recording = nullptr;
        if (s_hook)
        {
            UnhookWindowsHookEx(s_hook);
            s_hook = nullptr;
        }
    }
    if (slot >= 0)
    {
        m_views[slot]->SetRecording(false);
        ShowSlot(slot);
        SetNoteLine(m_note, wxString(), true);
    }
}

void HotkeyEditor::Finish(const std::string& result)
{
    const int slot = m_recordingSlot;
    Stop();
    if (slot < 0 || result.empty())
        return;
    wxString one = wxString::FromUTF8(result.c_str());
    if (m_plain) // keys to send: "LAlt + Shift", not "... #up"
    {
        one.Replace(" #up", "");
        one.Replace(" #double", "");
    }
    m_values[slot] = one;
    ShowSlot(slot);
    const wxString note = sameAs ? sameAs(one) : wxString();
    SetNoteLine(m_note, note, true);
    if (onChange)
        onChange();
}

void HotkeyEditor::CancelRecording()
{
    if (s_recording)
        s_recording->Stop();
}

LRESULT CALLBACK HotkeyEditor::HookProc(int code, WPARAM wParam, LPARAM lParam)
{
    // The hook runs on this window's thread (it was set from it): as short as can be, or Windows drops it.
    if (code != HC_ACTION || !s_recording)
        return CallNextHookEx(nullptr, code, wParam, lParam);
    const KBDLLHOOKSTRUCT* k = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lParam);
    if (k->flags & LLKHF_INJECTED)
        return CallNextHookEx(nullptr, code, wParam, lParam); // typed by a program, not by a person
    const bool down = wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN;
    if (k->scanCode == 0x21D)
        return 1; // the made-up LCtrl that AltGr layouts add to the right Alt
    if (k->vkCode == VK_ESCAPE)
    {
        if (down)
        {
            HotkeyEditor* editor = s_recording;
            editor->CallAfter([editor] { editor->Stop(); });
        }
        return 1;
    }
    s_recording->m_recordingSince = k->time;
    s_recording->m_recorder.Key(k->vkCode, down, k->time);
    return 1; // neither the engine nor Windows sees the keys being recorded
}
