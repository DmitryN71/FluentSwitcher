// Hotkeys as the engine keeps them in SimpleSwitcher.json and as this window shows and records them.
// Stored: "Ctrl + Shift + K", "Win + Shift #up" (on release), "Shift #double" (pressed twice), up to two
// per action separated by a comma: "Break, Shift + F24".
#pragma once

#include "fluent_controls.h"
#include "recorder.h"

#include <wx/timer.h>

#include <functional>
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

// The hotkeys of a stored list, one by one ("Break, Shift + F24" -> "Break", "Shift + F24").
std::vector<wxString> SplitHotkeys(const wxString& stored);

// A hotkey list of the engine as fields to record: `slots` fields in a row (two for an action, one
// for a key combination to send), and a line under them for advice. A click on a field records: the
// keys pressed next, caught by a keyboard hook that is there only while recording (and keeps them from
// the engine and from Windows meanwhile). Esc or a click elsewhere cancels; the cross clears.
class HotkeyEditor : public wxPanel
{
public:
    std::function<void()> onChange;
    // A recorded hotkey that some other action has too: a note about it, or empty.
    std::function<wxString(const wxString& one)> sameAs;

    // sides: LCtrl / RCtrl apart. plain: keys to send, not to catch - no "#up" / "#double".
    HotkeyEditor(wxWindow* parent, const wxString& stored, int slots, bool sides, bool plain = false);
    ~HotkeyEditor() override;
    wxString Value() const;
    void SetSides(bool sides) { m_sides = sides; }

    // Ends any recording (the window is left, for one).
    static void CancelRecording();
    // Two presses within this many ms are "#double" (the engine's quick_press_ms).
    static unsigned long s_doubleMs;

private:
    void Record(int slot);
    void Stop();
    void Finish(const std::string& result);
    void ShowSlot(int slot);
    static LRESULT CALLBACK HookProc(int code, WPARAM wParam, LPARAM lParam);

    std::vector<wxString> m_values;
    std::vector<HotkeyView*> m_views;
    wxStaticText* m_note = nullptr;
    bool m_sides;
    bool m_plain;
    int m_recordingSlot = -1;
    unsigned long m_recordingSince = 0;
    HotkeyRecorder m_recorder;
    wxTimer m_timer;

    static HotkeyEditor* s_recording;
    static HHOOK s_hook;
};
