// Copyright (c) 2026 Dmitry Novikov
// SPDX-License-Identifier: MIT -- see LICENSE next to this file.
#include "fluent_controls.h"

#include <wx/dcbuffer.h>

#include <algorithm>

namespace
{
// Fluent UI System Icons (Microsoft, MIT licence): dismiss, 20 px regular.
const char* const kDismiss =
    "m4.09 4.22.06-.07a.5.5 0 0 1 .63-.06l.07.06L10 9.29l5.15-5.14a.5.5 0 0 1 .63-.06l.07.06c.18.17.2.44.06.63l-.06.07L10.71 "
    "10l5.14 5.15c.18.17.2.44.06.63l-.06.07a.5.5 0 0 1-.63.06l-.07-.06L10 10.71l-5.15 5.14a.5.5 0 0 1-.63.06l-.07-.06a.5.5 0 0 "
    "1-.06-.63l.06-.07L9.29 10 4.15 4.85a.5.5 0 0 1-.06-.63l.06-.07z";

// The width a card's description wraps to: beside its control, or above one that is below it.
const int kDescription = 300, kDescriptionBelow = 480; // DIPs
}

wxStaticText* FluentText(wxWindow* parent, const wxString& s, int points, const wxColour& colour, bool bold)
{
    wxStaticText* t = new wxStaticText(parent, wxID_ANY, s);
    t->SetFont(UiFont(points, bold));
    t->SetForegroundColour(colour);
    return t;
}

wxColour WarningColour()
{
    return g.dark ? wxColour(0xff, 0x99, 0xa4) : wxColour(0xc4, 0x2b, 0x1c);
}

// ---------------------------------------------------------------------------------------------
// ToggleSwitch
// ---------------------------------------------------------------------------------------------
ToggleSwitch::ToggleSwitch(wxWindow* parent, bool on) : wxPanel(parent), m_on(on)
{
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetMinSize(FromDIP(wxSize(44, 24)));
    SetCursor(wxCursor(wxCURSOR_HAND));
    Bind(wxEVT_PAINT, &ToggleSwitch::OnPaint, this);
    Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent&) {
        SetFocus();
        Toggle();
    });
    Bind(wxEVT_SET_FOCUS, [this](wxFocusEvent& e) { Refresh(); e.Skip(); }); // the focus ring
    Bind(wxEVT_KILL_FOCUS, [this](wxFocusEvent& e) { Refresh(); e.Skip(); });
    Bind(wxEVT_KEY_DOWN, [this](wxKeyEvent& e) {
        if (e.GetKeyCode() == WXK_SPACE)
            Toggle();
        else
            e.Skip();
    });
}

void ToggleSwitch::SetOn(bool on)
{
    if (on != m_on)
    {
        m_on = on;
        Refresh();
    }
}

void ToggleSwitch::Toggle()
{
    m_on = !m_on;
    Refresh();
    if (onChange)
        onChange();
}

void ToggleSwitch::OnPaint(wxPaintEvent&)
{
    wxAutoBufferedPaintDC dc(this);
    const wxColour back = GetParent()->GetBackgroundColour();
    dc.SetBackground(wxBrush(back));
    dc.Clear();
    const wxSize size = GetClientSize();
    const int w = FromDIP(40), h = FromDIP(20);
    const wxRect track((size.x - w) / 2, (size.y - h) / 2, w, h);
    if (ShowsFocus(this)) // around the track, in the margin left for it
        DrawFocusRing(this, dc, wxRect(track).Inflate(FromDIP(2)), h / 2.0 + FromDIP(2));
    if (m_on)
        FillRound(dc, track, h / 2.0, g.accent);
    else
        FillRound(dc, track, h / 2.0, back, &g.text2);
    const int d = FromDIP(m_on ? 12 : 10);
    const int cx = m_on ? track.GetRight() - FromDIP(9) : track.x + FromDIP(10);
    const int cy = track.y + h / 2;
    const wxColour knob = m_on ? (g.dark ? wxColour(0, 0, 0) : wxColour(255, 255, 255)) : g.text2;
    FillRound(dc, wxRect(cx - d / 2, cy - d / 2, d, d), d / 2.0, knob);
}

// ---------------------------------------------------------------------------------------------
// NumberField
// ---------------------------------------------------------------------------------------------
NumberField::NumberField(wxWindow* parent, int value) : wxPanel(parent)
{
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetMinSize(FromDIP(wxSize(64, 32)));
    m_text = new wxTextCtrl(this, wxID_ANY, wxString::Format("%d", value), wxDefaultPosition, wxDefaultSize,
                            wxBORDER_NONE);
    m_text->SetFont(UiFont(10));
    m_text->SetBackgroundColour(g.input);
    m_text->SetForegroundColour(g.text);
    m_text->SetMaxLength(4);
    m_text->Bind(wxEVT_CHAR, [](wxKeyEvent& e) {
        const wxChar ch = e.GetUnicodeKey();
        if (ch == WXK_NONE || ch < WXK_SPACE || (ch >= '0' && ch <= '9')) // keys, Backspace, Ctrl+V, digits
            e.Skip();
    });
    m_text->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { if (onChange) onChange(); });
    m_text->Bind(wxEVT_SET_FOCUS, [this](wxFocusEvent& e) { Refresh(); e.Skip(); });
    m_text->Bind(wxEVT_KILL_FOCUS, [this](wxFocusEvent& e) { Refresh(); e.Skip(); });
    Bind(wxEVT_PAINT, &NumberField::OnPaint, this);
    Bind(wxEVT_SIZE, [this](wxSizeEvent& e) { LayoutText(); e.Skip(); });
    Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent&) { m_text->SetFocus(); });
}

int NumberField::Value() const
{
    long v = 0;
    return m_text->GetValue().ToLong(&v) && v > 0 ? (int)v : 0;
}

void NumberField::LayoutText()
{
    const wxSize size = GetClientSize();
    const int h = m_text->GetBestSize().y, pad = FromDIP(10);
    m_text->SetSize(pad, (size.y - h) / 2, wxMax(size.x - 2 * pad, FromDIP(20)), h);
}

void NumberField::OnPaint(wxPaintEvent&)
{
    wxAutoBufferedPaintDC dc(this);
    dc.SetBackground(wxBrush(GetParent()->GetBackgroundColour()));
    dc.Clear();
    const wxRect box(GetClientSize());
    const bool focus = FindFocus() == m_text;
    const wxColour idle = Mix(g.text2, g.input, 0.5);
    FillInput(dc, box, FromDIP(4), g.input, focus ? &g.accent : &idle, focus ? FromDIP(2) : 1);
}

// ---------------------------------------------------------------------------------------------
// HotkeyView and the line under it
// ---------------------------------------------------------------------------------------------
HotkeyView::HotkeyView(wxWindow* parent) : wxPanel(parent)
{
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetMinSize(FromDIP(wxSize(180, 32)));
    SetCursor(wxCursor(wxCURSOR_HAND));
    m_font = UiFont(10);
    m_cross = FluentIcon(kDismiss, g.text2, 12);
    Bind(wxEVT_PAINT, &HotkeyView::OnPaint, this);
    Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& e) {
        if (m_clearable && CrossRect().Contains(e.GetPosition()))
        {
            if (onClear)
                onClear();
            return;
        }
        if (onRecord)
            onRecord();
    });
    Bind(wxEVT_MOTION, [this](wxMouseEvent& e) { SetCrossHover(m_clearable && CrossRect().Contains(e.GetPosition())); });
    Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent&) { SetCrossHover(false); });
    Bind(wxEVT_SET_FOCUS, [this](wxFocusEvent& e) { Refresh(); e.Skip(); }); // the focus ring
    Bind(wxEVT_KILL_FOCUS, [this](wxFocusEvent& e) { Refresh(); e.Skip(); });
}

void HotkeyView::SetText(const wxString& text, bool grey)
{
    if (text != m_text || grey != m_grey)
    {
        m_text = text;
        m_grey = grey;
        Refresh();
    }
}

void HotkeyView::SetRecording(bool on)
{
    if (on != m_recording)
    {
        m_recording = on;
        Refresh();
    }
}

void HotkeyView::SetClearable(bool on, const wxString& tooltip)
{
    m_crossTip = tooltip;
    if (on == m_clearable)
        return;
    m_clearable = on;
    if (!on)
        SetCrossHover(false);
    Refresh();
}

wxRect HotkeyView::CrossRect() const
{
    const wxSize size = GetClientSize();
    const int w = FromDIP(28);
    return wxRect(size.x - w, 0, w, size.y);
}

void HotkeyView::SetCrossHover(bool on)
{
    if (on == m_crossHover)
        return;
    m_crossHover = on;
    SetToolTip(on ? m_crossTip : wxString());
    Refresh();
}

void HotkeyView::OnPaint(wxPaintEvent&)
{
    wxAutoBufferedPaintDC dc(this);
    dc.SetBackground(wxBrush(GetParent()->GetBackgroundColour()));
    dc.Clear();
    wxRect box(GetClientSize());
    if (m_recording) // waiting for a combination: the whole field framed in the accent colour
    {
        FillRound(dc, box, FromDIP(4), g.accent);
        FillRound(dc, wxRect(box).Deflate(FromDIP(2)), FromDIP(3), g.input);
    }
    else
        FillInput(dc, box, FromDIP(4), g.input);
    if (!m_recording && ShowsFocus(this)) // recording has its accent frame instead
        DrawFocusRing(this, dc, box, FromDIP(4));
    dc.SetFont(m_font);
    dc.SetTextForeground(m_grey ? g.text2 : g.text);
    const wxSize ts = dc.GetTextExtent(m_text);
    dc.DrawText(m_text, box.x + FromDIP(10), box.y + (box.height - ts.y) / 2);
    if (m_clearable)
    {
        const wxRect r = CrossRect().Deflate(FromDIP(4));
        if (m_crossHover)
            FillRound(dc, r, FromDIP(4), g.hover);
        const wxBitmap cross = m_cross.GetBitmapFor(this);
        dc.DrawBitmap(cross, r.x + (r.width - cross.GetLogicalWidth()) / 2, r.y + (r.height - cross.GetLogicalHeight()) / 2, true);
    }
}

wxStaticText* NoteLine(wxWindow* parent, const wxArrayString& notes, int* width)
{
    wxStaticText* t = new wxStaticText(parent, wxID_ANY, wxString(), wxDefaultPosition, wxDefaultSize, wxST_NO_AUTORESIZE);
    t->SetFont(UiFont(9));
    t->SetForegroundColour(WarningColour());
    wxClientDC dc(t);
    dc.SetFont(t->GetFont());
    int widest = 0;
    for (const wxString& note : notes)
        widest = std::max(widest, dc.GetTextExtent(note).x);
    *width = std::min(widest + parent->FromDIP(4), parent->FromDIP(360));
    t->SetMinSize(wxSize(*width, dc.GetCharHeight()));
    return t;
}

void SetNoteLine(wxStaticText* line, const wxString& text, bool hint)
{
    line->SetForegroundColour(hint ? g.text2 : WarningColour());
    wxClientDC dc(line);
    dc.SetFont(line->GetFont());
    const int width = std::max(line->GetSize().x, line->GetMinSize().x);
    wxString shown = wxControl::Ellipsize(text, dc, wxELLIPSIZE_END, width);
    line->SetToolTip(shown == text ? wxString() : text);
    shown.Replace("&", "&&");
    line->SetLabel(shown);
    line->Refresh();
}

// ---------------------------------------------------------------------------------------------
// Cards and pages
// ---------------------------------------------------------------------------------------------
// Painted whole on every resize: a card grown under a longer note must not keep its old bottom
// edge drawn across the middle.
Card::Card(wxWindow* parent)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL | wxFULL_REPAINT_ON_RESIZE)
{
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetBackgroundColour(g.card);
    Bind(wxEVT_PAINT, [this](wxPaintEvent&) {
        wxAutoBufferedPaintDC dc(this);
        dc.SetBackground(wxBrush(GetParent()->GetBackgroundColour()));
        dc.Clear();
        FillRound(dc, wxRect(GetClientSize()), FromDIP(6), g.card, &g.border);
    });
}

wxScrolledWindow* NewSettingsPage(wxWindow* parent, wxBoxSizer** column)
{
    wxScrolledWindow* page = new wxScrolledWindow(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL);
    page->SetBackgroundColour(g.bg);
    page->SetScrollRate(0, parent->FromDIP(16));
    *column = new wxBoxSizer(wxVERTICAL);
    wxBoxSizer* pageSizer = new wxBoxSizer(wxVERTICAL);
    pageSizer->Add(*column, 1, wxEXPAND | wxRIGHT | wxBOTTOM, parent->FromDIP(16));
    page->SetSizer(pageSizer);
    return page;
}

wxStaticText* AddSettingsCard(wxWindow* page, wxSizer* column, const wxString& title, const wxString& description,
                              const std::function<wxWindow*(wxWindow* card)>& make, bool controlBelow)
{
    const int pad = page->FromDIP(14);
    Card* card = new Card(page);
    wxBoxSizer* texts = new wxBoxSizer(wxVERTICAL);
    texts->Add(FluentText(card, title, 10, g.text));
    wxStaticText* d = FluentText(card, description, 9, g.text2);
    d->Wrap(page->FromDIP(controlBelow ? kDescriptionBelow : kDescription));
    texts->Add(d, 0, wxTOP, page->FromDIP(2));
    wxWindow* control = make(card);

    wxBoxSizer* outer = new wxBoxSizer(wxVERTICAL);
    if (!control)
        outer->Add(texts, 0, wxEXPAND | wxALL, pad);
    else if (controlBelow)
    {
        outer->Add(texts, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, pad);
        outer->Add(control, 0, wxEXPAND | wxALL, pad);
    }
    else
    {
        wxBoxSizer* row = new wxBoxSizer(wxHORIZONTAL);
        row->Add(texts, 1, wxALIGN_CENTER_VERTICAL);
        row->Add(control, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, page->FromDIP(16));
        outer->Add(row, 0, wxEXPAND | wxALL, pad);
    }
    card->SetSizer(outer);
    column->Add(card, 0, wxEXPAND | wxBOTTOM, page->FromDIP(4));
    return d;
}

void SetCardDescription(wxStaticText* label, const wxString& text)
{
    SetWrappedLabel(label, text, label->FromDIP(kDescription));
}

// ---------------------------------------------------------------------------------------------
// SectionNav
// ---------------------------------------------------------------------------------------------
SectionNav::SectionNav(wxWindow* parent)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxWANTS_CHARS | wxFULL_REPAINT_ON_RESIZE)
{
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetBackgroundColour(g.bg);
    m_font = UiFont(10);
    Bind(wxEVT_PAINT, &SectionNav::OnPaint, this);
    Bind(wxEVT_MOTION, [this](wxMouseEvent& e) { SetHover(HitTest(e.GetPosition())); });
    Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent&) { SetHover(-1); });
    Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& e) {
        const int hit = HitTest(e.GetPosition());
        if (hit >= 0 && !m_items[hit].action)
            SetFocus();
        if (hit >= 0)
            Activate(hit);
    });
    Bind(wxEVT_KEY_DOWN, [this](wxKeyEvent& e) {
        const int key = e.GetKeyCode();
        if ((key == WXK_UP || key == WXK_DOWN) && m_sections > 0)
        {
            SetKeyboardCues(true);
            Activate((m_selected + (key == WXK_UP ? m_sections - 1 : 1)) % m_sections);
        }
        else if (!HandleAsNavigationKey(e)) // Tab: on to the cards (it takes every key itself)
            e.Skip();
    });
    Bind(wxEVT_SET_FOCUS, [this](wxFocusEvent& e) { Refresh(); e.Skip(); });
    Bind(wxEVT_KILL_FOCUS, [this](wxFocusEvent& e) { Refresh(); e.Skip(); });
}

void SectionNav::AddSection(const char* icon, const wxString& label)
{
    m_items.insert(m_items.begin() + m_sections, Item{ FluentIcon(icon, g.text), label, false });
    m_sections++;
    InvalidateBestSize();
}

void SectionNav::AddAction(const char* icon, const wxString& label)
{
    m_items.push_back(Item{ FluentIcon(icon, g.text), label, true });
    InvalidateBestSize();
}

void SectionNav::Select(int section)
{
    m_selected = section;
    Refresh();
}

wxSize SectionNav::DoGetBestSize() const
{
    wxClientDC dc(const_cast<SectionNav*>(this));
    dc.SetFont(m_font);
    int text = 0;
    for (const Item& item : m_items)
        text = wxMax(text, dc.GetTextExtent(item.label).x);
    return wxSize(wxMax(FromDIP(200), FromDIP(kInset + 12 + 16 + 12 + 16) + text),
                  FromDIP(kTop + kItem * (int)m_items.size() + kGap + kTop));
}

// Sections from the top; actions at the bottom edge.
wxRect SectionNav::ItemRect(int i) const
{
    const wxSize size = GetClientSize();
    const int actions = (int)m_items.size() - m_sections;
    const int y = i < m_sections ? FromDIP(kTop) + i * FromDIP(kItem)
                                 : size.y - FromDIP(kTop) - (actions - (i - m_sections)) * FromDIP(kItem);
    return wxRect(FromDIP(kInset), y + FromDIP(2), size.x - 2 * FromDIP(kInset), FromDIP(kItem) - FromDIP(4));
}

int SectionNav::HitTest(const wxPoint& p) const
{
    for (int i = 0; i < (int)m_items.size(); i++)
        if (ItemRect(i).Contains(p))
            return i;
    return -1;
}

void SectionNav::SetHover(int i)
{
    if (i != m_hover)
    {
        m_hover = i;
        SetCursor(i >= 0 ? wxCursor(wxCURSOR_HAND) : wxNullCursor);
        Refresh();
    }
}

void SectionNav::Activate(int i)
{
    if (m_items[i].action)
    {
        if (onAction)
            onAction(i - m_sections);
        return;
    }
    Select(i);
    if (onSelect)
        onSelect(i);
}

void SectionNav::OnPaint(wxPaintEvent&)
{
    wxAutoBufferedPaintDC dc(this);
    dc.SetBackground(wxBrush(g.bg));
    dc.Clear();
    dc.SetFont(m_font);
    dc.SetTextForeground(g.text);
    for (int i = 0; i < (int)m_items.size(); i++)
    {
        const wxRect r = ItemRect(i);
        const bool selected = i == m_selected && !m_items[i].action;
        const bool ring = selected && ShowsFocus(this); // where the arrows are
        if (selected || i == m_hover)
            FillRound(dc, r, FromDIP(4), Mix(g.text, g.bg, selected ? 0.08 : 0.05), ring ? &g.text : nullptr);
        if (selected)
            FillRound(dc, wxRect(r.x, r.y + (r.height - FromDIP(16)) / 2, FromDIP(3), FromDIP(16)), FromDIP(1.5), g.accent);
        const wxBitmap icon = m_items[i].icon.GetBitmapFor(this);
        const int x = r.x + FromDIP(12);
        dc.DrawBitmap(icon, x, r.y + (r.height - icon.GetLogicalHeight()) / 2, true);
        dc.DrawText(m_items[i].label, x + icon.GetLogicalWidth() + FromDIP(12), r.y + (r.height - dc.GetCharHeight()) / 2);
    }
}

// ---------------------------------------------------------------------------------------------
// ColourGrid
// ---------------------------------------------------------------------------------------------
namespace
{
// The 48 accent colours of Windows' own settings, eight to a row.
const unsigned kWindowsColours[48] = {
    0xFFB900, 0xFF8C00, 0xF7630C, 0xCA5010, 0xDA3B01, 0xEF6950, 0xD13438, 0xFF4343,
    0xE74856, 0xE81123, 0xEA005E, 0xC30052, 0xE3008C, 0xBF0077, 0xC239B3, 0x9A0089,
    0x0078D7, 0x0063B1, 0x8E8CD8, 0x6B69D6, 0x8764B8, 0x744DA9, 0xB146C2, 0x881798,
    0x0099BC, 0x2D7D9A, 0x00B7C3, 0x038387, 0x00B294, 0x018574, 0x00CC6A, 0x10893E,
    0x7A7574, 0x5D5A58, 0x68768A, 0x515C6B, 0x567C73, 0x486860, 0x498205, 0x107C10,
    0x767676, 0x4C4A48, 0x69797E, 0x4A5459, 0x647C64, 0x525E54, 0x847545, 0x7E735F,
};
}

ColourGrid::ColourGrid(wxWindow* parent, const wxColour& current) : wxPopupTransientWindow(parent, wxBORDER_NONE)
{
    const wxSize size(FromDIP(kPad) * 2 + kCols * FromDIP(kCell) + (kCols - 1) * FromDIP(kGap),
                      FromDIP(kPad) * 2 + kRows * FromDIP(kCell) + (kRows - 1) * FromDIP(kGap));
    SetSize(size);
    for (int i = 0; i < kCols * kRows; i++)
        if (current.IsOk() && Colour(i) == current)
            m_current = m_hover = i;
    m_panel = new wxPanel(this, wxID_ANY, wxPoint(0, 0), size, wxWANTS_CHARS);
    m_panel->SetBackgroundStyle(wxBG_STYLE_PAINT);
    m_panel->Bind(wxEVT_PAINT, &ColourGrid::OnPaint, this);
    m_panel->Bind(wxEVT_MOTION, [this](wxMouseEvent& e) { SetHover(Cell(e.GetPosition())); });
    m_panel->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent&) { m_pressed = true; });
    m_panel->Bind(wxEVT_LEFT_UP, [this](wxMouseEvent& e) {
        const int cell = Cell(e.GetPosition());
        if (m_pressed && cell >= 0)
            Pick(cell);
        m_pressed = false;
    });
    m_panel->Bind(wxEVT_CHAR_HOOK, [this](wxKeyEvent& e) {
        if (!Key(e.GetKeyCode()))
            e.Skip();
    });
    ApplyDwm(this, true, nullptr);
}

bool ColourGrid::Key(int code)
{
    const int at = m_hover < 0 ? 0 : m_hover, count = kCols * kRows;
    switch (code)
    {
    case WXK_LEFT:  SetHover(wxMax(0, at - 1)); return true;
    case WXK_RIGHT: SetHover(wxMin(count - 1, at + 1)); return true;
    case WXK_UP:    SetHover(at >= kCols ? at - kCols : at); return true;
    case WXK_DOWN:  SetHover(at + kCols < count ? at + kCols : at); return true;
    case WXK_RETURN:
    case WXK_NUMPAD_ENTER:
    case WXK_SPACE:  if (m_hover >= 0) Pick(m_hover); return true;
    case WXK_ESCAPE:
    case WXK_TAB:    DismissAndNotify(); return true;
    default:         return false;
    }
}

void ColourGrid::OpenAt(const wxPoint& pos)
{
    Move(pos);
    Popup(m_panel);
    m_panel->SetFocus();
}

void ColourGrid::OnDismiss()
{
    if (onClosed)
        onClosed();
    CallAfter([this] { Destroy(); });
}

wxColour ColourGrid::Colour(int i)
{
    const unsigned c = kWindowsColours[i];
    return wxColour((c >> 16) & 0xff, (c >> 8) & 0xff, c & 0xff);
}

wxRect ColourGrid::CellRect(int i) const
{
    return wxRect(FromDIP(kPad) + (i % kCols) * FromDIP(kCell + kGap), FromDIP(kPad) + (i / kCols) * FromDIP(kCell + kGap),
                  FromDIP(kCell), FromDIP(kCell));
}

int ColourGrid::Cell(const wxPoint& p) const
{
    for (int i = 0; i < kCols * kRows; i++)
        if (CellRect(i).Contains(p))
            return i;
    return -1;
}

void ColourGrid::SetHover(int cell)
{
    if (cell != m_hover && cell >= 0)
    {
        m_hover = cell;
        m_panel->Refresh();
    }
}

void ColourGrid::Pick(int cell)
{
    if (onPick)
        onPick(Colour(cell));
    DismissAndNotify();
}

void ColourGrid::OnPaint(wxPaintEvent&)
{
    wxAutoBufferedPaintDC dc(m_panel);
    dc.SetBackground(wxBrush(g.card));
    dc.Clear();
    for (int i = 0; i < kCols * kRows; i++)
    {
        const wxRect r = CellRect(i);
        if (i == m_current || i == m_hover) // a ring around it, off the colour
        {
            wxRect ring = r, gap;
            ring.Inflate(FromDIP(3));
            gap = ring;
            gap.Deflate(FromDIP(2));
            FillRound(dc, ring, FromDIP(7), i == m_current ? g.text : g.text2);
            FillRound(dc, gap, FromDIP(5), g.card);
        }
        FillRound(dc, r, FromDIP(4), Colour(i));
    }
}
