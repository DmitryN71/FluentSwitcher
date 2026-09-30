#include "recorder.h"

#include <algorithm>
#include <cstdio>

// Keep in step with the engine's src/libtools/utils/VkNames.h.
std::string VkName(unsigned vk)
{
    if (vk == kWin) return "Win";
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) return std::string(1, (char)vk);
    if (vk >= VK_F1 && vk <= VK_F24) return "F" + std::to_string(vk - VK_F1 + 1);
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) return "NUMPAD" + std::to_string(vk - VK_NUMPAD0);
    switch (vk)
    {
    case VK_CAPITAL: return "CapsLock";
    case VK_SHIFT: return "Shift";
    case VK_LSHIFT: return "LShift";
    case VK_RSHIFT: return "RShift";
    case VK_CONTROL: return "Ctrl";
    case VK_LCONTROL: return "LCtrl";
    case VK_RCONTROL: return "RCtrl";
    case VK_MENU: return "Alt";
    case VK_LMENU: return "LAlt";
    case VK_RMENU: return "RAlt";
    case VK_LWIN: return "LWin";
    case VK_RWIN: return "RWin";
    case VK_APPS: return "Apps";
    case VK_ESCAPE: return "Esc";
    case VK_MULTIPLY: return "MULTIPLY";
    case VK_ADD: return "ADD";
    case VK_SEPARATOR: return "SEPARATOR";
    case VK_SUBTRACT: return "SUBTRACT";
    case VK_DECIMAL: return "DECIMAL";
    case VK_DIVIDE: return "DIVIDE";
    case VK_INSERT: return "Insert";
    case VK_DELETE: return "Delete";
    case VK_HOME: return "Home";
    case VK_END: return "End";
    case VK_NEXT: return "PageDown";
    case VK_PRIOR: return "PageUp";
    case VK_PAUSE: return "Break";
    case VK_CANCEL: return "Break"; // Ctrl+Break: the engine reads both as the one key Break
    case VK_SNAPSHOT: return "PrintScreen";
    case VK_SCROLL: return "ScrollLock";
    case VK_NUMLOCK: return "NumLock";
    case VK_SPACE: return "Space";
    case VK_BACK: return "Backspace";
    case VK_RETURN: return "Enter";
    case VK_TAB: return "Tab";
    case VK_OEM_1: return "OEM_1";
    case VK_OEM_2: return "OEM_2";
    case VK_OEM_3: return "OEM_3";
    case VK_OEM_4: return "OEM_4";
    case VK_OEM_5: return "OEM_5";
    case VK_OEM_6: return "OEM_6";
    case VK_OEM_7: return "OEM_7";
    case VK_OEM_8: return "OEM_8";
    case VK_OEM_AX: return "OEM_AX";
    case VK_OEM_102: return "OEM_102";
    case VK_OEM_PLUS: return "OEM_PLUS";
    case VK_OEM_COMMA: return "OEM_COMMA";
    case VK_OEM_MINUS: return "OEM_MINUS";
    case VK_OEM_PERIOD: return "OEM_PERIOD";
    case VK_BROWSER_BACK: return "BROWSER_BACK";
    case VK_BROWSER_FORWARD: return "BROWSER_FORWARD";
    case VK_BROWSER_REFRESH: return "BROWSER_REFRESH";
    case VK_BROWSER_STOP: return "BROWSER_STOP";
    case VK_BROWSER_SEARCH: return "BROWSER_SEARCH";
    case VK_BROWSER_FAVORITES: return "BROWSER_FAVORITES";
    case VK_BROWSER_HOME: return "BROWSER_HOME";
    case VK_VOLUME_MUTE: return "VOLUME_MUTE";
    case VK_VOLUME_DOWN: return "VOLUME_DOWN";
    case VK_VOLUME_UP: return "VOLUME_UP";
    case VK_MEDIA_NEXT_TRACK: return "MEDIA_NEXT_TRACK";
    case VK_MEDIA_PREV_TRACK: return "MEDIA_PREV_TRACK";
    case VK_MEDIA_STOP: return "MEDIA_STOP";
    case VK_MEDIA_PLAY_PAUSE: return "MEDIA_PLAY_PAUSE";
    case VK_LAUNCH_MAIL: return "LAUNCH_MAIL";
    case VK_LAUNCH_MEDIA_SELECT: return "LAUNCH_MEDIA_SELECT";
    case VK_LAUNCH_APP1: return "LAUNCH_APP1";
    case VK_LAUNCH_APP2: return "LAUNCH_APP2";
    }
    char s[16];
    std::snprintf(s, sizeof(s), "VK_%x", vk);
    return s;
}

bool IsModifier(unsigned vk)
{
    switch (vk)
    {
    case VK_SHIFT: case VK_LSHIFT: case VK_RSHIFT:
    case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL:
    case VK_MENU: case VK_LMENU: case VK_RMENU:
    case VK_LWIN: case VK_RWIN: case kWin:
        return true;
    }
    return false;
}

void HotkeyRecorder::Start(bool sides, unsigned long doubleMs)
{
    *this = HotkeyRecorder();
    m_sides = sides;
    m_doubleMs = doubleMs;
}

unsigned HotkeyRecorder::Normalize(unsigned vk) const
{
    if (vk == VK_CANCEL)
        return VK_PAUSE; // Ctrl+Break is the key Break
    if (m_sides)
        return vk;
    switch (vk)
    {
    case VK_LSHIFT: case VK_RSHIFT: return VK_SHIFT;
    case VK_LCONTROL: case VK_RCONTROL: return VK_CONTROL;
    case VK_LMENU: case VK_RMENU: return VK_MENU;
    case VK_LWIN: case VK_RWIN: return kWin;
    }
    return vk;
}

std::string HotkeyRecorder::Name(const std::vector<unsigned>& keys, const char* suffix) const
{
    // Modifiers first, in the order pressed; then the other keys - the last one is the engine's main key.
    std::vector<unsigned> ordered;
    for (unsigned k : keys)
        if (IsModifier(k)) ordered.push_back(k);
    for (unsigned k : keys)
        if (!IsModifier(k)) ordered.push_back(k);
    std::string s;
    for (unsigned k : ordered)
    {
        if (!s.empty()) s += " + ";
        s += VkName(k);
    }
    return s + suffix;
}

void HotkeyRecorder::Finish(const std::string& result)
{
    m_result = result;
    m_done = true;
}

void HotkeyRecorder::Key(unsigned vk, bool down, unsigned long timeMs)
{
    if (m_done)
        return;
    vk = Normalize(vk);
    auto held = std::find(m_down.begin(), m_down.end(), vk);
    if (down)
    {
        if (held != m_down.end())
            return; // auto-repeat
        if (m_down.empty() && !m_waiting)
            m_chord.clear();
        if (m_down.empty() && m_waiting) // a second chord begins within doubleMs
        {
            m_waiting = false;
            m_chord.clear();
        }
        m_down.push_back(vk);
        if (std::find(m_chord.begin(), m_chord.end(), vk) == m_chord.end())
            m_chord.push_back(vk);
        return;
    }
    if (held == m_down.end())
        return; // let go of a key pressed before the recording
    m_down.erase(held);
    if (!m_down.empty() || m_chord.empty())
        return;
    // Everything let go: one chord is complete.
    auto same = [](std::vector<unsigned> a, std::vector<unsigned> b) {
        std::sort(a.begin(), a.end());
        std::sort(b.begin(), b.end());
        return a == b;
    };
    if (!m_last.empty() && same(m_last, m_chord))
        return Finish(Name(m_chord, " #double"));
    m_last = m_chord;
    m_waiting = true;
    m_releasedAt = timeMs;
}

void HotkeyRecorder::Tick(unsigned long timeMs)
{
    if (m_done || !m_waiting || timeMs - m_releasedAt < m_doubleMs)
        return;
    const bool onlyMods = std::all_of(m_last.begin(), m_last.end(), [](unsigned k) { return IsModifier(k); });
    Finish(Name(m_last, onlyMods ? " #up" : ""));
}
