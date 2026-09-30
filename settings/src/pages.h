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


class SettingsFrame : public wxFrame
{
public:
    // folder: where SimpleSwitcher.exe and its SimpleSwitcher.json are. enginePid: a test engine, 0 = the
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

    void BuildGeneral();
    void BuildTyping();
    void BuildHotkeys();
    void BuildLayouts();
    void BuildCommands();
    void FillCommands(); // the cards of the commands again (one added, removed, its kind changed)
    void BuildAdvanced();
    void BuildAbout();

    void ShowSection(int section);
    // A note if another action has the hotkey `one` too.
    wxString SameHotkey(const wxString& one, const char* except) const;
    void RefreshEngine();
    bool HasChanges() const;
    bool Apply();
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
    ToggleSwitch* m_enabledSwitch = nullptr;
    ToggleSwitch* m_autostartSwitch = nullptr;
    wxWindow* m_notRunningCard = nullptr;

    SectionNav* m_nav = nullptr;
    wxStaticText* m_title = nullptr;
    wxStaticText* m_status = nullptr;
    wxBoxSizer* m_pagesSizer = nullptr;
    wxScrolledWindow* m_page = nullptr;
    wxBoxSizer* m_column = nullptr;
    std::vector<wxScrolledWindow*> m_pages;
    wxArrayString m_titles;

    wxScrolledWindow* m_commandsPage = nullptr;
    wxBoxSizer* m_commandsColumn = nullptr;

    FluentButton* m_apply = nullptr;
    wxTimer m_savedTimer;
    bool m_savedShown = false;
    int m_section = 0;         // shown now
    bool m_restarted = false;  // Apply started the window again (a new language): this one closes
};
