// Hotkey recording (src/recorder.cpp) fed with made-up keys: no keyboard, no window.
//   g++ -std=c++17 -Isrc tools/test_recorder.cpp src/recorder.cpp -o build/test_recorder.exe
#include "recorder.h"

#include <cstdio>

namespace
{
int failed = 0;

struct Press
{
    unsigned vk;
    bool down;
    unsigned long at;
};

void Check(const char* name, bool sides, std::initializer_list<Press> keys, unsigned long endAt, const char* want)
{
    HotkeyRecorder r;
    r.Start(sides, 280);
    for (const Press& p : keys)
    {
        r.Tick(p.at);
        r.Key(p.vk, p.down, p.at);
    }
    r.Tick(endAt);
    const bool ok = r.Done() && r.Result() == want;
    if (!ok)
        failed++;
    std::printf("%s %-44s got \"%s\"%s, want \"%s\"\n", ok ? "ok  " : "FAIL", name, r.Result().c_str(),
                r.Done() ? "" : " (not done)", want);
}
}

int main()
{
    Check("Ctrl+Break (Break comes as VK_CANCEL)", false,
          { { VK_RCONTROL, true, 0 }, { VK_CANCEL, true, 100 }, { VK_CANCEL, false, 101 }, { VK_RCONTROL, false, 200 } },
          600, "Ctrl + Break");
    Check("RCtrl+Break with sides", true,
          { { VK_RCONTROL, true, 0 }, { VK_CANCEL, true, 100 }, { VK_CANCEL, false, 101 }, { VK_RCONTROL, false, 200 } },
          600, "RCtrl + Break");
    Check("Break alone", false, { { VK_PAUSE, true, 0 }, { VK_PAUSE, false, 1 } }, 400, "Break");
    Check("Shift twice -> #double", false,
          { { VK_LSHIFT, true, 0 }, { VK_LSHIFT, false, 80 }, { VK_LSHIFT, true, 150 }, { VK_LSHIFT, false, 220 } },
          230, "Shift #double");
    Check("Shift twice too slow -> Shift #up", false,
          { { VK_LSHIFT, true, 0 }, { VK_LSHIFT, false, 80 }, { VK_LSHIFT, true, 500 }, { VK_LSHIFT, false, 560 } },
          1000, "Shift #up");
    Check("Win+Shift released -> #up", false,
          { { VK_LWIN, true, 0 }, { VK_LSHIFT, true, 50 }, { VK_LSHIFT, false, 150 }, { VK_LWIN, false, 160 } },
          600, "Win + Shift #up");
    Check("Shift then Win: still Win+Shift keys", false,
          { { VK_LSHIFT, true, 0 }, { VK_LWIN, true, 50 }, { VK_LWIN, false, 150 }, { VK_LSHIFT, false, 160 } },
          600, "Shift + Win #up");
    Check("LCtrl alone with sides -> LCtrl #up", true, { { VK_LCONTROL, true, 0 }, { VK_LCONTROL, false, 90 } }, 500,
          "LCtrl #up");
    Check("key before modifier: main key last", false,
          { { 'K', true, 0 }, { VK_LCONTROL, true, 10 }, { 'K', false, 50 }, { VK_LCONTROL, false, 60 } }, 500,
          "Ctrl + K");
    Check("Ctrl+Shift+K", false,
          { { VK_LCONTROL, true, 0 }, { VK_LSHIFT, true, 10 }, { 'K', true, 20 }, { 'K', false, 60 },
            { VK_LSHIFT, false, 70 }, { VK_LCONTROL, false, 80 } },
          500, "Ctrl + Shift + K");
    Check("auto-repeat ignored", false,
          { { VK_CAPITAL, true, 0 }, { VK_CAPITAL, true, 30 }, { VK_CAPITAL, true, 60 }, { VK_CAPITAL, false, 90 } }, 500,
          "CapsLock");
    Check("second, different chord replaces the first", false,
          { { 'A', true, 0 }, { 'A', false, 20 }, { VK_LCONTROL, true, 100 }, { 'B', true, 110 }, { 'B', false, 150 },
            { VK_LCONTROL, false, 160 } },
          600, "Ctrl + B");
    Check("F24 key", false, { { VK_F24, true, 0 }, { VK_F24, false, 10 } }, 400, "F24");
    Check("slash key", false, { { VK_OEM_2, true, 0 }, { VK_OEM_2, false, 10 } }, 400, "OEM_2");
    {
        HotkeyRecorder r;
        r.Start(false, 280);
        r.Key(VK_LSHIFT, true, 0);
        r.Tick(5000); // still held: not done
        const bool ok = !r.Done();
        if (!ok)
            failed++;
        std::printf("%s %s\n", ok ? "ok  " : "FAIL", "held key does not end the recording");
    }
    std::printf(failed ? "\n%d FAILED\n" : "\nall ok\n", failed);
    return failed ? 1 : 0;
}
