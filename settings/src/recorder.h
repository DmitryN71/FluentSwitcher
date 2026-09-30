// Recording a hotkey from the keys pressed, in the engine's format (VkNames.h, CHotKey::ToString):
// "Ctrl + Break", "Shift #double", "Win + Shift #up". No window here, so it can be tested with
// made-up key events (settings/tools/test_recorder.cpp).
//
// How a recording ends:
//   - all keys are let go, and nothing more is pressed for `doubleMs` -> the keys held together;
//     modifiers alone ("Shift", "Win + Shift") get "#up": fired on press they would get in the way
//     of every Shift+letter;
//   - the same keys pressed a second time within `doubleMs` -> "#double" at once.
// Keys are kept in the order the engine wants: modifiers first, the main key last.
#pragma once

#include <windows.h>

#include <string>
#include <vector>

// The engine's name of a key ("Ctrl", "LCtrl", "Break", "OEM_2", "VK_e9" for the rest).
// kWin: the engine's own "either Win" code (VKE_WIN).
const unsigned kWin = 2047;
std::string VkName(unsigned vk);
bool IsModifier(unsigned vk);

class HotkeyRecorder
{
public:
    // sides: keep left and right modifiers apart (LCtrl / RCtrl); else Ctrl, Shift, Alt, Win.
    void Start(bool sides, unsigned long doubleMs);
    // A key from the keyboard hook (vk as the hook reports it; Ctrl+Break comes as VK_CANCEL).
    void Key(unsigned vk, bool down, unsigned long timeMs);
    // Now and then (a timer): ends the recording once doubleMs have passed since everything was let go.
    void Tick(unsigned long timeMs);
    bool Done() const { return m_done; }
    // The recorded hotkey in the engine's format; empty if nothing was recorded.
    const std::string& Result() const { return m_result; }

private:
    unsigned Normalize(unsigned vk) const;
    std::string Name(const std::vector<unsigned>& keys, const char* suffix) const;
    void Finish(const std::string& result);

    bool m_sides = false;
    unsigned long m_doubleMs = 280;
    std::vector<unsigned> m_down;   // held now
    std::vector<unsigned> m_chord;  // everything pressed since the last time all were let go
    std::vector<unsigned> m_last;   // the chord before, when it may yet become "#double"
    unsigned long m_releasedAt = 0;
    bool m_waiting = false;         // all let go, waiting to see if the same comes again
    bool m_done = false;
    std::string m_result;
};
