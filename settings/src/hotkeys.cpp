#include "hotkeys.h"

#include <wx/tokenzr.h>

const std::vector<HotkeyAction>& HotkeyActions()
{
    static const std::vector<HotkeyAction> actions = {
        { "hk_RevertLastWord", "Исправить последнее слово",
          "Набранное не в той раскладке: стирает последнее слово, печатает его в другой раскладке и переключает её" },
        { "hk_RevertSeveralWords", "Исправить несколько слов",
          "Каждое следующее нажатие захватывает ещё одно слово назад" },
        { "hk_RevertAllRecentText", "Исправить весь недавний текст",
          "Всё, что набрано подряд в этом окне: до Enter, стрелок или смены окна" },
        { "hk_RevertSelelected", "Исправить выделенный текст",
          "Выделенное в любой программе печатается в другой раскладке" },
        { "hk_toUpperSelected", "Выделенное ПРОПИСНЫМИ / строчными",
          "Если выделенное уже прописными – строчными" },
        { "hk_InvertCaseSelected", "Выделенное иНВЕРСИЕЙ рЕГИСТРА", "Для текста, набранного с нажатым CapsLock" },
        { "hk_CycleSwitchLayout", "Следующая раскладка", "Переключает раскладку без исправления текста" },
        { "hk_EmulateCapsLock", "Нажать CapsLock", "Если CapsLock занят под сочетание, включить его можно так" },
        { "hk_ToggleEnabled", "Включить / выключить FluentSwitcher", "Работает и когда программа выключена" },
        { "hk_ShowMainWindow", "Открыть настройки", "Это окно" },
        { "hk_InsertWithoutFormat", "Вставить без оформления",
          "Если стоит FluentClipper, у него это уже есть: Ctrl+Shift+Insert" },
    };
    return actions;
}

wxString HotkeyDisplay(const wxString& stored)
{
    wxString shown;
    wxStringTokenizer parts(stored, ",");
    while (parts.HasMoreTokens())
    {
        wxString one = parts.GetNextToken().Strip(wxString::both);
        if (one.empty())
            continue;
        wxString suffix;
        if (one.Replace("#double", "") > 0)
            suffix = wxString::FromUTF8(" дважды");
        if (one.Replace("#up", "") > 0)
            suffix = wxString::FromUTF8(", при отпускании");
        one = one.Strip(wxString::both) + suffix;
        if (!shown.empty())
            shown += wxString::FromUTF8("   или   ");
        shown += one;
    }
    return shown;
}
