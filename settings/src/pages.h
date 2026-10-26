// The settings window: sections on the left, the cards of the section shown on the right, OK /
// Apply / Cancel at the bottom, as in FluentClipper. Changes are kept in a copy of the config until
// OK or Apply writes FluentSwitcher.json and tells the engine to read it again.
#pragma once

#include "config.h"
#include "fluent_controls.h"
#include "i18n.h"
#include "wordlist.h"

#include <wx/frame.h>
#include <wx/timer.h>
#include <wx/weakref.h>

#include <functional>
#include <unordered_map>
#include <vector>

namespace Update { struct State; } // Update.h
class TextField;                     // textfield.h


class SettingsFrame : public wxFrame
{
public:
    // folder: where FluentSwitcher.exe and its FluentSwitcher.json are. enginePid: a test engine, 0 = the
    // one in `folder`. loadError: the config could not be read (shown; OK and Apply do not save then). section: the one
    // to open on; -1 - the one it was closed on (RestorePlacement).
    SettingsFrame(const Config& config, const wxString& folder, unsigned long enginePid, const wxString& loadError,
                  int section = -1);

private:
    // Pages and cards.
    void Section(const char* icon, const wxString& title);
    void FinishPage();
    wxWindow* Toggle(const wxString& title, const wxString& description, const char* key, bool def);
    wxWindow* Choice(const wxString& title, const wxString& description, const wxArrayString& items, int selection,
                     std::function<void(int)> picked, bool below = false);
    // A choice of numbers: each item is a number in the file; a number that is not in the list shows as the nearest.
    wxWindow* Numbers(const wxString& title, const wxString& description, const char* key, int def,
                      const std::vector<int>& numbers, const wxArrayString& names);
    // The look of the tray icon or a flag (key: flagsSet, caret_flag_set, mouse_flag_set): the flag sets (the folders
    // in "flags" next to the program), the letters, for the tray also the app's icon. follow - the name of a first item
    // with the empty value: "as the one it follows" (the flag on the mouse pointer - as the flag at the cursor).
    wxWindow* FlagLook(const wxString& title, const wxString& description, const char* key, bool appIcon,
                       const char* inherit = "flagsSet", const wxString& follow = wxString());
    // A card with a list of words (the lists of the automatic switch, the exceptions of ДВе ЗАглавные): what it is for
    // and how many words it has; "Изменить…" opens the list in a window of its own (wordlist.h). key - the array in the
    // file; help - the text of that window; tip - the details, the tooltip of the title; defaults - the list while the
    // file has none (the engine's own defaults, Settings.h).
    void WordList(const char* key, WordKind kind, const wxString& title, const wxString& about, const wxString& help,
                  const wxString& tip, const wxArrayString& defaults = wxArrayString());
    // The counts of the lists' cards from m_edit again (after the window of a list, after Apply took the words the
    // engine learned meanwhile).
    void RefillWordLists();
    // A word just added to "Не переключать" or "Переключать всегда" takes out of the other what stands in its way (one
    // word - one list); the line to show at the bottom, or nothing.
    wxString LeaveOtherList(WordKind kind, const wxArrayString& was, const wxArrayString& now);
    // The entries of "Переключать всегда" that "Не переключать" covers (the exception wins: they never worked) - out of
    // m_edit; the line to show at the bottom, or nothing. When the window opens and at Apply.
    wxString DropCoveredForced();
    // The counts of switch-backs of the words moved into or out of the two lists since `saved` start over (in m_edit).
    void RestartMovedCounts(const Config& saved);
    // The list `key` while the file has none (the card's defaults); `words` out of it in m_edit, with their marks.
    wxArrayString DefaultsOf(const char* key) const;
    void DropWords(const char* key, const wxArrayString& words);
    // The words the engine learned while this window is open: into what it shows and into what it read (no change of
    // its own). When the window comes to the front and before a list opens.
    void TakeLearnedNow();

    void BuildGeneral();
    void BuildAutoSwitch();
    void BuildTyping();
    void BuildHotkeys();
    void BuildLayouts();
    void BuildTray();
    void BuildCaretFlag();
    void BuildSounds();
    void BuildCommands();
    void FillCommands(); // the cards of the commands again (one added, removed, its kind changed)
    void BuildAdvanced();
    void BuildAbout();
    // What the last update check found (update.json, Update.h); m_updatePage: a newer version's page, the
    // page of all versions when GitHub could not be reached (the browser may still get there), or empty.
    // m_updateInstall: the newer version can be installed by a click (its file and checksum known, UpdateInstall.h).
    wxString UpdateText(const Update::State& state);
    wxString UpdateButtonText() const;
    void CheckUpdateNow(FluentButton* button);

    void ShowSection(int section);
    // The search (the field above the sections; 1.5.9): the cards with all the words of the query - in the title, the
    // description, the details, the section's name, the items of the card's list or its hotkeys. Those with them in the
    // title first, then in the visible texts, then elsewhere.
    struct Found
    {
        int section;
        wxWindow* card;
        wxString title, about;
    };
    std::vector<Found> FindCards(const std::vector<wxString>& words) const;
    // The page of the results for the field's text, made anew (the cards change: the commands, the hidden ones); an empty
    // field - back to the section shown before.
    void Search();
    // A found card: its section, the card in sight and its control focused, the card outlined for a moment.
    void OpenFound(int section, wxWindow* card);
    void PaintHighlight(wxPaintEvent& e);
    void EndHighlight();
    // A note if another action has the hotkey `one` too.
    wxString SameHotkey(const wxString& one, const char* except) const;
    // A single press of keys that some action has "twice" too waits for the second press (the engine's quick_press_ms;
    // init0 on the forum, 22.10.2026: the layout came late, the first letters went in the old one). For the hotkeys
    // `stored`: why they fire later and how to help it, or empty.
    wxString DoubleWait(const wxString& stored) const;
    // The cards of the actions and of the layouts say so in their descriptions, again after every change of a hotkey.
    void RefreshDoubleWaits();
    struct DoubleWaitCard
    {
        wxStaticText* label;                // the card's description
        wxString about;                     // its own text; empty - hidden without the note (the layouts)
        std::function<wxString()> hotkeys;  // the card's hotkeys now
        wxString shown;                     // the note it shows now
    };
    std::vector<DoubleWaitCard> m_doubleWaits;
    void RefreshEngine();
    bool HasChanges() const;
    // The window's place and size from the last time ("settings_window"); false - none or off the screens now.
    bool RestorePlacement();
    // The section it was closed on ("settings_window" too), 0 - none.
    int LastSection() const;
    void SavePlacement();
    // reopen: a new language or theme opens the window again in it (Apply); false - the window is closing (Сохранить)
    // or busy otherwise, the new look comes with the next opening.
    bool Apply(bool reopen = true);
    // "Work in programs run as administrator" is on, the engine has no rights: offers to restart it as
    // administrator (Windows asks once) and does. False: not restarted.
    bool RestartElevated();
    void Changed();
    void SetStatus(const wxString& text, bool warning);

    Config m_saved, m_edit;
    wxString m_folder;
    unsigned long m_enginePid;
    bool m_canSave;

    HWND m_engine = nullptr;
    long m_state = 0;              // Engine::GetState when last asked; 0 = not running
    bool m_enabled = true;         // as the switches show them
    bool m_autostart = false;
    bool m_logging = false;        // the debug log
    ToggleSwitch* m_enabledSwitch = nullptr;
    ToggleSwitch* m_autostartSwitch = nullptr;
    ToggleSwitch* m_loggingSwitch = nullptr;
    FluentButton* m_reportStart = nullptr; // "Начать запись" / "Остановить запись" of the report to the developer
    wxWindow* m_notRunningCard = nullptr;

    SectionNav* m_nav = nullptr;
    wxStaticText* m_title = nullptr;
    wxStaticText* m_status = nullptr;
    wxBoxSizer* m_pagesSizer = nullptr;
    wxScrolledWindow* m_page = nullptr;
    wxBoxSizer* m_column = nullptr;
    std::vector<wxScrolledWindow*> m_pages;
    wxArrayString m_titles;

    TextField* m_search = nullptr;
    wxScrolledWindow* m_searchPage = nullptr;
    wxBoxSizer* m_searchColumn = nullptr;
    std::vector<wxWindow*> m_searchHits; // the rows of the results, in order
    bool m_searching = false;            // the results are shown (m_section - the section to go back to)
    std::unordered_map<wxWindow*, wxString> m_searchExtra; // a card -> the items of its list (Choice), for the search
    wxWeakRef<wxWindow> m_highlight;     // the found card, outlined (the commands' cards may go meanwhile)
    wxTimer m_highlightTimer;

    wxStaticText* m_updateLabel = nullptr; // the description of the "Обновления" card
    struct WordListOnPage
    {
        const char* key;
        WordKind kind;
        wxArrayString defaults;
        wxString about;
        wxStaticText* label; // the card's description: what the list is for and how many words it has
    };
    std::vector<WordListOnPage> m_wordLists;
    wxString m_updatePage;
    bool m_updateInstall = false;

    wxScrolledWindow* m_commandsPage = nullptr;
    wxBoxSizer* m_commandsColumn = nullptr;

    FluentButton* m_apply = nullptr;
    wxTimer m_savedTimer;
    bool m_savedShown = false;
    int m_section = 0;         // shown now
    bool m_restarted = false;  // Apply started the window again (a new language, theme): the new one closes this one
};
