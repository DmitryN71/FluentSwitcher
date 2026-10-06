// The settings window: sections on the left, the cards of the section shown on the right, Save /
// Apply / Cancel at the bottom, as in FluentClipper. Changes are kept in a copy of the config until
// Save or Apply writes SimpleSwitcher.json and tells the engine to read it again.
#pragma once

#include "config.h"
#include "fluent_controls.h"
#include "i18n.h"

#include <wx/frame.h>
#include <wx/timer.h>

#include <functional>
#include <vector>

namespace Update { struct State; } // Update.h


class SettingsFrame : public wxFrame
{
public:
    // folder: where FluentSwitcher.exe and its FluentSwitcher.json are. enginePid: a test engine, 0 = the
    // one in `folder`. loadError: the config could not be read (shown; Save is off then).
    SettingsFrame(const Config& config, const wxString& folder, unsigned long enginePid, const wxString& loadError,
                  int section = 0);

private:
    // Pages and cards.
    void Section(const char* icon, const wxString& title);
    void FinishPage();
    wxWindow* Toggle(const wxString& title, const wxString& description, const char* key, bool def);
    wxWindow* Choice(const wxString& title, const wxString& description, const wxArrayString& items, int selection,
                     std::function<void(int)> picked, bool below = false);
    // A card with a list of words (the exceptions of ДВе ЗАглавные, the lists of the automatic switch): how many there
    // are, "Изменить…" opens the list in a window of its own. key - the array in the file; tip - the details, shown
    // when the mouse is over the card; label - the member that keeps the card's description (it changes after the
    // list is edited); defaults - the list while the file has none (the engine's own defaults, Settings.h).
    void WordListCard(const char* key, const wxString& title, const wxString& about, const wxString& tip,
                      const wxString& editAbout, wxStaticText* SettingsFrame::*label,
                      const wxArrayString& defaults = wxArrayString());

    void BuildGeneral();
    void BuildTyping();
    void BuildHotkeys();
    void BuildLayouts();
    void BuildFlags();
    void BuildCommands();
    void FillCommands(); // the cards of the commands again (one added, removed, its kind changed)
    void BuildAdvanced();
    void BuildAbout();
    // What the last update check found (update.json, Update.h); m_updatePage: a newer version's page, the
    // page of all versions when GitHub could not be reached (the browser may still get there), or empty.
    wxString UpdateText(const Update::State& state);
    void CheckUpdateNow(FluentButton* button);

    void ShowSection(int section);
    // A note if another action has the hotkey `one` too.
    wxString SameHotkey(const wxString& one, const char* except) const;
    void RefreshEngine();
    bool HasChanges() const;
    bool Apply();
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
    wxWindow* m_notRunningCard = nullptr;

    SectionNav* m_nav = nullptr;
    wxStaticText* m_title = nullptr;
    wxStaticText* m_status = nullptr;
    wxBoxSizer* m_pagesSizer = nullptr;
    wxScrolledWindow* m_page = nullptr;
    wxBoxSizer* m_column = nullptr;
    std::vector<wxScrolledWindow*> m_pages;
    wxArrayString m_titles;

    wxStaticText* m_updateLabel = nullptr; // the description of the "Обновления" card
    wxStaticText* m_twoCapsExceptions = nullptr; // the description of the exceptions card of ДВе ЗАглавные
    wxStaticText* m_autoSwitchExceptions = nullptr; // the same of the automatic layout switch: never switched
    wxStaticText* m_autoSwitchForce = nullptr;      // ... and switched always
    wxString m_updatePage;

    wxScrolledWindow* m_commandsPage = nullptr;
    wxBoxSizer* m_commandsColumn = nullptr;

    FluentButton* m_apply = nullptr;
    wxTimer m_savedTimer;
    bool m_savedShown = false;
    int m_section = 0;         // shown now
    bool m_restarted = false;  // Apply started the window again (a new language): this one closes
};
