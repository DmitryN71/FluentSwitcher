// The languages of the settings window: Russian (the texts in the code), English (i18n.cpp) and Ukrainian
// (i18n_uk.cpp). The language is the engine's "gui_lang" (the same for the flag's menu); without one, as Windows.
#pragma once

#include <wx/string.h>

// N_("...") (wxWidgets' own, wx/translation.h): a text shown later through T(), marked for
// tools/extract_strings.py.
#include <wx/translation.h>

enum class Language
{
    Russian,
    English,
    Ukrainian,
};

void SetLanguage(Language language);
Language CurrentLanguage();
bool IsEnglish();

// "gui_lang" of the engine ("Russian", "English", "Ukrainian") -> the language; empty: the language of Windows
// (Russian or Ukrainian Windows - that one, any other - English).
Language LanguageFor(const wxString& guiLang);

// The text in the window's language (UTF-8 Russian in, as written in the code).
wxString T(const char* utf8);
