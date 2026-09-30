// Hotkeys as the engine keeps them in SimpleSwitcher.json and as this window shows them.
// Stored: "Ctrl + Shift + K", "Win + Shift #up" (on release), "Shift #double" (pressed twice), up to two
// per action separated by a comma: "Break, Shift + F24".
#pragma once

#include <wx/string.h>

#include <vector>

struct HotkeyAction
{
    const char* key;         // the name in "hotkeys" (the engine's HotKeyType)
    const char* title;       // UTF-8
    const char* description; // UTF-8
};

// The actions shown on the Hotkeys page, in the order shown.
const std::vector<HotkeyAction>& HotkeyActions();

// One stored hotkey list in words: "Shift + F24 #double" -> "Shift + F24 дважды"; two of them are
// joined with "или". Empty for none.
wxString HotkeyDisplay(const wxString& stored);
