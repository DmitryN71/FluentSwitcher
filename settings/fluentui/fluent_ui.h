// Copyright (c) 2026 Dmitry Novikov
// SPDX-License-Identifier: MIT -- see LICENSE next to this file.
//
// Fluent UI for wxWidgets on Windows, the look of FluentClipper: theme colours, fonts, the window
// frame (DWM), icons, rounded fills, the Windows 11 button, drop-down list and question box.
// The controls of a settings window are in fluent_controls.h.
#pragma once

#include <wx/wx.h>
#include <wx/bmpbndl.h>

#include <functional>
#include <vector>

// Every colour the controls use. Set it once, before the first window: g = MakeTheme(dark, accent).
struct Theme
{
    bool dark;
    wxColour bg, layer, card, input, border, text, text2, accent, sel, hover, star;
    wxColour control, controlHover, controlPressed, onAccent; // buttons and drop-down lists
    wxColour icon[5];   // FluentClipper's list: text, link, image, files, rich
    wxColour label[3];
};

extern Theme g;

// accent: "app" (the default light blue), "teal", "green", "purple", "red", "orange",
// "windows" (the colour chosen in Windows) or "#rrggbb" (one the user picked).
Theme MakeTheme(bool dark, const wxString& accent = "app");

// The accent colour for that key on a dark or light background, as MakeTheme takes it.
bool AccentColour(bool dark, const wxString& accent, wxColour* out);

// Windows 11 (build 22000 or later), not 10.
bool IsWindows11();

// Segoe UI Variable Text where Windows 11 has it, Segoe UI otherwise.
wxFont UiFont(double pointSize, bool bold = false);

// Caption/border colour and rounded corners (Windows 11; silently ignored on Windows 10).
void ApplyDwm(wxWindow* win, bool roundCorners, const wxColour* caption);

// Plain SVG text (stroke style, 24 px grid), rasterized for the DPI of the window that draws it.
wxBitmapBundle Svg(const char* body, const wxColour& stroke, const wxColour* fill = nullptr);

// A Fluent UI System Icons path (the "d" of a 20 px icon, github.com/microsoft/fluentui-system-icons,
// MIT licence) in one colour, `size` px at 100 % scaling.
wxBitmapBundle FluentIcon(const char* path, const wxColour& colour, int size = 16);

// A new text for a label that is wrapped to `width`. wxStaticText::Wrap() skips a width it was
// already wrapped to, so after SetLabel() the new text would stay on one line.
void SetWrappedLabel(wxStaticText* label, const wxString& text, int width);

void FillRound(wxDC& dc, const wxRect& r, double radius, const wxColour& fill, const wxColour* border = nullptr);

// The focus ring, as in Windows 11: it shows only while the keyboard is in use (Tab, the arrows)
// and goes on a mouse click, so a click does not leave a frame behind. The app switches it
// (in its wxApp::FilterEvent: Tab and the arrows on, a mouse button off); the control with the
// focus is repainted when it changes.
bool KeyboardCues();
void SetKeyboardCues(bool on);
// Should `w` draw its ring now: it has the focus and the keyboard is in use.
bool ShowsFocus(const wxWindow* w);
// The ring itself: 2 px in the text colour along the inside of r.
void DrawFocusRing(const wxWindow* w, wxDC& dc, const wxRect& r, double radius);

// A Windows 11 text box: the fill, a 1 px border and, when `bottom` is given, a band of that
// colour `height` px high along the bottom edge that follows the rounded corners (the accent of a
// box with the focus).
void FillInput(wxDC& dc, const wxRect& r, double radius, const wxColour& fill, const wxColour* bottom = nullptr, int height = 2);

// k of colour a, the rest of b.
wxColour Mix(const wxColour& a, const wxColour& b, double k);

// A Windows 11 button: rounded, filled; `accent` = the blue one for the main action.
// A click sends wxEVT_BUTTON with its id, like a wxButton, so wxID_OK / wxID_CANCEL end a dialog.
class FluentButton : public wxPanel
{
public:
    FluentButton(wxWindow* parent, wxWindowID id, const wxString& label, bool accent = false);
    // An icon-only button (a Fluent path, see FluentIcon); the tooltip says what it does.
    FluentButton(wxWindow* parent, wxWindowID id, const char* iconPath, const wxString& tooltip);
    // The same two with texts in quotes: without these, a label in quotes with `accent` would not
    // compile, and a tooltip in quotes would make the icon's path a label.
    FluentButton(wxWindow* parent, wxWindowID id, const char* label, bool accent)
        : FluentButton(parent, id, wxString(label), accent) {}
    FluentButton(wxWindow* parent, wxWindowID id, const char* iconPath, const char* tooltip)
        : FluentButton(parent, id, iconPath, wxString(tooltip)) {}
    bool AcceptsFocus() const override { return true; }
    bool Enable(bool enable = true) override; // an unavailable button is drawn pale
    // A new label; the button grows for a longer one and never shrinks, so a label that comes and
    // goes ("Apply" / "Saved") does not move its neighbours once both have been set.
    void SetText(const wxString& label);

private:
    void Init();
    void OnPaint(wxPaintEvent&);
    void Click();

    wxString m_label;
    wxBitmapBundle m_icon;
    bool m_accent;
    bool m_hover = false, m_pressed = false;
};

// A question in the Windows 11 look: the text, the main button (blue) and Cancel. Enter answers
// with the main button, Esc with Cancel. True = the main button. The title is the app's display
// name (wxApp::SetAppDisplayName).
bool AskFluent(wxWindow* parent, const wxString& text, const wxString& yes, const wxString& no);

// A Windows 11 drop-down list: the choice and a chevron; a click opens the list over it (the
// chosen item in its place), another click closes it. Up / Down change the choice without
// opening it; Space or Alt+Down open it.
class FluentChoice : public wxPanel
{
public:
    std::function<void()> onChange;
    std::function<void(int)> onReselect; // the chosen item picked in the list once more

    FluentChoice(wxWindow* parent, const wxArrayString& items, int selection);
    int GetSelection() const { return m_selection; }
    void SetSelection(int selection);
    bool AcceptsFocus() const override { return true; }
    // A dot of colour before each item (the accent colours); an invalid colour: no dot.
    void SetSwatches(const std::vector<wxColour>& swatches);
    // A popup of the owner's opened from this control (a ColourGrid for "Custom colour...") takes its keys
    // while it is open: keys(code) true when taken. nullptr when it has closed.
    void SetPopupKeys(std::function<bool(int key)> keys) { m_popupKey = std::move(keys); }

private:
    void OnPaint(wxPaintEvent&);
    void Open();
    void Choose(int selection);

    wxArrayString m_items;
    std::vector<wxColour> m_swatches;
    int m_selection;
    bool m_hover = false;
    bool m_open = false;       // its list is open
    std::function<bool(int key)> m_popupKey; // while it is open: its keys, true when taken
    wxLongLong m_closedAt = 0; // when the list closed: the click that closed it must not reopen it
};
