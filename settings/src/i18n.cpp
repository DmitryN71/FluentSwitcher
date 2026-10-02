#include "i18n.h"

#include <wx/msw/wrapwin.h>

#include <cstring>
#include <string>
#include <unordered_map>

namespace
{
bool g_english = false;

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
        { "Выделенное в любой программе печатается в другой раскладке",
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
        { "Работает и когда программа выключена", "Works even while the app is off" },
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
        { "Не включился: запустите FluentSwitcher от имени администратора или выключите работу в программах "
          "администратора",
          "Did not turn on: run FluentSwitcher as administrator or turn off work in administrator apps" },
        { "Не включился: включена другая копия программы", "Did not turn on: another copy of the app is on" },

        // General
        { "Основные", "General" },
        { "FluentSwitcher не запущен", "FluentSwitcher is not running" },
        { "Настройки сохранятся и подействуют при запуске", "Settings are saved and take effect when it starts" },
        { "Запустить", "Start" },
        { "Не нашёл FluentSwitcher.exe в папке программы", "FluentSwitcher.exe is not in the app folder" },
        { "FluentSwitcher включён", "FluentSwitcher is on" },
        { "Выключенный не исправляет текст и не отвечает на сочетания, кроме «Включить / выключить»",
          "When off, it fixes no text and answers no hotkeys except “Turn on / off”" },
        { "Запускать вместе с Windows", "Start with Windows" },
        { "Программа стартует при входе в Windows, видно только флаг у часов",
          "Starts when you sign in to Windows; only the flag by the clock shows" },
        { "Работать в программах, запущенных от имени администратора", "Work in apps run as administrator" },
        { "FluentSwitcher тогда работает с правами администратора: Windows спросит разрешения один раз, дальше "
          "он запускается через планировщик заданий без вопросов",
          "FluentSwitcher then runs as administrator: Windows asks once, after that it starts through the Task "
          "Scheduler without asking" },
        { "Чтобы работать в программах, запущенных от имени администратора, FluentSwitcher перезапустится "
          "с правами администратора. Windows спросит разрешения один раз: дальше программа запускается "
          "через планировщик заданий, без вопросов",
          "To work in programs run as administrator, FluentSwitcher will restart as administrator. Windows asks "
          "once: after that the app starts through the Task Scheduler without asking" },
        { "Перезапустить", "Restart" },
        { "Не сейчас", "Not now" },
        { "Без прав администратора FluentSwitcher выключен: перезапустите его или выключите работу "
          "в программах администратора",
          "Without administrator rights FluentSwitcher is off: restart it or turn off work in administrator apps" },
        { "Windows не дала прав администратора: FluentSwitcher запущен без них и выключен",
          "Windows gave no administrator rights: FluentSwitcher runs without them and is off" },
        { "FluentSwitcher перезапущен с правами администратора", "FluentSwitcher restarted as administrator" },
        { "Глянцевые", "Glossy" },
        { "Круглые", "Round" },
        { "Квадратные", "Square" },
        { "Всегда", "Always" },
        { "Ненадолго", "For a moment" },
        { "Не показывать", "Don't show" },
        { "Маленький", "Small" },
        { "Обычный", "Normal" },
        { "Крупный", "Large" },
        { "Очень крупный", "Extra large" },
        { "Размер флажка у курсора", "Size of the flag at the cursor" },
        { "Флажки", "Flags" },
        { "Флажок у текстового курсора", "Flag at the text cursor" },
        { "Показывает раскладку там, где вы печатаете", "Shows the layout where you type" },
        { "Сколько показывать «ненадолго»", "How long \"for a moment\" is" },
        { "После смены раскладки, окна или поля ввода", "After a change of the layout, the window or the input field" },
        { "1 секунду", "1 second" },
        { "2 секунды", "2 seconds" },
        { "3 секунды", "3 seconds" },
        { "5 секунд", "5 seconds" },
        { "10 секунд", "10 seconds" },
        { "Где флажок", "Where the flag is" },
        { "Если у края экрана места нет – с другой стороны строки", "No room at the screen's edge: on the other side of the line" },
        { "Под курсором", "Below the cursor" },
        { "Над курсором", "Above the cursor" },
        { "Прозрачность флажка у курсора", "Transparency of the flag at the cursor" },
        { "Чтобы не отвлекал от текста", "So that it does not distract from the text" },
        { "Нет", "None" },
        { "Слабая", "Light" },
        { "Средняя", "Medium" },
        { "Сильная", "Strong" },
        { "Очень сильная", "Very strong" },
        { "Максимальная", "Maximum" },
        { "При масштабе 100 %; на экранах с большим масштабом он крупнее",
          "At 100 % scale; on screens with a larger scale it is larger" },
        { "Значок программы вместо флага", "App icon instead of a flag" },
        { "Не показывать значок у часов", "No icon by the clock" },
        { "Флаг у часов", "Flag by the clock" },
        { "Показывает текущую раскладку", "Shows the current layout" },
        { "Язык", "Language" },
        { "Этого окна и меню у флага. Окно откроется на новом языке после сохранения",
          "Of this window and of the flag's menu. The window reopens in the new language after saving" },
        { "Тема", "Theme" },
        { "Этого окна. Оно откроется в новой теме после сохранения",
          "Of this window. It reopens in the new theme after saving" },
        { "Как в Windows", "As in Windows" },
        { "Светлая", "Light" },
        { "Тёмная", "Dark" },

        // Typing
        { "Набор текста", "Typing" },
        { "По пробелам и знакам препинания", "At spaces and punctuation" },
        { "Только по пробелам", "At spaces only" },
        { "По пробелам, знакам и «возможным знакам» – при исправлении нескольких слов",
          "At spaces, punctuation and “possible punctuation” – when fixing several words" },
        { "По пробелам, знакам и «возможным знакам» – всегда", "At spaces, punctuation and “possible punctuation” – always" },
        { "Где кончается слово", "Where a word ends" },
        { "Что исправлять как последнее слово. Знаки в конце слова исправляются вместе с ним: «cnjg?» – «стоп,». "
          "«Возможный знак» – клавиша, которая в одной раскладке буква, а в другой знак, например б и ,",
          "What is fixed as the last word. Punctuation at the end of a word is fixed with it: “cnjg?” – “стоп,”. "
          "“Possible punctuation” is a key that types a letter in one layout and punctuation in the other, like б and ," },
        { "Считать буквами", "Treat as letters" },
        { "Эти знаки не разделяют слова: some_name, кто-то", "These characters do not split words: some_name, well-known" },
        { "Как переключать раскладку", "How to switch the layout" },
        { "Если в какой-то программе раскладка после исправления не переключается, выберите второй способ: "
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
        { "При записи различать левые и правые Ctrl, Shift, Alt, Win", "Tell left and right Ctrl, Shift, Alt, Win apart when recording" },
        { "Только для записи: включите, чтобы записать, например, только правый Ctrl. Выключено – годится любой",
          "Only for recording: turn it on to record the right Ctrl only, for example. Off – either one fits" },
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
        { "Запустить программу или вставить текст. В тексте @@(…) нажимает клавиши: "
          "@@(Ctrl + A) – выделить всё, @@(Enter) – новая строка",
          "Start an app or type text. In the text, @@(…) presses keys: @@(Ctrl + A) selects all, @@(Enter) starts "
          "a new line" },
        { "Добавить команду", "Add a command" },
        { "Вставить текст", "Type text" },
        { "Запустить программу", "Start an app" },
        { "Текст печатается туда, где курсор", "The text is typed where the cursor is" },
        { "Программа, документ или папка; путь можно вставить или выбрать",
          "An app, a document or a folder; paste the path or browse for it" },
        { "Удалить команду", "Delete the command" },
        { "Включена", "On" },
        { "Выполнить сейчас", "Run now" },
        { "FluentSwitcher не запущен: команду выполнить некому", "FluentSwitcher is not running: nothing can run the command" },
        { "Текст, например: С уважением, Дмитрий", "Text, for example: Best regards, Dmitry" },
        { "Путь к программе", "Path to the app" },
        { "Выбрать…", "Browse…" },
        { "Программа для команды", "App for the command" },
        { "Программы (*.exe;*.bat;*.cmd;*.lnk)|*.exe;*.bat;*.cmd;*.lnk|Все файлы (*.*)|*.*",
          "Apps (*.exe;*.bat;*.cmd;*.lnk)|*.exe;*.bat;*.cmd;*.lnk|All files (*.*)|*.*" },
        { "Необязательно", "Optional" },
        { "Программа", "App" },
        { "Аргументы", "Arguments" },
        { "Пауза, мс", "Pause, ms" },
        { "Текст", "Text" },
        { "Сочетание", "Hotkey" },

        // Advanced
        { "Дополнительно", "Advanced" },
        { "Отключить залипание клавиш", "Turn off Sticky Keys" },
        { "Пять нажатий Shift и другие сочетания специальных возможностей Windows не будут открывать их окна",
          "Pressing Shift five times and other accessibility shortcuts of Windows won't open their windows" },
        { "Не перехватывать клавиши, которые уходят на удалённый компьютер", "Don't catch keys that go to a remote computer" },
        { "Для подключения к удалённому рабочему столу с этого компьютера", "For Remote Desktop connections from this computer" },
        { "Сочетания с Ctrl + Alt в раскладках с AltGr", "Ctrl + Alt hotkeys in layouts with AltGr" },
        { "Windows принимает Ctrl + Alt за правый Alt (AltGr) и печатает символ вместо сочетания: в немецкой, "
          "польской раскладке, в русской – ₽ на Ctrl + Alt + 8. FluentSwitcher на миг переключает раскладку, и "
          "программа получает сочетание",
          "Windows takes Ctrl + Alt for the right Alt (AltGr) and types a character instead of the hotkey: in German, "
          "Polish layouts, in Russian – ₽ on Ctrl + Alt + 8. FluentSwitcher switches the layout for a moment, and the "
          "app gets the hotkey" },
        { "Перепечатывать исправленное клавишами", "Retype fixes with keys" },
        { "Старый способ. Обычно исправленное слово вставляется готовыми символами: так новый Блокнот Windows 11 "
          "не теряет Shift. Включите, если какая-то программа не принимает такую вставку",
          "The old way. Normally a fixed word goes in as ready characters, so the new Windows 11 Notepad doesn't lose "
          "Shift. Turn it on if some app doesn't accept that" },
        { "Пауза между символами при исправлении, мс", "Pause between characters when fixing, ms" },
        { "Исправленное слово печатается по одному символу с этой паузой: новый Блокнот Windows 11 "
          "теряет и повторяет символы, отправленные разом. Обычно 8",
          "A fixed word is typed one character at a time with this pause: the new Windows 11 Notepad loses and repeats "
          "characters sent all at once. Usually 8" },
        { "Британский флаг для английского", "British flag for English" },
        { "Вместо американского", "Instead of the American one" },
        { "Интервал двойного нажатия, мс", "Double press interval, ms" },
        { "Два нажатия быстрее этого считаются двойным – для сочетаний «дважды». Обычно 250–350",
          "Two presses faster than this count as a double press – for “twice” hotkeys. Usually 250–350" },
        { "Журнал отладки", "Debug log" },
        { "Сразу и до выхода из FluentSwitcher каждое нажатие клавиш пишется в log\\FluentSwitcher.exe.log "
          "в папке программы. Пароли при этом не вводите; после проверки выключите и удалите журнал",
          "At once and until FluentSwitcher quits, every key press is written to log\\FluentSwitcher.exe.log in the "
          "app folder. Don't type passwords meanwhile; after the check, turn it off and delete the log" },
        { "FluentSwitcher не запущен: журнал вести некому", "FluentSwitcher is not running: nothing can keep the log" },

        // About
        { "О программе", "About" },
        { "Исправляет текст, набранный не в той раскладке, и переключает раскладки",
          "Fixes text typed in the wrong keyboard layout and switches layouts" },
        { "Основан на SimpleSwitcher", "Based on SimpleSwitcher" },
        { "Автор оригинала – Aegel5. FluentSwitcher – изменённая версия: окно настроек и флаги в стиле "
          "Windows 11, флажок у курсора, исправление с начала строки, запуск от администратора без "
          "вопросов и другие исправления",
          "The original is by Aegel5. FluentSwitcher is a modified version: a settings window and flags in the "
          "Windows 11 style, the flag at the cursor, fixing from the start of the line, running as administrator "
          "without prompts and other fixes" },
        { "Открыть на GitHub", "Open on GitHub" },
        { "Лицензия GPL-3.0", "GPL-3.0 license" },
        { "Программа бесплатная, исходный код открыт. Поставляется без каких-либо гарантий. Части "
          "других авторов – под своими лицензиями: wxWidgets, оформление FluentClipper, значки Fluent "
          "UI System Icons (Microsoft), флаги GoSquared и другие",
          "The app is free and open source. It comes without any warranty. Parts by others are under their own "
          "licenses: wxWidgets, the FluentClipper design, Fluent UI System Icons (Microsoft), GoSquared flags "
          "and more" },
        { "Лицензии", "Licenses" },
        { "Рядом с программой нет файла THIRD-PARTY-NOTICES.txt", "THIRD-PARTY-NOTICES.txt is not next to the app" },
    };
    return table;
}
}

void SetEnglish(bool english)
{
    g_english = english;
}

bool IsEnglish()
{
    return g_english;
}

bool EnglishFor(const wxString& guiLang)
{
    if (guiLang == "Russian")
        return false;
    if (!guiLang.empty())
        return true;
    return PRIMARYLANGID(GetUserDefaultUILanguage()) != LANG_RUSSIAN;
}

wxString T(const char* utf8)
{
    if (g_english)
    {
        const auto& table = English();
        auto it = table.find(utf8);
        if (it != table.end())
            return wxString::FromUTF8(it->second);
    }
    return wxString::FromUTF8(utf8);
}
