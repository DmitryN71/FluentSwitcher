#pragma once

class WorkerImplement {
    enum {
        SW_CLIENT_PUTTEXT = 1 << 0,
        SW_CLIENT_BACKSPACE = 1 << 1,
        SW_CLIENT_SetLang = 1 << 2,
        SW_CLIENT_CTRLC = 1 << 3,
        SW_CLIENT_CTRLV = 1 << 4,
        SW_CLIENT_NO_WAIT_LANG = 1 << 5,
    };

    struct ContextRevert {
        TKeyRevert keylist;
        HKL lay = 0;
        uint32_t flags = 0;
    };

    typedef std::vector<CHotKey> TKeyToRevert;

   public:
    WorkerImplement() {
        // IFS_LOG(PathUtils::GetPath_fileExe_lower(m_sSelfExeName));
        TimerClear();
        TimerCheckLay();
    }

    void TimerClear() {
        m_cycleList.ClearByTimer();
        Worker()->PostMsg([](auto p) { p->TimerClear(); }, 30000);
    }

    void TimerCheckLay() {
        CheckCurLay();
        Worker()->PostMsg([](auto p) { p->TimerCheckLay(); }, 100); // и звук переключения - без заметной задержки
    }

    void ClearAllWords() { m_cycleList.Clear(); }
    // Следующая раскладка у окна, которое сейчас впереди (щелчок по флагу у часов, TrayIcon.h).
    void SwitchToNextLayout() {
        IFS_LOG(AnalizeTopWnd());
        IFS_LOG(ProcessRevert({ .lay = getNextLang(), .flags = SW_CLIENT_SetLang }));
    }
    TStatus AnalizeTopWnd();
    void SwitchLangByEmulate(HKL lay);
    void CliboardChanged();
    TStatus GetClipStringCallback();
    void ClipboardClearFormat2() { IFS_LOG(m_clipWorker.ClipboardClearFormat()); }
    void ClipboardToSendData(const std::wstring& clipdata) {
        m_cycleList.Clear();

        HKL layouts[10];
        int count = GetKeyboardLayoutList(std::ssize(layouts), layouts);
        if (count == 0) {
            IFW_LOG(false);
            return;
        }

        for (auto c : clipdata) {
            if (c == L'\r')
                continue;
            auto lay = CurLay();
            SHORT res = VkKeyScanEx(c, lay);
            if (res == -1) {
                for (int i = 0; i < count; ++i) {
                    if (layouts[i] != lay)
                        res = VkKeyScanEx(c, layouts[i]);
                    if (res != -1)
                        break;
                }
            }
            if (res == -1) {
                IFS_LOG(SW_ERR_UNKNOWN, L"Cant scan char %c", c);
                continue;
            }
            BYTE mods = HIBYTE(res);
            BYTE vk_code = LOBYTE(res);

            TKeyType type = KEYTYPE_LETTER;
            // Пока сделаем супер-простое разделение
            if (StrUtils::IsSpace(c))
                type = KEYTYPE_SPACE;

            TKeyBaseInfo key{.vk_code = vk_code,
                             .scan_code = {},
                             .is_shift = TestFlag(mods, 0x1),
                             .is_caps = m_is_last_caps,
                             .type = type};
            m_cycleList.AddKeyToList(key);
        }

        RevertText(hk_RevertAllRecentText, true, true);
        if (m_clear_alfter_selected) {
            m_cycleList.Clear();
        }
    }

    void RevertText(HotKeyType typeRevert, bool no_backs = false, bool always_full_text = false) {
        auto nextLng = getNextLang();
        auto to_revert = m_cycleList.FillKeyToRevert(typeRevert, always_full_text);
        if (to_revert.keys.empty()) {
            LOG_ANY(L"nothing to revert. skip");
            return;
        }
        m_cycleList.SetSeparateLast();
        TextFixed();
        bool isNeedLangChange = to_revert.needLanguageChange;
        ContextRevert data;
        data.keylist = std::move(to_revert.keys);
        data.flags = SW_CLIENT_PUTTEXT | SW_CLIENT_SetLang | (no_backs ? 0 : SW_CLIENT_BACKSPACE);
        data.lay = isNeedLangChange ? nextLng : 0;
        IFS_LOG(ProcessRevert(std::move(data)));
        AutoLayoutIsOurs();
    }

    void ChangeForeground(HWND hwnd);
    void ProcessKeyMsg(const Message_KeyType& keyData);

    void RequestWaitClip(EClipRequest clRequest) {
        m_clipRequest = clRequest;
        // m_clipCounter = GetClipboardSequenceNumber();
        m_dwLastCtrlCReqvest = GetTickCount64();
    }
    HKL getNextLang();

    void CheckCurLay() {
        auto old_lay = topWndInfo2.lay;

        topWndInfo2 = Utils::GetFocusedWndInfo();

        if (topWndInfo2.lay == 0) {
            // оставим прежний
            topWndInfo2.lay = old_lay;
        } else {
            if (old_lay != topWndInfo2.lay) {
                // Окно - то, где раскладка менялась в прошлый раз, а не при прошлом опросе: щелчок по флагу на миг
                // делает активной панель задач, а переключение всё равно в том же окне.
                const bool otherWindow = m_layWindow && topWndInfo2.hwnd_top != m_layWindow;
                m_layWindow = topWndInfo2.hwnd_top;
                new_layout_request(topWndInfo2.lay, otherWindow);
            }
        }
    }
    HWND m_layWindow = nullptr; // окно впереди при последней смене раскладки

    // Исправление текста начинается: звук исправления, а смена раскладки из-за него - без звука переключения.
    static void TextFixed() { PostMessage(g_guiHandle, WM_TextFixed, 0, 0); }

    // ДВе ЗАглавные (TwoCaps.h): слово перед только что набранным пробелом; хук держит нажатия, пока решаем.
    // afterSpace: слово кончилось набранным пробелом (его тоже стереть и напечатать); иначе - придержанным Enter / Tab.
    void FixTwoCaps(bool afterSpace = true);
    // Исключения из настроек - для TwoCaps.h.
    static std::vector<std::wstring> TwoCapsExceptions();
    // ДВе ЗАглавные и при переводе раскладки: "LDe[" -> "ДВух" -> "Двух" - вторые буквы без Shift.
    static void FixTwoCapsInKeys(TKeyRevert& keys, HKL lay);
    // "Исправить последнее слово" сразу после такого исправления: вернуть слово и запомнить его в исключениях.
    bool UndoTwoCaps();
    // Отмена сейчас сработала бы (слово исправлено только что, дальше не печатали).
    bool TwoCapsUndoReady() const;
    struct {
        std::wstring word;       // слово, как набрано
        std::wstring typed;      // набранное со второй буквы (до исправления)
        std::wstring fixed;      // чем заменили
        ULONGLONG at = 0;
        size_t size = 0;         // набранных клавиш после исправления: другое число - уже печатали дальше
        TKeyBaseInfo* key = nullptr; // клавиша второй буквы в буфере слов
    } m_twoCaps;

    // Автопереключение (AutoSwitch.h): слово перед только что набранным пробелом (afterSpace) или придержанным
    // Enter / Tab набрано не в той раскладке - перевести, как "Исправить последнее слово". true - перевели.
    bool AutoSwitchLastWord(bool afterSpace = true);
    // Исключения автопереключения из настроек.
    static std::vector<std::wstring> AutoSwitchExceptions();
    // Последнее слово - в раскладку lay, как "Исправить последнее слово" (стереть, переключить, напечатать).
    void RevertLastWordTo(HKL lay);
    // "Исправить последнее слово" сразу после автопереключения - отмена: в счёт (на третью - в исключения).
    void CountAutoSwitchUndo();
    // Курсор переехал в том же окне (щелчок, стрелки): первое слово дальше может быть дописанной серединой.
    void CaretMoved() { m_autoWord.moved = true; }
    // Граница слова (пробел, Enter, Tab): приметы слова сначала, раскладка - как сейчас.
    void AutoWordEnd();
    // Раскладку только что сменил сам FluentSwitcher (перевод слова, выделенного): это не ручная смена.
    void AutoLayoutIsOurs() {
        CheckCurLay();
        m_autoWord.lay = CurLay();
    }
    struct {
        HKL lay = 0;            // раскладка на прошлой границе слова; 0 - не знаем (другое окно)
        bool backspace = false; // в слове стирали
        bool moved = false;     // курсор переезжал в том же окне
    } m_autoWord;
    struct {
        std::wstring word;      // как набрано (буквенная часть, строчными)
        ULONGLONG at = 0;
        size_t size = 0;        // набранных клавиш после переключения: другое число - уже печатали дальше
    } m_autoSwitched;

    TStatus FixCtrlAlt(CHotKey key);

    void SetNewLay(HKL lay) {
        LOG_ANY(L"Try set {} lay", (void*)lay);

        if (conf_get_unsafe()->AlternativeLayoutChange) {
            SwitchLangByEmulate(lay);
        } else {
            Utils::SetLayPost(topWndInfo2.hwnd_default, lay);
        }
    }
    HKL WaitOtherLay(HKL lay, ULONGLONG minWait = 15, ULONGLONG maxWait = 40) {

        auto start = GetTickCount64();        

        if(minWait > 0){
            Sleep(5);
        }
        // Дождемся смены языка. Нет смысла переходить в асинхронный режим. Можем ждать прямо здесь.

        while (true) {
            
            auto curL = GetKeyboardLayout(topWndInfo2.threadid_default);

            if (curL == 0) {
                LOG_WARN("WaitLay: cur lay == 0. continue wait");
            } else if (curL != lay) {
                auto waited = GetTickCount64() - start;
                if (waited < minWait) {
                    Sleep(minWait - waited);  // ждем оставшееся время.
                }
                LOG_ANY(L"WaitLay: new lay {} arrived after {}. totalwait={}", (void*)curL, waited, GetTickCount64() - start);
                return curL;
            }

            if (GetTickCount64() - start >= maxWait) {
                LOG_WARN(L"WaitLay: timeout language change for proc {}", m_sTopProcName.c_str());
                return 0;
            }

            Sleep(5);
        }
    }

    TStatus RunProcess(HotKeyType hk, bool after_wait = false);

    void ProcessOurHotKey(Message_Hotkey&& keyData);

    HKL CurLay() { return topWndInfo2.lay; }
    TStatus ProcessRevert(ContextRevert&& ctxRevert);

    void UpAllKeys(const vector<TKeyCode>& keys) {
        if (keys.size() == 0)
            return;

        LOG_ANY(L"UpAllKeys {}", (int)keys.size());

        InputSender inputSender;

        for (const auto& key : keys) {
            inputSender.Add(key, KEY_STATE_UP);
        }

        inputSender.Send();
    }

    void Fix_AltOrWin(const vector<TKeyCode>& keys) {
        // если хоткей вида Win + X и мы задисейблили клавишу X для системы, то произойдет появление меню
        // сделаем хак чтобы этого не было.

        if (keys.size() == 0)
            return;

        if (!std::any_of(keys.begin(), keys.end(), [](auto x) { return x == VK_LMENU || x == VK_LWIN; })) {
            return;
        }

        LOG_ANY(L"FixAltOrWin {}", (int)keys.size());
        InputSender inputSender;
        inputSender.Add(VK_CAPITAL, KEY_STATE_UP);
        inputSender.Send();
    }

   private:
    ULONGLONG m_lastHotKeyTime = 0;
    // Раскладка до "Следующей раскладки" по одному модификатору при отпускании (LShift, как в Punto):
    // если это было первое нажатие "дважды" той же клавиши, её возвращают перед исправлением.
    struct {
        ULONGLONG time = 0;
        HKL lay = 0;
        CHotKey key;
    } m_singleSwitch;
    ULONGLONG m_dwLastCtrlCReqvest = 0;
    EClipRequest m_clipRequest = CLRMY_NONE;
    DWORD m_dwIdThreadForeground = -1;
    DWORD m_dwIdProcoreground = -1;
    TopWndInfo topWndInfo2;

   public:
    std::wstring m_sTopProcName;

   private:
    std::wstring m_sTopProcPath;
    CClipWorker m_clipWorker;
    // std::wstring m_savedClipData;
    HotKeyType m_lastRevertRequest = hk_NULL;
    // std::wstring m_sSelfExeName;
    CycleRevertList m_cycleList;
    bool m_clear_alfter_selected = false;
    bool m_is_last_caps = false;
};
