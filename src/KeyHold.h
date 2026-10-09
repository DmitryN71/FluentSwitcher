// Придержанные нажатия ("ДВе ЗАглавные", TwoCaps.h; автопереключение, AutoSwitch.h). Слово исправляется в конце
// (пробел, Enter, Tab или знак сразу после него) или посреди слова, а пальцы в это время печатают дальше: если их
// нажатия дойдут до программы, пока движок стирает и перепечатывает слово, буквы перемешаются. Поэтому после такой
// клавиши хук не пропускает нажатия, а копит их, и поток хука потом отправляет их в том же порядке; нажатия с
// клавиатуры - клавишей, что на том же месте в раскладке, какая стоит при отправке (SendBatch).
//
// Всё состояние - в потоке хука: вызовы хука и сообщения его окна идут в нём по очереди, блокировки не нужны. У каждой
// придержки свой номер (Start): рабочий поток получает его в сообщении о клавише, исправляет, только пока держится она
// (Allowed), и просит отпустить её же (RequestRelease) - запоздалый ответ (через 3 с всё отпускается само) не тронет
// следующую. Отпущенное уходит порциями: до конца слова (пробел, Enter, Tab), клавиши не буквы (Shift, Pause, Ctrl:
// ею могут нажать сочетание) или нажатия при Ctrl, Alt, Win (сочетание) включительно - она проходит через хук
// последней в порции, и на ней слово проверяется, а
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

#include "ConsolePrograms.h"
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
// Придержанное нажатие; physical - с клавиатуры (не от другой программы): его клавишу можно назвать по месту на
// клавиатуре в раскладке, какая стоит при отправке (Send).
struct Held {
	INPUT in;
	bool physical = false;
};
inline std::deque<Held> held;
inline WORD downAs[512] = {}; // клавиша по месту (scan, 0x100 - расширенная): каким другим VK ушло её нажатие
inline size_t KeyPlace(DWORD scan, bool ext) { return (scan & 0xFF) | (ext ? 0x100 : 0); }
// Нажатие или отпускание с клавиатуры прошло мимо очереди - той клавишей, что дала Windows: то, каким VK ушло нажатие
// этой клавиши раньше, к нему уже не относится. Иначе отпускание её, придержанное потом, ушло бы чужой клавишей, а
// своя осталась бы нажатой (немецкая или французская раскладка после автопереключения).
inline void PassedLive(const KBDLLHOOKSTRUCT& k) {
	if (!(k.flags & LLKHF_INJECTED)) downAs[KeyPlace(k.scanCode, k.flags & LLKHF_EXTENDED)] = 0;
}
// Перехват подключили заново, клавиши забыли (Hooker::ClearAllKeys): забыть и это.
inline void ForgetRemaps() { std::fill(std::begin(downAs), std::end(downAs), WORD(0)); }

// Раскладка поля с фокусом - та же, что у движка (Utils::GetFocusedWndInfo): у окна впереди она бывает другой (окно
// Магазина - рамка ApplicationFrameHost, консоль - поток другой программы, окна впереди нет - поток хука).
inline HKL FocusLayout() {
	if (const HKL lay = Utils::GetFocusedWndInfo().lay) return lay;
	return GetKeyboardLayout(GetWindowThreadProcessId(GetForegroundWindow(), nullptr));
}
inline size_t replaying = 0;    // столько отправленных заново ещё не прошло через хук
// Нажатия других программ (AutoHotkey, Claude Enter Swap), отправленные заново, - с их собственной меткой (dwExtraInfo):
// по ней программа узнаёт своё и не принимает его за нажатое человеком. С нашей меткой AutoHotkey принимал наш повтор
// своего Enter за настоящий Enter, срабатывало его правило "Enter - Shift+Enter", и Ctrl+Enter в Claude отправлял
// сообщение только с третьего раза (а Shift залипал; Дмитрий 09.10.2026). Свои повторы таких нажатий узнаём по порядку:
// они возвращаются через хук в том же порядке, в каком ушли.
inline std::deque<INPUT> foreignOut;
// Отправленные заново больше не ждём (не вернулись, таймаут, перехват подключили заново).
inline void ReplaysGone() {
	replaying = 0;
	foreignOut.clear();
}
// Нажатие от программы (LLKHF_INJECTED) - наш повтор её нажатия? Да - вычеркнуть его (и пропавшие перед ним: их съел
// чужой перехват).
inline bool ForeignReplay(const KBDLLHOOKSTRUCT& k) {
	if (!(k.flags & LLKHF_INJECTED)) return false;
	const bool up = k.flags & LLKHF_UP;
	for (size_t i = 0; i < foreignOut.size(); i++) {
		const KEYBDINPUT& o = foreignOut[i].ki;
		const bool unicode = o.dwFlags & KEYEVENTF_UNICODE;
		if (o.dwExtraInfo != k.dwExtraInfo || ((o.dwFlags & KEYEVENTF_KEYUP) != 0) != up) continue;
		if (unicode ? (k.vkCode == VK_PACKET && k.scanCode == o.wScan) : k.vkCode == o.wVk) {
			foreignOut.erase(foreignOut.begin(), foreignOut.begin() + i + 1);
			return true;
		}
	}
	return false;
}
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
// исправляем - придерживать незачем; консоль из списка consolePrograms (autoswitch_console) - можно.
inline bool CanHold(const std::set<std::wstring>* consolePrograms = nullptr) {
	const HWND fg = GetForegroundWindow();
	if (IsConsoleWindow(fg) && !(consolePrograms && ConsolePrograms::Allowed(fg, *consolePrograms))) return false;
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
		ReplaysGone();
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
	// С клавиатуры - наша метка; от другой программы - её собственная (foreignOut).
	in.ki.dwExtraInfo = (k.flags & LLKHF_INJECTED) ? k.dwExtraInfo : c_Replayed;
	if (k.vkCode == VK_PACKET) { // знак Юникода от другой программы (KeePass, экранная клавиатура): он - в scanCode
		in.ki.wVk = 0;
		in.ki.dwFlags = (in.ki.dwFlags & KEYEVENTF_KEYUP) | KEYEVENTF_UNICODE;
	}
	const Held h{ in, !(k.flags & LLKHF_INJECTED) };
	if (first)
		held.push_front(h);
	else
		held.push_back(h);
	CheckTimeout();
	if (!active && replaying == 0) Next(); // ничего не держит, а очередь есть - отправить (лишнее сообщение не вредит)
}

// Отправить порцию: до первого конца слова (пробел, Enter, Tab) или клавиши не буквы включительно; all - всё сразу.
// Нажатая при Ctrl, Alt или Win буква - тоже последней: это сочетание, и на нём придержка ждёт, пока движок его
// сделает (HookerKeyboard: сочетание среди придержанных) - а набранное за ним, уйди оно той же порцией, попало бы в
// программу раньше, чем вставка или исправление.
// Клавиши с клавиатуры отправляются той клавишей, что на том же месте в раскладке сейчас: автопереключение могло
// сменить раскладку, пока они ждали, а в немецкой или французской раскладке на месте русской "н" - Z, а не Y.
// Сочетания (Ctrl, Alt, Win; не AltGr) - той клавишей, что нажата: Ctrl+Z после переключения на французскую иначе
// ушёл бы Ctrl+W и закрыл вкладку.
inline void SendBatch(bool all = false) {
	auto letter = [](WORD vk) {
		return (vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9') || (vk >= VK_OEM_1 && vk <= VK_OEM_3) ||
			(vk >= VK_OEM_4 && vk <= VK_OEM_8) || vk == VK_OEM_102 || vk == VK_SPACE || vk == 0; // 0 - знак Юникода
	};
	auto down = [](int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; };
	const bool command = down(VK_CONTROL) || down(VK_MENU) || down(VK_LWIN) || down(VK_RWIN);
	// Порция кончается на каждом модификаторе: их состояние сейчас - то, при котором нажаты её клавиши.
	const bool shortcut = (down(VK_CONTROL) && !down(VK_RMENU)) || down(VK_LMENU) || down(VK_LWIN) || down(VK_RWIN);
	const HKL lay = FocusLayout();
	std::vector<INPUT> list;
	while (!held.empty()) {
		Held h = held.front();
		held.pop_front();
		INPUT& in = h.in;
		const bool up = in.ki.dwFlags & KEYEVENTF_KEYUP;
		if (h.physical && in.ki.wScan && in.ki.wVk != VK_SPACE && letter(in.ki.wVk)) {
			const bool ext = in.ki.dwFlags & KEYEVENTF_EXTENDEDKEY;
			const size_t at = KeyPlace(in.ki.wScan, ext);
			if (!up) {
				const WORD vk = shortcut ? 0 : (WORD)MapVirtualKeyExW(in.ki.wScan | (ext ? 0xE000 : 0), MAPVK_VSC_TO_VK_EX, lay);
				const bool other = vk && vk != in.ki.wVk && vk != VK_SPACE && letter(vk);
				if (other) in.ki.wVk = vk;
				downAs[at] = other ? vk : 0;
			}
			else if (downAs[at]) { // отпускание - той же клавишей, что ушло нажатие (не той, что на месте сейчас)
				in.ki.wVk = downAs[at];
				downAs[at] = 0;
			}
		}
		list.push_back(in);
		const WORD vk = in.ki.wVk;
		if (!all && (!letter(vk) || (vk == VK_SPACE && !up) || (command && !up))) break;
	}
	if (list.empty()) return;
	since = now();
	timedOut = false;
	flushing = all;
	Watch();
	replaying += list.size(); // до отправки: они возвращаются через хук ещё внутри SendInput
	for (const INPUT& in : list)
		if (in.ki.dwExtraInfo != c_Replayed) foreignOut.push_back(in);
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
		// Уже названные по месту (и записанные в downAs) - как есть: второй раз их не переназывать.
		for (size_t i = list.size(); i-- > sent;) {
			held.push_front({ list[i], false });
			if (list[i].ki.dwExtraInfo != c_Replayed && !foreignOut.empty()) foreignOut.pop_back(); // не ушло - не ждать
		}
		const size_t unsent = list.size() - sent;
		replaying = replaying > unsent ? replaying - unsent : 0;
	}
	else
		sendFailed = false;
	// Хук отработал внутри SendInput (так в Windows), а вернулись не все: остальные съел чужой хук - не ждать их.
	if (replaying > 0 && replaying < sent) {
		LOG_WARN("hold: {} sent keys did not come back (another program's hook?), not waiting for them", replaying);
		ReplaysGone();
		if (!active && !held.empty()) Next();
	}
}

// Сообщение WM_Release в потоке хука: движок решил (или прошло 3 с - timeout).
inline void OnRelease(unsigned id, bool timeout) {
	if (id != generation || !active) return; // старая придержка или уже отпущена
	current = 0;
	active = false;
	if (timeout) {
		ReplaysGone(); // отправленные заново не вернулись - не ждать их
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
inline UINT firstKeys[2] = {}; // клавиши первых двух букв (i, 'i - fix_lone_i)
inline ULONGLONG letterAt = 0; // когда набрана последняя буква (FocusIn)
inline bool broken = false;    // в слове цифра, CapsLock, сочетание - не наш случай
inline std::atomic<bool> earlyDone = false; // рабочий поток: посреди этого слова решать больше нечего

inline bool IsLetterKey(UINT vk) {
	return (vk >= 'A' && vk <= 'Z') || (vk >= VK_OEM_1 && vk <= VK_OEM_3) || (vk >= VK_OEM_4 && vk <= VK_OEM_8) ||
		vk == VK_OEM_102;
}

// Клавиша, которая в одной из раскладок бывает знаком после слова: , . ; : ' " ? (б ю ж э и "/ ?" русской раскладки -
// буквы и точка там, где в английской знаки; Shift с 1 2 4 6 7 - знаки, разные в раскладках), и Shift+0 - ")" в
// обеих, как "!" (Shift+1). Знак ли это в другой раскладке на самом деле - решает движок.
inline bool SignKey(UINT vk, bool shift) {
	if (vk == VK_OEM_COMMA || vk == VK_OEM_PERIOD || vk == VK_OEM_1 || vk == VK_OEM_7 || vk == VK_OEM_2) return true;
	return shift && (vk == '1' || vk == '2' || vk == '4' || vk == '6' || vk == '7' || vk == '0');
}

inline void ResetWord() {
	word.clear();
	lastLetter = 0;
	firstKeys[0] = firstKeys[1] = 0;
	broken = false;
	earlyDone = false;
}

// Фокус перешёл (EVENT_OBJECT_FOCUS, Hooker::FocusProc): в другом поле слово начинается заново. Но не посреди набора:
// списки подсказок (адресная строка браузера, VS Code, упоминания в мессенджерах) передают фокус своей строке после
// каждой буквы - слово стиралось бы, и его конец не проверялся. Сочетание, которым перешли в другое поле (Ctrl+L,
// Ctrl+Shift+R), слово и так "портит" - его сбрасываем всегда.
inline void FocusIn() {
	if (word.empty() || broken || now() - letterAt > 500) ResetWord();
}

// Раскладка поля - английская (i - I, fix_lone_i: решает движок, а хук не держит клавиши зря в других).
inline bool EnglishLayout() {
	return PRIMARYLANGID(LOWORD((UINT_PTR)FocusLayout())) == LANG_ENGLISH;
}

// Раскладка поля - латиница (английская, немецкая...): латинские ДВе ЗАглавные в программах из "Без
// автопереключения" движок не исправляет (там это имена: ILogger, QString) - и держать незачем.
inline bool LatinLayout() {
	const UINT c = MapVirtualKeyExW('A', MAPVK_VK_TO_CHAR, FocusLayout()) & 0x7FFF;
	return c != 0 && c < 0x250;
}

// Клавиша - буква в раскладке поля (б ю ж э русской на месте , . ; '): слово продолжается, это не знак после него.
inline bool LetterHere(UINT vk) {
	const UINT c = MapVirtualKeyExW(vk, MAPVK_VK_TO_CHAR, FocusLayout()) & 0x7FFF;
	return c != 0 && IsCharAlphaW((wchar_t)c);
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
		if (word.size() < 2) firstKeys[1] = 0;
		if (word.empty()) firstKeys[0] = 0;
		return false;
	}
	// Громкость и плеер, NumLock и ScrollLock текст не трогают - слово продолжается (как у движка: AnalyzeTyped.h).
	if ((vk >= VK_VOLUME_MUTE && vk <= VK_MEDIA_PLAY_PAUSE) || vk == VK_NUMLOCK || vk == VK_SCROLL) return false;
	if (repeat && IsLetterKey(vk)) {
		broken = true;
		return false;
	}
	if (IsLetterKey(vk)) {
		word.push_back(shift);
		lastLetter = vk;
		if (word.size() <= 2) firstKeys[word.size() - 1] = vk;
		letterAt = now();
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

// Первые n клавиш слова могут быть ДВумя ЗАглавными: две заглавные, потом строчная (от трёх; дальше не смотрим: знак с
// Shift после слова - "ЧТо," в русской раскладке - хук видит как заглавную, и "ЧТо," не проверялось вовсе).
inline bool TwoCapsShape(size_t n) {
	return !broken && n >= 3 && n <= word.size() && word[0] && word[1] && !word[2];
}

// Конец слова (пробел, Enter, Tab): каким оно было. Слово на этом кончается.
struct WordEnd {
	bool twoCaps = false; // могло подойти под ДВе ЗАглавные (две заглавные, потом строчные)
	bool letters = false; // буквы без цифр и команд - его проверит автопереключение (и одну: список "Переключать всегда")
	bool loneI = false;   // может быть i, i'm, i've, i'll, i'd ('i - с кавычкой): до четырёх клавиш, первая буква -
	                      // строчная i, за ней не буква (in, is, it, if - слова, их не держим)
	size_t size = 0;      // букв
	UINT lastLetter = 0;  // клавиша последней
};
inline WordEnd EndWord() {
	WordEnd end;
	// От трёх букв ("ЧТо", "THe"; какие из них исправлять - решает движок: TwoCaps::Matches).
	end.twoCaps = TwoCapsShape(word.size());
	end.letters = !broken && !word.empty();
	auto alpha = [](UINT vk) { return vk >= 'A' && vk <= 'Z'; };
	end.loneI = end.letters && word.size() <= 4 &&
		((firstKeys[0] == 'I' && !word[0] && !alpha(firstKeys[1])) ||
		 (firstKeys[0] == VK_OEM_7 && firstKeys[1] == 'I' && word.size() >= 2 && !word[1]));
	end.size = word.size();
	end.lastLetter = lastLetter;
	ResetWord();
	return end;
}

}
