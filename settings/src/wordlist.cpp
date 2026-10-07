#include "wordlist.h"

#include "i18n.h"
#include "icons.h"
#include "textfield.h"

#include <wx/dcbuffer.h>
#include <wx/graphics.h>
#include <wx/scrolwin.h>
#include <wx/tokenzr.h>

#include <memory>

namespace
{
const int kWrap = 480; // DIPs: the description of the card, as wide as under a control below it

// The same keys in the other layouts: "http" - "реез", "см" - "cv". Each layout where the word can be typed (every
// character a key, with Shift or without it, not with AltGr) gives its text in each other layout; a key that is dead
// there or types more than one character - no form from that layout.
wxArrayString OtherForms(const wxString& word, const std::vector<HKL>& layouts)
{
    wxArrayString forms;
    for (HKL from : layouts)
    {
        std::vector<std::pair<UINT, bool>> keys; // the key and Shift
        for (wxUniChar c : word)
        {
            const SHORT key = VkKeyScanExW((WCHAR)c.GetValue(), from);
            if (key == -1 || (HIBYTE(key) & 6))
            {
                keys.clear();
                break;
            }
            keys.push_back({ LOBYTE(key), (HIBYTE(key) & 1) != 0 });
        }
        if (keys.empty())
            continue;
        for (HKL to : layouts)
        {
            if (to == from)
                continue;
            wxString text;
            for (const auto& [vk, shift] : keys)
            {
                BYTE state[256] = {};
                if (shift)
                    state[VK_SHIFT] = 0x80;
                wchar_t out[4] = {};
                // 4: the state of the keyboard is not changed (a dead key is not left pending)
                if (ToUnicodeEx(vk, MapVirtualKeyExW(vk, MAPVK_VK_TO_VSC, to), state, out, 4, 4, to) != 1)
                {
                    text.clear();
                    break;
                }
                text += out[0];
            }
            if (!text.empty() && !text.IsSameAs(word, false) && forms.Index(text, false) == wxNOT_FOUND)
                forms.Add(text);
        }
    }
    return forms;
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

// The button that opens and closes the card: a chevron, down when it is closed. Space or Enter too.
class Chevron : public wxPanel
{
public:
    std::function<void()> onClick;
    bool open = false;

    explicit Chevron(wxWindow* parent) : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxWANTS_CHARS)
    {
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        SetMinSize(FromDIP(wxSize(32, 32)));
        SetCursor(wxCursor(wxCURSOR_HAND));
        Bind(wxEVT_PAINT, [this](wxPaintEvent&) {
            wxAutoBufferedPaintDC dc(this);
            dc.SetBackground(wxBrush(GetParent()->GetBackgroundColour()));
            dc.Clear();
            const wxRect r(GetClientSize());
            if (m_hover)
                FillRound(dc, r, FromDIP(4), g.hover);
            std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::CreateFromUnknownDC(dc));
            if (gc)
            {
                const double cx = r.width / 2.0, cy = r.height / 2.0, s = FromDIP(5), d = open ? -s / 2 : s / 2;
                gc->SetPen(wxPen(g.text, wxMax(1, FromDIP(1))));
                wxPoint2DDouble points[] = { { cx - s, cy - d }, { cx, cy + d }, { cx + s, cy - d } };
                gc->StrokeLines(3, points);
            }
            if (ShowsFocus(this))
                DrawFocusRing(this, dc, r, FromDIP(4));
        });
        Bind(wxEVT_ENTER_WINDOW, [this](wxMouseEvent&) { m_hover = true; Refresh(); });
        Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent&) { m_hover = false; Refresh(); });
        Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent&) { if (onClick) onClick(); });
        Bind(wxEVT_KEY_DOWN, [this](wxKeyEvent& e) {
            const int key = e.GetKeyCode();
            if (key == WXK_SPACE || key == WXK_RETURN || key == WXK_NUMPAD_ENTER)
            {
                if (onClick)
                    onClick();
            }
            else if (!HandleAsNavigationKey(e))
                e.Skip();
        });
        Bind(wxEVT_SET_FOCUS, [this](wxFocusEvent& e) { Refresh(); e.Skip(); });
        Bind(wxEVT_KILL_FOCUS, [this](wxFocusEvent& e) { Refresh(); e.Skip(); });
    }

    bool AcceptsFocus() const override { return true; }

private:
    bool m_hover = false;
};
}

// The words of the card, one to a row, drawn by itself (a list of hundreds of words is not hundreds of windows). The row
// under the mouse is lit and has the cross at its end.
class WordRows : public wxPanel
{
public:
    struct Row
    {
        wxString word, forms; // forms: "и «см»", "набрано как «реез»"
        bool learned = false;
        bool hit = false;     // the word in the field
        int index = 0;        // in the card's words
    };

    std::function<void(int index)> onRemove;

    explicit WordRows(wxWindow* parent) : wxPanel(parent)
    {
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        SetBackgroundColour(g.card);
        m_cross = FluentIcon(kIconDismiss, g.text, 12);
        Bind(wxEVT_PAINT, &WordRows::OnPaint, this);
        Bind(wxEVT_MOTION, [this](wxMouseEvent& e) { SetHover(e.GetPosition()); });
        Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent&) { SetHover(wxPoint(-1, -1)); });
        Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& e) {
            SetHover(e.GetPosition());
            if (m_onCross && m_hover >= 0 && onRemove)
                onRemove(m_rows[m_hover].index);
        });
    }

    void SetRows(std::vector<Row> rows, const wxString& empty)
    {
        m_rows = std::move(rows);
        m_empty = empty;
        m_hover = -1;
        m_onCross = false;
        UnsetToolTip();
        SetCursor(wxNullCursor);
        InvalidateBestSize();
        Refresh();
    }

protected:
    wxSize DoGetBestSize() const override
    {
        return wxSize(FromDIP(200), RowHeight() * wxMax(1, (int)m_rows.size()));
    }

private:
    int RowHeight() const { return FromDIP(34); }
    wxRect RowRect(int i) const { return wxRect(0, i * RowHeight(), GetClientSize().x, RowHeight()); }
    wxRect CrossRect(int i) const
    {
        const wxRect r = RowRect(i);
        const int size = FromDIP(28);
        return wxRect(r.GetRight() - FromDIP(4) - size, r.y + (r.height - size) / 2, size, size);
    }

    void SetHover(const wxPoint& p)
    {
        int hover = p.y >= 0 && p.x >= 0 ? p.y / RowHeight() : -1;
        if (hover >= (int)m_rows.size())
            hover = -1;
        const bool cross = hover >= 0 && CrossRect(hover).Contains(p);
        if (hover == m_hover && cross == m_onCross)
            return;
        if (m_hover >= 0)
            RefreshRect(RowRect(m_hover));
        if (hover >= 0)
            RefreshRect(RowRect(hover));
        m_hover = hover;
        if (cross != m_onCross)
        {
            m_onCross = cross;
            SetCursor(cross ? wxCursor(wxCURSOR_HAND) : wxNullCursor);
            if (cross)
                SetToolTip(T("Убрать из списка"));
            else
                UnsetToolTip();
        }
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
            dc.DrawText(m_empty, wxPoint(FromDIP(12), (RowHeight() - dc.GetTextExtent(m_empty).y) / 2));
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
                FillRound(dc, wxRect(r).Deflate(0, FromDIP(2)), FromDIP(4), row.hit ? g.sel : g.hover);
            // On the right: the cross (under the mouse), before it "выучено"; the word and its forms - in what is left.
            int right = r.GetRight() - FromDIP(40);
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
            int x = r.x + FromDIP(12);
            dc.SetFont(wordFont);
            dc.SetTextForeground(g.text);
            const wxSize size = dc.GetTextExtent(row.word);
            dc.DrawText(row.word, x, r.y + (r.height - size.y) / 2);
            x += size.x + FromDIP(10);
            if (!row.forms.empty())
            {
                dc.SetFont(noteFont);
                dc.SetTextForeground(g.text2);
                dc.DrawText(row.forms, x, r.y + (r.height - dc.GetTextExtent(row.forms).y) / 2);
            }
            dc.DestroyClippingRegion();
            if (i == m_hover)
            {
                const wxRect c = CrossRect(i);
                if (m_onCross)
                    FillRound(dc, c, FromDIP(4), g.sel);
                const wxBitmap cross = m_cross.GetBitmapFor(this);
                dc.DrawBitmap(cross, c.x + (c.width - cross.GetLogicalWidth()) / 2,
                              c.y + (c.height - cross.GetLogicalHeight()) / 2, true);
            }
        }
    }

    std::vector<Row> m_rows;
    wxString m_empty;
    int m_hover = -1;
    bool m_onCross = false;
    wxBitmapBundle m_cross;
};

WordListCard::WordListCard(wxWindow* page, Kind kind, const wxString& title, const wxString& about,
                           const std::vector<HKL>& layouts)
    : Card(page), m_kind(kind), m_layouts(layouts), m_about(about)
{
    const int pad = FromDIP(14);
    wxStaticText* name = FluentText(this, title, 10, g.text); // the first child: CardTip puts the details on it
    m_count = FluentText(this, about, 9, g.text2);
    m_count->Wrap(FromDIP(kWrap));
    Chevron* chevron = new Chevron(this);
    chevron->onClick = [this] { Toggle(); };
    m_chevron = chevron;
    // The title and the description open and close it too, as the whole head of an expandable setting does.
    for (wxWindow* head : { (wxWindow*)name, (wxWindow*)m_count })
    {
        head->SetCursor(wxCursor(wxCURSOR_HAND));
        head->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent&) { Toggle(); });
    }
    wxBoxSizer* texts = new wxBoxSizer(wxVERTICAL);
    texts->Add(name);
    texts->Add(m_count, 0, wxTOP, FromDIP(2));
    wxBoxSizer* head = new wxBoxSizer(wxHORIZONTAL);
    head->Add(texts, 1, wxALIGN_CENTER_VERTICAL);
    head->Add(chevron, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(16));

    // Open: a line under the head, the field with "Добавить", the hint, the words.
    m_body = new wxPanel(this);
    m_body->SetBackgroundColour(g.card);
    wxPanel* line = new wxPanel(m_body, wxID_ANY, wxDefaultPosition, wxSize(-1, 1));
    line->SetBackgroundColour(g.border);
    m_field = new TextField(m_body, wxString(), 320, true);
    m_field->SetHint(T("Добавить или найти слово"));
    m_field->onChange = [this] {
        m_done.clear();
        Refill();
    };
    m_field->onEnter = [this] { Add(); };
    FluentButton* add = new FluentButton(m_body, wxID_ANY, T("Добавить"));
    add->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        Add();
        m_field->SetFocus();
    });
    m_hint = FluentText(m_body, wxString(), 9, g.text2);
    m_rows = new WordRows(m_body);
    m_rows->onRemove = [this](int index) { Remove(index); };
    wxBoxSizer* fieldRow = new wxBoxSizer(wxHORIZONTAL);
    fieldRow->Add(m_field, 0, wxALIGN_CENTER_VERTICAL);
    fieldRow->Add(add, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(8));
    wxBoxSizer* body = new wxBoxSizer(wxVERTICAL);
    body->Add(line, 0, wxEXPAND);
    body->Add(fieldRow, 0, wxLEFT | wxRIGHT | wxTOP, pad - 1);
    body->Add(m_hint, 0, wxLEFT | wxRIGHT | wxTOP, pad - 1);
    body->Add(m_rows, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(6));
    m_body->SetSizer(body);
    m_body->Hide();

    // The open part 1 px in from the sides (the card's border) and above the rounded corners at the bottom; closed, the
    // sizer leaves out the part and its margin.
    wxBoxSizer* inset = new wxBoxSizer(wxVERTICAL);
    inset->Add(m_body, 0, wxEXPAND | wxLEFT | wxRIGHT, 1);
    wxBoxSizer* outer = new wxBoxSizer(wxVERTICAL);
    outer->Add(head, 0, wxEXPAND | wxALL, pad);
    outer->Add(inset, 0, wxEXPAND | wxBOTTOM, FromDIP(6));
    SetSizer(outer);
}

void WordListCard::SetWords(const wxArrayString& words, const wxArrayString& learned)
{
    m_words = words;
    m_learned = learned;
    m_forms.clear();
    for (const wxString& w : m_words)
        m_forms.push_back(m_kind == Caps ? wxArrayString() : OtherForms(w, m_layouts));
    Refill();
}

void WordListCard::Toggle()
{
    m_open = !m_open;
    static_cast<Chevron*>(m_chevron)->open = m_open;
    m_chevron->Refresh();
    m_body->Show(m_open);
    Relayout();
    if (m_open)
        m_field->SetFocus(); // to type at once
    else
        m_chevron->SetFocus(); // not left in the hidden field
}

int WordListCard::Find(const wxString& word, bool* covered) const
{
    for (size_t i = 0; i < m_words.size(); i++)
        if (m_kind == Caps ? m_words[i] == word : m_words[i].IsSameAs(word, false))
        {
            *covered = false;
            return (int)i;
        }
    for (size_t i = 0; i < m_words.size(); i++)
        if (m_kind == Caps ? m_words[i].length() >= 4 && word.StartsWith(m_words[i])
                           : m_forms[i].Index(word, false) != wxNOT_FOUND)
        {
            *covered = true;
            return (int)i;
        }
    return -1;
}

// Enter or "Добавить": each word of the field (a pasted "a, b c" is three) that is not there yet.
void WordListCard::Add()
{
    wxArrayString added, already;
    wxStringTokenizer words(m_field->Value(), " ,;\t\r\n", wxTOKEN_STRTOK);
    while (words.HasMoreTokens())
    {
        const wxString w = words.GetNextToken();
        bool covered = false;
        if (Find(w, &covered) >= 0 || added.Index(w, m_kind == Caps) != wxNOT_FOUND)
        {
            already.Add(w);
            continue;
        }
        m_words.Add(w);
        m_forms.push_back(m_kind == Caps ? wxArrayString() : OtherForms(w, m_layouts));
        added.Add(w);
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
    if (!added.empty() && onChange)
        onChange();
}

void WordListCard::Remove(int index)
{
    const wxString word = m_words[index];
    m_words.RemoveAt(index);
    m_forms.erase(m_forms.begin() + index);
    const int learned = m_learned.Index(word);
    if (learned != wxNOT_FOUND)
        m_learned.RemoveAt(learned);
    m_done = wxString::Format(T("Удалено: %s"), word);
    Refill();
    if (onChange)
        onChange();
}

void WordListCard::Refill()
{
    const wxString query = m_field->Value().Strip(wxString::both);
    const wxString lower = query.Lower();
    bool covered = false;
    const int hit = query.empty() ? -1 : Find(query, &covered);
    std::vector<WordRows::Row> rows;
    for (int i = (int)m_words.size() - 1; i >= 0; i--) // the newest first
    {
        const wxString& w = m_words[i];
        bool match = query.empty() || i == hit || w.Lower().Contains(lower); // the word that covers it - too
        for (const wxString& f : m_forms[i])
            match = match || f.Lower().Contains(lower);
        if (!match)
            continue;
        wxString forms;
        if (!m_forms[i].empty())
        {
            wxArrayString quoted;
            for (const wxString& f : m_forms[i])
                quoted.Add(Quote(f));
            forms = wxString::Format(m_kind == Never ? T("и %s") : T("набрано как %s"), Join(quoted));
        }
        rows.push_back({ w, forms, m_learned.Index(w) != wxNOT_FOUND, i == hit, i });
    }
    m_rows->SetRows(std::move(rows), query.empty() ? T("Пока пусто") : T("Не найдено"));

    wxString hint;
    if (!m_done.empty())
        hint = m_done;
    else if (query.empty())
        hint = T("Введите слово: Enter добавит его, а список покажет похожие");
    else if (hit >= 0 && !covered)
        hint = T("Уже в списке");
    else if (hit >= 0 && m_kind == Caps)
        hint = wxString::Format(T("Уже закрыто словом %s: оно закрывает и слова, которые с него начинаются"),
                                Quote(m_words[hit]));
    else if (hit >= 0)
        hint = wxString::Format(T("%s – это %s в другой раскладке, уже в списке"), Quote(query), Quote(m_words[hit]));
    else
        hint = wxString::Format(T("Enter добавит %s"), Quote(query));
    m_hint->SetLabel(hint);

    SetWrappedLabel(m_count, m_about + "\n" +
                                 (m_words.empty() ? T("Пока пусто") : wxString::Format(T("Слов в списке: %zu"), m_words.size())),
                    FromDIP(kWrap));
    Relayout();
}

void WordListCard::Relayout()
{
    InvalidateBestSize();
    wxWindow* page = GetParent();
    page->Layout();
    Layout();
    m_body->Layout();
    if (wxScrolledWindow* scrolled = dynamic_cast<wxScrolledWindow*>(page))
        scrolled->FitInside();
}
