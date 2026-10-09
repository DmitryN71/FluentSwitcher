# FluentSwitcher

[Русский](README.md)

A keyboard layout switcher for Windows 10 and 11. It fixes text typed in the wrong layout and shows the current
layout with a flag by the clock and at the text cursor. The settings window is in the Windows 11 style.

FluentSwitcher is a modified version of [SimpleSwitcher](https://github.com/Aegel5/SimpleSwitcher) by Aegel5,
free and open source under GPL-3.0.

## What it does

- **Fixes the last word** typed in the wrong layout: erases it, types it in the other layout and switches to it.
- **Fixes the selected text** in any app.
- **Automatic layout switch** (turned on in Auto switch): a word typed in the wrong layout is fixed by itself after a
  space, Enter (Shift+Enter, Ctrl+Enter too) or Tab, and at once when its last key is a sign after a word in the other
  layout ("ПшеРгию" - "GitHub.", "b xnj&" - "и что?") - by the Windows dictionaries, as in LangBar++, and a long one already from the fourth letter
  ("njkm" - "толь"): the beginnings of words come from built-in lists of frequent words (Russian, English and Ukrainian). Short words go by their
  neighbours: "f vj;yj" - "а можно", "ns ult" - "ты где", while "plan B" and "5 шт" are left alone. A word with a typo
  too: "нфдлштп" - "yalking". Words with digits, abbreviations, a word typed again after the layout was switched by
  hand right after a fix, a word after Backspace, passwords, the console (but for the console apps listed, such as far.exe) and the apps of "No automatic switch in apps" are left alone; fixed
  by mistake - "Fix the last word" right after it brings the word back, and the third time remembers it; a word
  you keep fixing by hand goes to "Always switch" the third time. The lists are edited in a window of their own: one field adds and finds a word, each shows how it looks in the other layout; "Always switch" shows what is typed and what it becomes ("реез -> http"), either can be typed in, and the ⇄ button turns the direction; the words the app learned are marked. The journal
  of the automatic switch shows what switched by itself, what was switched back and what was fixed by hand, with the
  reason; its mistakes make a report for the forum topic - you see and can edit the text, the app sends nothing.
- **TWo INitial CApitals**: "THis" becomes "This" after a space, punctuation, Enter or Tab (three letters too: "THe" - "The" for frequent English words, "НЕт" - "Нет"). PCs, IDs, GHz, eM, iPhone, and Latin names like ILogger in code editors and apps without the automatic switch, are left alone, and there
  are exceptions; fixed by mistake - "Fix the last word" right after it brings the word back, and the third time remembers it.
  A word typed in the other layout ("GJgsnrf") is left to the layout fix, by the Windows dictionaries, and that gives
  "Попытка" at once. Turned on in Typing.
- **i → I**: the English "i" on its own becomes "I" (i'm - I'm), except in consoles and code editors, where i
  is a variable. On from the start, turned off in Typing.
- **Fixes the text from the start of the line**: selects from the cursor to the start of the line and fixes it.
- Fixes several last words or all recent text; UPPER / lower case and inverted case for the selection.
- **A flag at the text cursor**: always or for a moment - after a change of layout, window or text field and after
  a mouse click; a flag or the letters on a dark badge, below or above the cursor, four sizes, transparency. In
  browsers only in text fields. Set apart from the tray icon.
- **A tray icon**: flags for 74 languages (Flagpack, each size on the screen's pixels), regular or waving – with folds and a shadow, the British flag for English
  if you like, the letters EN, RU in the taskbar's text color or the app's icon; it can be hidden. A click and a
  double click on it do what you choose: the menu, the next layout (of the window you were typing in), on / off,
  settings.
- **A Windows 11 style menu by the icon**: rounded corners, icons, an "Enabled" toggle that keeps the menu open; the
  theme follows the taskbar.
- **Sounds**, as in Punto: a click when the layout is switched (with a hotkey of FluentSwitcher or Windows, with a
  click on the tray icon) and a double click when FluentSwitcher fixes text, each with a volume of its own. The sounds
  are the project's own (tools/make_sounds.py) and can be replaced with any WAV files in `sounds`; with `en.wav`,
  `ru.wav` each language gets a sound of its own.
- **Hotkeys on any keys**: left and right Ctrl, Shift, Alt, Win apart, double presses ("Shift twice"), firing on
  release, two hotkeys per action. Recorded in the settings window.
- **The layout with one Shift**, as in Punto: "Next layout" on a single Shift (left, right or either) fires on
  release, not with a letter, and gets along with "Shift twice".
- **Commands**: run programs and paste text with a hotkey.
- **Lists of apps**: where FluentSwitcher does not work at all (games; Advanced), where only the automatic switch is off (code editors) and in which console apps it works (Auto switch).
- **Works in apps run as administrator**: Windows asks once, after that FluentSwitcher starts through the Task
  Scheduler without asking.
- **Remote desktops and virtual machines** (Remote Desktop Connection, Windows App, Hyper-V, VMware, VirtualBox,
  TeamViewer, AnyDesk, RustDesk, Parsec, VNC): FluentSwitcher works in their windows as anywhere. If FluentSwitcher
  runs on that computer too, turn off "Advanced" – "Work in remote access windows" here, otherwise both copies fix
  the word.
- The settings window and the flag's menu are in English, Russian and Ukrainian, the theme as in Windows, light or dark.
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
  letters. Words of the other layout in it go by the Windows dictionaries: "«NTgthm» и «Ыещз»" becomes
  "«Теперь» и «Stop»".
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
Icons (Microsoft, MIT), Flagpack flags (MIT) and more.
