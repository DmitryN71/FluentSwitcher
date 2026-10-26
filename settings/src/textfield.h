// A one-line text box in the Windows 11 look (the kit has one for numbers only): the pages' fields, the field of the
// word lists (wordlist.cpp) and the search of the settings (pages.cpp).
#pragma once

#include "fluent_ui.h"

#include <wx/dcbuffer.h>
#include <wx/msw/wrapwin.h> // SetPropW: AllowAutoSwitch
#include <wx/textctrl.h>

#include <functional>

class TextField : public wxPanel
{
public:
    std::function<void()> onChange;
    std::function<void()> onEnter;        // with `enter`: Enter pressed in it
    std::function<void()> onFocus;        // the box got the focus
    std::function<bool(int key)> onKey;   // a key pressed in it, before the box takes it: true - taken (the arrows)

    // enter: Enter goes to onEnter (otherwise the box does not take it); Esc empties a box that is not empty. maxLength -
    // characters at most: paths and pasted lists are long (a path of 73 characters was cut at 64 and never matched).
    TextField(wxWindow* parent, const wxString& value, int width, bool enter = false, int maxLength = 64) : wxPanel(parent)
    {
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        SetMinSize(FromDIP(wxSize(width, 32)));
        m_text = new wxTextCtrl(this, wxID_ANY, value, wxDefaultPosition, wxDefaultSize,
                                wxBORDER_NONE | (enter ? wxTE_PROCESS_ENTER : 0));
        m_text->SetFont(UiFont(10));
        m_text->SetBackgroundColour(g.input);
        m_text->SetForegroundColour(g.text);
        m_text->SetMaxLength(maxLength);
        m_text->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { if (onChange) onChange(); });
        if (enter) // wxEVT_TEXT_ENTER without wxTE_PROCESS_ENTER is an assert of wxWidgets
        {
            m_text->Bind(wxEVT_TEXT_ENTER, [this](wxCommandEvent&) { if (onEnter) onEnter(); });
            m_text->Bind(wxEVT_CHAR_HOOK, [this](wxKeyEvent& e) {
                if (e.GetKeyCode() == WXK_ESCAPE && e.GetModifiers() == wxMOD_NONE && !m_text->IsEmpty())
                    m_text->Clear(); // Esc in an empty one closes the window, as everywhere
                else
                    e.Skip();
            });
        }
        m_text->Bind(wxEVT_KEY_DOWN, [this](wxKeyEvent& e) {
            if (!onKey || !onKey(e.GetKeyCode()))
                e.Skip();
        });
        m_text->Bind(wxEVT_SET_FOCUS, [this](wxFocusEvent& e) {
            Refresh();
            if (onFocus)
                onFocus();
            e.Skip();
        });
        m_text->Bind(wxEVT_KILL_FOCUS, [this](wxFocusEvent& e) { Refresh(); e.Skip(); });
        Bind(wxEVT_PAINT, [this](wxPaintEvent&) {
            wxAutoBufferedPaintDC dc(this);
            dc.SetBackground(wxBrush(GetParent()->GetBackgroundColour()));
            dc.Clear();
            const wxSize size = GetClientSize();
            FillInput(dc, wxRect(size), FromDIP(4), g.input, m_text->HasFocus() ? &g.accent : nullptr, FromDIP(2));
            if (m_icon.IsOk())
            {
                const wxBitmap icon = m_icon.GetBitmapFor(this);
                dc.DrawBitmap(icon, size.x - FromDIP(10) - icon.GetLogicalWidth(), (size.y - icon.GetLogicalHeight()) / 2,
                              true);
            }
        });
        Bind(wxEVT_SIZE, [this](wxSizeEvent& e) {
            LayoutText();
            e.Skip();
        });
        Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent&) { m_text->SetFocus(); });
    }

    wxString Value() const { return m_text->GetValue(); }
    void Clear() { m_text->Clear(); } // onChange too
    void SetHint(const wxString& hint) { m_text->SetHint(hint); }
    // The engine switches the layout of a word in this box as in any other program (the search of the settings). In its
    // other boxes it does not: words are typed into the lists as they are, "ghbdtn" too (Settings.h, OwnWindowTop - the
    // name of the property is the same there).
    void AllowAutoSwitch()
    {
        const HWND hwnd = (HWND)m_text->GetHWND();
        SetPropW(hwnd, L"FluentSwitcher.AutoSwitch", (HANDLE)1);
        m_text->Bind(wxEVT_DESTROY, [hwnd](wxWindowDestroyEvent& e) {
            RemovePropW(hwnd, L"FluentSwitcher.AutoSwitch");
            e.Skip();
        });
    }
    // An icon at the right end, grey: the magnifier of the search, as in Windows' own Settings (a Fluent path).
    void SetIcon(const char* path)
    {
        m_icon = FluentIcon(path, g.text2);
        LayoutText();
        Refresh();
    }
    void FocusText(bool selectAll)
    {
        m_text->SetFocus();
        if (selectAll)
            m_text->SelectAll();
    }

private:
    void LayoutText()
    {
        const wxSize size = GetClientSize();
        const int h = m_text->GetBestSize().y;
        const int icon = m_icon.IsOk() ? FromDIP(16 + 8) : 0;
        m_text->SetSize(FromDIP(10), (size.y - h) / 2, size.x - FromDIP(20) - icon, h);
    }

    wxTextCtrl* m_text;
    wxBitmapBundle m_icon;
};
