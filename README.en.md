# FluentSwitcher

[Русский](README.md)

A keyboard layout switcher for Windows 10 and 11. It fixes text typed in the wrong layout and shows the current
layout with a flag by the clock and at the text cursor. The settings window is in the Windows 11 style.

FluentSwitcher is a modified version of [SimpleSwitcher](https://github.com/Aegel5/SimpleSwitcher) by Aegel5,
free and open source under GPL-3.0.

## What it does

- **Fixes the last word** typed in the wrong layout: erases it, types it in the other layout and switches to it.
- **Fixes the selected text** in any app.
- **Fixes the text from the start of the line**: selects from the cursor to the start of the line and fixes it.
- Fixes several last words or all recent text; UPPER / lower case and inverted case for the selection.
- **A flag at the text cursor**: always or for a moment after a layout change, below or above the cursor, four
  sizes, transparency.
- **A flag by the clock**: glossy flags for 74 languages, the British flag for English if you like. A click and
  a double click on it do what you choose: the menu, the next layout (of the window you were typing in), on / off,
  settings.
- **Hotkeys on any keys**: left and right Ctrl, Shift, Alt, Win apart, double presses ("Shift twice"), firing on
  release, two hotkeys per action. Recorded in the settings window.
- **The layout with one Shift**, as in Punto: "Next layout" on a single Shift (left, right or either) fires on
  release, not with a letter, and gets along with "Shift twice".
- **Commands**: run programs and paste text with a hotkey.
- **Works in apps run as administrator**: Windows asks once, after that FluentSwitcher starts through the Task
  Scheduler without asking.
- The settings window is in English and Russian, its theme as in Windows, light or dark.
- **Update check**: once a day the app asks GitHub for the number of the latest version and sends nothing else;
  it tells about a new version with a notification by the clock, whether to download and install it is up to
  you. Turned off in "About", where "Check now" is too.

The default hotkeys: Shift twice – the last word, or the selected text when no word was typed;
Shift + CapsLock – several words, Ctrl + CapsLock – all recent text, Win + F8 – on / off, Win + Shift –
settings. Change them in the settings window, "Hotkeys".

## What is different from SimpleSwitcher

- A Windows 11 style settings window (wxWidgets and the UI kit of the FluentClipper clipboard manager) instead of
  the ImGui window. Everything is one file, `FluentSwitcher.exe`.
- The flag at the text cursor; fixing the text from the start of the line.
- A fixed word goes in as ready characters with a short pause: the new Windows 11 Notepad neither loses nor
  repeats characters, Chrome and Electron do not lose the first letter.
- The selected text is converted as a whole, by the layout of the whole line: punctuation does not turn into
  letters.
- "Shift twice" fires on the release of the second press only and does not confuse fast typing (Shift, then
  Shift + 7 at once).
- Ctrl + Break works; Home, End and the arrows are sent as extended keys (NumLock on does not drop the
  selection).
- The clipboard is restored whole – with formatting, pictures and files – and the temporary text does not get
  into the Windows clipboard history (Win + V) or clipboard managers.
- Running as administrator without a UAC prompt at sign-in.
- Removed: the Reminder, clearing clipboard formatting, layout items in the flag's menu.

## Install

Download from the [Releases](https://github.com/DmitryN71/FluentSwitcher/releases) page:

- `FluentSwitcher-<version>-setup.exe` – the installer. Installs for the current user into
  `%LOCALAPPDATA%\Programs\FluentSwitcher` without administrator rights, with a Start menu shortcut. Remove it in
  Settings → Apps.
- `FluentSwitcher-<version>.zip` – no installation: unpack anywhere and run `FluentSwitcher.exe`.

The program is not signed, so Windows SmartScreen may warn about an unknown publisher. The settings live in
`FluentSwitcher.json` next to the program.

## Build from source

You need Visual Studio 2026 or its Build Tools with C++ and ATL; CMake and Ninja come with Visual Studio.

```bat
build.cmd
```

The result is `build\x64-release\FluentSwitcher.exe`. CMake downloads wxWidgets 3.3 itself; to build from your own
copy of its sources, set `WXDIR`.

A release – the zip and the installer (needs [Inno Setup 6](https://jrsoftware.org/isinfo.php)):

```bat
powershell -File tools\make_release.ps1
```

## License

GPL-3.0, see [LICENSE](LICENSE). Parts by others are under their own licenses, their texts are in
[THIRD-PARTY-NOTICES.txt](THIRD-PARTY-NOTICES.txt): wxWidgets, the FluentClipper UI kit (MIT), Fluent UI System
Icons (Microsoft, MIT), GoSquared flags (MIT) and more.
