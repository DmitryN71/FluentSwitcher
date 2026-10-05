// Придержанные нажатия ("ДВе ЗАглавные", TwoCaps.h; автопереключение, AutoSwitch.h). Слово исправляется после
// пробела, а пальцы в это время
// печатают дальше: если их нажатия дойдут до программы, пока движок стирает и перепечатывает слово, буквы
// перемешаются. Поэтому после пробела, за которым может быть исправление, хук не пропускает нажатия, а копит их,
// и поток хука потом отправляет их теми же клавишами и в том же порядке.
//
// Всё состояние - в потоке хука: вызовы хука и сообщения его окна идут в нём по очереди, блокировки не нужны.
// Рабочий поток только просит отпустить (RequestRelease). Порядок: поток хука отправляет накопленное и держит
// придержку, пока отправленные нажатия не пройдут через хук; нажатие, успевшее раньше них, встаёт в очередь
// и уходит следующей порцией. Если что-то пошло не так, через 3 с всё отпускается само.
//
// Придерживать стоит, только если слово может подойти под правило: хук видит лишь клавиши и Shift, поэтому
// грубо смотрит на регистр букв (Track, EndWord), а точно решает движок по символам. Автопереключению годится
// любое слово из двух букв и больше без цифр и команд: проверка в движке - доли миллисекунды, и если менять нечего,
// придержанного обычно нет вовсе (следующая клавиша приходит позже).
#pragma once

#include <vector>

namespace KeyHold {

inline const ULONG_PTR c_Replayed = c_MyInjectedId ^ (ULONG_PTR)0x5EB1A7EDu; // так помечены отправленные заново
inline const UINT WM_Release = WM_APP + 0x61;                               // окну потока хука: отпустить
inline std::atomic<HWND> window = nullptr;                                   // окно потока хука (HookerThread.h)

// Рабочий поток: исправлять ещё можно - нажатия держатся. Если придержка уже отпущена (движок не ответил за 3 с),
// пальцы печатают дальше, и позднее исправление стёрло бы не то.
inline std::atomic<bool> fixAllowed = false;

// ----- поток хука -----
inline bool active = false;
inline ULONGLONG since = 0;
inline std::vector<INPUT> held;
inline size_t replaying = 0; // столько отправленных заново ещё не прошло через хук

// Рабочий поток: решено, исправлять или нет, - можно отпускать.
inline void RequestRelease() {
	if (HWND w = window) PostMessageW(w, WM_Release, 0, 0);
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

inline void Start() {
	fixAllowed = true;
	active = true;
	since = GetTickCount64();
	held.clear();
	replaying = 0;
}

inline void Hold(const KBDLLHOOKSTRUCT& k) {
	INPUT in{};
	in.type = INPUT_KEYBOARD;
	in.ki.wVk = (WORD)k.vkCode;
	in.ki.wScan = (WORD)k.scanCode;
	in.ki.dwFlags = ((k.flags & LLKHF_UP) ? KEYEVENTF_KEYUP : 0) | ((k.flags & LLKHF_EXTENDED) ? KEYEVENTF_EXTENDEDKEY : 0);
	in.ki.dwExtraInfo = c_Replayed;
	held.push_back(in);
	if (GetTickCount64() - since > 3000) { // движок так и не ответил - не держать клавиатуру
		LOG_WARN("hold: no answer for 3 s, letting the keys go");
		fixAllowed = false;
		RequestRelease();
	}
}

// Сообщение WM_Release в потоке хука.
inline void OnRelease() {
	fixAllowed = false;
	if (!active) return;
	if (held.empty()) {
		if (replaying == 0) active = false;
		return;
	}
	std::vector<INPUT> list;
	list.swap(held);
	const UINT sent = SendInput((UINT)list.size(), list.data(), sizeof(INPUT));
	LOG_ANY("hold: sent {} of {} held keys", sent, list.size());
	replaying += sent;
	if (replaying == 0) active = false; // не ушло ничего - держать нечего
}

// Нажатие, отправленное заново, прошло через хук.
inline void OnReplayed() {
	if (replaying > 0) replaying--;
	if (active && replaying == 0) {
		if (held.empty())
			active = false;
		else
			RequestRelease(); // пока шла отправка, накопились новые - следующей порцией
	}
}

// ----- регистр букв текущего слова, как его видит хук -----
inline std::vector<bool> word; // true - заглавная
inline bool broken = false;    // в слове цифра, CapsLock, сочетание - не наш случай

inline bool IsLetterKey(UINT vk) {
	return (vk >= 'A' && vk <= 'Z') || (vk >= VK_OEM_1 && vk <= VK_OEM_3) || (vk >= VK_OEM_4 && vk <= VK_OEM_8) ||
		vk == VK_OEM_102;
}

inline void ResetWord() {
	word.clear();
	broken = false;
}

// Нажатие, которое движок получает как набор (не пробел).
inline void Track(UINT vk, bool shift, bool caps, bool command) {
	if (CHotKey::IsKnownMods(vk)) return; // сам Shift (Ctrl...) - не буква и не помеха
	if (command || caps) {
		word.clear();
		broken = true;
		return;
	}
	if (vk == VK_BACK) {
		if (!word.empty()) word.pop_back();
		return;
	}
	if (IsLetterKey(vk)) {
		word.push_back(shift);
		return;
	}
	if (vk >= '0' && vk <= '9') {
		if (!shift) broken = true; // цифра; с Shift - знак ( «"!), он по краям слова не мешает
		return;
	}
	if (vk == VK_RETURN || vk == VK_TAB || vk == VK_ESCAPE || (vk >= VK_PRIOR && vk <= VK_DOWN) || vk == VK_DELETE) {
		ResetWord(); // новая строка, другое место
		return;
	}
	broken = true;
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
