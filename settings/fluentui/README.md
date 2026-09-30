# Fluent UI for wxWidgets

The Windows 11 look of FluentClipper, for any wxWidgets program on Windows: theme colors, fonts,
the window frame, icons, and the controls of a settings window. MIT license, © 2026 Dmitry Novikov
(see `LICENSE`). Only the look is here; FluentClipper's own code is not.

## Files

| File | What is in it |
|---|---|
| `fluent_ui.h/.cpp` | `Theme g` and `MakeTheme` (dark / light, accent), `IsWindows11`, `UiFont`, `ApplyDwm` (caption color, round corners), `Svg` and `FluentIcon`, `FillRound` / `FillInput` / `Mix`, the keyboard focus ring (`SetKeyboardCues`, `ShowsFocus`, `DrawFocusRing`), `SetWrappedLabel`, `FluentButton`, `FluentChoice` (drop-down list), `AskFluent` (a question box) |
| `fluent_controls.h/.cpp` | `ToggleSwitch`, `NumberField`, `HotkeyView` (the look of a hotkey field) with `NoteLine` / `SetNoteLine` (the line under it), `Card`, `NewSettingsPage`, `AddSettingsCard`, `SetCardDescription`, `SectionNav` (the sections on the left), `ColourGrid` (the 48 accent colors of Windows), `FluentText`, `WarningColour` |
| `LICENSE-FluentUI-System-Icons.txt` | Microsoft's MIT license for the icon paths (`FluentIcon` draws the "d" of an icon from github.com/microsoft/fluentui-system-icons; `HotkeyView` has the "dismiss" one built in) |

Nothing in here has texts of its own: every label is given by the caller.

## Needs

- wxWidgets 3.3 (core, base), Unicode, MSW. Windows 10 or 11; on Windows 10 there are no round corners.
- Link `dwmapi` and `advapi32` (FluentClipper links the usual wxMSW set, see its build.bat).
- A manifest for Common Controls 6, as any wxMSW program (`wx/msw/wx.rc` or your own): without it
  Windows gives the old comctl32, and the program stops at the start (0xC0000139).
- Texts in quotes are ASCII; others come as `wxString::FromUTF8("...")` or `L"..."`.
- The compiler flags of the program: C++17.

## Setting up

```cpp
#include "fluent_controls.h"

bool MyApp::OnInit()
{
    SetAppDisplayName("FluentSwitcher"); // the title of AskFluent
    g = MakeTheme(wxSystemSettings::GetAppearance().IsDark(), "app"); // before the first window
    ...
}

// The focus ring shows only while the keyboard is in use, as in Windows 11.
int MyApp::FilterEvent(wxEvent& e)
{
    const wxEventType type = e.GetEventType();
    if (type == wxEVT_CHAR_HOOK)
    {
        const int key = static_cast<wxKeyEvent&>(e).GetKeyCode();
        if (key == WXK_TAB || key == WXK_UP || key == WXK_DOWN || key == WXK_LEFT || key == WXK_RIGHT)
            SetKeyboardCues(true);
    }
    else if (type == wxEVT_LEFT_DOWN || type == wxEVT_RIGHT_DOWN || type == wxEVT_MIDDLE_DOWN)
        SetKeyboardCues(false);
    return Event_Skip;
}
```

## A settings window

As FluentClipper's: the sections on the left, the title and the cards of the section on the right.

```cpp
SetBackgroundColour(g.bg);
ApplyDwm(this, false, &g.bg);
SectionNav* nav = new SectionNav(this);
wxStaticText* title = FluentText(this, "General", 20, g.text, true);
wxBoxSizer* pages = new wxBoxSizer(wxVERTICAL);

wxBoxSizer* column = nullptr;
wxScrolledWindow* page = NewSettingsPage(this, &column);
pages->Add(page, 1, wxEXPAND);
nav->AddSection(kIconSettings, "General"); // a Fluent icon path of your own

ToggleSwitch* autostart = nullptr;
AddSettingsCard(page, column, "Start with Windows", "Runs hidden, only the tray icon shows",
                [&](wxWindow* card) { return autostart = new ToggleSwitch(card, true); });
autostart->onChange = [&] { /* ... */ };

wxBoxSizer* right = new wxBoxSizer(wxVERTICAL);
right->Add(title, 0, wxTOP | wxBOTTOM, FromDIP(12));
right->Add(pages, 1, wxEXPAND);
wxBoxSizer* sizer = new wxBoxSizer(wxHORIZONTAL);
sizer->Add(nav, 0, wxEXPAND | wxLEFT | wxTOP | wxBOTTOM, FromDIP(8));
sizer->Add(right, 1, wxEXPAND | wxLEFT, FromDIP(20));
SetSizer(sizer);
nav->onSelect = [&](int section) { /* show that page, hide the others, set the title */ };
```

## A hotkey field

`HotkeyView` only draws. Derive from it and do the recording yourself:

- `onRecord` comes on a click;
- bind `wxEVT_CHAR_HOOK` for the keys;
- `SetText(text, grey)` shows the hotkey, or a grey placeholder while there is none or while recording;
- `SetRecording(true)` draws the accent frame;
- `SetClearable(true, tooltip)` shows the cross, and `onClear` comes when it is clicked.

Put a `NoteLine` under it for advice or a refusal (`SetNoteLine(line, text, hint)`). FluentClipper's
`HotkeyField` (settings.cpp) is one such field.

## Where the files live

The source is `FluentClipper\fluentui`: FluentClipper is built from these very files. Another
program keeps a copy; a change is made here first and copied on.
