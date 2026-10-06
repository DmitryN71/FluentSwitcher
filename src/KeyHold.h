// Придержанные нажатия ("ДВе ЗАглавные", TwoCaps.h; автопереключение, AutoSwitch.h). Слово исправляется после
// пробела (или посреди слова), а пальцы в это время печатают дальше: если их нажатия дойдут до программы, пока движок
// стирает и перепечатывает слово, буквы перемешаются. Поэтому после такой клавиши хук не пропускает нажатия, а копит
// их, и поток хука потом отправляет их теми же клавишами и в том же порядке.
//
// Всё состояние - в потоке хука: вызовы хука и сообщения его окна идут в нём по очереди, блокировки не нужны. У каждой
// придержки свой номер (Start): рабочий поток получает его в сообщении о клавише, исправляет, только пока держится она
// (Allowed), и просит отпустить её же (RequestRelease) - запоздалый ответ (через 3 с всё отпускается само) не тронет
// следующую. Отпущенное уходит порциями: до конца слова (пробел, Enter, Tab) или клавиши не буквы (Shift, Pause, Ctrl:
// ею могут нажать сочетание) включительно - она проходит через хук последней в порции, и на ней слово проверяется, а
// сочетание делается, как набранное руками (сочетание тоже держит придержку, пока движок его не сделает); остальное
// ждёт - его отправит следующая порция (Next), если эта проверка не начала новую придержку. Новые нажатия, пока что-то
// держится или возвращается, встают в очередь (Busy).
// Движок печатает исправление (Claim) - таймаут ждёт, пока он не закончит (но не дольше 15 с): длинное слово при
// медленной перепечатке печатается дольше 3 с.
//
// SendInput из потока хука прогоняет отправленное через хук тут же, внутри себя: нажатия возвращаются (OnReplayed)
// раньше, чем он вернёт управление. Поэтому возвращающиеся считаются до отправки, а следующая порция уходит только из
// цикла сообщений окна (WM_Next), не из хука. (В 1.5.0-test4 счёт шёл после отправки, не сходился, и клавиатура после
// первого переключения выпускала по клавише раз в 3 с.)
//
// Клавиатура не должна залипать ни при какой ошибке: пока что-то держится, таймер окна раз в полсекунды проверяет
// сроки (Tick) - движок не ответил за 3 с - отдать всё сразу; отправленное не вернулось через хук (его съел чужой хук:
// сразу после SendInput, если остальные вернулись внутри него, иначе через 1 с) - не ждать его; SendInput отправил не
// всё (другой рабочий стол) - остальное ещё раз по таймеру. Три неудачи подряд - 5 минут не придерживать вовсе
// (исправлений в это время нет). Чужая программа, подменившая нашу клавишу своей посреди отправки (AutoHotkey), -
// её клавиша проходит на месте нашей (InPlace).
// Проверка без клавиатуры - tools\test_hold.cmd.
//
// Придерживать стоит, только если слово может подойти под правило: хук видит лишь клавиши и Shift, поэтому
// грубо смотрит на регистр букв (Track, EndWord), а точно решает движок по символам. Автопереключению годится
// любое слово из двух букв и больше без цифр и команд: проверка в движке - доли миллисекунды, и если менять нечего,
// придержанного обычно нет вовсе (следующая клавиша приходит позже).
//
// Посреди слова (автопереключение, не дожидаясь конца слова) - так же: с четвёртой по восьмую букву хук пропускает
// букву, а следующие нажатия придерживает, пока движок решает, не переключить ли уже сейчас (EarlyPoint; проверка -
// несколько миллисекунд). Движок решил, что посреди этого слова больше нечего (переключил или не его случай), - earlyDone, и до
// конца слова буквы идут без задержки.
#pragma once

#include "RemoteDesktop.h"

#include <deque>
#include <vector>

namespace KeyHold {

inline const ULONG_PTR c_Replayed = c_MyInjectedId ^ (ULONG_PTR)0x5EB1A7EDu; // так помечены отправленные заново
// Окну потока хука: отпустить (wParam - номер придержки, lParam 1 - по таймауту).
inline const UINT WM_Release = WM_APP + 0x61;
inline const UINT WM_Next = WM_APP + 0x62;  // окну потока хука: отправить следующую порцию
inline constexpr UINT_PTR kTimer = 0x61;    // таймер окна потока хука, пока что-то держится (Tick)
inline std::atomic<HWND> window = nullptr; // окно потока хука (HookerThread.h)

// Отправка нажатий, сообщения окну и часы - через указатели: проверка (tools\test_hold.cpp) подменяет их и гоняет
// придержку без клавиатуры.
inline UINT(WINAPI* sendInput)(UINT, LPINPUT, int) = ::SendInput;
inline BOOL(WINAPI* postMessage)(HWND, UINT, WPARAM, LPARAM) = ::PostMessageW;
inline ULONGLONG(WINAPI* now)() = ::GetTickCount64;

// Номер придержки, которую сейчас можно исправлять; 0 - никакую: отпущена (движок не ответил за 3 с), пальцы печатают
// дальше, и позднее исправление стёрло бы не то. kSending - движок её исправляет (Claim).
inline constexpr unsigned kSending = 0x80000000u;
inline std::atomic<unsigned> current = 0;
// Рабочий поток: придержка id ещё держится - исправлять можно.
inline bool Allowed(unsigned id) {
	return id != 0 && (current & ~kSending) == id;
}
// Рабочий поток: начать печатать исправление придержки id (таймаут подождёт); false - её уже отпустили.
inline bool Claim(unsigned id) {
	unsigned expected = id;
	if (id && current.compare_exchange_strong(expected, id | kSending)) return true;
	return id && expected == (id | kSending);
}
// ... и закончить.
inline void Unclaim(unsigned id) {
	if (!id) return;
	unsigned expected = id | kSending;
	current.compare_exchange_strong(expected, id);
}
struct Claimed {
	unsigned id;
	~Claimed() { Unclaim(id); }
};

// Курсор переезжал (щелчок, другое окно): рабочий поток бросает печатать исправление, иначе оно ушло бы в другое место.
inline std::atomic<unsigned> caretMoves = 0;

// ----- поток хука -----
inline unsigned generation = 0; // номер последней придержки
inline bool active = false;     // придержка: движок решает
inline ULONGLONG since = 0;     // начало придержки или последней отправки - от него сроки
inline bool timedOut = false;
inline std::deque<INPUT> held;
inline size_t replaying = 0;    // столько отправленных заново ещё не прошло через хук
inline bool batchEnded = false; // последнее отправленное прошло через хук: что дальше - AfterKey
inline bool flushing = false;   // последняя отправка - всё сразу после неудачи (не в счёт удачи)
inline bool sending = false;    // поток хука внутри SendInput (SendBatch): хук сейчас зовётся из него
inline bool sendFailed = false; // последняя отправка не ушла вся: остальное - по таймеру
inline int failures = 0;        // неудач подряд (таймаут, отправленное не вернулось)
inline ULONGLONG pausedUntil = 0; // три неудачи подряд - до этой минуты не придерживать

// Рабочий поток: решено, исправлять или нет, - можно отпускать придержку id.
inline void RequestRelease(unsigned id) {
	if (HWND w = window) postMessage(w, WM_Release, id, 0);
}

// Следующая порция - из цикла сообщений (OnNext): SendInput внутри хука вызвал бы хук ещё глубже.
inline void Next() {
	if (HWND w = window) postMessage(w, WM_Next, 0, 0);
}

// Пока что-то держится - таймер проверяет сроки (Tick), даже если клавиши больше не нажимают.
inline void Watch() {
	if (HWND w = window) SetTimer(w, kTimer, 500, nullptr);
}

// Нажатия держатся: идёт придержка, отправленное ещё возвращается или ждёт своей порции.
inline bool Busy() {
	return active || replaying > 0 || !held.empty();
}

// Нажатие от другой программы посреди нашей отправки - её ответ на нашу клавишу (AutoHotkey, PowerToys меняют одну
// клавишу на другую; Windows добавляет Ctrl к AltGr): пропустить на месте той клавиши, а не в конец очереди.
inline bool InPlace(const KBDLLHOOKSTRUCT& k) {
	return sending && (k.flags & LLKHF_INJECTED);
}

// Можно начать придержку на этом нажатии: ничего не держится и не возвращается (отправленное заново - последнее в
// порции), и придержка не на паузе после неудач.
inline bool CanStart() {
	return !active && replaying == 0 && now() >= pausedUntil;
}

// Консоль (командная строка, Windows Terminal, ConEmu, mintty): там команды и пути, а не слова - не исправляем.
inline bool IsConsoleWindow(HWND w) {
	wchar_t cls[64] = {};
	GetClassNameW(w, cls, 64);
	for (const wchar_t* name : { L"ConsoleWindowClass", L"CASCADIA_HOSTING_WINDOW_CLASS", L"VirtualConsoleClass", L"mintty" })
		if (wcscmp(cls, name) == 0) return true;
	return false;
}

// Окно впереди запущено от администратора, а мы нет: Windows не даст отправить ему нажатия - придерживать нельзя,
// они бы пропали. Окно удалённого рабочего стола или виртуальной машины (RemoteDesktop.h) и консоль: там мы ничего не
// исправляем - придерживать незачем.
inline bool CanHold() {
	const HWND fg = GetForegroundWindow();
	if (IsConsoleWindow(fg)) return false;
	DWORD pid = 0;
	GetWindowThreadProcessId(fg, &pid);
	HANDLE process = pid ? OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid) : nullptr;
	if (!process) return Utils::IsSelfElevated();
	const bool remote = RemoteDesktop::IsClientProcess(process);
	if (remote || Utils::IsSelfElevated()) {
		CloseHandle(process);
		return !remote;
	}
	bool elevated = false;
	HANDLE token = nullptr;
	if (OpenProcessToken(process, TOKEN_QUERY, &token)) {
		TOKEN_ELEVATION e{};
		DWORD size = 0;
		elevated = GetTokenInformation(token, TokenElevation, &e, sizeof(e), &size) && e.TokenIsElevated;
		CloseHandle(token);
	}
	CloseHandle(process);
	return !elevated;
}

// Начать придержку; её номер - рабочему потоку. Ждущие своей порции остаются: они набраны после этой клавиши.
inline unsigned Start() {
	active = true;
	since = now();
	timedOut = false;
	if (++generation >= kSending) generation = 1;
	current = generation;
	Watch();
	return generation;
}

// Неудача: таймаут или отправленное не вернулось. Три подряд - что-то не так в этой системе: не придерживать 5 минут.
inline void Fail() {
	if (++failures < 3) return;
	failures = 0;
	pausedUntil = now() + 5 * 60 * 1000;
	LOG_WARN("hold: three failures in a row, not holding keys for 5 minutes");
}

// Сроки: отправленное не вернулось через хук за 1 с - не ждать его (не посреди отправки: она сама долгая, если
// медлит чужой хук); движок не ответил за 3 с - отпустить.
inline void CheckTimeout() {
	const ULONGLONG waited = now() - since;
	if (replaying > 0 && !sending && waited > 1000) {
		LOG_WARN("hold: {} sent keys did not come back in 1 s, not waiting for them", replaying);
		replaying = 0;
		Fail();
		if (!active && !held.empty()) Next();
	}
	if (!active || timedOut || waited <= 3000) return;
	// Печатает исправление - подождать (не дольше 15 с); нет - забрать у него придержку, пока он не начал.
	unsigned expected = generation;
	const bool idle = current.compare_exchange_strong(expected, 0) || expected == 0;
	if (idle || waited > 15000) {
		LOG_WARN("hold: no answer for 3 s, letting the keys go");
		timedOut = true;
		current = 0;
		if (HWND w = window) postMessage(w, WM_Release, generation, 1);
	}
}

// first - в начало очереди: Enter / Tab, на котором начали придержку, а за ним уже ждут набранные позже.
inline void Hold(const KBDLLHOOKSTRUCT& k, bool first = false) {
	INPUT in{};
	in.type = INPUT_KEYBOARD;
	in.ki.wVk = (WORD)k.vkCode;
	in.ki.wScan = (WORD)k.scanCode;
	in.ki.dwFlags = ((k.flags & LLKHF_UP) ? KEYEVENTF_KEYUP : 0) | ((k.flags & LLKHF_EXTENDED) ? KEYEVENTF_EXTENDEDKEY : 0);
	in.ki.dwExtraInfo = c_Replayed;
	if (k.vkCode == VK_PACKET) { // знак Юникода от другой программы (KeePass, экранная клавиатура): он - в scanCode
		in.ki.wVk = 0;
		in.ki.dwFlags = (in.ki.dwFlags & KEYEVENTF_KEYUP) | KEYEVENTF_UNICODE;
	}
	if (first)
		held.push_front(in);
	else
		held.push_back(in);
	CheckTimeout();
	if (!active && replaying == 0) Next(); // ничего не держит, а очередь есть - отправить (лишнее сообщение не вредит)
}

// Отправить порцию: до первого конца слова (пробел, Enter, Tab) или клавиши не буквы включительно; all - всё сразу.
inline void SendBatch(bool all = false) {
	auto letter = [](WORD vk) {
		return (vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9') || (vk >= VK_OEM_1 && vk <= VK_OEM_3) ||
			(vk >= VK_OEM_4 && vk <= VK_OEM_8) || vk == VK_OEM_102 || vk == VK_SPACE || vk == 0; // 0 - знак Юникода
	};
	std::vector<INPUT> list;
	while (!held.empty()) {
		const INPUT in = held.front();
		held.pop_front();
		list.push_back(in);
		const WORD vk = in.ki.wVk;
		if (!all && (!letter(vk) || (vk == VK_SPACE && !(in.ki.dwFlags & KEYEVENTF_KEYUP)))) break;
	}
	if (list.empty()) return;
	since = now();
	timedOut = false;
	flushing = all;
	Watch();
	replaying += list.size(); // до отправки: они возвращаются через хук ещё внутри SendInput
	sending = true;
	const UINT sent = sendInput((UINT)list.size(), list.data(), sizeof(INPUT));
	const DWORD error = GetLastError();
	sending = false;
	LOG_ANY("hold: sent {} of {} held keys, {} wait", sent, list.size(), held.size());
	if (sent < list.size()) {
		// Не ушли (другой рабочий стол: UAC, блокировка) - обратно в начало очереди, ещё раз - по таймеру (Tick): иначе
		// пропали бы и отпускания Ctrl, Shift, и они остались бы нажатыми.
		if (!sendFailed) {
			LOG_WARN("hold: {} keys not sent, error {}, will try again", list.size() - sent, error);
			Fail();
		}
		sendFailed = true;
		for (size_t i = list.size(); i-- > sent;) held.push_front(list[i]);
		const size_t unsent = list.size() - sent;
		replaying = replaying > unsent ? replaying - unsent : 0;
	}
	else
		sendFailed = false;
	// Хук отработал внутри SendInput (так в Windows), а вернулись не все: остальные съел чужой хук - не ждать их.
	if (replaying > 0 && replaying < sent) {
		LOG_WARN("hold: {} sent keys did not come back (another program's hook?), not waiting for them", replaying);
		replaying = 0;
		if (!active && !held.empty()) Next();
	}
}

// Сообщение WM_Release в потоке хука: движок решил (или прошло 3 с - timeout).
inline void OnRelease(unsigned id, bool timeout) {
	if (id != generation || !active) return; // старая придержка или уже отпущена
	current = 0;
	active = false;
	if (timeout) {
		replaying = 0; // отправленные заново не вернулись - не ждать их
		Fail();
		SendBatch(true); // что-то не так - отдать клавиатуру сразу, без порций
		return;
	}
	if (replaying == 0) {
		if (held.empty()) failures = 0; // ответил вовремя, держать было нечего
		SendBatch();
	}
}

// Сообщение WM_Next в потоке хука: порция вернулась - следующую (на паузе - всё сразу).
inline void OnNext() {
	if (!active && replaying == 0 && !held.empty()) SendBatch(now() < pausedUntil);
}

// Таймер окна потока хука (kTimer): сроки, пока что-то держится.
inline void Tick() {
	if (!Busy()) {
		if (HWND w = window) KillTimer(w, kTimer);
		return;
	}
	CheckTimeout();
	if (!active && replaying == 0 && !held.empty()) Next();
}

// Нажатие, отправленное заново, пришло в хук (до своей обработки).
inline void OnReplayed() {
	if (replaying > 0 && --replaying == 0) {
		batchEnded = true;
		if (!flushing) failures = 0; // порция вернулась вся
	}
}

// После каждого нажатия: порция вернулась, а на её последней клавише придержка не началась - следующую.
inline void AfterKey() {
	if (!batchEnded) return;
	batchEnded = false;
	if (!active && replaying == 0 && !held.empty()) Next();
}

// ----- регистр букв текущего слова, как его видит хук -----
inline std::vector<bool> word; // true - заглавная
inline UINT lastLetter = 0;    // клавиша последней буквы слова
inline bool broken = false;    // в слове цифра, CapsLock, сочетание - не наш случай
inline std::atomic<bool> earlyDone = false; // рабочий поток: посреди этого слова решать больше нечего

inline bool IsLetterKey(UINT vk) {
	return (vk >= 'A' && vk <= 'Z') || (vk >= VK_OEM_1 && vk <= VK_OEM_3) || (vk >= VK_OEM_4 && vk <= VK_OEM_8) ||
		vk == VK_OEM_102;
}

inline void ResetWord() {
	word.clear();
	lastLetter = 0;
	broken = false;
	earlyDone = false;
}

// Нажатие, которое движок получает как набор (не пробел). true - в слово добавилась буква. repeat - автоповтор
// зажатой клавиши: так слова не набирают ("ааааа", W в игре), слово - не наш случай (не считать буквы автоповтора, а
// с ними и придерживать пробел после них).
inline bool Track(UINT vk, bool shift, bool caps, bool command, bool repeat = false) {
	if (CHotKey::IsKnownMods(vk)) return false; // сам Shift (Ctrl...) - не буква и не помеха
	if (command || caps) {
		word.clear();
		broken = true;
		return false;
	}
	if (vk == VK_BACK) {
		if (!word.empty()) word.pop_back();
		return false;
	}
	if (repeat && IsLetterKey(vk)) {
		broken = true;
		return false;
	}
	if (IsLetterKey(vk)) {
		word.push_back(shift);
		lastLetter = vk;
		return true;
	}
	if (vk >= '0' && vk <= '9') {
		if (!shift) broken = true; // цифра; с Shift - знак ( «"!), он по краям слова не мешает
		return false;
	}
	if (vk == VK_RETURN || vk == VK_TAB || vk == VK_ESCAPE || (vk >= VK_PRIOR && vk <= VK_DOWN) || vk == VK_DELETE) {
		ResetWord(); // новая строка, другое место
		return false;
	}
	broken = true;
	return false;
}

// Буква, после которой движок может переключить посреди слова (AutoSwitch::DecideEarly): с четвёртой по восьмую
// (AutoSwitch::kEarlyMin, kEarlyMax), пока он не сказал, что в этом слове больше нечего.
inline bool EarlyPoint() {
	return !broken && !earlyDone && word.size() >= 4 && word.size() <= 8;
}

// Конец слова (пробел, Enter, Tab): каким оно было. Слово на этом кончается.
struct WordEnd {
	bool twoCaps = false; // могло подойти под ДВе ЗАглавные (две заглавные, потом строчные)
	bool letters = false; // буквы без цифр и команд - его проверит автопереключение (и одну: список "Переключать всегда")
	size_t size = 0;      // букв
	UINT lastLetter = 0;  // клавиша последней
};
inline WordEnd EndWord() {
	WordEnd end;
	end.twoCaps = !broken && word.size() >= 4 && word[0] && word[1] && !word[2] && !word[3];
	end.letters = !broken && !word.empty();
	end.size = word.size();
	end.lastLetter = lastLetter;
	ResetWord();
	return end;
}

}
