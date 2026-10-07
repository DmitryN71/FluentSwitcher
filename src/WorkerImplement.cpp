#include "WorkerImplement.h"
#include "ParseSnippet.h"
#include "LayoutConvert.h"
#include "AutoSwitch.h"
#include "WordStart.h"
#include "FieldText.h"

#include <atlbase.h>
#include <UIAutomation.h>
#include <fstream>

void WorkerImplement::ProcessKeyMsg(const Message_KeyType& keyData) {
    m_holdId = keyData.hold || keyData.held_end ? keyData.holdId : 0;
    m_caretBase = KeyHold::caretMoves;
    struct Release {
        unsigned id;
        ~Release() { KeyHold::RequestRelease(id); }
    };
    if (keyData.held_end) {
        // Хук придержал Enter / Tab после слова (и Shift+Enter, Ctrl+Enter): исправить и отпустить. Сама клавиша придёт
        // потом, как обычный набор. Модификатор, отпущенный на время исправления (LiftHeldMods), - нажать снова до того:
        // придержанный Enter уйдёт с ним, а отпускание его пальцем придёт за ним (хук держит и его).
        Release release{ keyData.holdId };
        m_heldMods.clear();
        for (TKeyCode k : keyData.cur_hotKey)
            if (CHotKey::IsKnownMods(k)) m_heldMods.push_back(k);
        if (!AutoSwitchLastWord(false)) FixTwoCaps(false);
        // Только пока придержка наша: отнятую (щелчок, другое окно, таймаут) хук уже отпускает сам, и нажатый снова
        // модификатор мог бы остаться нажатым - его отпускание пальцем уже ушло.
        if (m_heldModsUp && KeyHold::Allowed(keyData.holdId)) {
            InputSender sender;
            for (TKeyCode k : m_heldMods) sender.Add(k, KEY_STATE_DOWN);
            sender.Send();
        }
        m_heldMods.clear();
        m_heldModsUp = false;
        AutoWordEnd();
        return;
    }
    TKeyCode vkCode = keyData.vkCode;
    auto scan_ext = keyData.scan_ext;

    const auto& cur_hotkey = keyData.cur_hotKey;

    LOG_ANY("ProcessKeyMsg {} curState={}", CHotKey::ToString(vkCode), cur_hotkey.ToString());

    m_is_last_caps =
        keyData.is_caps;  // сохраним последнее известное значение. Нажатие caps по идее должно нам привести сюда.

    if (CHotKey::IsKnownMods(vkCode)) {
        if (keyData.hold) KeyHold::RequestRelease(keyData.holdId); // не бывает (держат после буквы и пробела), но не держать зря
        return;  // не очищаем текущий буфер нажатых клавиш так как они могут быть частью наших хот-кеев
    }

    // if (keyData.hk != hk_NULL) { // это событие down для нашей hot key up.
    //	if (!IsNeedSavedWords(keyData.hk)) {
    //		ClearAllWords();
    //	}
    //	return; // пропуск.
    // }

    TKeyBaseInfo key{
        .vk_code = vkCode,
        .scan_code = scan_ext,
        .is_shift = cur_hotkey.HasMod(VK_SHIFT),
        .is_caps = keyData.is_caps,
    };
    auto get_lay = [this] {
        CheckCurLay();  // попробуем добавить получение текущей раскладки на каждое нажатие для более точного
                        // определения символа (ради capslock handle).
        return topWndInfo2.lay;
    };
    key.type = AnalizeTyped(cur_hotkey, vkCode, scan_ext, get_lay, key);

    switch (key.type) {
        case KEYTYPE_BACKSPACE: {
            m_cycleList.DeleteLastSymbol();
            m_autoWord.backspace = true;
            break;
        }
        default: {
            bool is_shift = cur_hotkey.HasMod(VK_SHIFT);

            // todo: так это не работает. Нужно добавлять Shift только буквам (сложно) или временно включать/отключать
            // capslock.
            // if (keyData.is_caps) is_shift = !is_shift;

            m_cycleList.AddKeyToList(key, topWndInfo2.lay);
            break;
        }
        case KEYTYPE_COMMAND_NO_CLEAR:
            break;
        case KEYTYPE_NONE:
        case KEYTYPE_COMMAND_CLEAR: {
            ClearAllWords();
            // Enter, Esc - конец слова; стрелки, Home, End, Delete, сочетания - курсор мог переехать.
            if (Utils::is_in(vkCode, VK_RETURN, VK_ESCAPE))
                AutoWordEnd();
            else
                CaretMoved();
            break;
        }
    }

    if (keyData.hold) {
        // Хук придерживает нажатия после этого пробела (или буквы посреди слова): решить и отпустить, что бы ни случилось.
        Release release{ keyData.holdId };
        if (keyData.sign || keyData.early) {
            // Знак после слова в другой раскладке - слово целиком; не он - посреди слова, если это буква там.
            if (!(keyData.sign && AutoSwitchAtSign()) && keyData.early) AutoSwitchEarly();
        }
        else if (!AutoSwitchLastWord())
            FixTwoCaps();
    }
    if (key.type == KEYTYPE_SPACE) AutoWordEnd();
}

// TStatus ClipHasTextFormating(bool& fres)
//{
//	fres = false;
//
//	CAutoClipBoard clip;
//	IFS_RET(Open2(clip));
//
//	UINT format = 0;
//	while (1)
//	{
//		format = EnumClipboardFormats(format);
//		//LOG_ANY(L"Found format %u", format);
//		if (format == 0)
//		{
//			break;
//		}
//		if (format >= 40000)
//		{
//			fres = true;
//			break;
//		}
//	}
//
//	RETURN_SUCCESS;
// }

void toUpper(std::wstring& buf) {
    auto copy = buf;
    StrUtils::ToUpper(buf);
    if (copy == buf) {
        StrUtils::ToLower(buf);
    }
}

void InvertCase(std::wstring& buf) {
    for (auto& c : buf) {
        if (std::iswupper(c)) {
            c = std::towlower(c);
        } else {
            c = std::towupper(c);
        }
    }
}

// Пауза между стиранием и набором исправленного: новый Блокнот Windows 11, получив букву сразу за Backspace (и за
// сменой раскладки), иногда её теряет ("Stop" -> "top"). С журналом отладки, который чуть замедляет отправку, - нет.
static const DWORD c_afterErase = 40;

namespace {
// Поле пароля (обычное поле Windows с ES_PASSWORD): там ничего не исправляем.
bool IsPasswordFocus() {
    GUITHREADINFO gti{ sizeof(gti) };
    HWND fg = GetForegroundWindow();
    if (!fg || !GetGUIThreadInfo(GetWindowThreadProcessId(fg, nullptr), &gti) || !gti.hwndFocus) return false;
    wchar_t cls[64] = {};
    GetClassNameW(gti.hwndFocus, cls, 64);
    _wcslwr_s(cls);
    return wcsstr(cls, L"edit") && (GetWindowLongW(gti.hwndFocus, GWL_STYLE) & ES_PASSWORD);
}

IUIAutomation* Uia() {
    static CComPtr<IUIAutomation> uia = [] {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED); // уже (словари) - не страшно
        CComPtr<IUIAutomation> u;
        if (FAILED(u.CoCreateInstance(CLSID_CUIAutomation8))) u.CoCreateInstance(CLSID_CUIAutomation);
        CComPtr<IUIAutomation2> u2;
        if (u && SUCCEEDED(u->QueryInterface(IID_PPV_ARGS(&u2))) && u2) {
            u2->put_ConnectionTimeout(500); // зависшая программа держит не дольше полсекунды
            u2->put_TransactionTimeout(500);
        }
        return u;
    }();
    return uia;
}

// Поле пароля там, где оно не окно Edit (браузеры, программы на Electron, WinUI): UI Automation, IsPassword у
// элемента в фокусе. Спрашиваем, только когда уже решили переключать, - это поход в чужую программу.
bool IsPasswordUia() {
    IUIAutomation* uia = Uia();
    CComPtr<IUIAutomationElement> el;
    BOOL password = FALSE;
    return uia && SUCCEEDED(uia->GetFocusedElement(&el)) && el && SUCCEEDED(el->get_CurrentIsPassword(&password)) &&
        password;
}

// После щелчка или стрелок в том же окне: перед набранным словом в поле в фокусе не буква - слово набрано с начала
// (AutoSwitch::StartedAfterBoundary: 1 - да, 0 - нет, -1 - не узнать).
int StartedAfterBoundary(const std::wstring& typed) {
    IUIAutomation* uia = Uia();
    CComPtr<IUIAutomationElement> el;
    std::wstring before;
    bool atStart = false;
    if (!uia || FAILED(uia->GetFocusedElement(&el)) || !el ||
        !FieldText::BeforeCaret(el, (int)typed.size() + 8, before, atStart))
        return -1;
    return AutoSwitch::StartedAfterBoundary(before, typed, atStart);
}

// Программа впереди: имя файла (для журнала автопереключения).
std::wstring ForegroundProgram() {
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    std::wstring path, name;
    if (!pid || Utils::GetProcLowerNameByPid(pid, path, name) != SW_ERR_SUCCESS) return L"?";
    return name;
}

// Консоль (командная строка, Windows Terminal, ConEmu, mintty): там команды и пути, а не слова.
bool IsConsole() { return KeyHold::IsConsoleWindow(GetForegroundWindow()); }

// Редактор кода впереди: там i - переменная (for i in, int i = 0), а не местоимение (fix_lone_i).
bool IsCodeEditor() {
    static const wchar_t* const editors[] = {
        L"code.exe", L"code - insiders.exe", L"cursor.exe", L"windsurf.exe", L"devenv.exe", L"idea64.exe",
        L"pycharm64.exe", L"clion64.exe", L"rider64.exe", L"webstorm64.exe", L"goland64.exe", L"phpstorm64.exe",
        L"rubymine64.exe", L"datagrip64.exe", L"studio64.exe", L"fleet.exe", L"sublime_text.exe", L"notepad++.exe",
        L"zed.exe", L"atom.exe",
    };
    const std::wstring name = ForegroundProgram();
    for (const wchar_t* e : editors)
        if (name == e) return true;
    return false;
}
}

namespace {
// Текст клавиш набранного [begin, end) в раскладке lay; клавиша - не один знак - пусто.
std::wstring TailText(const CycleRevertList& list, size_t begin, size_t end, HKL lay) {
    std::wstring text;
    for (size_t i = begin; i < end; i++) {
        auto c = InputSender::KeyText(list.KeyAt(i), lay, false);
        if (c.size() != 1) return {};
        text += c;
    }
    return text;
}

// Контекст слова tail[0] (AutoSwitch::ContextOf) - по слову перед ним; lay - раскладка, в которой набрано слово.
AutoSwitch::Context ContextBefore(const CycleRevertList& list, const std::vector<CycleRevertList::TailWord>& tail,
                                  bool fixedBefore, HKL lay) {
    using AutoSwitch::Context;
    if (tail.size() < 2) return fixedBefore ? Context::Same : Context::Start;
    const auto& prev = tail[1];
    if (prev.lay != lay) return Context::Start; // набрано в другой раскладке (сменили руками) - как с начала
    const std::wstring text = TailText(list, prev.begin, prev.end, lay);
    if (text.empty()) return Context::Unknown;
    const std::wstring lang = Utils::GetNameForHKL_simple(lay);
    return AutoSwitch::ContextOf(text, lang, [&](const std::wstring& w) { return SpellCheck::CheckAnyCase(w, lang); });
}

// Слово tail[0] переключается из lay в other: с какой клавиши переводить вместе с короткими словами перед ним
// (AutoSwitch::Retro, до трёх слов, подряд); tail[0].begin - только само слово.
size_t RetroBegin(const CycleRevertList& list, const std::vector<CycleRevertList::TailWord>& tail, bool fixedBefore,
                  HKL lay, HKL other, const std::vector<std::wstring>& exceptions) {
    if (tail.empty()) return list.Size();
    const std::wstring lang = Utils::GetNameForHKL_simple(lay), otherLang = Utils::GetNameForHKL_simple(other);
    std::vector<AutoSwitch::RetroWord> words;
    for (size_t i = 1; i < tail.size(); i++) {
        const auto& word = tail[i];
        AutoSwitch::RetroWord w{ TailText(list, word.begin, word.end, lay), TailText(list, word.begin, word.end, other) };
        bool caps = false;
        for (size_t k = word.begin; k < word.end; k++) caps = caps || list.KeyAt(k).is_caps;
        w.sameLayout = word.lay == lay && !caps && !w.typed.empty() && !w.there.empty();
        w.fixedAfter = fixedBefore && i + 1 == tail.size();
        words.push_back(w);
    }
    const size_t count = AutoSwitch::RetroCount(words, lang, otherLang, exceptions,
                                                [&](const std::wstring& w) { return SpellCheck::CheckAnyCase(w, lang); });
    if (count) LOG_ANY(L"autoswitch: and {} words before it", count);
    return count ? tail[count].begin : tail[0].begin;
}
}

void WorkerImplement::RetypeTail(size_t begin, size_t middle, HKL first, HKL rest) {
    TKeyRevert a, b;
    for (size_t i = begin; i < middle && i < m_cycleList.Size(); i++) a.push_back(m_cycleList.KeyAt(i));
    for (size_t i = middle; i < m_cycleList.Size(); i++) b.push_back(m_cycleList.KeyAt(i));
    if (a.empty() && b.empty()) return;
    const int delay = (int)std::min<uint32_t>(conf_get_unsafe()->retype_delay_ms, 100);
    TextFixed();
    LiftHeldMods();
    const auto stop = CaretStop();
    InputSender::SendVkKeyPaced(VK_BACK, (int)(a.size() + b.size()), delay, stop);
    Sleep(c_afterErase); // новый Блокнот теряет первую букву, если она приходит сразу за стиранием
    InputSender::SendKeysAsText(a, first, m_is_last_caps, delay, stop);
    InputSender::SendKeysAsText(b, rest, m_is_last_caps, delay, stop);
    if (stop()) LOG_ANY("retype stopped: the caret moved");
    m_cycleList.SetLayFrom(begin, first);
    m_cycleList.SetLayFrom(middle, rest);
    m_cycleList.SetSeparateLast();
}

bool WorkerImplement::ByHandAfterOurs(const std::wstring& typed, bool partial) const {
    if (!m_autoWord.lay || topWndInfo2.lay == m_autoWord.lay || !m_lastAutoSwitch ||
        GetTickCount64() - m_lastAutoSwitch >= 30000 || m_lastSwitchedTyped.empty())
        return false;
    const std::wstring now = AutoSwitch::Lower(AutoSwitch::Letters(typed).core);
    if (now.empty()) return false;
    if (now == m_lastSwitchedTyped) return true;
    // Переключённое посреди слова - его начало: набирают дальше; посреди слова сейчас - набрано начало того.
    return (m_lastSwitchedEarly && now.starts_with(m_lastSwitchedTyped)) ||
        (partial && m_lastSwitchedTyped.starts_with(now));
}

// Слова перепечатываемого с двумя заглавными в начале ("ЕРу еуые" - "THe test", "ЕРуку" - "THere": Shift отпустили
// поздно) - по правилу "ДВух ЗАглавных": вторая буква строчная (так же FixText при "Исправить последнее слово";
// автопереключение переводит слово раньше, чем его увидело бы правило). Слово из трёх букв под правило не подходит
// (PCs, IDs, GHz так и пишутся) - его исправляем, только если словарь знает его так ("The"), а с двумя заглавными нет.
// Меняется только перепечатываемое: в буфере слово как набрано, и отмена вернёт его как было.
void WorkerImplement::TwoCapsInKeys(TKeyRevert& keys, HKL to) {
    if (!conf_get_unsafe()->two_caps) return;
    const auto exceptions = TwoCapsExceptions();
    const std::wstring lang = Utils::GetNameForHKL_simple(to);
    for (size_t i = 0; i < keys.size();) {
        const size_t b = i;
        std::wstring w;
        for (; i < keys.size(); i++) {
            const auto c = InputSender::KeyText(keys[i], to, false);
            if (c.size() != 1 || !TwoCaps::IsLetter(c[0])) break;
            w += c;
        }
        if (i == b) {
            i++;
            continue;
        }
        bool fix = TwoCaps::Matches(w, exceptions);
        if (!fix && w.size() == 3 && TwoCaps::IsUpper(w[0]) && TwoCaps::IsUpper(w[1]) && TwoCaps::IsLower(w[2]) &&
            std::find(exceptions.begin(), exceptions.end(), w) == exceptions.end()) {
            std::wstring one = w;
            one[1] = TwoCaps::ToLower(one[1]);
            fix = SpellCheck::Check(w, lang, true) == SpellCheck::Result::NotWord &&
                SpellCheck::Check(one, lang, true) == SpellCheck::Result::Word;
        }
        if (fix) {
            LOG_ANY(L"autoswitch: two capitals in {}", w);
            keys[b + 1].is_shift = false;
        }
    }
}

void WorkerImplement::SwitchTail(size_t begin, HKL to, bool wordEnded) {
    TKeyRevert list = m_cycleList.KeysFrom(begin);
    if (list.empty()) return;
    TwoCapsInKeys(list, to);
    TextFixed();
    LiftHeldMods();
    IFS_LOG(ProcessRevert({ .keylist = std::move(list), .lay = to,
                            .flags = SW_CLIENT_PUTTEXT | SW_CLIENT_SetLang | SW_CLIENT_BACKSPACE }));
    m_cycleList.SetLayFrom(begin, to);
    if (wordEnded) m_cycleList.SetSeparateLast();
    AutoLayoutIsOurs();
}

bool WorkerImplement::AutoSwitchLastWord(bool afterSpace) {
    GETCONF;
    if (!cfg->autoswitch || !KeyHold::Allowed(m_holdId)) return false;
    auto keys = afterSpace ? m_cycleList.LastWordKeys() : m_cycleList.TrailingWordKeys();
    if (keys.empty()) return false;
    // record - запомнить слово и причину для журнала (не пароль).
    auto no = [this, &keys](const char* why, bool record = true) {
        LOG_ANY("autoswitch: no, {}", why);
        if (record) {
            std::wstring text;
            for (auto* key : keys) text += InputSender::KeyText(*key, CurLay(), false);
            m_autoNo = { text, why };
        }
        return false;
    };
    if (cfg->IsSkipProgramTop() || IsPasswordFocus() || IsConsole())
        return no("a password, a console or an excluded program", false);
    if (m_autoWord.backspace) return no("the word was edited with Backspace");
    if (m_autoWord.undone) return no("switched back by hand in this word");
    CheckCurLay();
    const HKL lay = CurLay();
    std::wstring typed;
    for (auto* key : keys) {
        if (key->is_caps) return no("CapsLock");
        auto c = InputSender::KeyText(*key, lay, false);
        if (c.size() != 1) return no("a key that is not one letter");
        typed += c;
    }
    if (ByHandAfterOurs(typed, false)) return no("typed again after the layout was switched back by hand");
    const auto exceptions = AutoSwitchExceptions();
    const auto forced = AutoSwitchForced();
    // После щелчка или стрелок в том же окне могли дописывать середину слова: короткие куски (там меньше четырёх букв) не
    // трогаем - если только перед словом в поле не пробел, начало строки или знак (Дмитрий 06.10: щёлкнул в поле ответа,
    // набрал "nj - "Это" ждало соседа). Поле спрашиваем раз за слово и только о коротком.
    int boundary = -2;
    auto minLetters = [&](const std::wstring& there) -> size_t {
        if (!m_autoWord.moved) return 2;
        const size_t n = AutoSwitch::Letters(there).core.size();
        if (n < 2 || n >= 4) return 4;
        if (boundary == -2) {
            boundary = StartedAfterBoundary(typed);
            LOG_ANY(L"autoswitch: {} after a click or arrows, before it in the field: {}", typed,
                    boundary == 1 ? L"not a letter" : boundary == 0 ? L"a letter" : L"unknown");
        }
        return boundary == 1 ? 2 : 4;
    };
    auto dictionary = [](HKL l) {
        return [lang = Utils::GetNameForHKL_simple(l)](const std::wstring& w) { return SpellCheck::CheckAnyCase(w, lang); };
    };
    auto suggestions = [](HKL l) {
        return [lang = Utils::GetNameForHKL_simple(l)](const std::wstring& w) { return SpellCheck::Suggest(w, lang); };
    };
    // Слово перед этим - контекст коротких слов (AutoSwitch.h); слова перед ним - их переводят вместе с этим.
    bool fixedBefore = false;
    const auto tail = m_cycleList.TailWords(afterSpace, 6, &fixedBefore);
    const auto context = ContextBefore(m_cycleList, tail, fixedBefore, lay);
    for (HKL other : cfg->layouts_info.EnabledLayouts()) {
        if (other == lay) continue;
        std::wstring there;
        for (auto* key : keys) {
            auto c = InputSender::KeyText(*key, other, false);
            if (c.size() != 1) {
                there.clear();
                break;
            }
            there += c;
        }
        if (there.empty()) continue;
        // Те же буквы и там (русская и украинская раскладки - почти одни клавиши): переключить значило бы сменить только
        // раскладку посреди текста.
        if (AutoSwitch::Lower(there) == AutoSwitch::Lower(typed)) continue;
        // "Переключать всегда" - без словаря и правил (кроме исключений): "еру" - the, "ф" - a.
        const bool force = AutoSwitch::Forced(typed, there, forced) && !AutoSwitch::Excepted(typed, there, exceptions);
        auto shortWord = AutoSwitch::Short::No;
        if (!force) {
            const char* why = AutoSwitch::Skip(typed, there, minLetters(there), exceptions);
            if (!why)
                why = AutoSwitch::DecideWhy(typed, there, dictionary(lay), dictionary(other), suggestions(lay),
                                            suggestions(other), ShortWords::TrustDictionary(Utils::GetNameForHKL_simple(lay)));
            // Частое короткое слово своего языка ("шт", "руб", "ул", "gb") словарь может и не знать: "5 шт" - не "5 in".
            if (!why && AutoSwitch::FrequentAsTyped(typed, Utils::GetNameForHKL_simple(lay)))
                why = "a frequent short word as typed";
            // Короткое слово, которое словарь пропускает (ns - "ты", tot - "еще", "f&" - "а?"): по частоте и соседям.
            if (why && !AutoSwitch::Excepted(typed, there, exceptions) &&
                (strcmp(why, "too short") == 0 || strcmp(why, "a word as typed") == 0 ||
                 strcmp(why, "a short word with a sign after it") == 0)) {
                shortWord = AutoSwitch::ShortWord(typed, there, Utils::GetNameForHKL_simple(lay),
                                                  Utils::GetNameForHKL_simple(other), context, dictionary(lay));
                if (shortWord != AutoSwitch::Short::No) {
                    LOG_ANY(L"autoswitch: {} / {}: a short word, {}", typed, there, std::wstring(why, why + strlen(why)));
                    why = nullptr;
                }
            }
            if (why) {
                LOG_ANY(L"autoswitch: {} / {}: no, {}", typed, there, std::wstring(why, why + strlen(why)));
                m_autoNo = { typed, why };
                continue;
            }
        }
        // Короткие слова перед ним, набранные так же ("f" перед "vj;yj"), - вместе с ним.
        const size_t begin = RetroBegin(m_cycleList, tail, fixedBefore, lay, other, exceptions);
        const bool retro = !tail.empty() && begin < tail[0].begin;
        // Одно короткое слово без соседа-подтверждения ждёт: его переведёт следующее слово, если переключится.
        if (shortWord == AutoSwitch::Short::WithPartner && !retro) {
            LOG_ANY(L"autoswitch: {} / {}: a short word alone, waits for the next word", typed, there);
            m_autoNo = { typed, "a short word alone" };
            continue;
        }
        if (IsPasswordUia()) return no("a password field", false);
        if (!KeyHold::Claim(m_holdId)) return no("too late: the keys went on", false);
        KeyHold::Claimed claimed{ m_holdId };
        LOG_ANY(L"autoswitch: {} -> {}{}", typed, there, force ? L" (switch always)" : L"");
        m_autoNo = {};
        // Переводятся те же клавиши, что проверены (слово до пробела), с короткими словами перед ним.
        const size_t total = m_cycleList.Size();
        const size_t wordBegin = tail.empty() ? total - keys.size() - (afterSpace ? 1 : 0) : tail[0].begin;
        const size_t wordEnd = wordBegin + keys.size(), first = retro ? begin : wordBegin;
        auto trimmed = [](std::wstring s) {
            while (!s.empty() && s.back() == L' ') s.pop_back();
            return s;
        };
        const std::wstring from = TailText(m_cycleList, first, wordEnd, lay), to = TailText(m_cycleList, first, wordEnd, other);
        const std::wstring retroFrom = trimmed(TailText(m_cycleList, first, wordBegin, lay)),
                           retroTo = trimmed(TailText(m_cycleList, first, wordBegin, other));
        SwitchTail(first, other, true);
        m_autoSwitched = { .word = AutoSwitch::Lower(AutoSwitch::Letters(typed).core), .at = GetTickCount64(),
                           .size = m_cycleList.Size(), .typed = from, .there = to, .wordTyped = typed, .wordThere = there,
                           .retroTyped = retroFrom, .retroThere = retroTo, .from = lay, .to = other, .span = total - first,
                           .retro = wordBegin - first, .pair = shortWord == AutoSwitch::Short::WithPartner,
                           .total = m_cycleList.Total() };
        m_lastAutoSwitch = GetTickCount64();
        m_lastSwitchedTyped = AutoSwitch::Lower(AutoSwitch::Letters(typed).core);
        m_lastSwitchedEarly = false;
        RememberSwitch(typed, there);
        Journal("switched", from, to);
        return true;
    }
    return false;
}

bool WorkerImplement::AutoSwitchAtSign() {
    GETCONF;
    if (!cfg->autoswitch || !KeyHold::Allowed(m_holdId)) return false;
    auto keys = m_cycleList.TrailingWordKeys();
    if (keys.size() < 3) return false;
    CheckCurLay();
    const HKL lay = CurLay();
    const auto& last = *keys.back();
    const std::wstring here = InputSender::KeyText(last, lay, false);
    bool sign = false;
    for (HKL other : cfg->layouts_info.EnabledLayouts()) {
        if (other == lay) continue;
        const std::wstring there = InputSender::KeyText(last, other, false);
        sign = sign || (there.size() == 1 && there != here && wcschr(L".,;:?!\"'", there[0]));
        // ... или знак, который в обеих раскладках один и тот же и кончает слово: Shift+1 - "!", Shift+0 - ")"
        // ("Щщзы!" после щелчка ждало пробела, Дмитрий 07.10).
        sign = sign || (there.size() == 1 && there == here && wcschr(L"!)", there[0]));
    }
    if (!sign) return false;
    std::wstring typed;
    for (auto* key : keys) typed += InputSender::KeyText(*key, lay, false);
    // Набранное - слово или начало слова своего языка: оно, скорее, ещё пишется ("шаб" - "шаблон", хотя там "if,").
    const std::wstring core = AutoSwitch::Letters(typed).core;
    if (core.size() < 2) return false;
    if (WordStart::Typed(core, Utils::GetNameForHKL_simple(lay))) {
        LOG_ANY(L"autoswitch: {} at a sign: a word or the beginning of one as typed", typed);
        return false;
    }
    LOG_ANY(L"autoswitch: {} at a sign that ends a word in the other layout: checked as at its end", typed);
    return AutoSwitchLastWord(false);
}

void WorkerImplement::AutoSwitchEarly() {
    GETCONF;
    // Посреди этого слова больше не решать: хук до конца слова пропускает буквы без задержки.
    auto never = [this](const char* why) {
        if (KeyHold::Allowed(m_holdId)) KeyHold::earlyDone = true; // запоздалый ответ - уже не об этом слове
        LOG_ANY("autoswitch early: not in this word, {}", why);
    };
    if (!cfg->autoswitch || !cfg->autoswitch_early || !KeyHold::Allowed(m_holdId)) return never("off or too late");
    if (cfg->IsSkipProgramTop() || IsPasswordFocus() || IsConsole())
        return never("a password, a console or an excluded program");
    if (m_autoWord.backspace) return never("the word was edited with Backspace");
    if (m_autoWord.undone) return never("switched back by hand in this word");
    CheckCurLay();
    const HKL lay = CurLay();
    auto keys = m_cycleList.TrailingWordKeys();
    if (keys.empty()) return;
    std::wstring typed;
    for (auto* key : keys) {
        if (key->is_caps) return never("CapsLock");
        auto c = InputSender::KeyText(*key, lay, false);
        if (c.size() != 1) return never("a key that is not one letter");
        typed += c;
    }
    if (ByHandAfterOurs(typed, true)) return never("typed again after the layout was switched back by hand");
    const auto exceptions = AutoSwitchExceptions();
    const auto forced = AutoSwitchForced();
    bool fixedBefore = false;
    const auto tail = m_cycleList.TailWords(false, 6, &fixedBefore);
    // После щелчка или стрелок в том же окне могли дописывать середину слова: на букву позже - если только поле не
    // говорит, что перед словом пробел, начало строки или знак (Дмитрий 07.10: вернулся в окно, щёлкнул в пустое поле,
    // "Щщзы" - четыре буквы - не стало "Oops"). Поле спрашиваем, пока оно не ответит, ответ - на всё слово.
    size_t minLetters = AutoSwitch::kEarlyMin;
    if (m_autoWord.moved) {
        if (m_autoWord.boundary < 0) {
            m_autoWord.boundary = StartedAfterBoundary(typed);
            LOG_ANY(L"autoswitch early: {} after a click or arrows, before it in the field: {}", typed, m_autoWord.boundary);
        }
        if (m_autoWord.boundary != 1) minLetters++;
    }
    const std::wstring lang = Utils::GetNameForHKL_simple(lay);
    bool later = false;
    for (HKL other : cfg->layouts_info.EnabledLayouts()) {
        if (other == lay) continue;
        std::wstring there;
        for (auto* key : keys) {
            auto c = InputSender::KeyText(*key, other, false);
            if (c.size() != 1) {
                there.clear();
                break;
            }
            there += c;
        }
        if (there.empty()) continue;
        // Те же буквы и там (русская и украинская раскладки): ждать буквы, которая их различит (ы - і).
        if (AutoSwitch::Lower(there) == AutoSwitch::Lower(typed)) {
            later = true;
            continue;
        }
        const std::wstring otherLang = Utils::GetNameForHKL_simple(other);
        // Свои слова ("Переключать всегда", в нужном виде: mofii) - тоже начала слов: "ьщаш" - mofi…
        auto forcedStarts = [&](const std::wstring& w) {
            const std::wstring l = WordStart::Lower(w);
            for (const auto& f : forced) {
                const std::wstring fl = WordStart::Lower(f);
                if (fl.size() > l.size() && fl.compare(0, l.size(), l) == 0) return true;
            }
            return false;
        };
        auto thereStarts = [&](const std::wstring& w) { return WordStart::There(w, otherLang) || forcedStarts(w); };
        // Слово из "Переключать всегда" набрано целиком - сейчас, не дожидаясь пробела, если так не начинается ни одно
        // слово своего языка. Правило ниже смотрит и на букву раньше, а "рее" - начало "реестр": выученное "http"
        // ("реез") переключалось только на пробеле (Дмитрий 07.10).
        const bool forcedWhole = AutoSwitch::Forced(typed, there, forced) &&
            !AutoSwitch::Excepted(typed, there, exceptions) && !WordStart::Typed(AutoSwitch::Letters(typed).core, lang);
        auto verdict = forcedWhole ? AutoSwitch::EarlyVerdict{ AutoSwitch::Early::Switch, nullptr }
            : AutoSwitch::DecideEarly(typed, there, minLetters, exceptions,
                                      [&](const std::wstring& w) { return WordStart::Typed(w, lang); }, thereStarts);
        // "Не в этом слове" (заглавная внутри, аббревиатура) - но там начало слова из "Переключать всегда": ждать его
        // целиком. "ЬЩАш" - заглавная внутри, решение "никогда" на четвёртой букве, и "ЬЩАшш" - MOFii переключалось только
        // на пробеле, а "реез" - http сразу (Дмитрий 07.10).
        if (verdict.what == AutoSwitch::Early::Never && forcedStarts(there) && !AutoSwitch::Excepted(typed, there, exceptions))
            verdict = { AutoSwitch::Early::NotYet, "a beginning of a word of Always switch" };
        if (verdict.what != AutoSwitch::Early::Switch) {
            later = later || verdict.what == AutoSwitch::Early::NotYet;
            LOG_ANY(L"autoswitch early: {} / {}: {}, {}", typed, there,
                    verdict.what == AutoSwitch::Early::NotYet ? L"not yet" : L"no",
                    std::wstring(verdict.why, verdict.why + strlen(verdict.why)));
            continue;
        }
        if (IsPasswordUia()) return never("a password field");
        if (!KeyHold::Claim(m_holdId)) return; // пока решали, придержку отпустили (3 с): пальцы печатают дальше
        KeyHold::Claimed claimed{ m_holdId };
        LOG_ANY(L"autoswitch early: {} -> {}", typed, there);
        KeyHold::earlyDone = true;
        // Короткие слова перед ним, набранные так же ("f" перед "vj;y"), - вместе с ним.
        const size_t total = m_cycleList.Size(), wordBegin = tail.empty() ? total - keys.size() : tail[0].begin;
        const size_t begin = tail.empty() ? wordBegin : RetroBegin(m_cycleList, tail, fixedBefore, lay, other, exceptions);
        const std::wstring more = L"\u2026";
        const std::wstring from = TailText(m_cycleList, begin, total, lay) + more;
        const std::wstring to = TailText(m_cycleList, begin, total, other) + more;
        std::wstring retroFrom = TailText(m_cycleList, begin, wordBegin, lay), retroTo = TailText(m_cycleList, begin, wordBegin, other);
        while (!retroFrom.empty() && retroFrom.back() == L' ') retroFrom.pop_back();
        while (!retroTo.empty() && retroTo.back() == L' ') retroTo.pop_back();
        // Как "Исправить последнее слово", но слово не кончилось: его буквы в буфере остаются одним словом (без
        // SetSeparateLast) - конец слова проверит его целиком, "Исправить последнее слово" вернёт целиком.
        SwitchTail(begin, other, false);
        m_autoSwitched = { .word = AutoSwitch::Lower(AutoSwitch::Letters(typed).core), .at = GetTickCount64(),
                           .size = m_cycleList.Size(), .typed = from, .there = to, .wordTyped = typed + more,
                           .wordThere = there + more, .retroTyped = retroFrom, .retroThere = retroTo, .early = true,
                           .ends = m_wordEnds, .from = lay, .to = other, .span = total - begin, .retro = wordBegin - begin,
                           .total = m_cycleList.Total() };
        m_lastAutoSwitch = GetTickCount64();
        m_lastSwitchedTyped = AutoSwitch::Lower(AutoSwitch::Letters(typed).core);
        m_lastSwitchedEarly = true;
        RememberSwitch(typed, there);
        Journal("switched", from, to);
        return;
    }
    if (!later) never("nothing to switch to");
}

void WorkerImplement::WarmUpEarly() {
    GETCONF;
    if (!cfg->autoswitch || !cfg->autoswitch_early) return;
    for (HKL l : cfg->layouts_info.EnabledLayouts()) WordStart::Available(Utils::GetNameForHKL_simple(l));
}

std::vector<std::wstring> WorkerImplement::AutoSwitchExceptions() {
    std::vector<std::wstring> words;
    for (const auto& e : conf_get_unsafe()->autoswitch_exceptions) words.push_back(StrUtils::Convert(e));
    return words;
}

std::vector<std::wstring> WorkerImplement::AutoSwitchForced() {
    std::vector<std::wstring> words;
    for (const auto& e : conf_get_unsafe()->autoswitch_force) words.push_back(StrUtils::Convert(e));
    return words;
}

void WorkerImplement::Journal(const char* what, const std::wstring& from, const std::wstring& to,
                              const std::string& note) {
    GETCONF;
    if (!cfg->autoswitch || !cfg->autoswitch_journal) return;
    SYSTEMTIME st{};
    GetLocalTime(&st);
    const std::wstring label = StrUtils::Convert(std::string(LOC(what)));
    const std::wstring after = note.empty() ? L"" : L"  [" + StrUtils::Convert(note) + L"]";
    const std::wstring line = std::format(L"{:02}.{:02}.{:04} {:02}:{:02}:{:02}  {:<10} {} \u2192 {}  ({}){}\r\n", st.wDay,
                                          st.wMonth, st.wYear, st.wHour, st.wMinute, st.wSecond, label, from, to,
                                          ForegroundProgram(), after);
    // Пишет файл поток окна (gui2/main.cpp, WriteJournalLine): здесь, пока хук держит нажатия, диск (и антивирус на нём)
    // не ждём. Строка - с временем и программой этой минуты.
    auto* utf8 = new std::string(StrUtils::Convert(line));
    if (!PostMessageW(g_guiHandle, WM_JournalLine, 0, (LPARAM)utf8)) delete utf8;
}

void WriteJournalLine(const std::string& utf8) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path dir = PathUtils::GetPath_folder_noLower2() / L"log";
    fs::create_directories(dir, ec);
    const fs::path file = dir / L"autoswitch.log";
    if (fs::exists(file, ec) && fs::file_size(file, ec) > 1024 * 1024) // больше мегабайта - в старый, начать заново
        fs::rename(file, dir / L"autoswitch.old.log", ec);
    std::ofstream out(file, std::ios::binary | std::ios::app);
    out.write(utf8.data(), (std::streamsize)utf8.size());
}

void WorkerImplement::RememberSwitch(const std::wstring& typed, const std::wstring& there) {
    m_recentSwitches.emplace_back(AutoSwitch::Lower(AutoSwitch::Letters(typed).core),
                                  AutoSwitch::Lower(AutoSwitch::Letters(there).core));
    if (m_recentSwitches.size() > 64) m_recentSwitches.pop_front();
}

std::optional<WorkerImplement::HandFix> WorkerImplement::TakeHandFix() {
    GETCONF;
    if (!cfg->autoswitch) return std::nullopt;
    auto keys = m_cycleList.LastWordKeys();
    if (keys.empty()) keys = m_cycleList.TrailingWordKeys();
    const HKL lay = CurLay(), next = cfg->layouts_info.NextEnabledLayout(lay);
    if (keys.empty() || !lay || !next) return std::nullopt;
    HandFix fix;
    for (auto* key : keys) {
        fix.typed += InputSender::KeyText(*key, lay, false);
        fix.fixed += InputSender::KeyText(*key, next, false);
    }
    fix.lang = Utils::GetNameForHKL_simple(lay);
    // Только что отменённое автопереключение исправляют снова ("Shift дважды" по привычке сразу после него - отмена, ещё
    // раз - обратно): переключение было верным, отмена не в счёт исключений (иначе на третий раз верное слово ушло бы в
    // "Не переключать").
    const auto undone = std::exchange(m_lastUndo, {});
    if (!undone.word.empty() && GetTickCount64() - undone.at < 10000 &&
        AutoSwitch::Lower(AutoSwitch::Letters(fix.typed).core) == undone.word)
        fix.undoneAgain = undone.word;
    // Почему автопереключение его не тронуло: проверяло это слово - его причина; нет - до проверки не дошло (быстрый
    // набор во время другой проверки, окно от администратора, отключено).
    fix.why = m_autoNo.typed == fix.typed ? m_autoNo.why : "not checked";
    return fix;
}

// Слово, которое автопереключение не тронуло, исправили вручную: в счёт "Переключать всегда" (на третий раз - туда,
// gui2/main.cpp) - в нужном виде, строчными. Не в счёт:
//   - одна буква, знак внутри или цифры (адрес, код);
//   - набранное - слово своего языка (или словаря нет): "Переключать всегда" обходит словарь, и выученное "ты" (из
//     "ns") переключало бы каждое английское ns;
//   - поздняя отмена автопереключения (больше 10 с после него): возвращают то, что оно переключило ("лог" - "kju"),
//     и выученное "kju" ломало бы правильно набранное "лог";
//   - слово из "Не переключать" и слово, которое и так в "Переключать всегда".
std::wstring WorkerImplement::LearnableFix(const HandFix& fix) {
    if (!fix.undoneAgain.empty()) return {};
    const auto part = AutoSwitch::Letters(fix.fixed);
    const std::wstring word = AutoSwitch::Lower(part.core);
    const std::wstring typedCore = AutoSwitch::Letters(fix.typed).core, typed = AutoSwitch::Lower(typedCore);
    if (word.size() < 2 || part.inner || std::ranges::any_of(fix.typed, [](wchar_t c) { return iswdigit(c) != 0; }))
        return {};
    if (SpellCheck::CheckAnyCase(typedCore, fix.lang) != SpellCheck::Result::NotWord) return {};
    for (const auto& [was, became] : m_recentSwitches)
        if (!became.empty() && typed.starts_with(became) && word.starts_with(was)) return {};
    if (AutoSwitch::Excepted(fix.typed, fix.fixed, AutoSwitchExceptions()) ||
        AutoSwitch::Forced(fix.typed, fix.fixed, AutoSwitchForced()))
        return {};
    return word;
}

void WorkerImplement::HandFixDone(const HandFix& fix) {
    GETCONF;
    if (!fix.undoneAgain.empty()) PostMessageW(g_guiHandle, WM_AutoSwitchUnlearn, 0, (LPARAM)new std::wstring(fix.undoneAgain));
    // Исправили обратно то, что только что исправили вручную (случайное нажатие - и ещё одно, назад): оба - не слова для
    // "Переключать всегда"; ушедшее в счёт - снять.
    const auto prev = std::exchange(m_lastHandFix, {});
    const bool back = !prev.fixed.empty() && GetTickCount64() - prev.at < 10000 &&
        AutoSwitch::Lower(AutoSwitch::Letters(fix.fixed).core) == prev.typed &&
        AutoSwitch::Lower(AutoSwitch::Letters(fix.typed).core) == prev.fixed;
    if (back && prev.counted)
        PostMessageW(g_guiHandle, WM_AutoSwitchUnlearnForce, 0, (LPARAM)new std::wstring(prev.fixed));
    const std::wstring learn = back ? std::wstring() : LearnableFix(fix);
    // Пароль - ни в счёт, ни в журнал. Спрашиваем поле (UI Automation) только когда есть что записать; исправление уже
    // напечатано - вопрос его не задерживает.
    if (learn.empty() && !cfg->autoswitch_journal) {
        if (!back) m_lastHandFix = { AutoSwitch::Lower(AutoSwitch::Letters(fix.fixed).core),
                                     AutoSwitch::Lower(AutoSwitch::Letters(fix.typed).core), false, GetTickCount64() };
        return;
    }
    if (IsPasswordFocus() || IsPasswordUia()) return;
    if (!learn.empty()) PostMessageW(g_guiHandle, WM_AutoSwitchLearnForce, 0, (LPARAM)new std::wstring(learn));
    if (!back) m_lastHandFix = { AutoSwitch::Lower(AutoSwitch::Letters(fix.fixed).core),
                                 AutoSwitch::Lower(AutoSwitch::Letters(fix.typed).core), !learn.empty(), GetTickCount64() };
    if (!cfg->autoswitch_journal) return;
    Journal("by hand", fix.typed, fix.fixed,
            !fix.undoneAgain.empty() ? "fixed again right after switching back: the switch was right"
            : back                   ? "fixed back right after a fix by hand: not counted"
                                     : fix.why);
}

bool WorkerImplement::CountAutoSwitchUndo(AutoUndo* undo) {
    auto last = std::exchange(m_autoSwitched, {});
    if (last.word.empty() || GetTickCount64() - last.at > 10000) return false;
    if (last.early) {
        // Посреди слова: исправляют то же слово - его ещё набирают или только что кончили пробелом.
        const bool same = m_wordEnds == last.ends ? !m_cycleList.TrailingWordKeys().empty()
                                                  : m_wordEnds == last.ends + 1 && !m_cycleList.LastWordKeys().empty();
        if (!same) return false;
        if (m_wordEnds == last.ends) m_autoWord.undone = true; // слово ещё набирают: в нём больше не переключать
    }
    // После переключения ничего не набирали (Total() - и после 90 клавиш, когда Size() уже не растёт).
    else if (m_cycleList.Size() != last.size || m_cycleList.Total() != last.total)
        return false;
    // Переведённое - всё, что с него начинается (набранное после переключения посреди слова - тоже: Total() вырос на
    // столько). Стёрли больше, чем набрали, - уже не то: как обычно, вернётся последнее слово.
    const size_t keys = last.span && m_cycleList.Total() >= last.total ? last.span + (m_cycleList.Total() - last.total) : 0;
    const bool span = keys && keys <= m_cycleList.Size() && last.retro < keys;
    if (span && last.retro && !last.pair) {
        // Слово переключено уверенно, а короткие перед ним - по соседству: сначала вернуть только их (ошибиться могли в
        // них); слово остаётся, и следующее нажатие вернёт его - уже в счёт исключений.
        LOG_ANY(L"autoswitch: {} switched back", last.retroTyped);
        Journal("switched back", last.retroThere, last.retroTyped);
        auto rest = last;
        rest.at = GetTickCount64();
        rest.typed = last.wordTyped;
        rest.there = last.wordThere;
        rest.retroTyped.clear();
        rest.retroThere.clear();
        rest.span = last.span - last.retro;
        rest.retro = 0;
        m_autoSwitched = rest;
        if (undo) *undo = { .tail = keys, .retro = last.retro, .all = false, .from = last.from, .to = last.to };
        return true;
    }
    LOG_ANY(L"autoswitch: {} switched back", last.word);
    Journal("switched back", last.there, last.typed);
    PostMessageW(g_guiHandle, WM_AutoSwitchLearn, 0, (LPARAM)new std::wstring(last.word));
    m_lastUndo = { .word = last.word, .at = GetTickCount64() };
    if (undo && span) *undo = { .tail = keys, .retro = 0, .all = true, .from = last.from, .to = last.to };
    return true;
}

void WorkerImplement::AutoWordEnd() {
    m_wordEnds++;
    m_autoWord.backspace = m_autoWord.moved = m_autoWord.undone = false;
    m_autoWord.boundary = -1;
    if (!conf_get_unsafe()->autoswitch) return;
    CheckCurLay();
    m_autoWord.lay = CurLay();
}

void WorkerImplement::FixTwoCaps(bool afterSpace) {
    GETCONF;
    if ((!cfg->two_caps && !cfg->fix_lone_i) || !KeyHold::Allowed(m_holdId) || cfg->IsSkipProgramTop() || IsPasswordFocus())
        return;
    auto keys = afterSpace ? m_cycleList.LastWordKeys() : m_cycleList.TrailingWordKeys();
    if (keys.empty()) return;
    const HKL lay = CurLay();
    std::wstring text;
    for (auto* key : keys) {
        if (key->is_caps) return; // с CapsLock регистр значит другое
        auto c = InputSender::KeyText(*key, lay, false);
        if (c.size() != 1) return; // клавиша = один символ, иначе не сосчитать, что стирать
        text += c;
    }
    auto fix = cfg->two_caps ? TwoCaps::Analyze(text, TwoCapsExceptions()) : TwoCaps::Fix{};
    // Английское i отдельным словом - I; только в английской раскладке и не в консоли или редакторе кода (там i -
    // переменная: for i in, int i = 0).
    if (fix.tail.empty() && cfg->fix_lone_i && Utils::GetNameForHKL_simple(lay).starts_with(L"en") && !IsConsole() &&
        !IsCodeEditor())
        fix = TwoCaps::LoneI(text, TwoCapsExceptions());
    if (fix.tail.empty()) return;
    // Слово, набранное в чужой раскладке ("GJgsnrf" - не английское, "попытка" - русское): правило его не трогает, его
    // исправит перевод раскладки, и сразу с заглавными ("Попытка"). Словари - Windows (WinDictionary.h); нет словаря -
    // как раньше.
    if (!fix.upper && SpellCheck::Check(fix.word, Utils::GetNameForHKL_simple(lay)) == SpellCheck::Result::NotWord) {
        for (HKL other : cfg->layouts_info.EnabledLayouts()) {
            if (other == lay) continue;
            std::wstring there;
            for (auto* key : keys) there += InputSender::KeyText(*key, other, false);
            size_t begin = 0, end = there.size();
            while (begin < end && !TwoCaps::IsLetter(there[begin])) begin++;
            while (end > begin && !TwoCaps::IsLetter(there[end - 1])) end--;
            const std::wstring word = there.substr(begin, end - begin);
            if (!word.empty() && SpellCheck::Check(word, Utils::GetNameForHKL_simple(other)) == SpellCheck::Result::Word) {
                LOG_ANY(L"two caps: {} is {} in the other layout, left for the layout fix", fix.word, word);
                return;
            }
        }
    }
    const std::wstring typed = text.substr(fix.from);
    if (!KeyHold::Claim(m_holdId)) return; // пока решали, придержку отпустили (3 с): пальцы печатают дальше
    KeyHold::Claimed claimed{ m_holdId };
    LOG_ANY(L"two caps: {} -> {}{}", text, text.substr(0, fix.from), fix.tail);
    TextFixed();
    LiftHeldMods();
    const int delay = (int)std::min<uint32_t>(cfg->retype_delay_ms, 100);
    const std::wstring space = afterSpace ? L" " : L"";
    const auto stop = CaretStop();
    InputSender::SendVkKeyPaced(VK_BACK, (int)(typed.size() + space.size()), delay, stop); // со второй буквы (и пробел)
    Sleep(c_afterErase); // новый Блокнот теряет первую букву, если она приходит сразу за стиранием
    InputSender::SendTextPaced(fix.tail + space, delay, stop);
    if (stop()) {
        LOG_ANY("two caps stopped: the caret moved");
        m_twoCaps = {};
        return;
    }
    keys[fix.from]->is_shift = fix.upper; // и в буфере слов: вторая буква теперь строчная (i - заглавная)
    // Отмена - только после пробела: после Enter сообщение уже ушло, после Tab курсор может быть в другом поле.
    if (afterSpace)
        m_twoCaps = { fix.word, typed, fix.tail, GetTickCount64(), m_cycleList.Size(), keys[fix.from], m_cycleList.Total(),
                      fix.upper };
    else
        m_twoCaps = {};
}

std::vector<std::wstring> WorkerImplement::TwoCapsExceptions() {
    std::vector<std::wstring> words;
    for (const auto& e : conf_get_unsafe()->two_caps_exceptions) words.push_back(StrUtils::Convert(e));
    return words;
}

void WorkerImplement::FixTwoCapsInKeys(TKeyRevert& keys, HKL lay) {
    std::wstring text;
    for (const auto& key : keys) {
        if (key.is_caps) return;
        auto c = InputSender::KeyText(key, lay, false);
        if (c.size() != 1) return; // клавиша = один символ, иначе не сопоставить
        text += c;
    }
    const std::wstring fixed = TwoCaps::FixText(text, TwoCapsExceptions());
    for (size_t i = 0; i < text.size(); i++) {
        if (fixed[i] != text[i]) keys[i].is_shift = false;
    }
    if (fixed != text) LOG_ANY(L"two caps in the layout fix: {} -> {}", text, fixed);
}

bool WorkerImplement::TwoCapsUndoReady() const {
    return !m_twoCaps.word.empty() && GetTickCount64() - m_twoCaps.at <= 10000 && m_cycleList.Size() == m_twoCaps.size &&
        m_cycleList.Total() == m_twoCaps.total;
}

bool WorkerImplement::UndoTwoCaps() {
    auto last = std::exchange(m_twoCaps, {});
    if (last.word.empty() || GetTickCount64() - last.at > 10000 || m_cycleList.Size() != last.size ||
        m_cycleList.Total() != last.total)
        return false;
    LOG_ANY(L"two caps: {} back, it is an exception now", last.word);
    const int delay = (int)std::min<uint32_t>(conf_get_unsafe()->retype_delay_ms, 100);
    const auto stop = CaretStop();
    InputSender::SendVkKeyPaced(VK_BACK, (int)last.fixed.size() + 1, delay, stop);
    Sleep(c_afterErase);
    InputSender::SendTextPaced(last.typed + L" ", delay, stop);
    if (stop()) return true; // курсор переехал: бросили, не в счёт
    last.key->is_shift = !last.upper;
    PostMessageW(g_guiHandle, WM_TwoCapsLearn, 0, (LPARAM)new std::wstring(last.word));
    return true;
}

TStatus WorkerImplement::GetClipStringCallback() {
    LOG_ANY(L"GetClipStringCallback");
    // Отдельное сообщение, позже сочетания: его придержка (если оно было набрано под ней) уже отпущена, и по её номеру
    // CaretStop остановил бы вставку - щелчок и смена окна считаются с этой минуты (KeyHold::caretMoves).
    m_holdId = 0;
    m_caretBase = KeyHold::caretMoves;

    auto data = m_clipWorker.getCurString();

    bool pasted = false;
    if (data.empty()) {
        LOG_ANY(L"data empty");
    } else {
        if (m_lastRevertRequest == hk_RevertLine && data.find_first_of(L"\r\n") != std::wstring::npos) {
            // Shift+Home перевода строки не выделяет. Он в буфере - значит, выделять было нечего (курсор
            // в начале строки), а программа по Ctrl+C без выделения скопировала всю строку (VS Code и др.).
            LOG_ANY(L"line: copied text has a line break, nothing was selected. skip");
        } else if (Utils::is_in(m_lastRevertRequest, hk_RevertSelelected, hk_RevertLine)) {
            // Выделенное переводится в другую раскладку в памяти и вставляется одним Ctrl+V. Раньше оно
            // перепечатывалось клавишами: Блокнот Windows 11 терял первую из них ("эта" -> "та"), а раскладка
            // определялась по каждому символу отдельно ("комбинация" -> "ком,инация"). См. LayoutConvert.h.
            GETCONF;
            std::vector<HKL> layouts{ std::from_range, cfg->layouts_info.EnabledLayouts() };
            HKL from = LayoutConvert::Source(data, layouts, CurLay());
            HKL to = cfg->layouts_info.NextEnabledLayout(from);
            // Слова другой раскладки среди текста ("Ыещз" в «NTgthm» и «Ыещз») - по словарю, каждое отдельно; больше
            // 1000 таких слов не проверяем (выделили полдокумента - не держать буфер обмена).
            int checks = 0;
            auto next = [&cfg](HKL lay) { return cfg->layouts_info.NextEnabledLayout(lay); };
            auto wrong = [&checks](const std::wstring& word, HKL lay, const std::wstring& conv, HKL to) {
                if (++checks > 1000) return false;
                if (!SpellCheck::WrongLayout(word, Utils::GetNameForHKL_simple(lay), conv, Utils::GetNameForHKL_simple(to)))
                    return false;
                LOG_ANY(L"convert: {} -> {} by the dictionary", word, conv);
                return true;
            };
            auto converted = (to == 0 || to == from) ? data : LayoutConvert::ConvertWords(data, from, to, layouts, next, wrong);
            if (to == 0 || to == from) {
                LOG_WARN(L"no layout to convert {} to", (void*)from);
            } else if (converted == data) {
                // Ни одной буквы (цифры, знаки; Excel без выделения копирует всю ячейку): текст тот же,
                // и раскладку не меняем - иначе она переключится "сама".
                LOG_ANY(L"convert: nothing changes. skip");
            } else {
                if (cfg->two_caps) converted = TwoCaps::FixText(converted, TwoCapsExceptions()); // "LDe[" -> "Двух"
                LOG_ANY(L"convert selected {} -> {}, {} chars", (void*)from, (void*)to, converted.size());
                m_cycleList.Clear();
                TextFixed();
                RequestWaitClip(CLRMY_hk_INSERT);
                m_clipWorker.setString(converted);
                IFS_LOG(ProcessRevert({ .lay = to, .flags = SW_CLIENT_SetLang | SW_CLIENT_NO_WAIT_LANG | SW_CLIENT_CTRLV }));
                AutoLayoutIsOurs();
                pasted = true;
            }
        } else if (data.length() > 100) {
            LOG_ANY(L"TOO MANY TO REVERT. SKIP");
        } else if (m_lastRevertRequest == hk_toUpperSelected || m_lastRevertRequest == hk_InvertCaseSelected) {
            if (m_lastRevertRequest == hk_toUpperSelected)
                toUpper(data);
            else
                InvertCase(data);

            TextFixed();
            RequestWaitClip(CLRMY_hk_INSERT);
            m_clipWorker.setString(data);

            IFS_LOG(ProcessRevert({.flags = SW_CLIENT_CTRLV}));
            pasted = true;
        }
    }

    // Старое содержимое буфера - на место. После Ctrl+V - не сразу: программа читает буфер, когда
    // обработает нажатие, и слишком раннее восстановление вставило бы старое содержимое.
    auto restore = [this] {
        if (m_clipWorker.HasBackup()) {
            RequestWaitClip(CLRMY_hk_RESTORE);  // делаем это только чтобы не вызывалась очистка формата
            m_clipWorker.Restore();
        }
        // С буфером закончили. Сигнал "буфер занят" держим ещё 300 мс: FluentClipper читает буфер через
        // 80 мс после последнего изменения, и восстановленное содержимое тоже не должно попасть в историю.
        Worker()->PostMsg([this, busy = m_clipWorker.busy.Current()](auto) { m_clipWorker.busy.Reset(busy); }, 300);
    };
    if (pasted) {
        Worker()->PostMsg([restore](auto) { restore(); }, 250);
    } else {
        restore();
    }

    RETURN_SUCCESS;
}

void WorkerImplement::CliboardChanged() {
    LOG_ANY(L"ClipboardChangedInt");

    if (!g_enabled.IsEnabled()) {
        LOG_ANY(L"skip because disabled");
    }

    auto dwTime = GetTickCount64() - m_dwLastCtrlCReqvest;
    bool isRecent = dwTime <= 500;

    EClipRequest request = m_clipRequest;
    m_clipRequest = CLRMY_NONE;

    if (request != CLRMY_NONE) {
        if (!isRecent) {
            LOG_WARN(L"Request not recent");
        } else {
            if (request == CLRMY_GET_FROM_CLIP) {
                Worker()->PostMsg([](WorkerImplement* w) { w->GetClipStringCallback(); }, 20);
                return;
            }

            if (request == CLRMY_hk_COPY) {
                return;
            }

            return;
        }
    }

    // --- This is user request ----
    // "Убирать оформление при каждом копировании" (fClipboardClearFormat) FluentSwitcher не делает: это дело
    // менеджера буфера, а FluentClipper как раз хранит оформление.

    LOG_ANY(L"ClipboardChangedInt complete");
}

void WorkerImplement::ChangeForeground(HWND hwnd) {
    LOG_ANY(L"Now foreground hwnd={}", (void*)hwnd);
    m_autoWord = {}; // другое окно: своя раскладка, свой курсор
    m_autoSwitched = {};
    m_twoCaps = {};
    WarmUpEarly();
    DWORD procId = 0;
    DWORD threadid = GetWindowThreadProcessId(hwnd, &procId);
    if (threadid != m_dwIdThreadForeground && procId != m_dwIdProcoreground) {
        // IFS_LOG(Utils::GetProcLowerNameByPid(procId, m_sTopProcPath, m_sTopProcName));
        // LOG_INFO_2(L"threadid=%d, procId=%d, sname=%s", threadid, procId, m_sTopProcName.c_str());
        ClearAllWords();
    } else {
        LOG_ANY(L"skip clear words");
    }
    m_dwIdThreadForeground = threadid;
    m_dwIdProcoreground = procId;
}

namespace {
void ProcessSnippet(const string& s) {
    if (s.empty())
        return;
    auto parts = ParseSnippet(s);
    InputSender is;
    for (const auto& it : parts) {
        // VK_CODE
        if (it.inBrackets) {
            for (const auto& hk_string : StrUtils::Split(it.text, ',')) {
                auto hk = CHotKey::FromString(hk_string);
                is.AddPressVk(hk);
            }
            continue;
        }

        // UNICODE
        auto str = StrUtils::Convert(it.text);
        for (auto c : str) {
            is.AddUnicodePress(c);
        }
    }
    is.Send();
}
}  // namespace

TStatus WorkerImplement::RunProcess(HotKeyType hk, bool after_wait) {
    GETCONF;

    int i = hk;
    ResetFlag(i, hk_RunProgram_flag);
    if (i >= cfg->run_programs.size()) {
        return SW_ERR_UNKNOWN;
    }
    const auto& it = cfg->run_programs[i];

    if (!it.enabled) {
        RETURN_SUCCESS;
    }

    if (it.delay > 0 && !after_wait) {
        Worker()->PostMsg([hk](auto p) { p->RunProcess(hk, true); }, it.delay);
        RETURN_SUCCESS;
    }

    if (it.type == CommandType::Snippet) {
        ProcessSnippet(it.cmd);
        RETURN_SUCCESS;
    }

    auto wpath = StrUtils::Convert(it.cmd);
    auto wargs = StrUtils::Convert(it.args);

    PathUtils::NormalizeDelims(wpath);

    LOG_ANY(L"run program {} {}", wpath.c_str(), wargs.c_str());

    procstart::CreateProcessParm parm;
    parm.sExe = wpath.c_str();
    parm.sCmd = wargs.c_str();

    // todo - use proxy process for unelevated.
    // parm.admin = it.elevated ? TSWAdmin::SW_ADMIN_ON : TSWAdmin::SW_ADMIN_OFF;

    parm.mode = procstart::SW_CREATEPROC_SHELLEXE;
    CAutoHandle hProc;
    IFS_RET(procstart::SwCreateProcess(parm, hProc));

    RETURN_SUCCESS;
}

void WorkerImplement::ProcessOurHotKey(Message_Hotkey&& keyData) {
    // Нажато среди придержанных: отпустить их, когда сочетание сделано (или пропущено).
    struct Release {
        unsigned id;
        ~Release() {
            if (id) KeyHold::RequestRelease(id);
        }
    } release{ keyData.holdId };
    m_holdId = keyData.holdId;
    m_caretBase = KeyHold::caretMoves;
    // Движок будет печатать: таймаут придержки подождёт его (до 15 с), как при автопереключении. Не вышло - её уже
    // отпустили, сочетание делается без неё, как раньше.
    if (keyData.holdId && !KeyHold::Claim(keyData.holdId)) LOG_ANY("hotkey hold {} already let go", keyData.holdId);
    KeyHold::Claimed claimed{ keyData.holdId };
    auto hk = keyData.hk;
    const auto& key = keyData.hotkey;

    if (keyData.delayed_from != 0 && keyData.delayed_from <= m_lastHotKeyTime) {
        LOG_ANY("skip hotkey {} possible was double press", key.ToString());
        return;
    }
    m_lastHotKeyTime = GetTickCount64();

    GETCONF;

    m_clear_alfter_selected = hk == hk_RevertSelelected;

    if (IsNeedSavedWords(hk) && !m_cycleList.HasAnySymbol()) {
        bool found = false;
        for (const auto& [hk2, key2] : cfg->All_hot_keys()) {
            if (!IsNeedSavedWords(hk2) && key.Compare(key2)) {
                // Есть точно такой же хот-кей, не требующий сохраненных слов, используем его.
                hk = hk2;
                found = true;
                break;
            }
        }
        if (!found) {
            LOG_ANY(L"skip hotkey {} because no saved word", (int)hk);
            return;
        }
    }

    if (key.GetKeyup()) {
        // дадим событию up время на обработку в ОС, так как для нас, она уже поднята
        // (т.е. мы ее не поднимаем и она может конфликтовать с тем, что мы собираемся ввести)
        LOG_ANY(L"pause for #up key");
        Sleep(5);
    }

    // "Следующая раскладка" одним модификатором (Shift, как в Punto) набранное не забывает: это может быть
    // первое нажатие "Shift дважды", которое исправляет слово, набранное до него.
    const bool singleSwitch = hk == hk_CycleSwitchLayout && key.OnlyMods() && key.GetKeyup();
    if (!IsNeedSavedWords(hk) && !Utils::is_in(hk, hk_EmulateCapsLock, hk_EmulateScrollLock) && !singleSwitch) {
        ClearAllWords();
    }

    m_lastRevertRequest = hk;

    LOG_ANY("Hotkey start {}({})", HotKeyTypeName(hk), (int)hk);

    if (!g_enabled.IsEnabled() && hk != hk_ToggleEnabled) {
        LOG_ANY("Skip hk because disabled");
        return;
    }

    Fix_AltOrWin(keyData.cur_keys_down);

    if (hk == hk_ToggleEnabled) {
        try_toggle_enable();
        return;
    }

    if (hk == hk_ShowMainWindow) {
        show_main_wind();
        return;
    }

    if (TestFlag(hk, hk_RunProgram_flag)) {
        IFS_LOG(RunProcess(hk));
        return;
    }

    // Если нам нужно самим что-то вводить, то сбросим сразу все клавиши для системы.
    // Будет двойной (или даже тройной и более) up, но пока что это не проблема...
    UpAllKeys(keyData.cur_keys_down);

    if (hk == hk_Fix_RAlt) {
        FixCtrlAlt(keyData.hotkey);
        return;
    }

    if (Utils::is_in(hk, hk_EmulateCapsLock, hk_EmulateScrollLock)) {
        TKeyCode k = (hk == hk_EmulateCapsLock) ? VK_CAPITAL : VK_SCROLL;
        InputSender::SendVkKey(k);
        return;
    }

    auto process = [this, hk, cfg, &key]() -> TStatus {
        if (hk == hk_InsertWithoutFormat) {
            IFS_RET(m_clipWorker.ClipboardClearFormat());
            CHotKey ctrlv(VK_CONTROL, VKE_V);
            InputSender::SendWithPause(ctrlv);
            RETURN_SUCCESS;
        }

        // CHANGE LAYOUT WITHOUT REVERT

        IFS_RET(AnalizeTopWnd());

        // Первое нажатие уже переключило раскладку (LShift при отпускании), а это было "Shift дважды":
        // раскладку назад, дальше как будто одного нажатия не было.
        const auto single = std::exchange(m_singleSwitch, {});
        if (key.IsDouble() && single.lay && GetTickCount64() - single.time < 1000 &&
            key.Compare(single.key, CHotKey::COMPARE_IGNORE_KEYUP | CHotKey::COMPARE_IGNORE_DOUBLE)) {
            if (hk == hk_RevertLastWord && !TwoCapsUndoReady()) {
                // Перевод слова сам поставит раскладку, и она та же, что уже дал одиночный Shift: не переключать туда
                // и обратно (новый Блокнот теряет символы, когда раскладка меняется несколько раз подряд), а только
                // считать от прежней - ProcessRevert увидит, что нужная уже стоит.
                LOG_ANY("double {} after the single press: counted from {:x}, no switch back", key.ToString(),
                        (ULONGLONG)single.lay);
                topWndInfo2.lay = single.lay;
            }
            else if (CurLay() != single.lay) {
                LOG_ANY("double {} after the single press: layout back to {:x}", key.ToString(), (ULONGLONG)single.lay);
                IFS_RET(ProcessRevert({.lay = single.lay, .flags = SW_CLIENT_SetLang}));
                // CurLay() - запомненная раскладка, сама она обновится только через 100 мс (TimerCheckLay);
                // исправление ниже должно считать от возвращённой, иначе напечатает слово как было.
                topWndInfo2.lay = single.lay;
            }
        }

        // Сразу после исправления ДВух ЗАглавных "Исправить последнее слово" возвращает слово (уже после того, как
        // возвращена раскладка одиночного Shift выше).
        if (hk == hk_RevertLastWord && UndoTwoCaps()) {
            RETURN_SUCCESS;
        }

        if (hk == hk_CycleSwitchLayout) {
            const HKL before = CurLay();
            IFS_RET(ProcessRevert({.lay = getNextLang(), .flags = SW_CLIENT_SetLang}));
            if (key.OnlyMods() && key.GetKeyup())
                m_singleSwitch = { GetTickCount64(), before, key };
            RETURN_SUCCESS;
        }

        if (TestFlag(hk, hk_SetLayout_flag)) {
            int i = hk;
            ResetFlag(i, hk_SetLayout_flag);

            auto info = cfg->layouts_info.GetLayoutIndex(i);
            if (info == nullptr) {
                LOG_WARN(L"not found hot key for set layout");
                RETURN_SUCCESS;
            }

            IFS_RET(ProcessRevert({.lay = info->layout, .flags = SW_CLIENT_SetLang | SW_CLIENT_NO_WAIT_LANG}));

            RETURN_SUCCESS;
        }

        // REVERT AND CHANGE LAYOUT

        if (Utils::is_in(hk, hk_RevertSelelected, hk_toUpperSelected, hk_InvertCaseSelected, hk_RevertLine)) {
            // "Буфер занят" - до конца восстановления (GetClipStringCallback). Если буфер так и не
            // изменится (ничего не выделено), сигнал снимется сам через 3 с.
            int busy = m_clipWorker.busy.Set();
            Worker()->PostMsg([this, busy](auto) { m_clipWorker.busy.Reset(busy); }, 3000);
            // m_savedClipData = m_clipWorker.getCurString();
            RequestWaitClip(CLRMY_GET_FROM_CLIP);  // регистрируем запрос.
            LOG_ANY(L"save buff");
            m_clipWorker.BackupCurrent();
            if (m_clipWorker.HasBackup()) {
                // пулим очистку памяти на всякий случай
                Worker()->PostMsg(
                    [this](auto w) {
                        if ((GetTickCount64() - m_dwLastCtrlCReqvest) > 7000) {
                            m_clipWorker.ClearBackup();
                        }
                    },
                    10000);
            }
            if (hk == hk_RevertLine) {
                // Выделить от курсора до начала строки; дальше - как выделенный текст.
                InputSender::SendWithPause(CHotKey(VK_LSHIFT, VK_HOME));
            }
            IFS_RET(ProcessRevert({.flags = SW_CLIENT_CTRLC}));
            RETURN_SUCCESS;
        }

        // ---------------classic revert---------------

        if (!Utils::is_in(hk, hk_RevertLastWord, hk_RevertSeveralWords, hk_RevertAllRecentText)) {
            IFS_RET(SW_ERR_UNKNOWN, L"Unknown typerevert {}", (int)hk);
        }

        if (hk == hk_RevertLastWord) {
            AutoUndo undo;
            if (!CountAutoSwitchUndo(&undo)) {
                // Исправление вручную: что исправляют - до перепечатки, счёт и журнал - после неё.
                const auto fix = TakeHandFix();
                RevertText(hk);
                if (fix) HandFixDone(*fix);
                RETURN_SUCCESS;
            }
            if (undo.tail && undo.from) {
                // Вернуть ровно то, что переводили (а не "последнее слово" буфера - оно может делиться иначе).
                const size_t begin = m_cycleList.Size() - undo.tail;
                if (undo.all)
                    SwitchTail(begin, undo.from, true);
                else {
                    // "а можно" - "f можно": короткие слова - назад, слово остаётся. Раскладка - та, в которую
                    // переключили: одиночный Shift из "Shift дважды" мог её уже сменить.
                    CheckCurLay();
                    if (undo.to && CurLay() != undo.to) IFS_LOG(ProcessRevert({ .lay = undo.to, .flags = SW_CLIENT_SetLang }));
                    RetypeTail(begin, begin + undo.retro, undo.from, undo.to);
                    AutoLayoutIsOurs();
                }
                RETURN_SUCCESS;
            }
        }
        RevertText(hk);

        RETURN_SUCCESS;
    };

    IFS_LOG(process());
}

TStatus WorkerImplement::ProcessRevert(ContextRevert&& ctxRevert) {
    
    bool fDels = false;

    // Раскладка, в которой будет набран текст: новая, если её меняем, иначе текущая.
    HKL target = CurLay();
    if (TestFlag(ctxRevert.flags, SW_CLIENT_SetLang) && ctxRevert.lay) {
        auto prevLay = CurLay();
        const HKL want = ctxRevert.lay != (HKL)HKL_NEXT ? ctxRevert.lay
            : conf_get_unsafe()->layouts_info.NextEnabledLayout(prevLay);
        if (want && want != prevLay && GetKeyboardLayout(topWndInfo2.threadid_default) == want) {
            // Нужная раскладка уже стоит (её только что переключил одиночный Shift): второй раз не переключаем.
            LOG_ANY(L"layout {} is already there, no switch", (void*)want);
            target = want;
            topWndInfo2.lay = want;
        }
        else {
            // Сразу нужную, а не "следующую": если прошлое переключение ещё не дошло, два "следующих" вернули бы назад.
            SetNewLay(want ? want : ctxRevert.lay);
            HKL got = WaitOtherLay(prevLay, 15, 40);
            target = want;
            if (target == 0) target = got;
        }
    }

    const int delay = (int)std::min<uint32_t>(conf_get_unsafe()->retype_delay_ms, 100);
    // Щелчок или другое окно, пока печатаем: бросить - остальное ушло бы в другое место.
    const auto stop = CaretStop();
    if (TestFlag(ctxRevert.flags, SW_CLIENT_PUTTEXT) && TestFlag(ctxRevert.flags, SW_CLIENT_BACKSPACE)) {
        InputSender::SendVkKeyPaced(VK_BACK, ctxRevert.keylist.size(), delay, stop);
        Sleep(c_afterErase); // новый Блокнот теряет первую букву, если она приходит сразу за стиранием
    }
    if (stop()) {
        LOG_ANY(L"revert stopped: the caret moved");
        RETURN_SUCCESS;
    }

    if (TestFlag(ctxRevert.flags, SW_CLIENT_PUTTEXT) && target != 0 && conf_get_unsafe()->two_caps && !m_is_last_caps) {
        FixTwoCapsInKeys(ctxRevert.keylist, target);
    }

    if (TestFlag(ctxRevert.flags, SW_CLIENT_PUTTEXT)) {
        if (conf_get_unsafe()->retype_keys || target == 0) {
            InputSender::SendKeys(ctxRevert.keylist, m_is_last_caps);
        } else {
            InputSender::SendKeysAsText(ctxRevert.keylist, target, m_is_last_caps, delay, stop);
        }
    }

    if (TestFlag(ctxRevert.flags, SW_CLIENT_CTRLC)) {
        LOG_ANY(L"Send ctrlc...");
        CHotKey ctrlc(VK_CONTROL, VKE_C);
        InputSender::SendWithPause(ctrlc);
    }

    if (TestFlag(ctxRevert.flags, SW_CLIENT_CTRLV)) {
        CHotKey ctrlc(VK_CONTROL, VKE_V);
        InputSender::SendWithPause(ctrlc);
    }

    LOG_ANY(L"Revert complete");

    RETURN_SUCCESS;
}

HKL WorkerImplement::getNextLang() {
    HKL result = (HKL)HKL_NEXT;

    GETCONF;

    // если все enabled - то обычная циклическая смена
    if (cfg->layouts_info.AllLayoutEnabled()) {
        return result;
    }

    auto lay = CurLay();
    if (lay == 0) {
        LOG_WARN(L"___ NOT FOUND CUR LAY");
        return result;
    }

    lay = cfg->layouts_info.NextEnabledLayout(lay);
    if (lay == 0) {
        LOG_WARN(L"___ NOT FOUND NEXT LAY");
    }

    return lay;
};

void WorkerImplement::SwitchLangByEmulate(HKL lay) {
    GETCONF;

    CHotKey altshift = cfg->win_hotkey_cycle_lang;
    bool switch_until = false;

    if ((size_t)lay != HKL_NEXT) {
        auto info = cfg->layouts_info.GetLayoutInfo(lay);
        if (info != nullptr && !info->win_hotkey.IsEmpty()) {
            altshift = info->win_hotkey;
        } else {
            if (topWndInfo2.lay == lay) {  // на верх пока не выносим проверку, так как нет 100% гарантии в корректности
                                           // определения тек. раскладки, пока тестим.
                return;
            }
            if (cfg->layouts_info.info.size() != 2)
                switch_until = true;
        }
    }

    if (altshift.IsEmpty()) {
        LOG_WARN(L"hot key not setup. skip switch");
        return;
    }

    LOG_ANY("Emulate with {}", altshift.ToString());

    InputSender::SendHotKey(altshift);

    if (switch_until) {
        auto cur = topWndInfo2.lay;
        for (int i = 0; i < std::ssize(cfg->layouts_info.info) - 2; i++) {
            auto next = WaitOtherLay(cur, 0);
            if (next == 0) {
                LOG_WARN(L"cant't wait in emulate");
                return;
            }
            if (next == lay) {
                LOG_ANY("ok. found lay");
                return;
            }
            LOG_ANY("skip lay={}", (void*)next);
            cur = next;
            InputSender::SendHotKey(altshift);
        }
        LOG_WARN("not found needed lay!");
    }
}

TStatus WorkerImplement::AnalizeTopWnd() {
    CheckCurLay();

    IFS_LOG(Utils::GetProcLowerNameByPid(topWndInfo2.pid_top, m_sTopProcPath, m_sTopProcName));

    LOG_ANY(L"AnalizeTopWnd pid_top={}, pid_default={}, lay={:x} prg={}", topWndInfo2.pid_top, topWndInfo2.pid_default,
            (ULONGLONG)topWndInfo2.lay, m_sTopProcName);

    RETURN_SUCCESS;
}

TStatus WorkerImplement::FixCtrlAlt(CHotKey key) {
    IFS_RET(AnalizeTopWnd());

    GETCONF;

    auto lay = cfg->fixRAlt_lay_;
    auto curLay = CurLay();

    HKL temp = 0;
    bool just_send = false;

    if (cfg->layouts_info.GetLayoutInfo(lay) == nullptr) {
        auto str = std::format(L"{:x}", (size_t)lay);
        // const TChar* s = L"00000409";
        LOG_ANY(L"load temp layout {}", str);
        temp = LoadKeyboardLayout(str.c_str(), KLF_ACTIVATE);
        IFW_LOG(temp != NULL);

        if (temp == 0) {
            // не получится...
            just_send = true;
        } else {
            Utils::SetLayPost(topWndInfo2.hwnd_default, temp);
        }

    } else {
        SetNewLay(lay);
    }

    WaitOtherLay(curLay);

    // отправляем
    InputSender::SendHotKey(key);

    if (!just_send) {
        // переключаемся обратно.
        Sleep(200);
        SetNewLay(curLay);
    }

    if (temp != 0) {
        LOG_ANY(L"unload temp layout");
        IFW_LOG(UnloadKeyboardLayout(temp));
    }

    RETURN_SUCCESS;
}
