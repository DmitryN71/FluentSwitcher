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
// ждёт - его отпустит эта проверка или, если её нет, сразу (AfterKey). Новые нажатия, пока что-то держится или
// возвращается, встают в очередь (Busy).
// Движок печатает исправление (Claim) - таймаут ждёт, пока он не закончит (но не дольше 15 с): длинное слово при
// медленной перепечатке печатается дольше 3 с.
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

#include <deque>
#include <vector>

namespace KeyHold {

inline const ULONG_PTR c_Replayed = c_MyInjectedId ^ (ULONG_PTR)0x5EB1A7EDu; // так помечены отправленные заново
// Окну потока хука: отпустить (wParam - номер придержки, lParam 1 - по таймауту).
inline const UINT WM_Release = WM_APP + 0x61;
inline std::atomic<HWND> window = nullptr; // окно потока хука (HookerThread.h)

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
inline ULONGLONG since = 0;     // начало придержки или последней отправки - от него 3 с
inline bool timedOut = false;
inline std::deque<INPUT> held;
inline size_t replaying = 0;    // столько отправленных заново ещё не прошло через хук
inline bool batchEnded = false; // последнее отправленное прошло через хук: что дальше - AfterKey

// Рабочий поток: решено, исправлять или нет, - можно отпускать придержку id.
inline void RequestRelease(unsigned id) {
	if (HWND w = window) PostMessageW(w, WM_Release, id, 0);
}

// Нажатия держатся: идёт придержка, отправленное ещё возвращается или ждёт своей порции.
inline bool Busy() {
	return active || replaying > 0 || !held.empty();
}

// Можно начать придержку на этом нажатии: ничего не держится и не возвращается (отправленное заново - последнее в
// порции).
inline bool CanStart() {
	return !active && replaying == 0;
}

// Окно впереди запущено от администратора, а мы нет: Windows не даст отправить ему нажатия - придерживать нельзя,
// они бы пропали.
inline bool CanHold() {
	if (Utils::IsSelfElevated()) return true;
	DWORD pid = 0;
	GetWindowThreadProcessId(GetForegroundWindow(), &pid);
	HANDLE process = pid ? OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid) : nullptr;
	if (!process) return false;
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
	since = GetTickCount64();
	timedOut = false;
	if (++generation >= kSending) generation = 1;
	current = generation;
	return generation;
}

// first - в начало очереди: Enter / Tab, на котором начали придержку, а за ним уже ждут набранные позже.
inline void Hold(const KBDLLHOOKSTRUCT& k, bool first = false) {
	INPUT in{};
	in.type = INPUT_KEYBOARD;
	in.ki.wVk = (WORD)k.vkCode;
	in.ki.wScan = (WORD)k.scanCode;
	in.ki.dwFlags = ((k.flags & LLKHF_UP) ? KEYEVENTF_KEYUP : 0) | ((k.flags & LLKHF_EXTENDED) ? KEYEVENTF_EXTENDEDKEY : 0);
	in.ki.dwExtraInfo = c_Replayed;
	if (first)
		held.push_front(in);
	else
		held.push_back(in);
	const ULONGLONG waited = GetTickCount64() - since;
	if (!timedOut && waited > 3000) { // движок так и не ответил - не держать клавиатуру
		// Печатает исправление - подождать (не дольше 15 с); нет - забрать у него придержку, пока он не начал.
		unsigned expected = generation;
		const bool idle = current.compare_exchange_strong(expected, 0) || expected == 0;
		if (idle || waited > 15000) {
			LOG_WARN("hold: no answer for 3 s, letting the keys go");
			timedOut = true;
			current = 0;
			if (HWND w = window) PostMessageW(w, WM_Release, generation, 1);
		}
	}
}

// Отправить порцию: до первого конца слова (пробел, Enter, Tab) или клавиши не буквы включительно.
inline void SendBatch() {
	auto letter = [](WORD vk) {
		return (vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9') || (vk >= VK_OEM_1 && vk <= VK_OEM_3) ||
			(vk >= VK_OEM_4 && vk <= VK_OEM_8) || vk == VK_OEM_102 || vk == VK_SPACE;
	};
	std::vector<INPUT> list;
	while (!held.empty()) {
		const INPUT in = held.front();
		held.pop_front();
		list.push_back(in);
		const WORD vk = in.ki.wVk;
		if (!letter(vk) || (vk == VK_SPACE && !(in.ki.dwFlags & KEYEVENTF_KEYUP))) break;
	}
	if (list.empty()) return;
	since = GetTickCount64();
	timedOut = false;
	const UINT sent = SendInput((UINT)list.size(), list.data(), sizeof(INPUT));
	LOG_ANY("hold: sent {} of {} held keys, {} wait", sent, list.size(), held.size());
	replaying += sent;
}

// Сообщение WM_Release в потоке хука: движок решил (или прошло 3 с - timeout).
inline void OnRelease(unsigned id, bool timeout) {
	if (id != generation) return; // старая придержка: уже отпущена
	current = 0;
	active = false;
	if (timeout) replaying = 0; // отправленные заново не вернулись за 3 с - не ждать их
	if (replaying == 0) SendBatch();
}

// Нажатие, отправленное заново, пришло в хук (до своей обработки).
inline void OnReplayed() {
	if (replaying > 0 && --replaying == 0) batchEnded = true;
}

// После каждого нажатия: порция вернулась, а на её последней клавише придержка не началась - следующую.
inline void AfterKey() {
	if (!batchEnded) return;
	batchEnded = false;
	if (!active && replaying == 0 && !held.empty()) SendBatch();
}

// ----- регистр букв текущего слова, как его видит хук -----
inline std::vector<bool> word; // true - заглавная
inline bool broken = false;    // в слове цифра, CapsLock, сочетание - не наш случай
inline std::atomic<bool> earlyDone = false; // рабочий поток: посреди этого слова решать больше нечего

inline bool IsLetterKey(UINT vk) {
	return (vk >= 'A' && vk <= 'Z') || (vk >= VK_OEM_1 && vk <= VK_OEM_3) || (vk >= VK_OEM_4 && vk <= VK_OEM_8) ||
		vk == VK_OEM_102;
}

inline void ResetWord() {
	word.clear();
	broken = false;
	earlyDone = false;
}

// Нажатие, которое движок получает как набор (не пробел). true - в слово добавилась буква.
inline bool Track(UINT vk, bool shift, bool caps, bool command) {
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
	if (IsLetterKey(vk)) {
		word.push_back(shift);
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
};
inline WordEnd EndWord() {
	WordEnd end;
	end.twoCaps = !broken && word.size() >= 4 && word[0] && word[1] && !word[2] && !word[3];
	end.letters = !broken && !word.empty();
	ResetWord();
	return end;
}

}
