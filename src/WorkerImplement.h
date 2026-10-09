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
        size_t bufferBegin = SIZE_MAX; // keylist - клавиши буфера слов с этой: исправленные заглавные - и в нём
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

    void ClearAllWords() {
        m_cycleList.Clear();
        m_autoSwitched = {}; // набранного больше нет - и отменять нечего
        m_twoCaps = {};      // (и указатель на клавишу в буфере больше не годен)
        m_autoWord.twoCapsUndone = false;
    }
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
        data.bufferBegin = m_cycleList.Size() - data.keylist.size();
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
                // делает активной панель задач, а переключение всё равно в том же окне. Окно целиком (GA_ROOT), а не
                // элемент с фокусом: Alt в новых приложениях Windows (Блокнот) переносит фокус внутри окна - и смена
                // раскладки сочетанием на Alt считалась переходом в другое окно, без звука (Дмитрий 09.10.2026).
                // Переключили мы сами в этом окне (SetNewLay, последняя секунда) - это переключение, даже если прошлая
                // смена была в другом окне (первое переключение в окне после перехода в него). В другом окне - нет:
                // туда могли уйти сразу после нашего переключения.
                const HWND root = topWndInfo2.hwnd_top ? GetAncestor(topWndInfo2.hwnd_top, GA_ROOT) : nullptr;
                const bool ours = root == m_ourSwitchRoot && GetTickCount64() - m_ourSwitchAt <= 1000;
                const bool otherWindow = m_layWindow && root != m_layWindow && !ours;
                m_layWindow = root;
                new_layout_request(topWndInfo2.lay, otherWindow);
            }
        }
    }
    HWND m_layWindow = nullptr;    // окно впереди (целиком) при последней смене раскладки
    ULONGLONG m_ourSwitchAt = 0;   // когда раскладку в последний раз переключили мы (SetNewLay)
    HWND m_ourSwitchRoot = nullptr; // ... и в каком окне (целиком)

    // Исправление текста начинается: звук исправления, а смена раскладки из-за него - без звука переключения.
    static void TextFixed() { PostMessage(g_guiHandle, WM_TextFixed, 0, 0); }

    // Слово кончилось Shift+Enter или Ctrl+Enter (held_end): модификатор ещё нажат. Перед тем как стирать и печатать -
    // отпустить его (Ctrl+Backspace стёр бы слово целиком); снова его нажимает ProcessKeyMsg. Не исправляем - не трогаем.
    std::vector<TKeyCode> m_heldMods;
    bool m_heldModsUp = false;
    void LiftHeldMods() {
        if (m_heldMods.empty() || m_heldModsUp) return;
        UpAllKeys(m_heldMods);
        m_heldModsUp = true;
    }

    // ДВе ЗАглавные и i -> I (TwoCaps.h: Analyze, LoneI) в последнем слове; хук держит нажатия, пока решаем.
    // afterSpace: слово кончилось набранным пробелом (его тоже стереть и напечатать); иначе - придержанным Enter / Tab
    // или знаком (atSign). atSign - на знаке после слова ("OLd." - "Old."): знак уже в буфере; в этой раскладке это буква
    // или цифра (ю, б) - не трогаем; знак, что бывает и внутри слова (. , ;), - только словарное слово, i - нет; "!" и ")",
    // одинаковые во всех раскладках, - граница: слово перед ним, знак стереть и напечатать, как пробел.
    void FixTwoCaps(bool afterSpace = true, bool atSign = false);
    // Набрали пробел сразу после исправления на знаке (ДВе ЗАглавные, AutoSwitchAtSign): отмена - и после него, вместе с
    // ним (иначе "Исправить последнее слово" переводило бы "Old. " в другую раскладку).
    void KeepUndoOverSpace();
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
        size_t changes = 0;      // CycleRevertList::Changes() после исправления (Size() после 90 клавиш не растёт)
        bool upper = false;      // исправление сделало букву заглавной (i - I): отмена - строчной
        std::wstring space;      // что после слова: пробел (" "), на знаке - ничего, на "!" - он, с пробелом после
                                 // него - и пробел (KeepUndoOverSpace); отмена стирает и печатает и это
    } m_twoCaps;

    // Автопереключение (AutoSwitch.h): слово перед только что набранным пробелом (afterSpace) или придержанным
    // Enter / Tab набрано не в той раскладке - перевести, как "Исправить последнее слово". true - перевели.
    bool AutoSwitchLastWord(bool afterSpace = true);
    // Посреди слова (с четвёртой буквы; хук придерживает нажатия после неё): переключить уже сейчас, если набранное -
    // не начало слова своего языка, а в другой раскладке - начало (AutoSwitch::DecideEarly). Слово продолжается.
    void AutoSwitchEarly();
    // Клавиша, которая в другой раскладке - знак после слова (. , ; : ? ! " '): "ПшеРгию" - "GitHub.", "b xnj&" - "и
    // что?". Слово проверяется сразу, как в конце (AutoSwitchLastWord), без пробела; но не когда набранное - слово или
    // начало слова своего языка ("шаб" - "шаблон"). true - переключено. (Дмитрий 06.10: "приходится делать пробел".)
    bool AutoSwitchAtSign();
    // Предсказание текста (WordStart.h) для включённых раскладок - загрузить заранее, а не на букве, пока держатся
    // нажатия.
    void WarmUpEarly();
    // Исключения автопереключения и список "Переключать всегда" из настроек.
    static std::vector<std::wstring> AutoSwitchExceptions();
    static std::vector<std::wstring> AutoSwitchForced();
    // Журнал автопереключения (log\autoswitch.log, если включён): what - что случилось, from -> to; note - в скобках
    // после (почему не переключилось само).
    static void Journal(const char* what, const std::wstring& from, const std::wstring& to, const std::string& note = {});
    // "Исправить последнее слово" вручную - слово, которое автопереключение не поймало: в журнал и в счёт "Переключать
    // всегда". TakeHandFix - что исправляют (до перепечатки: потом клавиши уже в другой раскладке), HandFixDone - счёт
    // и журнал после неё (проверка пароля через UI Automation не задерживает исправление).
    struct HandFix {
        std::wstring typed, fixed; // слово как набрано и каким станет
        std::wstring lang;         // язык раскладки, в которой набрано
        std::string why;           // почему автопереключение его не тронуло (для журнала)
        std::wstring undoneAgain;  // исправляют только что отменённое автопереключение: его слово (не в счёт отмен)
    };
    std::optional<HandFix> TakeHandFix();
    void HandFixDone(const HandFix& fix);
    // В счёт "Переключать всегда" (строчными, в нужном виде) - или пусто: не учить (HandFixDone).
    std::wstring LearnableFix(const HandFix& fix);
    struct {
        std::wstring fixed; // каким стало (строчными, только буквы)
        std::wstring typed; // как было набрано
        bool counted = false; // ушло в счёт "Переключать всегда"
        ULONGLONG at = 0;
    } m_lastHandFix; // последнее исправление вручную: исправили обратно сразу после - случайное нажатие, снять
    // Последние автопереключения (строчными, только буквы; посреди слова - его начало): исправление вручную, которое
    // возвращает такое слово, - поздняя отмена, а не слово, которое надо учить.
    std::deque<std::pair<std::wstring, std::wstring>> m_recentSwitches; // набрано, стало
    void RememberSwitch(const std::wstring& typed, const std::wstring& there);
    // "Исправить последнее слово" сразу после автопереключения - отмена: в счёт (на третью - в исключения). true - это
    // была отмена; undo - что вернуть (если переведённое ещё в буфере, иначе tail 0 - как обычно, последнее слово).
    struct AutoUndo {
        size_t tail = 0;  // клавиш с конца набранного - всё переведённое
        size_t retro = 0; // из них сначала - короткие слова перед словом
        bool all = false; // вернуть всё в раскладку from; иначе - только короткие слова (слово остаётся в to)
        HKL from = 0, to = 0; // раскладка до переключения и после
    };
    bool CountAutoSwitchUndo(AutoUndo* undo = nullptr);
    struct {
        std::wstring word; // как в m_autoSwitched.word
        ULONGLONG at = 0;
    } m_lastUndo; // последняя отмена автопереключения: исправили снова сразу после неё - не в счёт (TakeHandFix)
    // Перевести набранное с клавиши begin до конца в раскладку to (стереть, переключить, напечатать). wordEnded - слово
    // кончилось: отметка "исправлено", дальше - новое слово.
    // false - нечего или бросили посреди (щелчок): не переключено.
    bool SwitchTail(size_t begin, HKL to, bool wordEnded);
    void TwoCapsInKeys(TKeyRevert& keys, HKL to);
    // Перепечатать набранное с клавиши begin: до middle - в раскладке first, дальше - в rest; раскладку не менять.
    void RetypeTail(size_t begin, size_t middle, HKL first, HKL rest);
    // Курсор переехал (щелчок, другое окно): печатающееся исправление - бросить. Под придержкой - она отнята (щелчок и
    // смена окна отнимают её с той минуты, как она началась); без неё - с начала сообщения (KeyHold::caretMoves).
    std::function<bool()> CaretStop() const {
        return [id = m_holdId, base = m_caretBase] { return id ? !KeyHold::Allowed(id) : KeyHold::caretMoves != base; };
    }
    unsigned m_caretBase = 0; // KeyHold::caretMoves в начале сообщения
    ULONGLONG m_keyAt = 0;    // когда пришло сообщение о последней нажатой клавише (SettleBeforeErase)
    // Перед стиранием: клавиша, набранная только что (знак после слова), - ещё в пути к программе. Новый Блокнот, получив
    // Backspace через 6 мс после запятой, иногда его терял: "ЧТо," - "ЧТто," (Дмитрий 08.10.2026). Не раньше 40 мс после неё.
    void SettleBeforeErase() const {
        const ULONGLONG since = GetTickCount64() - m_keyAt;
        if (since < 40) Sleep((DWORD)(40 - since));
    }
    // Курсор переехал в том же окне (щелчок, стрелки): первое слово дальше может быть дописанной серединой.
    void CaretMoved() {
        m_autoWord.moved = true;
        m_autoWord.boundary = -1; // новое место - спросить поле заново
        if (m_autoSwitched.early) m_autoSwitched = {}; // слово, переключённое посреди, осталось позади
    }
    // Граница слова (пробел, Enter, Tab): приметы слова сначала, раскладка - как сейчас.
    void AutoWordEnd();
    // Раскладку сменили вручную после прошлой границы слова, а FluentSwitcher сам переключал слово не больше 30 с назад:
    // похоже, это исправляют его ошибку (вернули раскладку и перепечатывают) - это слово не трогать, иначе он переключил
    // бы его снова. Без недавнего переключения ручная смена - просто выбор раскладки: ошибся раскладкой - слово
    // проверяется как всякое (Дмитрий 06.10: переключил вручную не туда, набрал "ьщашш" - не переключилось).
    // И только если набирают то самое слово, как его набрали тогда (typed; partial - посреди слова, набрано начало):
    // другое слово после ручной смены - как всякое (раскладку переключили по привычке, не глядя, а программа уже
    // переключила сама, - тем более надо исправить; Дмитрий 06.10: "yt", "тщ" через раз - проверял, переключая
    // раскладку руками между словами).
    bool ByHandAfterOurs(const std::wstring& typed, bool partial) const;
    ULONGLONG m_lastAutoSwitch = 0; // когда FluentSwitcher сам переключил слово (в конце или посреди)
    std::wstring m_lastSwitchedTyped;  // то слово, как его набрали (строчными, только буквы)
    bool m_lastSwitchedEarly = false;  // переключено посреди: в m_lastSwitchedTyped - его начало
    // Раскладку только что сменил сам FluentSwitcher (перевод слова, выделенного): это не ручная смена.
    void AutoLayoutIsOurs() {
        CheckCurLay();
        m_autoWord.lay = CurLay();
    }
    struct {
        HKL lay = 0;            // раскладка на прошлой границе слова; 0 - не знаем (другое окно)
        bool backspace = false; // в слове стирали
        bool moved = false;     // курсор переезжал в том же окне
        bool undone = false;    // переключение посреди этого слова отменили - в нём больше не переключать
        bool twoCapsUndone = false; // ДВе ЗАглавные этого слова исправили на знаке и отменили - больше не исправлять
        int boundary = -1;      // после того, как курсор переезжал: перед словом в поле граница (1) или буква (0), -1 - не
                                // спрашивали или поле не сказало (StartedAfterBoundary)
    } m_autoWord;
    struct {
        std::wstring word;      // как набрано (буквенная часть, строчными)
        ULONGLONG at = 0;
        size_t size = 0;        // набранных клавиш после переключения: другое число - уже печатали дальше
        std::wstring typed, there; // как набрано и чем стало - для журнала (с короткими словами перед ним)
        std::wstring wordTyped, wordThere;   // ... само слово
        std::wstring retroTyped, retroThere; // ... короткие слова перед ним
        bool early = false;     // посреди слова: его набирают дальше, отмена считается, пока это слово (ends)
        unsigned ends = 0;      // m_wordEnds при переключении
        HKL from = 0, to = 0;   // раскладка до переключения и после
        size_t span = 0;        // переведено клавиш с конца набранного (при переключении)
        size_t retro = 0;       // ... из них - коротких слов перед словом
        bool pair = false;      // само слово - короткое, переключено вместе с ними (AutoSwitch::Short::WithPartner)
        size_t total = 0;       // CycleRevertList::Total() при переключении
        size_t changes = 0;     // CycleRevertList::Changes() при переключении: другое - потом набирали или стирали
    } m_autoSwitched;
    unsigned m_holdId = 0;      // номер придержки текущего сообщения (KeyHold::Allowed)
    unsigned m_wordEnds = 0;    // границ слов (AutoWordEnd) с начала работы
    // Последнее слово, которое автопереключение проверило и не тронуло, и почему - в журнал, если его исправят вручную.
    struct {
        std::wstring typed;
        std::string why;
    } m_autoNo;

    TStatus FixCtrlAlt(CHotKey key);

    void SetNewLay(HKL lay) {
        LOG_ANY(L"Try set {} lay", (void*)lay);
        m_ourSwitchAt = GetTickCount64();
        m_ourSwitchRoot = topWndInfo2.hwnd_top ? GetAncestor(topWndInfo2.hwnd_top, GA_ROOT) : nullptr;

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
