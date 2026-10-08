#include "wordlist.h"

#include "i18n.h"
#include "icons.h"
#include "textfield.h"

#include <wx/dcbuffer.h>
#include <wx/dialog.h>
#include <wx/filedlg.h>
#include <wx/filename.h>
#include <wx/scrolwin.h>
#include <wx/tokenzr.h>

#include <algorithm>

#include "../../src/WordStart.h" // after wxWidgets: Windows headers of its own; the engine's lists, in this exe too

namespace
{
// A word in one layout: its text and the language of the layout ("ru-RU").
struct Form
{
    wxString text;
    std::wstring language;
};

std::wstring LanguageOf(HKL layout)
{
    wchar_t name[LOCALE_NAME_MAX_LENGTH] = {};
    return LCIDToLocaleName(MAKELCID(LOWORD((UINT_PTR)layout), SORT_DEFAULT), name, LOCALE_NAME_MAX_LENGTH, 0)
        ? std::wstring(name)
        : std::wstring();
}

// Lower case in every alphabet: wxString::Lower and IsSameAs(.., false) in this process change Latin letters only (no
// locale is set here), and "СМ" was another word than "см" - the engine, CharLowerW, has them as one.
wxString LowerAll(const wxString& s)
{
    std::wstring w = s.ToStdWstring();
    if (!w.empty())
        CharLowerBuffW(w.data(), (DWORD)w.size());
    return wxString(w);
}

bool SameText(const wxString& a, const wxString& b)
{
    return a.length() == b.length() && LowerAll(a) == LowerAll(b);
}

// The word and the same keys in the other layouts: "http" (en-US), "реез" (ru-RU). The first is the word itself, in the
// language of the first layout it can be typed in (every character a key, with Shift or without it, not with AltGr); then
// its text in each other layout - a key that is dead there or types more than one character gives none. Nowhere to
// type it - the word alone. The same key is the same place on the keyboard (the scan code), as the engine has it: in a
// German layout Z is where the Russian "н" is, not "я".
std::vector<Form> FormsOf(const wxString& word, const std::vector<HKL>& layouts)
{
    std::vector<Form> forms{ { word, std::wstring() } };
    for (HKL from : layouts)
    {
        std::vector<std::pair<UINT, bool>> keys; // the scan code and Shift
        for (wxUniChar c : word)
        {
            const SHORT key = VkKeyScanExW((WCHAR)c.GetValue(), from);
            const UINT scan = key == -1 ? 0 : MapVirtualKeyExW(LOBYTE(key), MAPVK_VK_TO_VSC, from);
            if (key == -1 || (HIBYTE(key) & 6) || !scan)
            {
                keys.clear();
                break;
            }
            keys.push_back({ scan, (HIBYTE(key) & 1) != 0 });
        }
        if (keys.empty())
            continue;
        if (forms[0].language.empty())
            forms[0].language = LanguageOf(from);
        for (HKL to : layouts)
        {
            if (to == from)
                continue;
            wxString text;
            for (const auto& [scan, shift] : keys)
            {
                BYTE state[256] = {};
                if (shift)
                    state[VK_SHIFT] = 0x80;
                wchar_t out[4] = {};
                const UINT vk = MapVirtualKeyExW(scan, MAPVK_VSC_TO_VK, to);
                // 4: the state of the keyboard is not changed (a dead key is not left pending)
                if (!vk || ToUnicodeEx(vk, scan, state, out, 4, 4, to) != 1)
                {
                    text.clear();
                    break;
                }
                text += out[0];
            }
            bool known = text.empty() || SameText(text, word);
            for (const Form& f : forms)
                known = known || SameText(f.text, text);
            if (!known)
                forms.push_back({ text, LanguageOf(to) });
        }
    }
    return forms;
}

wxArrayString OtherForms(const wxString& word, const std::vector<HKL>& layouts)
{
    wxArrayString texts;
    const std::vector<Form> forms = FormsOf(word, layouts);
    for (size_t i = 1; i < forms.size(); i++)
        texts.Add(forms[i].text);
    return texts;
}

bool IsLatin(const wxString& text)
{
    for (wxUniChar c : text)
        if (wxIsalpha(c))
            return c.GetValue() < 0x250; // Latin letters, with their accents
    return false;
}

// "Переключать всегда" keeps the word as it should be; one may type into the field either of the two - what is typed by
// mistake (ЬЩАшш, Дмитрий 07.10) or what it should become (MOFii). The one to keep: the one that is a word or the
// beginning of a word of its language (by the lists of the engine, WordStart: щас - not ofc, ок - not jr); when both or
// neither are - the Latin one (http, MOFii, the: the names and abbreviations a Russian keyboard mistypes); when both are
// Latin or neither - as typed. A wrong guess is one click on ⇄ of its row.
wxString TargetOf(const wxString& word, const std::vector<HKL>& layouts)
{
    const std::vector<Form> forms = FormsOf(word, layouts);
    if (forms.size() < 2)
        return word;
    std::vector<const Form*> known;
    for (const Form& f : forms)
        if (WordStart::Known(f.text.ToStdWstring(), f.language) == WordStart::Result::Yes)
            known.push_back(&f);
    if (known.size() == 1)
        return known[0]->text;
    for (const Form& f : forms)
        if (IsLatin(f.text) && !IsLatin(word))
            return f.text;
    return word;
}

wxString Quote(const wxString& s)
{
    return wxString::Format(T("«%s»"), s);
}

wxString Join(const wxArrayString& words)
{
    wxString s;
    for (const wxString& w : words)
        s += (s.empty() ? "" : ", ") + w;
    return s;
}


// The words, one to a row, drawn by the list itself (hundreds of words are not hundreds of windows); it is as tall as
// its rows, the box around it scrolls. The row under the mouse is lit and has its buttons at the end: the cross that
// removes the word, and in "Переключать всегда" ⇄, which turns the direction.
class WordRows : public wxPanel
{
public:
    struct Row
    {
        wxString word;
        wxString typed;   // "Переключать всегда": what is typed (реез), drawn before the word with an arrow
        wxString forms;   // "Не переключать": "и «см»", after the word
        wxString swapTip; // the tooltip of ⇄; empty - no ⇄
        bool learned = false;
        bool hit = false; // the word in the field
        int index = 0;    // in the dialog's words
    };

    std::function<void(int index)> onRemove, onSwap;

    explicit WordRows(wxWindow* parent) : wxPanel(parent)
    {
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        SetBackgroundColour(g.card);
        m_cross = FluentIcon(kIconDismiss, g.text, 12);
        m_swap = FluentIcon(kIconAutoSwitch, g.text, 14);
        Bind(wxEVT_PAINT, &WordRows::OnPaint, this);
        Bind(wxEVT_MOTION, [this](wxMouseEvent& e) { SetHover(e.GetPosition()); });
        Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent&) { SetHover(wxPoint(-1, -1)); });
        Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& e) {
            SetHover(e.GetPosition());
            if (m_hover < 0)
                return;
            const int index = m_rows[m_hover].index;
            if (m_button == Button::Cross && onRemove)
                onRemove(index);
            else if (m_button == Button::Swap && onSwap)
                onSwap(index);
        });
    }

    void SetRows(std::vector<Row> rows, const wxString& empty)
    {
        m_rows = std::move(rows);
        m_empty = empty;
        m_hover = -1;
        m_button = Button::None;
        UnsetToolTip();
        SetCursor(wxNullCursor);
        SetMinSize(wxSize(FromDIP(200), RowHeight() * wxMax(1, (int)m_rows.size())));
        Refresh();
    }

private:
    enum class Button { None, Cross, Swap };

    int RowHeight() const { return FromDIP(36); }
    wxRect RowRect(int i) const { return wxRect(0, i * RowHeight(), GetClientSize().x, RowHeight()); }
    wxRect ButtonRect(int i, int place) const // place 0 - the last one (the cross), 1 - before it
    {
        const wxRect r = RowRect(i);
        const int size = FromDIP(28);
        return wxRect(r.GetRight() - FromDIP(4) - size - place * (size + FromDIP(2)), r.y + (r.height - size) / 2, size,
                      size);
    }

    void SetHover(const wxPoint& p)
    {
        int hover = p.y >= 0 && p.x >= 0 ? p.y / RowHeight() : -1;
        if (hover >= (int)m_rows.size())
            hover = -1;
        Button button = Button::None;
        if (hover >= 0 && ButtonRect(hover, 0).Contains(p))
            button = Button::Cross;
        else if (hover >= 0 && !m_rows[hover].swapTip.empty() && ButtonRect(hover, 1).Contains(p))
            button = Button::Swap;
        if (hover == m_hover && button == m_button)
            return;
        if (m_hover >= 0)
            RefreshRect(RowRect(m_hover));
        if (hover >= 0)
            RefreshRect(RowRect(hover));
        m_hover = hover;
        m_button = button;
        SetCursor(button != Button::None ? wxCursor(wxCURSOR_HAND) : wxNullCursor);
        if (button == Button::Cross)
            SetToolTip(T("Убрать из списка"));
        else if (button == Button::Swap)
            SetToolTip(m_rows[hover].swapTip);
        else
            UnsetToolTip();
    }

    void DrawButton(wxDC& dc, const wxRect& r, const wxBitmapBundle& icon, bool lit)
    {
        if (lit)
            FillRound(dc, r, FromDIP(4), g.sel);
        const wxBitmap b = icon.GetBitmapFor(this);
        dc.DrawBitmap(b, r.x + (r.width - b.GetLogicalWidth()) / 2, r.y + (r.height - b.GetLogicalHeight()) / 2, true);
    }

    // Only the rows the update touches, straight on the window: a buffer for the whole list would be a picture metres
    // long, and text in a picture of its own came out at 96 dpi (smaller at 125 %).
    void OnPaint(wxPaintEvent&)
    {
        wxPaintDC dc(this);
        wxRect box = GetUpdateRegion().GetBox();
        box.Intersect(wxRect(GetClientSize()));
        if (box.IsEmpty())
            return;
        dc.SetBrush(wxBrush(g.card));
        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.DrawRectangle(box);
        const wxFont wordFont = UiFont(10), noteFont = UiFont(9);
        if (m_rows.empty())
        {
            dc.SetFont(noteFont);
            dc.SetTextForeground(g.text2);
            dc.DrawText(m_empty, FromDIP(12), (RowHeight() - dc.GetTextExtent(m_empty).y) / 2);
            return;
        }
        const int first = box.y / RowHeight(), last = wxMin((int)m_rows.size() - 1, box.GetBottom() / RowHeight());
        const wxColour line = Mix(g.border, g.card, 0.6);
        for (int i = first; i <= last; i++)
        {
            const Row& row = m_rows[i];
            const wxRect r = RowRect(i);
            if (i > 0)
            {
                dc.SetPen(wxPen(line));
                dc.DrawLine(r.x + FromDIP(8), r.y, r.GetRight() - FromDIP(8), r.y);
            }
            if (i == m_hover || row.hit)
                FillRound(dc, wxRect(r).Deflate(FromDIP(4), FromDIP(2)), FromDIP(4), row.hit ? g.sel : g.hover);
            // On the right: the buttons (of the row under the mouse), before them "выучено"; the texts - in what is left.
            int right = ButtonRect(i, row.swapTip.empty() ? 0 : 1).x - FromDIP(8);
            if (row.learned)
            {
                const wxString learned = T("выучено");
                dc.SetFont(noteFont);
                dc.SetTextForeground(g.accent);
                const wxSize size = dc.GetTextExtent(learned);
                dc.DrawText(learned, right - size.x, r.y + (r.height - size.y) / 2);
                right -= size.x + FromDIP(12);
            }
            dc.SetClippingRegion(wxRect(r.x, r.y, wxMax(0, right - r.x), r.height));
            int x = r.x + FromDIP(14);
            auto text = [&](const wxString& s, const wxFont& font, const wxColour& colour, int gap) {
                dc.SetFont(font);
                dc.SetTextForeground(colour);
                const wxSize size = dc.GetTextExtent(s);
                dc.DrawText(s, x, r.y + (r.height - size.y) / 2);
                x += size.x + gap;
            };
            if (!row.typed.empty())
            {
                text(row.typed, wordFont, g.text2, FromDIP(10));
                text(wxString::FromUTF8("→"), wordFont, g.text2, FromDIP(10)); // →
            }
            text(row.word, wordFont, g.text, FromDIP(10));
            if (!row.forms.empty())
                text(row.forms, noteFont, g.text2, 0);
            dc.DestroyClippingRegion();
            if (i == m_hover)
            {
                DrawButton(dc, ButtonRect(i, 0), m_cross, m_button == Button::Cross);
                if (!row.swapTip.empty())
                    DrawButton(dc, ButtonRect(i, 1), m_swap, m_button == Button::Swap);
            }
        }
    }

    std::vector<Row> m_rows;
    wxString m_empty;
    int m_hover = -1;
    Button m_button = Button::None;
    wxBitmapBundle m_cross, m_swap;
};

// The window of a list: what it is for, the field with "Добавить", the hint, the words in a box that scrolls, how many
// there are and "Готово" / "Отмена".
class WordListDialog : public wxDialog
{
public:
    WordListDialog(wxWindow* parent, WordKind kind, const wxString& title, const wxString& about,
                   const std::vector<HKL>& layouts, const WordListWords& list)
        : wxDialog(parent, wxID_ANY, title, wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
          m_kind(kind), m_layouts(layouts), m_words(list.words), m_learned(list.learned)
    {
        SetBackgroundColour(g.bg);
        ApplyDwm(this, false, &g.bg);
        const int pad = FromDIP(20);
        for (const wxString& w : m_words)
            m_forms.push_back(Forms(w));

        wxStaticText* text = FluentText(this, wxString(), 10, g.text);
        SetWrappedLabel(text, about, FromDIP(520));
        m_field = new TextField(this, wxString(), 400, true, 4096); // paths, pasted lists
        m_field->SetHint(m_kind == WordKind::Programs ? T("Добавить или найти приложение") : T("Добавить или найти слово"));
        m_field->onChange = [this] {
            m_done.clear();
            Refill();
        };
        m_field->onEnter = [this] { Add(); };
        FluentButton* add = new FluentButton(this, wxID_ANY, T("Добавить"));
        add->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            Add();
            m_field->SetFocus();
        });
        // Programs: one from a file too.
        FluentButton* pick = nullptr;
        if (m_kind == WordKind::Programs)
        {
            pick = new FluentButton(this, wxID_ANY, T("Выбрать…"));
            pick->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
                wxFileDialog dialog(this, T("Приложение"), wxString(), wxString(), T("Приложения (*.exe)|*.exe"),
                                    wxFD_OPEN | wxFD_FILE_MUST_EXIST);
                if (dialog.ShowModal() != wxID_OK)
                    return;
                m_field->Clear();
                AddText(wxFileName(dialog.GetPath()).GetFullName());
                m_field->SetFocus();
            });
        }
        m_hint = FluentText(this, wxString(), 9, g.text2);

        // The box of the words: drawn like the cards, a scrolled window inside it.
        wxPanel* box = new wxPanel(this);
        box->SetBackgroundStyle(wxBG_STYLE_PAINT);
        box->Bind(wxEVT_PAINT, [box](wxPaintEvent&) {
            wxAutoBufferedPaintDC dc(box);
            dc.SetBackground(wxBrush(g.bg));
            dc.Clear();
            FillRound(dc, wxRect(box->GetClientSize()), box->FromDIP(6), g.card, &g.border);
        });
        m_scroll = new wxScrolledWindow(box, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL);
        m_scroll->SetBackgroundColour(g.card);
        m_scroll->SetScrollRate(0, FromDIP(18));
        m_rows = new WordRows(m_scroll);
        m_rows->onRemove = [this](int index) { Remove(index); };
        m_rows->onSwap = [this](int index) { Swap(index); };
        wxBoxSizer* rows = new wxBoxSizer(wxVERTICAL);
        rows->Add(m_rows, 0, wxEXPAND);
        m_scroll->SetSizer(rows);
        wxBoxSizer* inside = new wxBoxSizer(wxVERTICAL);
        inside->Add(m_scroll, 1, wxEXPAND | wxALL, FromDIP(4));
        box->SetSizer(inside);
        box->SetMinSize(FromDIP(wxSize(520, 330)));

        m_count = FluentText(this, wxString(), 9, g.text2);
        wxBoxSizer* fieldRow = new wxBoxSizer(wxHORIZONTAL);
        fieldRow->Add(m_field, 1, wxALIGN_CENTER_VERTICAL);
        fieldRow->Add(add, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(8));
        if (pick)
            fieldRow->Add(pick, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(8));
        wxBoxSizer* buttons = new wxBoxSizer(wxHORIZONTAL);
        buttons->Add(m_count, 1, wxALIGN_CENTER_VERTICAL);
        buttons->Add(new FluentButton(this, wxID_OK, T("Готово"), true), 0, wxALIGN_CENTER_VERTICAL);
        buttons->Add(new FluentButton(this, wxID_CANCEL, T("Отмена")), 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(8));
        wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
        sizer->Add(text, 0, wxLEFT | wxRIGHT | wxTOP, pad);
        sizer->Add(fieldRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, pad);
        wxBoxSizer* hintRow = new wxBoxSizer(wxHORIZONTAL);
        hintRow->Add(m_hint, 0, wxLEFT | wxRIGHT, pad);
        sizer->Add(hintRow, 0, wxTOP, FromDIP(6));
        sizer->Add(box, 1, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, pad);
        sizer->Add(buttons, 0, wxEXPAND | wxALL, pad);
        SetSizer(sizer);

        // Esc - clears the field (TextField), in an empty one cancels; Enter in the field adds, Ctrl+Enter - done.
        Bind(wxEVT_CHAR_HOOK, [this](wxKeyEvent& e) {
            if (e.GetKeyCode() == WXK_ESCAPE)
                EndModal(wxID_CANCEL);
            else if ((e.GetKeyCode() == WXK_RETURN || e.GetKeyCode() == WXK_NUMPAD_ENTER) && e.ControlDown())
                EndModal(wxID_OK);
            else
                e.Skip();
        });
        Refill();
        Fit();
        SetMinSize(GetSize());
        CentreOnParent();
        m_field->SetFocus();
    }

    WordListWords Result() const { return { m_words, m_learned }; }

private:
    wxArrayString Forms(const wxString& word) const
    {
        return m_kind == WordKind::Caps || m_kind == WordKind::Programs ? wxArrayString() : OtherForms(word, m_layouts);
    }

    // The word of the list that `word` repeats (*covered false) or that has it as its other form or covers it (true);
    // -1 - none.
    int Find(const wxString& word, bool* covered) const
    {
        const bool caps = m_kind == WordKind::Caps;
        for (size_t i = 0; i < m_words.size(); i++)
            if (caps ? m_words[i] == word : SameText(m_words[i], word))
            {
                *covered = false;
                return (int)i;
            }
        for (size_t i = 0; i < m_words.size(); i++)
            if (caps ? m_words[i].length() >= 4 && word.StartsWith(m_words[i])
                     : std::any_of(m_forms[i].begin(), m_forms[i].end(), [&](const wxString& f) { return SameText(f, word); }))
            {
                *covered = true;
                return (int)i;
            }
        return -1;
    }

    // What a word of the field goes into the list as: in "Переключать всегда" - the form to get (TargetOf); a program -
    // in small letters, a name without an extension with ".exe" ("far" - "far.exe").
    wxString Kept(const wxString& word) const
    {
        if (m_kind == WordKind::Programs)
        {
            wxString name = LowerAll(word);
            if (!name.Contains("\\") && !name.Contains("/") && !name.Contains("."))
                name += ".exe";
            return name;
        }
        return m_kind == WordKind::Always ? TargetOf(word, m_layouts) : word;
    }

    // "«реез» станет «http»" of a word of "Переключать всегда" (as kept); no other form - empty.
    wxString Direction(const wxString& word, const wxArrayString& forms) const
    {
        if (forms.empty())
            return wxString();
        wxArrayString typed;
        for (const wxString& f : forms)
            typed.Add(Quote(f));
        return wxString::Format(T("набранное %s станет %s"), Join(typed), Quote(word));
    }

    // Enter or "Добавить": each word of the field (a pasted "a, b c" is three) that is not there yet.
    void Add() { AddText(m_field->Value()); }

    // Programs are separated by commas only: a path has spaces ("C:\\Program Files\\Far Manager\\Far.exe").
    void AddText(const wxString& text)
    {
        wxArrayString added, already;
        wxStringTokenizer words(text, m_kind == WordKind::Programs ? ",;\t\r\n" : " ,;\t\r\n", wxTOKEN_STRTOK);
        while (words.HasMoreTokens())
        {
            // "far.exe, code.exe": " code.exe" with its space never matched in the engine. And a path in quotes
            // (Explorer's "Copy as path": "C:\Program Files\Far Manager\Far.exe") - without them.
            wxString w = words.GetNextToken().Strip(wxString::both);
            if (m_kind == WordKind::Programs && w.length() >= 2 && (w[0] == '"' || w[0] == '\'') && w.Last() == w[0])
                w = w.Mid(1, w.length() - 2).Strip(wxString::both);
            if (w.empty())
                continue;
            bool covered = false;
            const wxString kept = Kept(w);
            if (Find(w, &covered) >= 0 || Find(kept, &covered) >= 0)
            {
                already.Add(w);
                continue;
            }
            m_words.Add(kept);
            m_forms.push_back(Forms(kept));
            added.Add(m_kind == WordKind::Always && !m_forms.back().empty()
                          ? Direction(kept, m_forms.back())
                          : kept);
        }
        if (added.empty() && already.empty())
            return;
        m_field->Clear(); // the whole list again
        if (added.empty())
            m_done = wxString::Format(T("Уже в списке: %s"), Join(already));
        else
            m_done = wxString::Format(T("Добавлено: %s"), Join(added)) +
                     (already.empty() ? wxString() : wxString::Format(T("; уже в списке: %s"), Join(already)));
        Refill();
    }

    void Remove(int index)
    {
        const wxString word = m_words[index];
        m_words.RemoveAt(index);
        m_forms.erase(m_forms.begin() + index);
        const int learned = m_learned.Index(word);
        if (learned != wxNOT_FOUND)
            m_learned.RemoveAt(learned);
        m_done = wxString::Format(T("Удалено: %s"), word);
        Refill();
    }

    // ⇄ of a word of "Переключать всегда": the list keeps its other form - what was typed becomes what is got.
    void Swap(int index)
    {
        if (m_forms[index].empty())
            return;
        const wxString word = m_words[index], other = m_forms[index][0];
        const int learned = m_learned.Index(word);
        if (learned != wxNOT_FOUND)
            m_learned.RemoveAt(learned); // the user's own now
        m_words[index] = other;
        m_forms[index] = Forms(other);
        m_done = wxString::Format(T("Теперь %s"), Direction(other, m_forms[index]));
        Refill();
    }

    void Refill()
    {
        const wxString query = m_field->Value().Strip(wxString::both);
        const wxString lower = LowerAll(query);
        bool covered = false;
        int hit = query.empty() ? -1 : Find(query, &covered);
        if (hit < 0 && !query.empty() && Kept(query) != query)
            hit = Find(Kept(query), &covered);
        std::vector<WordRows::Row> rows;
        for (int i = (int)m_words.size() - 1; i >= 0; i--) // the newest first
        {
            const wxString& w = m_words[i];
            bool match = query.empty() || i == hit || LowerAll(w).Contains(lower); // the word that covers it - too
            for (const wxString& f : m_forms[i])
                match = match || LowerAll(f).Contains(lower);
            if (!match)
                continue;
            WordRows::Row row;
            row.word = w;
            row.learned = m_learned.Index(w) != wxNOT_FOUND;
            row.hit = i == hit;
            row.index = i;
            if (m_kind == WordKind::Always && !m_forms[i].empty())
            {
                row.typed = Join(m_forms[i]);
                row.swapTip = wxString::Format(T("Наоборот: %s"), Direction(m_forms[i][0], Forms(m_forms[i][0])));
            }
            else if (m_kind == WordKind::Never && !m_forms[i].empty())
            {
                wxArrayString quoted;
                for (const wxString& f : m_forms[i])
                    quoted.Add(Quote(f));
                row.forms = wxString::Format(T("и %s"), Join(quoted));
            }
            rows.push_back(row);
        }
        m_rows->SetRows(std::move(rows), query.empty() ? T("Пока пусто") : T("Не найдено"));
        m_scroll->FitInside();
        m_scroll->Layout();

        wxString hint;
        if (!m_done.empty())
            hint = m_done;
        else if (query.empty())
            hint = m_kind == WordKind::Always
                ? T("Введите слово – как оно должно быть или как набирается по ошибке: Enter добавит его")
                : m_kind == WordKind::Programs ? T("Имя файла приложения, например far.exe, или путь к нему: Enter добавит")
                : T("Введите слово: Enter добавит его, а список покажет похожие");
        else if (hit >= 0 && (m_kind == WordKind::Programs ||
                              (!covered && (m_kind == WordKind::Caps ? m_words[hit] == query : SameText(m_words[hit], query)))))
            hint = T("Уже в списке");
        else if (m_kind == WordKind::Programs)
            hint = wxString::Format(T("Enter добавит %s"), Quote(Kept(query)));
        else if (hit >= 0 && m_kind == WordKind::Caps)
            hint = wxString::Format(T("Уже закрыто словом %s: оно закрывает и слова, которые с него начинаются"),
                                    Quote(m_words[hit]));
        else if (hit >= 0)
            hint = wxString::Format(T("%s – это %s в другой раскладке, уже в списке"), Quote(query), Quote(m_words[hit]));
        else if (m_kind == WordKind::Always && !Forms(Kept(query)).empty())
            hint = wxString::Format(T("Enter добавит: %s"), Direction(Kept(query), Forms(Kept(query))));
        else
            hint = wxString::Format(T("Enter добавит %s"), Quote(query));
        m_hint->SetLabel(hint);
        m_count->SetLabel(WordListCount(m_kind, m_words.size()));
        Layout();
    }

    WordKind m_kind;
    std::vector<HKL> m_layouts;
    wxArrayString m_words, m_learned;
    std::vector<wxArrayString> m_forms; // of each word: its text in the other layouts
    TextField* m_field = nullptr;
    wxStaticText* m_hint = nullptr;
    wxStaticText* m_count = nullptr;
    wxScrolledWindow* m_scroll = nullptr;
    WordRows* m_rows = nullptr;
    wxString m_done; // what the last add, removal or ⇄ did, for the hint until the field changes
};
}

wxString WordListCount(WordKind kind, size_t n)
{
    if (n == 0)
        return T("Пока пусто");
    return wxString::Format(kind == WordKind::Programs ? T("Приложений в списке: %zu") : T("Слов в списке: %zu"), n);
}

bool EditWordList(wxWindow* parent, WordKind kind, const wxString& title, const wxString& about,
                  const std::vector<HKL>& layouts, WordListWords* list)
{
    WordListDialog dialog(parent, kind, title, about, layouts, *list);
    if (dialog.ShowModal() != wxID_OK)
        return false;
    *list = dialog.Result();
    return true;
}
