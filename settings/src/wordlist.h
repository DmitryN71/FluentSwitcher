// The lists of words - "Не переключать" and "Переключать всегда" of the automatic switch, the exceptions of ДВе
// ЗАглавные - in a window of their own (Дмитрий 07.10: on the page, fifty words made the page endless; before that, a
// text box where every word had to be typed on a line of its own). One field adds a word (Enter) and finds it in the
// list while it is typed; the words one to a row, the newest first, with how each looks in the other layout, "выучено"
// for those the app added itself, a cross that removes it. Caramba Switcher's list as the model, with one entry for both
// layouts where Caramba has two.
#pragma once

#include "fluent_controls.h"

#include <wx/msw/wrapwin.h>

#include <vector>

enum class WordKind
{
    Never,  // "Не переключать": either form - cv is см as well
    Always, // "Переключать всегда": kept as it should be (http); typed is its form in the other layout (реез)
    Caps,   // the exceptions of ДВе ЗАглавные: as written, letter case and all; a word covers the words that begin
            // with it (from 4 letters, TwoCaps::Matches)
};

// A list as the file has it: the words (the newest last) and those of them the app learned.
struct WordListWords
{
    wxArrayString words, learned;
};

// The window of a list. layouts: those that take part in the switch, for the other forms of the words. True - "Готово":
// *list is the new list.
bool EditWordList(wxWindow* parent, WordKind kind, const wxString& title, const wxString& about,
                  const std::vector<HKL>& layouts, WordListWords* list);
