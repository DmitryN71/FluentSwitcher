#include "WorkerImplement.h"
#include "ParseSnippet.h"
#include "LayoutConvert.h"

void WorkerImplement::ProcessKeyMsg(const Message_KeyType& keyData) {
    if (keyData.held_end) {
        // Хук придержал Enter / Tab после слова: исправить и отпустить. Сама клавиша придёт потом, как обычный набор.
        struct Release { ~Release() { KeyHold::RequestRelease(); } } release;
        FixTwoCaps(false);
        return;
    }
    TKeyCode vkCode = keyData.vkCode;
    auto scan_ext = keyData.scan_ext;

    const auto& cur_hotkey = keyData.cur_hotKey;

    LOG_ANY("ProcessKeyMsg {} curState={}", CHotKey::ToString(vkCode), cur_hotkey.ToString());

    m_is_last_caps =
        keyData.is_caps;  // сохраним последнее известное значение. Нажатие caps по идее должно нам привести сюда.

    if (CHotKey::IsKnownMods(vkCode)) {
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
            break;
        }
        default: {
            bool is_shift = cur_hotkey.HasMod(VK_SHIFT);

            // todo: так это не работает. Нужно добавлять Shift только буквам (сложно) или временно включать/отключать
            // capslock.
            // if (keyData.is_caps) is_shift = !is_shift;

            m_cycleList.AddKeyToList(key);
            break;
        }
        case KEYTYPE_COMMAND_NO_CLEAR:
            break;
        case KEYTYPE_NONE:
        case KEYTYPE_COMMAND_CLEAR: {
            ClearAllWords();
            break;
        }
    }

    if (keyData.hold) {
        // Хук придерживает нажатия после этого пробела: решить и отпустить, что бы ни случилось.
        struct Release { ~Release() { KeyHold::RequestRelease(); } } release;
        FixTwoCaps();
    }
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
}

void WorkerImplement::FixTwoCaps(bool afterSpace) {
    GETCONF;
    if (!cfg->two_caps || !KeyHold::fixAllowed || cfg->IsSkipProgramTop() || IsPasswordFocus()) return;
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
    const auto fix = TwoCaps::Analyze(text, TwoCapsExceptions());
    if (fix.tail.empty()) return;
    // Слово, набранное в чужой раскладке ("GJgsnrf" - не английское, "попытка" - русское): правило его не трогает, его
    // исправит перевод раскладки, и сразу с заглавными ("Попытка"). Словари - Windows (WinDictionary.h); нет словаря -
    // как раньше.
    if (SpellCheck::Check(fix.word, Utils::GetNameForHKL_simple(lay)) == SpellCheck::Result::NotWord) {
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
    LOG_ANY(L"two caps: {} -> {}{}", text, text.substr(0, fix.from), fix.tail);
    TextFixed();
    const int delay = (int)std::min<uint32_t>(cfg->retype_delay_ms, 100);
    const std::wstring space = afterSpace ? L" " : L"";
    InputSender::SendVkKeyPaced(VK_BACK, (int)(typed.size() + space.size()), delay); // со второй буквы (и пробел)
    Sleep(c_afterErase); // новый Блокнот теряет первую букву, если она приходит сразу за стиранием
    InputSender::SendTextPaced(fix.tail + space, delay);
    keys[fix.from]->is_shift = false; // и в буфере слов вторая буква теперь строчная
    // Отмена - только после пробела: после Enter сообщение уже ушло, после Tab курсор может быть в другом поле.
    if (afterSpace)
        m_twoCaps = { fix.word, typed, fix.tail, GetTickCount64(), m_cycleList.Size(), keys[fix.from] };
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
    return !m_twoCaps.word.empty() && GetTickCount64() - m_twoCaps.at <= 10000 && m_cycleList.Size() == m_twoCaps.size;
}

bool WorkerImplement::UndoTwoCaps() {
    auto last = std::exchange(m_twoCaps, {});
    if (last.word.empty() || GetTickCount64() - last.at > 10000 || m_cycleList.Size() != last.size) return false;
    LOG_ANY(L"two caps: {} back, it is an exception now", last.word);
    const int delay = (int)std::min<uint32_t>(conf_get_unsafe()->retype_delay_ms, 100);
    InputSender::SendVkKeyPaced(VK_BACK, (int)last.fixed.size() + 1, delay);
    Sleep(c_afterErase);
    InputSender::SendTextPaced(last.typed + L" ", delay);
    last.key->is_shift = true;
    PostMessageW(g_guiHandle, WM_TwoCapsLearn, 0, (LPARAM)new std::wstring(last.word));
    return true;
}

TStatus WorkerImplement::GetClipStringCallback() {
    LOG_ANY(L"GetClipStringCallback");

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
    if (TestFlag(ctxRevert.flags, SW_CLIENT_PUTTEXT) && TestFlag(ctxRevert.flags, SW_CLIENT_BACKSPACE)) {
        InputSender::SendVkKeyPaced(VK_BACK, ctxRevert.keylist.size(), delay);
        Sleep(c_afterErase); // новый Блокнот теряет первую букву, если она приходит сразу за стиранием
    }

    if (TestFlag(ctxRevert.flags, SW_CLIENT_PUTTEXT) && target != 0 && conf_get_unsafe()->two_caps && !m_is_last_caps) {
        FixTwoCapsInKeys(ctxRevert.keylist, target);
    }

    if (TestFlag(ctxRevert.flags, SW_CLIENT_PUTTEXT)) {
        if (conf_get_unsafe()->retype_keys || target == 0) {
            InputSender::SendKeys(ctxRevert.keylist, m_is_last_caps);
        } else {
            InputSender::SendKeysAsText(ctxRevert.keylist, target, m_is_last_caps, delay);
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
