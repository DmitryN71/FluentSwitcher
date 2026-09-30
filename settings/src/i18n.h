// The languages of the settings window: Russian (the texts in the code) and English (i18n.cpp).
// The language is the engine's "gui_lang" (the same for the flag's menu); without one, as Windows.
#pragma once

#include <wx/string.h>

// N_("...") (wxWidgets' own, wx/translation.h): a text shown later through T(), marked for
// tools/extract_strings.py.
#include <wx/translation.h>

void SetEnglish(bool english);
bool IsEnglish();

// "gui_lang" of the engine ("Russian", "English") -> English or not; empty: the language of Windows.
bool EnglishFor(const wxString& guiLang);

// The text in the window's language (UTF-8 Russian in, as written in the code).
wxString T(const char* utf8);
