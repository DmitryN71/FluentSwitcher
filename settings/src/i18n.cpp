#include "i18n.h"

#include <wx/msw/wrapwin.h>

#include <cstring>
#include <string>
#include <unordered_map>

const std::unordered_map<std::string, const char*>& UkrainianTexts(); // i18n_uk.cpp

namespace
{
Language g_language = Language::Russian;

// Russian (as in the code) -> English. American spelling; "app", not "program".
// tools/extract_strings.py lists the texts of the code that are not here yet.
const std::unordered_map<std::string, const char*>& English()
{
    static const std::unordered_map<std::string, const char*> table = {
        // Hotkey actions (hotkeys.cpp)
        { "Исправить последнее слово", "Fix the last word" },
        { "Набранное не в той раскладке: стирает последнее слово, печатает его в другой раскладке и переключает её",
          "Typed in the wrong layout: erases the last word, types it in the other layout and switches to it" },
        { "Исправить текст с начала строки", "Fix the text from the start of the line" },
        { "Выделяет от курсора до начала строки (Shift+Home) и исправляет, как выделенный текст",
          "Selects from the cursor to the start of the line (Shift+Home) and fixes it like selected text" },
        { "Исправить несколько слов", "Fix several words" },
        { "Каждое следующее нажатие захватывает ещё одно слово назад. Способ SimpleSwitcher, в новом Блокноте "
          "путает текст – лучше «с начала строки»",
          "Each next press takes one more word back. The SimpleSwitcher way; in the new Notepad it garbles text – "
          "“from the start of the line” works better" },
        { "Исправить весь недавний текст", "Fix all recent text" },
        { "Всё, что набрано подряд в этом окне: до Enter, стрелок или смены окна",
          "Everything typed in a row in this window: back to Enter, the arrows or a window switch" },
        { "Исправить выделенный текст", "Fix the selected text" },
        { "Выделенное в любом приложении печатается в другой раскладке",
          "The selection in any app is retyped in the other layout" },
        { "Выделенное ПРОПИСНЫМИ / строчными", "Selection to UPPERCASE / lowercase" },
        { "Если выделенное уже прописными – строчными", "If it is uppercase already, to lowercase" },
        { "Выделенное иНВЕРСИЕЙ рЕГИСТРА", "Selection in iNVERTED cASE" },
        { "Для текста, набранного с нажатым CapsLock", "For text typed with CapsLock on" },
        { "Следующая раскладка", "Next layout" },
        { "Переключает раскладку без исправления текста. Можно и одним Shift, как в Punto: он срабатывает при "
          "отпускании, а с буквой – нет",
          "Switches the layout without fixing text. A single Shift works too, as in Punto: it fires on release, "
          "and not when pressed with a letter" },
        { "Нажать CapsLock", "Press CapsLock" },
        { "Если CapsLock занят под сочетание, включить его можно так", "If CapsLock is taken by a hotkey, this turns it on" },
        { "Включить / выключить FluentSwitcher", "Turn FluentSwitcher on / off" },
        { "Работает и когда приложение выключено", "Works even while the app is off" },
        { "Включить / выключить автопереключение", "Turn the auto switch on / off" },
        { "Включено ли – видно в меню у значка у часов, там же его можно и переключить",
          "Whether it is on shows in the tray icon's menu, where it can be switched too" },
        { "Открыть настройки", "Open settings" },
        { "Это окно", "This window" },

        // Hotkey fields
        { " дважды", " twice" },
        { ", при отпускании", ", on release" },
        { "   или   ", "   or   " },
        { "Не назначено", "Not set" },
        { "Ещё одно сочетание", "Another hotkey" },
        { "Нажмите сочетание или дважды одну клавишу. Esc – отмена", "Press a combination, or one key twice. Esc cancels" },
        { "Так же назначено: «", "Also set for “" },
        { "»", "”" },
        { "Нажмите сочетание…", "Press a combination…" },
        { "Убрать сочетание", "Remove the hotkey" },

        // Window
        { "Папка журнала", "Log folder" },
        { "Там файл FluentSwitcher.exe.log – его можно приложить к сообщению об ошибке",
          "FluentSwitcher.exe.log is there – attach it to a bug report" },
        { "Открыть", "Open" },
        { "Журнала ещё нет: включите его выше и повторите ошибку", "No log yet: turn it on above and repeat the problem" },
        { "Закрыть FluentSwitcher", "Quit FluentSwitcher" },
        { "Закрыть FluentSwitcher? Исправление раскладки не будет работать до следующего запуска.",
          "Quit FluentSwitcher? Layout fixing will not work until it starts again." },
        { "Закрыть", "Quit" },
        { "Отмена", "Cancel" },
        { "Сохранить", "Save" },
        { "Сохранено", "Saved" },
        { "Применить", "Apply" },
        { "Не удалось прочитать FluentSwitcher.json: ", "Can't read FluentSwitcher.json: " },
        { "Не удалось сохранить: ", "Can't save: " },
        { "Автозапуск не изменился: в режиме «от имени администратора» для этого нужны права администратора. ",
          "Autostart did not change: in the “as administrator” mode this needs administrator rights. " },
        { "Не включился: запустите FluentSwitcher от имени администратора или выключите работу в приложениях "
          "администратора",
          "Did not turn on: run FluentSwitcher as administrator or turn off work in administrator apps" },
        { "Не включился: включена другая копия приложения", "Did not turn on: another copy of the app is on" },

        // General
        { "Основные", "General" },
        { "FluentSwitcher не запущен", "FluentSwitcher is not running" },
        { "Настройки сохранятся и подействуют при запуске", "Settings are saved and take effect when it starts" },
        { "Запустить", "Start" },
        { "Не нашёл FluentSwitcher.exe в папке приложения", "FluentSwitcher.exe is not in the app folder" },
        { "FluentSwitcher включён", "FluentSwitcher is on" },
        { "Выключенный не исправляет текст и не отвечает на сочетания, кроме «Включить / выключить»",
          "When off, it fixes no text and answers no hotkeys except “Turn on / off”" },
        { "Запускать вместе с Windows", "Start with Windows" },
        { "Приложение стартует при входе в Windows, видно только значок у часов",
          "Starts when you sign in to Windows; only the tray icon shows" },
        { "Работать в приложениях, запущенных от имени администратора", "Work in apps run as administrator" },
        { "FluentSwitcher тогда работает с правами администратора: Windows спросит разрешения один раз, дальше "
          "он запускается через планировщик заданий без вопросов",
          "FluentSwitcher then runs as administrator: Windows asks once, after that it starts through the Task "
          "Scheduler without asking" },
        { "Нужна учётная запись администратора. Под обычной Windows запустила бы FluentSwitcher от "
          "имени другого пользователя и спрашивала бы его пароль при каждом запуске",
          "Needs an administrator account. Under a standard one, Windows would run FluentSwitcher as another user and "
          "ask for that user's password at every start" },
        { "Под обычной учётной записью Windows этот режим не работает",
          "This mode does not work under a standard Windows account" },
        { "Чтобы работать в приложениях, запущенных от имени администратора, FluentSwitcher перезапустится "
          "с правами администратора. Windows спросит разрешения один раз: дальше приложение запускается "
          "через планировщик заданий, без вопросов",
          "To work in programs run as administrator, FluentSwitcher will restart as administrator. Windows asks "
          "once: after that the app starts through the Task Scheduler without asking" },
        { "Перезапустить", "Restart" },
        { "Не сейчас", "Not now" },
        { "Без прав администратора FluentSwitcher выключен: перезапустите его или выключите работу "
          "в приложениях администратора",
          "Without administrator rights FluentSwitcher is off: restart it or turn off work in administrator apps" },
        { "Windows не дала прав администратора: FluentSwitcher запущен без них и выключен",
          "Windows gave no administrator rights: FluentSwitcher runs without them and is off" },
        { "FluentSwitcher перезапущен с правами администратора", "FluentSwitcher restarted as administrator" },
        { "Всегда", "Always" },
        { "Ненадолго", "For a moment" },
        { "Не показывать", "Don't show" },
        { "Маленький", "Small" },
        { "Обычный", "Normal" },
        { "Крупный", "Large" },
        { "Очень крупный", "Extra large" },
        { "Значок у часов", "Tray icon" },
        { "Показывать значок у часов", "Show the tray icon" },
        { "В области уведомлений, у часов: раскладка, меню и уведомления FluentSwitcher. Без значка настройки "
          "открывает сочетание «Открыть настройки»",
          "In the notification area, by the clock: the layout, the menu and the notifications of FluentSwitcher. "
          "Without the icon, the settings open with the \"Open settings\" hotkey" },
        { "Вид значка", "Look of the icon" },
        { "Флаг раскладки, её буквы или значок FluentSwitcher", "The layout's flag, its letters or the FluentSwitcher icon" },
        { "Обычные флаги", "Regular flags" },
        { "Флаги с переливом", "Waving flags" },
        { "Значок приложения", "App icon" },
        { "Щелчок по значку", "Click on the icon" },
        { "Двойной щелчок по значку", "Double click on the icon" },
        { "Флаг у курсора", "Flag at the cursor" },
        { "Вид флага", "Look of the flag" },
        { "Флаг раскладки или её буквы на тёмной плашке", "The layout's flag or its letters on a dark badge" },
        { "Где показывать", "Where to show" },
        { "Размер", "Size" },
        { "Прозрачность", "Transparency" },
        { "Звуки", "Sounds" },
        { "Флаг английской раскладки", "Flag of the English layout" },
        { "У часов и у текстового курсора", "In the tray and at the text cursor" },
        { "Американский", "American" },
        { "Британский", "British" },
        { "Браузер: назад", "Browser back" },
        { "Браузер: вперёд", "Browser forward" },
        { "Браузер: обновить", "Browser refresh" },
        { "Браузер: стоп", "Browser stop" },
        { "Поиск", "Search" },
        { "Избранное", "Favorites" },
        { "Домой", "Browser home" },
        { "Без звука", "Mute" },
        { "Тише", "Volume down" },
        { "Громче", "Volume up" },
        { "Следующий трек", "Next track" },
        { "Предыдущий трек", "Previous track" },
        { "Стоп", "Stop" },
        { "Воспроизведение / пауза", "Play / pause" },
        { "Почта", "Mail" },
        { "Медиаплеер", "Media player" },
        { "Этот компьютер", "This PC" },
        { "Калькулятор", "Calculator" },
        { "Без автопереключения в приложениях",
          "No automatic switch in apps" },
        { "Всё остальное там работает: исправление сочетанием, флаг у курсора, ДВе ЗАглавные кириллицей",
          "Everything else works there: fixes with a hotkey, the flag at the cursor, Cyrillic TWo INitial CApitals" },
        { "Приложения, где автопереключение не нужно: редакторы кода, приложения с командами. Исправление сочетанием и флаг у курсора там работают; латинские имена (ILogger) и i там не исправляются. Имя файла приложения (code.exe) или путь к нему",
          "Apps where the automatic switch is not wanted: code editors, apps with commands. Fixes with a hotkey and the flag at the cursor work there; Latin names (ILogger) and i are not fixed there. The app's file name (code.exe) or its path" },
        { "FluentSwitcher там молчит: ни сочетаний, ни исправлений, ни автопереключения, ни флага у курсора",
          "FluentSwitcher keeps quiet there: no hotkeys, no fixes, no automatic switch, no flag at the cursor" },
        { "Приложения, где FluentSwitcher молчит совсем: игры, приложения со своими сочетаниями. Имя файла приложения (far.exe) или путь к нему. Где не нужно только автопереключение – «Автопереключение» – «Без автопереключения в приложениях»",
          "Apps where FluentSwitcher keeps quiet altogether: games, apps with hotkeys of their own. The app's file name (far.exe) or its path. Where only the automatic switch is not wanted - Auto switch - No automatic switch in apps" },
        { "«ДВух» станет «Двух», «НЕт» – «Нет»: после пробела, знака препинания, Enter или Tab",
          "\"THis\" becomes \"This\", \"NOt\" - \"Not\": after a space, punctuation, Enter or Tab" },
        { "PCs, IDs, GHz, МГц, МВт, eM, iPhone и слова из исключений не трогаются; английские из трёх букв – только частые слова: THe, WAs; в редакторах кода латинские имена (ILogger) тоже. Исправилось зря – сразу нажмите «Исправить последнее слово» (Shift дважды): слово вернётся. Перевод раскладки тоже исправляет ДВе ЗАглавные: LDe[ – Двух",
          "PCs, IDs, GHz, eM, iPhone and the exceptions are left alone; English words of three letters - only frequent ones: THe, WAs; in code editors Latin names (ILogger) too. Fixed by mistake? Press \"Fix the last word\" (Shift twice) right away: the word comes back. A layout fix fixes TWo INitial CApitals too: EРшы becomes This" },
        { "Флаг у текстового курсора", "Flag at the text cursor" },
        { "Показывает раскладку там, где вы печатаете", "Shows the layout where you type" },
        { "Сколько показывать «ненадолго»", "How long \"for a moment\" is" },
        { "После смены раскладки, окна или поля ввода", "After a change of the layout, the window or the input field" },
        { "1 секунду", "1 second" },
        { "2 секунды", "2 seconds" },
        { "3 секунды", "3 seconds" },
        { "5 секунд", "5 seconds" },
        { "10 секунд", "10 seconds" },
        { "Если у края экрана места нет – с другой стороны строки", "No room at the screen's edge: on the other side of the line" },
        { "Под курсором", "Below the cursor" },
        { "Над курсором", "Above the cursor" },
        { "Чтобы не отвлекал от текста", "So that it does not distract from the text" },
        { "Нет", "None" },
        { "Ничего", "Nothing" },
        { "Исправлять ДВе ЗАглавные", "Fix TWo INitial CApitals" },
        { "«ДВух» станет «Двух» после пробела, Enter или Tab. PCs, IDs, GHz, eM, iPhone и слова из исключений не трогаются. "
          "Исправилось зря – сразу нажмите «Исправить последнее слово» (Shift дважды): слово вернётся. Тот же "
          "перевод раскладки исправляет и ДВе ЗАглавные: LDe[ – Двух",
          "“THis” becomes “This” after a space, Enter or Tab. PCs, IDs, GHz, eM, iPhone and the exceptions are left alone. "
          "Fixed by mistake? Press “Fix the last word” (Shift twice) right away: the word comes back. A layout fix "
          "fixes TWo INitial CApitals too: ЕРшы becomes This" },
        { "Исключения для ДВух ЗАглавных", "Exceptions for TWo INitial CApitals" },
        { "Слова, которые так и пишутся. Слово закрывает и те, что с него начинаются: ИПшник – и ИПшники. "
          "Само слово попадает сюда после третьей отмены",
          "Words that are written so on purpose. A word also covers the words that start with it: IPsec – and "
          "IPsecs. A word gets here by itself the third time it is brought back" },
        { "Пока пусто", "Empty so far" },

        // The card of a list of words (wordlist.cpp)
        { "Добавить или найти слово", "Add or find a word" },

        // The lists of apps (Дополнительно)
        { "Приложений в списке: %zu", "Apps in the list: %zu" },
        { "Не работать в приложениях",
          "Don't work in apps" },
        { "Имя файла – как в Диспетчере задач на вкладке «Подробности». Путь – если нужно одно приложение из нескольких с тем же именем",
          "The file name as Task Manager shows it on the Details tab. A path - when one of several apps with the same name is meant" },
        { "Автопереключение в консоли",
          "Auto switch in consoles" },
        { "Консольные приложения, где оно работает: far.exe. Пароль в консоли Windows от текста не отличает",
          "Console apps where it works: far.exe. In a console Windows does not tell a password from text" },
        { "Консольные приложения, где автопереключение работает. В консоли его нет: там вводят команды и пароли, а пароль Windows от текста не отличает. Имя файла приложения (far.exe) или путь к нему",
          "Console apps where the automatic switch works. In consoles it is off: commands and passwords are typed there, and Windows does not tell a password from text. The app's file name (far.exe) or its path" },
        { "В обычной консоли – и приложение, запущенное в ней: far.exe из cmd. В Windows Terminal и ConEmu приложение вкладки не узнать: добавьте WindowsTerminal.exe или ConEmu64.exe – и автопереключение будет во всех вкладках. Не вводите пароли там, где оно включено",
          "In a plain console - an app started in it too: far.exe from cmd. In Windows Terminal and ConEmu the app of a tab cannot be told: add WindowsTerminal.exe or ConEmu64.exe, and the automatic switch works in all their tabs. Do not type passwords where it is on" },
        { "Добавить или найти приложение",
          "Add or find an app" },
        { "Приложения (*.exe)|*.exe",
          "Apps (*.exe)|*.exe" },
        { "Имя файла приложения, например far.exe, или путь к нему: Enter добавит",
          "The app's file name, say far.exe, or its path: Enter adds it" },
        { "Добавить", "Add" },
        { "Убрать из списка", "Remove from the list" },
        { "выучено", "learned" },
        { "«%s»", "\"%s\"" },
        { "и %s", "and %s" },
        { "набранное %s станет %s", "typed %s becomes %s" },
        { "Теперь %s", "Now %s" },
        { "Наоборот: %s", "The other way: %s" },
        { "Enter добавит: %s", "Enter adds: %s" },
        { "Введите слово – как оно должно быть или как набирается по ошибке: Enter добавит его",
          "Type a word - as it should be or as it gets typed by mistake: Enter adds it" },
        { "Изменить…", "Edit…" },
        { "Слова, которые автопереключение не трогает. Одно слово – в обеих раскладках: cv закрывает и «см»",
          "Words the automatic switch leaves alone. One word stands for both layouts: cv covers \"см\" too" },
        { "Слова, которые переключаются сразу, как набраны целиком, даже если словарь их не знает. Слева – что "
          "набрано, справа – что получится. Вводить можно любое из двух: приложение само поймёт, что из них "
          "слово; не так – ⇄ на строке меняет направление",
          "Words switched as soon as they are typed whole, even when the dictionary does not know them. On the left - "
          "what is typed, on the right - what it becomes. Either of the two can be typed in: the app works out which of "
          "them is the word; if it guesses wrong, ⇄ on the row turns the direction" },
        { "Слова, которые так и пишутся: VMware, IPsec. Слово от четырёх букв закрывает и те, что с него "
          "начинаются: IPsec – и IPsecs. Регистр букв важен",
          "Words that are written so on purpose: VMware, IPsec. A word of four letters or more also covers the words "
          "that start with it: IPsec - and IPsecs. Letter case matters" },
        { "Не найдено", "Nothing found" },
        { "Уже в списке", "Already in the list" },
        { "Уже в списке: %s", "Already in the list: %s" },
        { "Добавлено: %s", "Added: %s" },
        { "; уже в списке: %s", "; already in the list: %s" },
        { "Удалено: %s", "Removed: %s" },
        { "Введите слово: Enter добавит его, а список покажет похожие",
          "Type a word: Enter adds it, and the list shows the ones like it" },
        { "Уже закрыто словом %s: оно закрывает и слова, которые с него начинаются",
          "Already covered by %s: it covers the words that start with it as well" },
        { "%s – это %s в другой раскладке, уже в списке", "%s is %s in the other layout, already in the list" },
        { "Enter добавит %s", "Enter adds %s" },
        { "Слов в списке: %zu", "Words in the list: %zu" },
        { "Готово", "Done" },
        { "Тихий", "Quiet" },
        { "Средний", "Medium" },
        { "Громкий", "Loud" },
        { "Звук при переключении раскладки", "Sound when the layout is switched" },
        { "Сочетанием FluentSwitcher или Windows, щелчком по значку у часов. Звук – switch.wav в папке sounds рядом с "
          "приложением; положите туда en.wav, ru.wav – и у каждого языка будет свой",
          "With a hotkey of FluentSwitcher or Windows, with a click on the tray icon. The sound is switch.wav in the "
          "sounds folder next to the app; put en.wav, ru.wav there for a sound of each language" },
        { "Звук при исправлении текста", "Sound when text is fixed" },
        { "Когда FluentSwitcher исправляет слово или выделенный текст. Звук – fix.wav в папке sounds",
          "When FluentSwitcher fixes a word or the selected text. The sound is fix.wav in the sounds folder" },
        { "Меню", "Menu" },
        { "Включить / выключить", "Turn on / off" },
        { "«Следующая раскладка» – у окна, где вы печатали, и курсор остаётся там. Если назначен и двойной "
          "щелчок, одиночный срабатывает чуть позже: ждёт, не будет ли второго",
          "“Next layout” – of the window you were typing in, and the cursor stays there. If a double click is set "
          "too, a single one fires a bit later: it waits to see if a second comes" },
        { "Правый щелчок всегда открывает меню", "A right click always opens the menu" },
        { "Слабая", "Light" },
        { "Средняя", "Medium" },
        { "Сильная", "Strong" },
        { "Очень сильная", "Very strong" },
        { "Максимальная", "Maximum" },
        { "При масштабе 100 %; на экранах с большим масштабом он крупнее",
          "At 100 % scale; on screens with a larger scale it is larger" },
        { "Язык", "Language" },
        { "Этого окна и меню значка у часов. Окно откроется на новом языке после сохранения",
          "Of this window and of the tray icon's menu. The window reopens in the new language after saving" },
        { "Тема", "Theme" },
        { "Этого окна. Оно откроется в новой теме после сохранения",
          "Of this window. It reopens in the new theme after saving" },
        { "Как в Windows", "As in Windows" },
        { "Светлая", "Light" },
        { "Тёмная", "Dark" },

        // Typing
        { "Набор текста", "Typing" },
        { "Автопереключение", "Auto switch" },
        { "Автопереключение раскладки", "Switch the layout automatically" },
        { "Слово не в той раскладке исправляется само после пробела, Enter или Tab: ghbdtn – «привет»",
          "A word in the wrong layout is fixed by itself after a space, Enter or Tab: ghbdtn - \"привет\"" },
        { "Переключает, когда набранного нет в словаре Windows своего языка, а те же клавиши в другой раскладке – "
          "слово. Короткие слова решает по соседям: f vj;yj – «а можно», ns ult – «ты где», а plan B и «5 шт» не "
          "трогает.\nНе трогает: слова с цифрами, аббревиатуры, адреса и почту, опечатки в английских словах, слово, "
          "перепечатанное после ручной смены раскладки сразу после исправления, слово после Backspace, пароли, "
          "консоль.\nИсправилось зря – сразу нажмите «Исправить "
          "последнее слово» (Shift дважды): слово вернётся, а на третий раз попадёт в «Не переключать»",
          "Switches when the word typed is not in the Windows dictionary of its language while the same keys in the "
          "other layout are a word. Short words go by their neighbours: f vj;yj - \"а можно\", ns ult - \"ты где\", "
          "while plan B and \"5 шт\" are left alone.\nLeaves alone: words with digits, abbreviations, addresses and "
          "mail, typos in English words, a word typed again after the layout was switched by hand right after a fix, "
          "a word after Backspace, passwords, the console.\nFixed by mistake - press \"Fix the last word\" (Shift twice) at once: the word comes back, "
          "and the third time it goes to \"Never switch\"" },
        { "Не ждать конца слова", "Do not wait for the end of the word" },
        { "Переключать с четвёртой буквы: njkm станет «толь», ыщьу – some",
          "Switch from the fourth letter: njkm becomes \"толь\", ыщьу - some" },
        { "Переключает посреди слова, когда так не начинается ни одно слово своего языка, а те же клавиши в другой "
          "раскладке – начало слова. Начала слов – по спискам частых слов, встроенным в приложение (330 тысяч "
          "русских, 150 тысяч английских и 98 тысяч украинских форм), и по списку «Переключать всегда». В конце "
          "слова оно проверяется "
          "ещё раз по словарю.\nПереключилось зря – нажмите «Исправить последнее слово» (Shift дважды): слово вернётся, а на "
          "третий раз его начало попадёт в «Не переключать»",
          "Switches in the middle of a word when no word of its language begins like that while the same keys in the "
          "other layout are the beginning of a word. The beginnings come from lists of frequent words built into the "
          "app (330 thousand Russian, 150 thousand English and 98 thousand Ukrainian forms) and from \"Always switch\". At the end "
          "of the word it is checked once more by the dictionary.\nSwitched by mistake - press \"Fix the last word\" (Shift "
          "twice): the word comes back, and the third time its beginning goes to \"Never switch\"" },
        { "Не переключать", "Never switch" },
        { "Например, cv или см – в любой раскладке", "For example cv or см - in either layout" },
        { "Слово попадает сюда и само – после третьей отмены автопереключения, с отметкой «выучено»",
          "A word also gets here by itself, after the automatic switch is undone the third time, marked \"learned\"" },
        { "Переключать всегда", "Always switch" },
        { "Даже если словарь их не знает или это одна буква: the, a",
          "Even when the dictionary does not know them or it is one letter: the, a" },
        { "Пишите слово в том виде, какой нужен: the – и набранное «еру» станет the, a – и «ф» станет a. Слово в "
          "другом виде (еру) переключало бы правильно набранное. Слово попадает сюда и само – после третьего "
          "исправления вручную («Исправить последнее слово»), с отметкой «выучено»",
          "Write the word as it should be: the - and \"еру\" typed becomes the, a - and \"ф\" becomes a. A word in "
          "the other form (еру) would switch what is typed right. A word gets here by itself too - after it is fixed "
          "by hand (\"Fix the last word\") the third time, marked \"learned\"" },
        { "Журнал автопереключения", "Journal of the automatic switch" },
        { "Что переключилось само, что вернули и что исправили вручную",
          "What switched by itself, what was switched back and what was fixed by hand" },
        { "Открыть", "Open" },
        { "Журнала ещё нет: включите его и подождите первого переключения",
          "No journal yet: turn it on and wait for the first switch" },
        { "Файл autoswitch.log в папке log рядом с приложением: по нему видно, где автопереключение "
          "ошибается и что пропускает. Пароли туда не попадают – в их полях оно не работает",
          "The file autoswitch.log in the log folder next to the program: it shows where the automatic switch is "
          "wrong and what it misses. Passwords do not get there - it does not work in their fields" },
        { "Исправлять i на I", "Fix i to I" },
        { "Английское «i» отдельным словом станет «I»: i am – I am, i'm – I'm",
          "The English \"i\" on its own becomes \"I\": i am - I am, i'm - I'm" },
        { "Только в английской раскладке и не в консоли или редакторе кода (VS Code, Visual Studio, JetBrains, "
          "Notepad++): там i – переменная. Исправилось зря – сразу нажмите «Исправить последнее слово» (Shift дважды): "
          "вернётся «i», а на третий раз оно попадёт в исключения ДВух ЗАглавных",
          "Only in the English layout and not in a console or a code editor (VS Code, Visual Studio, JetBrains, "
          "Notepad++): there i is a variable. Fixed by mistake - press \"Fix the last word\" (Shift twice) at once: \"i\" "
          "comes back, and the third time it goes to the exceptions of TWo INitial CApitals" },
        { "Отчёт об ошибках для форума", "A report of the mistakes for the forum" },
        { "Что вы вернули и что исправили вручную – из журнала. Текст видно до отправки",
          "What you switched back and what you fixed by hand - from the journal. You see the text before sending" },
        { "Собрать…", "Make…" },
        { "Журнала ещё нет: включите его выше и поработайте с автопереключением",
          "No journal yet: turn it on above and work with the automatic switch" },
        { "Ошибок в журнале нет: ничего не возвращали и не исправляли вручную",
          "No mistakes in the journal: nothing was switched back or fixed by hand" },
        { "Отчёт для форума", "Report for the forum" },
        { "Только ошибки из журнала: что вы вернули и что исправили вручную; в скобках – причина, она для "
          "разработчика. Вычеркните то, что не хотите показывать. Приложение ничего не отправляет: «Копировать» "
          "положит текст в буфер обмена – вставьте его в сообщение в теме FluentSwitcher на форуме",
          "Only the mistakes from the journal: what you switched back and what you fixed by hand; in brackets is the "
          "reason, it is for the developer. Strike out what you don't want to show. The program sends nothing: \"Copy\" "
          "puts the text into the clipboard - paste it into a post in the FluentSwitcher topic on the forum" },
        { "Копировать", "Copy" },
        { "Открыть тему на форуме", "Open the forum topic" },
        { "Скопировано", "Copied" },
        { "Отчёт FluentSwitcher", "FluentSwitcher report" },
        { "Раскладки: ", "Layouts: " },
        { "Автопереключение: %s, не ждать конца слова: %s, ДВе ЗАглавные: %s",
          "Automatic switch: %s, do not wait for the end of the word: %s, TWo INitial CApitals: %s" },
        { "вкл.", "on" },
        { "выкл.", "off" },
        { "Журнал с %s по %s: само – %d, вернули – %d, вручную – %d",
          "Journal from %s to %s: by itself - %d, switched back - %d, by hand - %d" },
        { "Последние %zu ошибок из %zu", "The last %zu mistakes of %zu" },
        { "Слова, которые так и пишутся: VMware, IPsec", "Words that are written so: VMware, IPsec" },
        { "Слово закрывает и те, что с него начинаются: ИПшник – и ИПшники. Само слово попадает сюда после "
          "третьей отмены, с отметкой «выучено»",
          "A word also covers the words that start with it: IPsec - and IPsecs. A word gets here by itself the third "
          "time it is brought back, marked \"learned\"" },
        { "Буквы: EN, RU", "Letters: EN, RU" },
        { "Считать буквами", "Treat as letters" },
        { "Эти знаки не разделяют слова: some_name, кто-то", "These characters do not split words: some_name, well-known" },
        { "Как переключать раскладку", "How to switch the layout" },
        { "Если в каком-то приложении раскладка после исправления не переключается, выберите второй способ: "
          "FluentSwitcher нажмёт то сочетание, которым раскладка переключается в Windows",
          "If the layout does not switch after a fix in some app, choose the second way: FluentSwitcher presses the "
          "combination that switches the layout in Windows" },
        { "Обычный", "Usual" },
        { "Нажимать сочетание Windows", "Press the Windows combination" },
        { "Сочетание, которым раскладка переключается в Windows", "The combination that switches the layout in Windows" },
        { "FluentSwitcher нажимает его сам при втором способе. Обычно Alt + Shift или Win + Пробел",
          "FluentSwitcher presses it itself with the second way. Usually Alt + Shift or Win + Space" },

        // Hotkeys, layouts
        { "Сочетания клавиш", "Hotkeys" },
        { "Различать левые и правые Ctrl, Shift, Alt, Win", "Tell left and right Ctrl, Shift, Alt, Win apart" },
        { "При записи сочетания: включите и запишите сочетание заново – например, только левый Shift. "
          "Выключено – годится любой",
          "When recording a hotkey: turn it on and record the hotkey again – the left Shift only, for example. "
          "Off – either one fits" },
        { "Запишите нужное сочетание заново: теперь левые и правые клавиши различаются",
          "Record the hotkey again: left and right keys are told apart now" },
        { "Раскладки", "Layouts" },
        { "Раскладок пока нет", "No layouts yet" },
        { "FluentSwitcher заполнит список раскладками Windows при запуске",
          "FluentSwitcher fills the list with the Windows layouts when it starts" },
        { "Участвует в переключении и исправлении. Своё сочетание включает сразу эту раскладку, например левый "
          "Ctrl – английскую, правый – русскую",
          "Takes part in switching and fixing. Its own hotkey switches straight to this layout: the left Ctrl to "
          "English and the right one to Russian, for example" },

        // Commands
        { "Команды", "Commands" },
        { "Команды по сочетанию клавиш", "Commands on hotkeys" },
        { "Запустить приложение или вставить текст. В тексте @@(…) нажимает клавиши: "
          "@@(Ctrl + A) – выделить всё, @@(Enter) – новая строка",
          "Start an app or type text. In the text, @@(…) presses keys: @@(Ctrl + A) selects all, @@(Enter) starts "
          "a new line" },
        { "Добавить команду", "Add a command" },
        { "Вставить текст", "Type text" },
        { "Запустить приложение", "Start an app" },
        { "Текст печатается туда, где курсор", "The text is typed where the cursor is" },
        { "Приложение, документ или папка; путь можно вставить или выбрать",
          "An app, a document or a folder; paste the path or browse for it" },
        { "Удалить команду", "Delete the command" },
        { "Включена", "On" },
        { "Выполнить сейчас", "Run now" },
        { "FluentSwitcher не запущен: команду выполнить некому", "FluentSwitcher is not running: nothing can run the command" },
        { "Текст, например: С уважением, Дмитрий", "Text, for example: Best regards, Dmitry" },
        { "Путь к приложению", "Path to the app" },
        { "Выбрать…", "Browse…" },
        { "Приложение для команды", "App for the command" },
        { "Приложения (*.exe;*.bat;*.cmd;*.lnk)|*.exe;*.bat;*.cmd;*.lnk|Все файлы (*.*)|*.*",
          "Apps (*.exe;*.bat;*.cmd;*.lnk)|*.exe;*.bat;*.cmd;*.lnk|All files (*.*)|*.*" },
        { "Необязательно", "Optional" },
        { "Приложение", "App" },
        { "Аргументы", "Arguments" },
        { "Пауза, мс", "Pause, ms" },
        { "Текст", "Text" },
        { "Сочетание", "Hotkey" },

        // Advanced
        { "Дополнительно", "Advanced" },
        { "Отключить залипание клавиш", "Turn off Sticky Keys" },
        { "Пять нажатий Shift и другие сочетания специальных возможностей Windows не будут открывать их окна",
          "Pressing Shift five times and other accessibility shortcuts of Windows won't open their windows" },
        { "Сочетания с Ctrl + Alt в раскладках с AltGr", "Ctrl + Alt hotkeys in layouts with AltGr" },
        { "Windows принимает Ctrl + Alt за правый Alt (AltGr) и печатает символ вместо сочетания: в немецкой, "
          "польской раскладке, в русской – ₽ на Ctrl + Alt + 8. FluentSwitcher на миг переключает раскладку, и "
          "приложение получает сочетание",
          "Windows takes Ctrl + Alt for the right Alt (AltGr) and types a character instead of the hotkey: in German, "
          "Polish layouts, in Russian – ₽ on Ctrl + Alt + 8. FluentSwitcher switches the layout for a moment, and the "
          "app gets the hotkey" },
        { "Работать в окнах удалённого доступа", "Work in remote access windows" },
        { "Удалённый рабочий стол, AnyDesk, TeamViewer, виртуальные машины. Выключите, если FluentSwitcher стоит и "
          "на том компьютере – иначе слово исправят обе копии",
          "Remote Desktop, AnyDesk, TeamViewer, virtual machines. Turn it off if FluentSwitcher runs on that computer "
          "too – otherwise both copies fix the word" },
        { "Перепечатывать исправленное клавишами", "Retype fixes with keys" },
        { "Старый способ. Обычно исправленное слово вставляется готовыми символами: так новый Блокнот Windows 11 "
          "не теряет Shift. Включите, если какое-то приложение не принимает такую вставку",
          "The old way. Normally a fixed word goes in as ready characters, so the new Windows 11 Notepad doesn't lose "
          "Shift. Turn it on if some app doesn't accept that" },
        { "Пауза между символами при исправлении, мс", "Pause between characters when fixing, ms" },
        { "Исправленное слово печатается по одному символу с этой паузой: новый Блокнот Windows 11 "
          "теряет и повторяет символы, отправленные разом. Обычно 8",
          "A fixed word is typed one character at a time with this pause: the new Windows 11 Notepad loses and repeats "
          "characters sent all at once. Usually 8" },
        { "Интервал двойного нажатия, мс", "Double press interval, ms" },
        { "Два нажатия быстрее этого считаются двойным – для сочетаний «дважды». Обычно 250–350",
          "Two presses faster than this count as a double press – for “twice” hotkeys. Usually 250–350" },
        { "Журнал отладки", "Debug log" },
        { "До выхода из FluentSwitcher каждое нажатие клавиш пишется в log\\FluentSwitcher.exe.log "
          "в папке приложения. Пароли при этом не вводите; после проверки выключите и удалите журнал",
          "Until FluentSwitcher quits, every key press is written to log\\FluentSwitcher.exe.log in the "
          "app folder. Don't type passwords meanwhile; after the check, turn it off and delete the log" },
        { "Журнал отладки не переключился. ", "The debug log did not switch. " },
        { "FluentSwitcher не запущен: журнал вести некому", "FluentSwitcher is not running: nothing can keep the log" },
        { "Отчёт для разработчика", "Report to the developer" },
        { "Если что-то работает не так: начните запись, повторите ошибку и сохраните отчёт. В записи нет "
          "набранного текста – вместо букв только их число, пароли в неё не попадают. Отчёт появится на "
          "рабочем столе – его можно прочитать, – и откроется письмо разработчику, приложите отчёт к нему",
          "If something works wrong: start recording, repeat the error and save the report. The recording has no "
          "typed text – only the number of letters instead of them, passwords never get into it. The report appears on "
          "the desktop – you can read it – and a letter to the developer opens; attach the report to it" },
        { "Остановить запись", "Stop recording" },
        { "Начать запись", "Start recording" },
        { "Сохранить отчёт", "Save the report" },
        { "FluentSwitcher не запущен: записывать некому", "FluentSwitcher is not running: nothing can record" },
        { "FluentSwitcher не ответил", "FluentSwitcher did not answer" },
        { "Запись остановлена: сохраните отчёт", "Recording stopped: save the report" },
        { "Идёт запись. Повторите ошибку и нажмите «Сохранить отчёт». Через час запись остановится сама",
          "Recording. Repeat the error and click “Save the report”. The recording stops by itself in an hour" },
        { "Записи ещё нет: нажмите «Начать запись» и повторите ошибку",
          "Nothing recorded yet: click “Start recording” and repeat the error" },
        { "Не удалось сохранить отчёт на рабочем столе", "Could not save the report on the desktop" },
        { "Отчёт на рабочем столе: %s. Приложите его к письму на %s",
          "The report is on the desktop: %s. Attach it to a letter to %s" },
        { "Отчёт на рабочем столе: %s. Открывается письмо на %s с ним; если файла в письме нет – приложите его сами",
          "The report is on the desktop: %s. A letter to %s opens with it; if the file is not in the letter, attach it "
          "yourself" },
        { "Отправьте этот файл на %s", "Send this file to %s" },
        { "отчёт для разработчика", "report to the developer" },
        { "учётная запись: ", "account: " },
        { "администратор", "administrator" },
        { "обычная", "standard" },
        { "Сочетания Windows для смены: ", "Windows hotkeys for switching: " },
        { "языка – %s, раскладки – %s", "language – %s, layout – %s" },
        { "как по умолчанию", "default" },
        { "не назначено", "not assigned" },
        { "Программы, которые тоже работают с клавиатурой: ", "Other programs that work with the keyboard: " },
        { "нет", "none" },
        { "Настройки (без списков слов и текстов команд)", "Settings (without the word lists and the commands' texts)" },
        { "не прочитались: ", "could not be read: " },
        { "Запись (без набранного текста: вместо букв – их число)",
          "Recording (no typed text: the number of letters instead of them)" },
        { "FluentSwitcher %s – отчёт", "FluentSwitcher %s – report" },
        { "Что делали:", "What you did:" },
        { "Что ожидали:", "What you expected:" },
        { "Что получилось:", "What happened:" },
        { "Отчёт – файл %s на рабочем столе, приложите его к письму.",
          "The report is the file %s on the desktop; please attach it to this letter." },

        // About
        { "О приложении", "About" },
        { "Исправляет текст, набранный не в той раскладке, и переключает раскладки",
          "Fixes text typed in the wrong keyboard layout and switches layouts" },
        { "Основан на SimpleSwitcher", "Based on SimpleSwitcher" },
        { "Автор оригинала – Aegel5. FluentSwitcher – изменённая версия: окно настроек и флаги в стиле "
          "Windows 11, флаг у курсора, исправление с начала строки, запуск от администратора без "
          "вопросов и другие исправления",
          "The original is by Aegel5. FluentSwitcher is a modified version: a settings window and flags in the "
          "Windows 11 style, the flag at the cursor, fixing from the start of the line, running as administrator "
          "without prompts and other fixes" },
        { "Открыть на GitHub", "Open on GitHub" },
        { "Проверять обновления", "Check for updates" },
        { "Раз в день приложение спрашивает у GitHub номер последней версии, больше ничего не отправляет. "
          "Скачивать и ставить новую – решаете вы",
          "Once a day the app asks GitHub for the number of the latest version and sends nothing else. "
          "Whether to download and install it is up to you" },
        { "Обновления", "Updates" },
        { "Проверить сейчас", "Check now" },
        { "Скачать", "Download" },
        { "Открыть страницу загрузки", "Open the download page" },
        { "Проверяю…", "Checking…" },
        { "Ещё не проверялось", "Not checked yet" },
        { "Проверено: %s", "Checked: %s" },
        { "Вышла версия %s", "Version %s is out" },
        { "У вас последняя версия", "You have the latest version" },
        { "Не удалось связаться с GitHub. Страница загрузки откроется в браузере",
          "Could not reach GitHub. The download page opens in the browser" },
        { "Не удалось записать update.json в папку приложения", "Could not write update.json in the app's folder" },
        { "Лицензия GPL-3.0", "GPL-3.0 license" },
        { "Приложение бесплатное, исходный код открыт. Поставляется без каких-либо гарантий. Части "
          "других авторов – под своими лицензиями: wxWidgets, оформление FluentClipper, значки Fluent "
          "UI System Icons (Microsoft), флаги Flagpack и другие",
          "The app is free and open source. It comes without any warranty. Parts by others are under their own "
          "licenses: wxWidgets, the FluentClipper design, Fluent UI System Icons (Microsoft), Flagpack flags "
          "and more" },
        { "Лицензии", "Licenses" },
        { "Рядом с приложением нет файла THIRD-PARTY-NOTICES.txt", "THIRD-PARTY-NOTICES.txt is not next to the app" },
    };
    return table;
}
}

void SetLanguage(Language language)
{
    g_language = language;
}

Language CurrentLanguage()
{
    return g_language;
}

bool IsEnglish()
{
    return g_language == Language::English;
}

Language LanguageFor(const wxString& guiLang)
{
    if (guiLang == "Russian")
        return Language::Russian;
    if (guiLang == "Ukrainian")
        return Language::Ukrainian;
    if (!guiLang.empty())
        return Language::English;
    switch (PRIMARYLANGID(GetUserDefaultUILanguage()))
    {
    case LANG_RUSSIAN: return Language::Russian;
    case LANG_UKRAINIAN: return Language::Ukrainian;
    default: return Language::English;
    }
}

wxString T(const char* utf8)
{
    if (g_language != Language::Russian)
    {
        const auto& table = g_language == Language::English ? English() : UkrainianTexts();
        auto it = table.find(utf8);
        if (it != table.end())
            return wxString::FromUTF8(it->second);
    }
    return wxString::FromUTF8(utf8);
}
