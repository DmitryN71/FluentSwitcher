// A list of words on a settings page - the lists of the automatic switch, the exceptions of ДВе ЗАглавные - on a card
// that opens in place, as the expandable settings of Windows 11. Closed: the title, what the list is for and how many
// words it has. Open: a field that adds a word (Enter) and finds it in the list while it is typed, and the words one to
// a row, the newest first - how the word looks in the other layout, "выучено" when the app added it itself, a cross that
// removes it. No window of its own: the words go into the settings as they change, Save or Apply writes them.
// (Дмитрий 07.10: instead of the window where every word had to be typed on a line of its own; Caramba Switcher's list
// as the model, with one entry for both layouts where Caramba has two.)
#pragma once

#include "fluent_controls.h"

#include <wx/msw/wrapwin.h>

#include <functional>
#include <vector>

class TextField;
class WordRows;

class WordListCard : public Card
{
public:
    enum Kind
    {
        Never,  // "Не переключать": either form - cv is см as well
        Always, // "Переключать всегда": the word as it should be - "http" is typed as "реез"
        Caps,   // the exceptions of ДВе ЗАглавные: as written, letter case and all; a word covers the words that begin
                // with it (from 4 letters, TwoCaps::Matches)
    };

    // A word added or removed: Words() and Learned() are the new lists.
    std::function<void()> onChange;

    // The title is the card's first child (CardTip finds it there). layouts: those that take part in the switch, for the
    // other forms of a word.
    WordListCard(wxWindow* page, Kind kind, const wxString& title, const wxString& about, const std::vector<HKL>& layouts);

    // The words in the order of the file (the newest last), and which of them the app learned.
    void SetWords(const wxArrayString& words, const wxArrayString& learned);
    const wxArrayString& Words() const { return m_words; }
    const wxArrayString& Learned() const { return m_learned; }

private:
    void Toggle();
    void Add();
    void Remove(int index);
    // The word of the list that `word` repeats (*covered false) or that has it as its other form or covers it (true);
    // -1 - none.
    int Find(const wxString& word, bool* covered) const;
    void Refill(); // the rows, the hint, the count after a change of the words or of the field
    void Relayout();

    Kind m_kind;
    std::vector<HKL> m_layouts;
    wxArrayString m_words, m_learned;
    std::vector<wxArrayString> m_forms; // of each word: its text in the other layouts
    wxString m_about;
    bool m_open = false;

    wxStaticText* m_count = nullptr;
    wxWindow* m_chevron = nullptr;
    wxPanel* m_body = nullptr;
    TextField* m_field = nullptr;
    wxStaticText* m_hint = nullptr;
    WordRows* m_rows = nullptr;
    wxString m_done; // what the last add or remove did, for the hint until the field changes
};
