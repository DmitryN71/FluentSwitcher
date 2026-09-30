// Copyright (c) 2026 Dmitry Novikov
// SPDX-License-Identifier: MIT -- see LICENSE next to this file.
//
// The controls of a settings window in the Windows 11 look, as FluentClipper's: the on/off switch,
// a box for a number, the look of a hotkey field and the line under it, the cards of a page, the
// sections on the left, the grid of Windows accent colours. Colours come from `g` (fluent_ui.h);
// every text is given by the caller.
#pragma once

#include "fluent_ui.h"

#include <wx/popupwin.h>
#include <wx/scrolwin.h>

#include <functional>
#include <vector>

// A label in the UI font: `points` in size, in that colour.
wxStaticText* FluentText(wxWindow* parent, const wxString& s, int points, const wxColour& colour, bool bold = false);

// Red for a refusal or an error, in the dark or the light theme.
wxColour WarningColour();

// The Windows 11 on/off switch. A click or Space toggles it.
class ToggleSwitch : public wxPanel
{
public:
    std::function<void()> onChange; // toggled by the user

    ToggleSwitch(wxWindow* parent, bool on);
    bool IsOn() const { return m_on; }
    void SetOn(bool on); // no onChange
    bool AcceptsFocus() const override { return true; }

private:
    void Toggle();
    void OnPaint(wxPaintEvent&);

    bool m_on;
};

// A small box for a whole number: digits only, the accent line while it has the focus.
class NumberField : public wxPanel
{
public:
    std::function<void()> onChange;

    NumberField(wxWindow* parent, int value);
    // 0 when the box is empty or holds something else.
    int Value() const;

private:
    void LayoutText();
    void OnPaint(wxPaintEvent&);

    wxTextCtrl* m_text;
};

// The look of a hotkey field; which keys count and what is free is the program's own (derive from
// it and bind wxEVT_CHAR_HOOK, or set the callbacks). It shows a text: the hotkey, or grey (none,
// "Press a combination...", the old one while a new one is awaited). While recording, the whole
// field is framed in the accent colour. The cross at the end clears it (SetClearable).
class HotkeyView : public wxPanel
{
public:
    std::function<void()> onRecord; // a click on the field (not on the cross)
    std::function<void()> onClear;  // a click on the cross

    explicit HotkeyView(wxWindow* parent);
    void SetText(const wxString& text, bool grey);
    void SetRecording(bool on);
    bool IsRecording() const { return m_recording; }
    // The cross shown or not; its tooltip.
    void SetClearable(bool on, const wxString& tooltip = wxString());
    bool IsClearable() const { return m_clearable; }
    bool AcceptsFocus() const override { return true; }

private:
    wxRect CrossRect() const;
    void SetCrossHover(bool on);
    void OnPaint(wxPaintEvent&);

    wxString m_text, m_crossTip;
    bool m_grey = true, m_recording = false, m_clearable = false, m_crossHover = false;
    wxFont m_font;
    wxBitmapBundle m_cross;
};

// The line under a field for grey advice or a red refusal, there from the start and as wide as the
// longest of `notes` could be (at most 360 px), in one line: a note that comes or goes only changes
// its text, so nothing around it moves, grows or is drawn again. *width: that width, for the field
// above to match.
wxStaticText* NoteLine(wxWindow* parent, const wxArrayString& notes, int* width);

// Grey advice (hint) or a red refusal in a NoteLine, or nothing; too long for it, it ends in "..."
// and the tooltip has it all.
void SetNoteLine(wxStaticText* line, const wxString& text, bool hint);

// A rounded card holding one setting.
class Card : public wxPanel
{
public:
    explicit Card(wxWindow* parent);
};

// A page of the settings: it scrolls; its cards go into *column.
wxScrolledWindow* NewSettingsPage(wxWindow* parent, wxBoxSizer** column);

// A card on a page: the title and the description on the left, the control that `make` creates in
// the card on the right (below them with controlBelow; none if it returns nullptr). Returns the
// description, for SetCardDescription.
wxStaticText* AddSettingsCard(wxWindow* page, wxSizer* column, const wxString& title, const wxString& description,
                              const std::function<wxWindow*(wxWindow* card)>& make, bool controlBelow = false);

// A card's description gets a new text: wrapped like the first one, so the card keeps its width.
void SetCardDescription(wxStaticText* label, const wxString& text);

// The sections on the left of a settings window, as in Windows 11's own settings: an icon and a
// name; the section shown has a fill and the accent "pill". Actions (open a folder, quit) sit at the
// bottom, apart. Up / Down go through the sections when it has the focus.
class SectionNav : public wxPanel
{
public:
    std::function<void(int section)> onSelect;
    std::function<void(int action)> onAction;

    explicit SectionNav(wxWindow* parent);
    // icon: a Fluent path (see FluentIcon).
    void AddSection(const char* icon, const wxString& label);
    void AddAction(const char* icon, const wxString& label);
    void Select(int section);
    bool AcceptsFocus() const override { return true; }

protected:
    wxSize DoGetBestSize() const override;

private:
    static const int kItem = 38, kTop = 4, kInset = 4, kGap = 24; // DIPs

    struct Item
    {
        wxBitmapBundle icon;
        wxString label;
        bool action;
    };

    wxRect ItemRect(int i) const;
    int HitTest(const wxPoint& p) const;
    void SetHover(int i);
    void Activate(int i);
    void OnPaint(wxPaintEvent&);

    std::vector<Item> m_items; // the sections, then the actions
    int m_sections = 0;
    int m_selected = 0, m_hover = -1;
    wxFont m_font;
};

// The 48 accent colours of Windows' own settings, eight to a row, in a popup like the lists': the
// mouse or the arrows and Enter pick one, Esc or a click elsewhere closes it; the current colour has
// a ring. It deletes itself once closed.
class ColourGrid : public wxPopupTransientWindow
{
public:
    std::function<void(const wxColour&)> onPick;
    std::function<void()> onClosed;

    static const int kCell = 28, kGap = 4, kPad = 10, kCols = 8, kRows = 6; // DIPs

    ColourGrid(wxWindow* parent, const wxColour& current);
    // A key while it is open (the owner passes them here: the focus stays on its control, see
    // FluentChoice::SetPopupKeys). True when it was the grid's: not for the window under it.
    bool Key(int code);
    void OpenAt(const wxPoint& pos);

protected:
    void OnDismiss() override;

private:
    static wxColour Colour(int i);
    wxRect CellRect(int i) const;
    int Cell(const wxPoint& p) const;
    void SetHover(int cell);
    void Pick(int cell);
    void OnPaint(wxPaintEvent&);

    wxPanel* m_panel;
    int m_current = -1, m_hover = -1;
    bool m_pressed = false;
};
