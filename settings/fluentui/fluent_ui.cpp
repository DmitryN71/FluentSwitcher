// Copyright (c) 2026 Dmitry Novikov
// SPDX-License-Identifier: MIT -- see LICENSE next to this file.
#include "fluent_ui.h"

#include <wx/dcbuffer.h>
#include <wx/display.h>
#include <wx/fontenum.h>
#include <wx/graphics.h>
#include <wx/popupwin.h>
#include <wx/time.h>
#include <wx/utils.h>

#include <memory>

#include <dwmapi.h>

// ---------------------------------------------------------------------------------------------
// 1. Theme: every colour the controls use lives here.
// ---------------------------------------------------------------------------------------------
Theme g;

bool IsWindows11()
{
    // FLUENTUI_TEST_WIN10 (or FLUENTCLIPPER_TEST_WIN10): draw as on Windows 10, for a look at it on 11.
    static const bool yes = wxCheckOsVersion(10, 0, 22000) && !wxGetEnv("FLUENTUI_TEST_WIN10", nullptr) &&
                            !wxGetEnv("FLUENTCLIPPER_TEST_WIN10", nullptr);
    return yes;
}

// The accent colour chosen in Windows, as Windows' own apps show it: on Windows 11 a lighter
// shade on dark, a darker one on light (the palette Windows keeps for its apps); on Windows 10 the
// colour itself. The colour of the window frames where the palette is missing.
static bool WindowsAccent(bool dark, wxColour* out)
{
    BYTE palette[32] = {};
    DWORD size = sizeof(palette);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Accent",
                     L"AccentPalette", RRF_RT_REG_BINARY, nullptr, palette, &size) == ERROR_SUCCESS && size >= 32)
    {
        // "light 2" / "dark 1" / the accent, as R, G, B, A
        const BYTE* c = palette + (!IsWindows11() ? 3 : dark ? 1 : 4) * 4;
        *out = wxColour(c[0], c[1], c[2]);
        return true;
    }
    DWORD argb = 0;
    BOOL opaque = FALSE;
    if (SUCCEEDED(DwmGetColorizationColor(&argb, &opaque)))
    {
        const wxColour base((argb >> 16) & 0xff, (argb >> 8) & 0xff, argb & 0xff);
        *out = !IsWindows11() ? base : dark ? Mix(base, *wxWHITE, 0.6) : Mix(base, *wxBLACK, 0.8);
        return true;
    }
    return false;
}

// The own accents ("app" first, FluentClipper's light blue): on Windows 11 a lighter shade for the dark theme and a darker one
// for the light theme, as Windows 11 does with its accent colour; on Windows 10 one colour for both,
// as there.
struct AccentSet { const char* key; wxColour dark, light, win10; };
static const AccentSet kAccents[] = {
    { "app",    wxColour(0x60, 0xcd, 0xff), wxColour(0x00, 0x5f, 0xb8), wxColour(0x00, 0x78, 0xd7) },
    { "teal",   wxColour(0x4c, 0xd4, 0xc4), wxColour(0x00, 0x78, 0x6c), wxColour(0x03, 0x83, 0x87) },
    { "green",  wxColour(0x6c, 0xcb, 0x5f), wxColour(0x10, 0x7c, 0x10), wxColour(0x10, 0x7c, 0x10) },
    { "purple", wxColour(0xc5, 0xa6, 0xff), wxColour(0x6b, 0x3f, 0xc4), wxColour(0x74, 0x4d, 0xa9) },
    { "red",    wxColour(0xff, 0x80, 0x8f), wxColour(0xc4, 0x2b, 0x3c), wxColour(0xe8, 0x11, 0x23) },
    { "orange", wxColour(0xff, 0xb3, 0x5c), wxColour(0xb3, 0x51, 0x00), wxColour(0xca, 0x50, 0x10) },
};

bool AccentColour(bool dark, const wxString& key, wxColour* out)
{
    if (key == "windows")
        return WindowsAccent(dark, out);
    if (key.StartsWith("#")) // one the user picked: as it is
    {
        wxColour own(key);
        if (!own.IsOk())
            return false;
        *out = own;
        return true;
    }
    for (const AccentSet& a : kAccents)
        if (key == a.key)
        {
            *out = !IsWindows11() ? a.win10 : dark ? a.dark : a.light;
            return true;
        }
    return false;
}

Theme MakeTheme(bool dark, const wxString& accentKey)
{
    Theme t;
    t.dark = dark;
    if (dark)
    {
        t.bg = wxColour(0x20, 0x20, 0x20);    t.layer = wxColour(0x27, 0x27, 0x27);
        t.card = wxColour(0x2c, 0x2c, 0x2c);  t.input = wxColour(0x1f, 0x1f, 0x1f);
        t.border = wxColour(0x3a, 0x3a, 0x3a);
        t.text = wxColour(0xff, 0xff, 0xff);  t.text2 = wxColour(0xa8, 0xa8, 0xa8);
        t.accent = wxColour(0x60, 0xcd, 0xff);
        t.sel = wxColour(0x3a, 0x3a, 0x3a);   t.hover = wxColour(0x31, 0x31, 0x31);
        t.star = wxColour(0xff, 0xd1, 0x66);
        t.control = wxColour(0x2d, 0x2d, 0x2d);  t.controlHover = wxColour(0x32, 0x32, 0x32);
        t.controlPressed = wxColour(0x27, 0x27, 0x27);  t.onAccent = wxColour(0x00, 0x00, 0x00);
        t.icon[0] = wxColour(0xc8, 0xc8, 0xc8); t.icon[1] = wxColour(0x9b, 0xe3, 0xa8);
        t.icon[2] = wxColour(0xe3, 0xb2, 0xf0); t.icon[3] = wxColour(0xf2, 0xcf, 0x7e);
        t.icon[4] = wxColour(0xc9, 0xb8, 0xff);
        t.label[0] = wxColour(0xff, 0x8a, 0x8a); t.label[1] = wxColour(0x7f, 0xc1, 0xff);
        t.label[2] = wxColour(0x8f, 0xdc, 0x9c);
    }
    else
    {
        t.bg = wxColour(0xf3, 0xf3, 0xf3);    t.layer = wxColour(0xfb, 0xfb, 0xfb);
        t.card = wxColour(0xff, 0xff, 0xff);  t.input = wxColour(0xff, 0xff, 0xff);
        t.border = wxColour(0xe0, 0xe0, 0xe0);
        t.text = wxColour(0x1b, 0x1b, 0x1b);  t.text2 = wxColour(0x61, 0x61, 0x61);
        t.accent = wxColour(0x00, 0x5f, 0xb8);
        t.sel = wxColour(0xe6, 0xe6, 0xe6);   t.hover = wxColour(0xef, 0xef, 0xef);
        t.star = wxColour(0xa8, 0x64, 0x00);
        t.control = wxColour(0xfb, 0xfb, 0xfb);  t.controlHover = wxColour(0xf6, 0xf6, 0xf6);
        t.controlPressed = wxColour(0xf0, 0xf0, 0xf0);  t.onAccent = wxColour(0xff, 0xff, 0xff);
        t.icon[0] = wxColour(0x5a, 0x5a, 0x5a); t.icon[1] = wxColour(0x1e, 0x6b, 0x2b);
        t.icon[2] = wxColour(0x6a, 0x1f, 0x8a); t.icon[3] = wxColour(0x7a, 0x4b, 0x00);
        t.icon[4] = wxColour(0x4a, 0x2f, 0x9a);
        t.label[0] = wxColour(0xb3, 0x26, 0x1e); t.label[1] = wxColour(0x0b, 0x5c, 0xad);
        t.label[2] = wxColour(0x1d, 0x6b, 0x2c);
    }
    wxColour accent;
    if (AccentColour(dark, accentKey, &accent) || AccentColour(dark, "app", &accent))
    {
        t.accent = accent;
        const int brightness = (accent.Red() * 299 + accent.Green() * 587 + accent.Blue() * 114) / 1000;
        t.onAccent = brightness > 140 ? wxColour(0, 0, 0) : wxColour(0xff, 0xff, 0xff); // text on it
    }
    return t;
}

wxFont UiFont(double pointSize, bool bold)
{
    static const wxString face =
        wxFontEnumerator::IsValidFacename("Segoe UI Variable Text") ? "Segoe UI Variable Text" : "Segoe UI";
    wxFontInfo info(pointSize);
    info.FaceName(face);
    if (bold)
        info.Bold();
    return wxFont(info);
}

// ---------------------------------------------------------------------------------------------
// 2. DWM: caption/border colour and rounded corners (Windows 11; silently ignored on Windows 10).
// ---------------------------------------------------------------------------------------------
void ApplyDwm(wxWindow* win, bool roundCorners, const wxColour* caption)
{
    const DWORD kCornerPreference = 33, kBorderColor = 34, kCaptionColor = 35;
    HWND hwnd = (HWND)win->GetHandle();
    if (roundCorners)
    {
        int round = 2; // DWMWCP_ROUND
        DwmSetWindowAttribute(hwnd, kCornerPreference, &round, sizeof(round));
    }
    COLORREF border = RGB(g.border.Red(), g.border.Green(), g.border.Blue());
    DwmSetWindowAttribute(hwnd, kBorderColor, &border, sizeof(border));
    if (caption)
    {
        COLORREF c = RGB(caption->Red(), caption->Green(), caption->Blue());
        DwmSetWindowAttribute(hwnd, kCaptionColor, &c, sizeof(c));
    }
}

// ---------------------------------------------------------------------------------------------
// 3. Icons: plain SVG text, rasterized by wxWidgets for the DPI of the window that draws them.
// ---------------------------------------------------------------------------------------------
wxBitmapBundle Svg(const char* body, const wxColour& stroke, const wxColour* fill)
{
    wxString s = wxString::Format(
        "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 24 24' fill='%s' "
        "stroke='%s' stroke-width='2' stroke-linecap='round' stroke-linejoin='round'>%s</svg>",
        fill ? fill->GetAsString(wxC2S_HTML_SYNTAX) : wxString("none"),
        stroke.GetAsString(wxC2S_HTML_SYNTAX), body);
    return wxBitmapBundle::FromSVG(s.utf8_str().data(), wxSize(16, 16));
}

// ---------------------------------------------------------------------------------------------
// Small drawing helpers
// ---------------------------------------------------------------------------------------------
void FillRound(wxDC& dc, const wxRect& r, double radius, const wxColour& fill, const wxColour* border)
{
    std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::CreateFromUnknownDC(dc));
    if (!gc)
    {
        dc.SetBrush(wxBrush(fill));
        dc.SetPen(border ? wxPen(*border) : *wxTRANSPARENT_PEN);
        dc.DrawRoundedRectangle(r, radius);
        return;
    }
    gc->SetBrush(wxBrush(fill));
    gc->SetPen(border ? wxPen(*border) : *wxTRANSPARENT_PEN);
    if (border) // a 1 px line on the middle of the pixels, or it smears over two
        gc->DrawRoundedRectangle(r.x + 0.5, r.y + 0.5, r.width - 1, r.height - 1, radius);
    else
        gc->DrawRoundedRectangle(r.x, r.y, r.width, r.height, radius);
}

static bool g_keyboardCues = false;

bool KeyboardCues()
{
    return g_keyboardCues;
}

void SetKeyboardCues(bool on)
{
    if (on == g_keyboardCues)
        return;
    g_keyboardCues = on;
    if (wxWindow* focus = wxWindow::FindFocus())
        focus->Refresh();
}

bool ShowsFocus(const wxWindow* w)
{
    return g_keyboardCues && w->HasFocus();
}

void DrawFocusRing(const wxWindow* w, wxDC& dc, const wxRect& r, double radius)
{
    const int width = w->FromDIP(2);
    const wxPen pen(g.text, width);
    std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::CreateFromUnknownDC(dc));
    if (!gc)
    {
        dc.SetPen(pen);
        dc.SetBrush(*wxTRANSPARENT_BRUSH);
        dc.DrawRoundedRectangle(wxRect(r).Deflate(width / 2), radius);
        return;
    }
    gc->SetPen(pen);
    gc->SetBrush(*wxTRANSPARENT_BRUSH);
    const double half = width / 2.0;
    gc->DrawRoundedRectangle(r.x + half, r.y + half, r.width - width, r.height - width, radius);
}

void FillInput(wxDC& dc, const wxRect& r, double radius, const wxColour& fill, const wxColour* bottom, int height)
{
    FillRound(dc, r, radius, fill, &g.border);
    if (!bottom)
        return;
    std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::CreateFromUnknownDC(dc));
    if (!gc)
    {
        dc.SetPen(wxPen(*bottom, height));
        dc.DrawLine(r.x + (int)radius, r.GetBottom() - height / 2, r.GetRight() - (int)radius, r.GetBottom() - height / 2);
        return;
    }
    // The lowest `height` pixels of the same rounded shape: straight along the bottom, curving up
    // into the corners.
    gc->Clip(r.x, r.y + r.height - height, r.width, height);
    gc->SetBrush(wxBrush(*bottom));
    gc->SetPen(*wxTRANSPARENT_PEN);
    gc->DrawRoundedRectangle(r.x, r.y, r.width, r.height, radius);
}

// An icon: a Fluent path in one colour, 16 px at 100 % scaling.
wxBitmapBundle FluentIcon(const char* path, const wxColour& colour, int size)
{
    const wxString svg = wxString::Format(
        "<svg xmlns='http://www.w3.org/2000/svg' width='20' height='20' viewBox='0 0 20 20' fill='%s'>"
        "<path d='%s'/></svg>",
        colour.GetAsString(wxC2S_HTML_SYNTAX), path);
    return wxBitmapBundle::FromSVG(svg.utf8_str().data(), wxSize(size, size));
}

void SetWrappedLabel(wxStaticText* label, const wxString& text, int width)
{
    wxString shown = text;
    shown.Replace("&", "&&"); // a path or a name with '&' shows it, not an underlined letter
    label->SetLabel(shown);
    label->Wrap(-1); // forget the old width, see ui.h
    label->Wrap(width);
}

wxColour Mix(const wxColour& a, const wxColour& b, double k) // k of a, the rest of b
{
    return wxColour((unsigned char)(a.Red() * k + b.Red() * (1 - k)), (unsigned char)(a.Green() * k + b.Green() * (1 - k)),
                    (unsigned char)(a.Blue() * k + b.Blue() * (1 - k)));
}


// ---------------------------------------------------------------------------------------------
// FluentButton
// ---------------------------------------------------------------------------------------------
FluentButton::FluentButton(wxWindow* parent, wxWindowID id, const wxString& label, bool accent)
    : wxPanel(parent, id), m_label(label), m_accent(accent)
{
    wxClientDC dc(this);
    dc.SetFont(UiFont(10));
    SetMinSize(wxSize(wxMax(FromDIP(96), dc.GetTextExtent(label).x + FromDIP(32)), FromDIP(32)));
    Init();
}

FluentButton::FluentButton(wxWindow* parent, wxWindowID id, const char* iconPath, const wxString& tooltip)
    : wxPanel(parent, id), m_icon(FluentIcon(iconPath, g.text)), m_accent(false)
{
    SetMinSize(FromDIP(wxSize(40, 32)));
    SetToolTip(tooltip);
    Init();
}

void FluentButton::Init()
{
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    Bind(wxEVT_PAINT, &FluentButton::OnPaint, this);
    Bind(wxEVT_ENTER_WINDOW, [this](wxMouseEvent&) { m_hover = true; Refresh(); });
    Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent&) { m_hover = false; Refresh(); });
    Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent&) {
        m_pressed = true;
        CaptureMouse();
        Refresh();
    });
    Bind(wxEVT_LEFT_UP, [this](wxMouseEvent& e) {
        const bool wasPressed = m_pressed;
        m_pressed = false;
        if (HasCapture())
            ReleaseMouse();
        Refresh();
        if (wasPressed && wxRect(GetClientSize()).Contains(e.GetPosition()))
            Click();
    });
    Bind(wxEVT_MOUSE_CAPTURE_LOST, [this](wxMouseCaptureLostEvent&) { m_pressed = false; Refresh(); });
    Bind(wxEVT_SET_FOCUS, [this](wxFocusEvent& e) { Refresh(); e.Skip(); }); // the focus ring
    Bind(wxEVT_KILL_FOCUS, [this](wxFocusEvent& e) { Refresh(); e.Skip(); });
    Bind(wxEVT_KEY_DOWN, [this](wxKeyEvent& e) {
        if (e.GetKeyCode() == WXK_SPACE || e.GetKeyCode() == WXK_RETURN)
            Click();
        else
            e.Skip();
    });
    // Enter never gets as far as KEY_DOWN in a dialog (Windows' dialog handling eats it): here
    // first, it presses the button that has the focus.
    Bind(wxEVT_CHAR_HOOK, [this](wxKeyEvent& e) {
        if ((e.GetKeyCode() == WXK_RETURN || e.GetKeyCode() == WXK_NUMPAD_ENTER) && e.GetModifiers() == wxMOD_NONE)
            Click();
        else
            e.Skip();
    });
}

void FluentButton::Click()
{
    wxCommandEvent event(wxEVT_BUTTON, GetId());
    event.SetEventObject(this);
    ProcessWindowEvent(event);
}

void FluentButton::SetText(const wxString& label)
{
    m_label = label;
    wxClientDC dc(this);
    dc.SetFont(UiFont(10));
    const int width = dc.GetTextExtent(label).x + FromDIP(32);
    if (width > GetMinSize().x)
    {
        SetMinSize(wxSize(width, GetMinSize().y));
        if (GetParent())
            GetParent()->Layout();
    }
    Refresh();
}

bool FluentButton::Enable(bool enable)
{
    if (!wxPanel::Enable(enable))
        return false;
    if (!enable)
        m_hover = m_pressed = false; // a disabled window gets no "mouse left" to clear them
    Refresh();
    return true;
}

void FluentButton::OnPaint(wxPaintEvent&)
{
    wxAutoBufferedPaintDC dc(this);
    dc.SetBackground(wxBrush(GetParent()->GetBackgroundColour()));
    dc.Clear();
    wxColour fill, text = g.text;
    if (!IsThisEnabled())
    {
        fill = m_accent ? Mix(g.accent, g.bg, 0.45) : g.control;
        text = m_accent ? Mix(g.onAccent, fill, 0.6) : Mix(g.text2, g.control, 0.55);
    }
    else if (m_accent)
    {
        fill = m_pressed ? Mix(g.accent, g.bg, 0.8) : m_hover ? Mix(g.accent, g.text, 0.9) : g.accent;
        text = g.onAccent;
    }
    else
        fill = m_pressed ? g.controlPressed : m_hover ? g.controlHover : g.control;
    const wxRect r(GetClientSize());
    FillRound(dc, r, FromDIP(4), fill, m_accent ? nullptr : &g.border);
    if (ShowsFocus(this))
        DrawFocusRing(this, dc, r, FromDIP(4));
    dc.SetFont(UiFont(10));
    dc.SetTextForeground(text);
    if (m_icon.IsOk())
    {
        const wxBitmap icon = m_icon.GetBitmapFor(this);
        dc.DrawBitmap(icon, (r.width - icon.GetLogicalWidth()) / 2, (r.height - icon.GetLogicalHeight()) / 2, true);
        return;
    }
    const wxSize ts = dc.GetTextExtent(m_label);
    dc.DrawText(m_label, (r.width - ts.x) / 2, (r.height - ts.y) / 2);
}

bool AskFluent(wxWindow* parent, const wxString& text, const wxString& yes, const wxString& no)
{
    wxDialog dialog(parent, wxID_ANY, wxTheApp ? wxTheApp->GetAppDisplayName() : wxString());
    dialog.SetBackgroundColour(g.bg);
    ApplyDwm(&dialog, false, &g.bg);
    const int pad = dialog.FromDIP(20);
    wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
    wxStaticText* label = new wxStaticText(&dialog, wxID_ANY, text);
    label->SetFont(UiFont(10));
    label->SetForegroundColour(g.text);
    label->Wrap(dialog.FromDIP(380));
    sizer->Add(label, 0, wxALL, pad);
    wxBoxSizer* buttons = new wxBoxSizer(wxHORIZONTAL);
    buttons->AddStretchSpacer();
    buttons->Add(new FluentButton(&dialog, wxID_OK, yes, true));
    if (!no.empty()) // without it: a message with one button
        buttons->Add(new FluentButton(&dialog, wxID_CANCEL, no), 0, wxLEFT, dialog.FromDIP(8));
    sizer->Add(buttons, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, pad);
    dialog.SetSizerAndFit(sizer);
    dialog.Bind(wxEVT_CHAR_HOOK, [&dialog](wxKeyEvent& e) {
        if (e.GetKeyCode() == WXK_ESCAPE)
            dialog.EndModal(wxID_CANCEL);
        else if (e.GetKeyCode() == WXK_RETURN || e.GetKeyCode() == WXK_NUMPAD_ENTER)
            dialog.EndModal(wxID_OK);
        else
            e.Skip();
    });
    dialog.CentreOnParent();
    return dialog.ShowModal() == wxID_OK;
}

// ---------------------------------------------------------------------------------------------
// FluentChoice
// ---------------------------------------------------------------------------------------------
namespace
{
// A dot of colour before an item's text, its left edge at x, centred on y; how far the text moves.
// No colour (one still to be picked): an empty ring.
int DrawSwatch(const wxWindow* w, wxDC& dc, const wxColour& colour, int x, int y)
{
    const int d = w->FromDIP(12);
    const wxRect r(x, y - d / 2, d, d);
    if (colour.IsOk())
        FillRound(dc, r, d / 2.0, colour, &g.border);
    else
        FillRound(dc, r, d / 2.0, g.card, &g.text2);
    return d + w->FromDIP(8);
}

// The open list of a FluentChoice, drawn the way Windows 11 draws it: a rounded card with a
// shadow, one row per item, the chosen one with an accent bar, the one under the mouse lighter.
class ChoicePopup : public wxPopupTransientWindow
{
public:
    std::function<void(int)> onPick;
    std::function<void()> onClosed;

    static const int kRow = 32, kPad = 4; // DIPs

    ChoicePopup(wxWindow* parent, const wxArrayString& items, int selection, int width,
                const std::vector<wxColour>& swatches)
        : wxPopupTransientWindow(parent, wxBORDER_NONE), m_items(items), m_swatches(swatches), m_selection(selection),
          m_hover(selection)
    {
        const wxSize size(width, FromDIP(kPad) * 2 + FromDIP(kRow) * (int)items.size());
        SetSize(size);
        m_panel = new wxPanel(this, wxID_ANY, wxPoint(0, 0), size, wxWANTS_CHARS);
        m_panel->SetBackgroundStyle(wxBG_STYLE_PAINT);
        m_panel->Bind(wxEVT_PAINT, &ChoicePopup::OnPaint, this);
        m_panel->Bind(wxEVT_MOTION, [this](wxMouseEvent& e) { SetHover(Row(e.GetPosition())); });
        // A row is picked when the button goes up on it after going down in the list (the press
        // that opened the list happened on the control, under it).
        m_panel->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent&) { m_pressed = true; });
        m_panel->Bind(wxEVT_LEFT_UP, [this](wxMouseEvent& e) {
            const int row = Row(e.GetPosition());
            if (m_pressed && row >= 0)
                Pick(row);
            m_pressed = false;
        });
        m_panel->Bind(wxEVT_CHAR_HOOK, [this](wxKeyEvent& e) {
            if (!Key(e.GetKeyCode()))
                e.Skip();
        });
        ApplyDwm(this, true, nullptr); // round corners and a shadow, like the calendar
    }

    // A key while the list is open (the focus stays on the control, which passes them here). True
    // when it was the list's: Esc and Enter must not reach the dialog (cancel, save) under it.
    bool Key(int code)
    {
        switch (code)
        {
        case WXK_UP:     SetHover(wxMax(0, m_hover - 1)); return true;
        case WXK_DOWN:   SetHover(wxMin((int)m_items.size() - 1, m_hover + 1)); return true;
        case WXK_HOME:   SetHover(0); return true;
        case WXK_END:    SetHover((int)m_items.size() - 1); return true;
        case WXK_RETURN:
        case WXK_NUMPAD_ENTER:
        case WXK_SPACE:  if (m_hover >= 0) Pick(m_hover); return true;
        case WXK_ESCAPE:
        case WXK_TAB:    DismissAndNotify(); return true;
        default:         return false;
        }
    }

    // Where row i starts, from the top of the list.
    int RowTop(int i) const { return FromDIP(kPad) + i * FromDIP(kRow); }

    void OpenAt(const wxPoint& pos)
    {
        Move(pos);
        Popup(m_panel);
        m_panel->SetFocus();
    }

protected:
    void OnDismiss() override
    {
        if (onClosed)
            onClosed();
        CallAfter([this] { Destroy(); }); // not inside its own handlers
    }

private:
    int Row(const wxPoint& p) const
    {
        if (p.y < FromDIP(kPad) || p.x < 0 || p.x >= m_panel->GetClientSize().x)
            return -1;
        const int row = (p.y - FromDIP(kPad)) / FromDIP(kRow);
        return row < (int)m_items.size() ? row : -1;
    }

    void SetHover(int row)
    {
        if (row != m_hover && row >= 0)
        {
            m_hover = row;
            m_panel->Refresh();
        }
    }

    void Pick(int row)
    {
        if (onPick)
            onPick(row);
        DismissAndNotify(); // Dismiss() alone would not call OnDismiss()
    }

    void OnPaint(wxPaintEvent&)
    {
        wxAutoBufferedPaintDC dc(m_panel);
        dc.SetBackground(wxBrush(g.card));
        dc.Clear();
        dc.SetFont(UiFont(10));
        const int width = m_panel->GetClientSize().x;
        for (int i = 0; i < (int)m_items.size(); i++)
        {
            const wxRect row(FromDIP(kPad), RowTop(i), width - 2 * FromDIP(kPad), FromDIP(kRow));
            const wxRect back = row.Deflate(0, FromDIP(2));
            if (i == m_hover)
                FillRound(dc, back, FromDIP(4), Mix(g.text, g.card, 0.09));
            else if (i == m_selection)
                FillRound(dc, back, FromDIP(4), Mix(g.text, g.card, 0.06));
            if (i == m_selection) // the accent bar of the chosen item
            {
                const int h = FromDIP(16), w = FromDIP(3);
                FillRound(dc, wxRect(back.x, back.y + (back.height - h) / 2, w, h), w / 2.0, g.accent);
            }
            int x = row.x + FromDIP(12);
            if (i < (int)m_swatches.size())
                x += DrawSwatch(m_panel, dc, m_swatches[i], x, row.y + row.height / 2);
            dc.SetTextForeground(g.text);
            const wxSize ts = dc.GetTextExtent(m_items[i]);
            dc.DrawText(m_items[i], x, row.y + (row.height - ts.y) / 2);
        }
    }

    wxPanel* m_panel;
    wxArrayString m_items;
    std::vector<wxColour> m_swatches;
    int m_selection, m_hover;
    bool m_pressed = false;
};
}

FluentChoice::FluentChoice(wxWindow* parent, const wxArrayString& items, int selection)
    : wxPanel(parent), m_items(items), m_selection(selection)
{
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    wxClientDC dc(this);
    dc.SetFont(UiFont(10));
    int widest = 0;
    for (const wxString& item : m_items)
        widest = wxMax(widest, dc.GetTextExtent(item).x);
    SetMinSize(wxSize(widest + FromDIP(52), FromDIP(32)));
    Bind(wxEVT_PAINT, &FluentChoice::OnPaint, this);
    Bind(wxEVT_ENTER_WINDOW, [this](wxMouseEvent&) { m_hover = true; Refresh(); });
    Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent&) { m_hover = false; Refresh(); });
    Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent&) { SetFocus(); });
    Bind(wxEVT_SET_FOCUS, [this](wxFocusEvent& e) { Refresh(); e.Skip(); }); // the focus ring
    Bind(wxEVT_KILL_FOCUS, [this](wxFocusEvent& e) { Refresh(); e.Skip(); });
    // The list opens when the button goes up: opened on the press, the release would land in it.
    // A click on the control while the list is open closes it (the list goes on the press), and
    // that same click must not open it again.
    Bind(wxEVT_LEFT_UP, [this](wxMouseEvent&) {
        if ((wxGetLocalTimeMillis() - m_closedAt).GetValue() > 300)
            Open();
    });
    // Keys come here first (a dialog would take the arrows for moving between controls): with the
    // list open they are the list's; closed, the arrows change the choice, Space opens it.
    Bind(wxEVT_CHAR_HOOK, [this](wxKeyEvent& e) {
        const int key = e.GetKeyCode();
        if (m_popupKey)
        {
            if (!m_popupKey(key))
                e.Skip();
            return;
        }
        if (key == WXK_SPACE || (key == WXK_DOWN && e.AltDown()))
            Open();
        else if (key == WXK_UP && e.GetModifiers() == wxMOD_NONE)
            Choose(m_selection - 1);
        else if (key == WXK_DOWN && e.GetModifiers() == wxMOD_NONE)
            Choose(m_selection + 1);
        else
            e.Skip();
    });
}

void FluentChoice::SetSelection(int selection)
{
    m_selection = selection;
    Refresh();
}

void FluentChoice::SetSwatches(const std::vector<wxColour>& swatches)
{
    const bool had = !m_swatches.empty();
    m_swatches = swatches;
    if (!had && !m_swatches.empty()) // room for the dot
        SetMinSize(GetMinSize() + wxSize(FromDIP(20), 0));
    Refresh();
}

void FluentChoice::Choose(int selection)
{
    if (selection < 0 || selection >= (int)m_items.size() || selection == m_selection)
        return;
    m_selection = selection;
    Refresh();
    if (onChange)
        onChange();
}

void FluentChoice::Open()
{
    if (m_open || m_items.empty())
        return;
    wxClientDC dc(this);
    dc.SetFont(UiFont(10));
    int widest = 0;
    for (const wxString& item : m_items)
        widest = wxMax(widest, dc.GetTextExtent(item).x);
    const int width = wxMax(GetClientSize().x, widest + FromDIP(m_swatches.empty() ? 40 : 60));
    ChoicePopup* popup = new ChoicePopup(this, m_items, m_selection, width, m_swatches);
    popup->onPick = [this](int i) {
        if (i == m_selection && onReselect)
            CallAfter([this, i] { onReselect(i); }); // once the list is gone
        else
            Choose(i);
    };
    popup->onClosed = [this] {
        m_open = false;
        m_popupKey = nullptr; // the list goes right after
        m_closedAt = wxGetLocalTimeMillis();
        Refresh();
    };
    m_popupKey = [popup](int key) { return popup->Key(key); };
    // As in Windows 11: the chosen item lies right over the control; the list stays on the screen.
    const int selected = m_selection >= 0 && m_selection < (int)m_items.size() ? m_selection : 0;
    const wxPoint origin = ClientToScreen(wxPoint(0, 0));
    wxPoint pos(origin.x, origin.y + (GetClientSize().y - FromDIP(ChoicePopup::kRow)) / 2 - popup->RowTop(selected));
    const wxRect area = wxDisplay(this).GetClientArea();
    const wxSize size = popup->GetSize();
    pos.x = wxMax(area.x, wxMin(pos.x, area.GetRight() - size.x));
    pos.y = wxMax(area.y, wxMin(pos.y, area.GetBottom() - size.y));
    m_open = true;
    Refresh();
    popup->OpenAt(pos);
}

void FluentChoice::OnPaint(wxPaintEvent&)
{
    wxAutoBufferedPaintDC dc(this);
    dc.SetBackground(wxBrush(GetParent()->GetBackgroundColour()));
    dc.Clear();
    const wxRect r(GetClientSize());
    FillRound(dc, r, FromDIP(4), m_hover ? g.controlHover : g.control, &g.border);
    if (ShowsFocus(this))
        DrawFocusRing(this, dc, r, FromDIP(4));

    dc.SetFont(UiFont(10));
    dc.SetTextForeground(g.text);
    const wxString text = m_selection >= 0 && m_selection < (int)m_items.size() ? m_items[m_selection] : wxString();
    int x = FromDIP(11);
    if (m_selection >= 0 && m_selection < (int)m_swatches.size())
        x += DrawSwatch(this, dc, m_swatches[m_selection], x, r.height / 2);
    const wxSize ts = dc.GetTextExtent(text);
    dc.DrawText(text, x, (r.height - ts.y) / 2);

    // The chevron, drawn smooth.
    std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::CreateFromUnknownDC(dc));
    if (gc)
    {
        const double cx = r.GetRight() - FromDIP(16), cy = r.height / 2.0, s = FromDIP(4);
        gc->SetPen(wxPen(g.text2, wxMax(1, FromDIP(1))));
        wxPoint2DDouble points[] = { { cx - s, cy - s / 2 }, { cx, cy + s / 2 }, { cx + s, cy - s / 2 } };
        gc->StrokeLines(3, points);
    }
}
