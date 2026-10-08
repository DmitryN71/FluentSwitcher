#pragma once

#include "CaretFlagPoke.h"

class Hooker {

	CAutoHHOOK hHookKeyGlobal;
	CAutoHHOOK hHookMouseGlobal;
	CAutoHWINEVENTHOOK hHookEventGlobal;
	CAutoHWINEVENTHOOK hHookEventFocus;
	CAutoHWINEVENTHOOK hHookEventGlobalSwitchDesk;

public:
	void ClearAllKeys() {
		hookerKeyb = {}; // пересоздаем весь рабочий state.
		KeyHold::ForgetRemaps();
		Worker()->PostMsg(Message_ClearWorlds{});
	}

public: class HookerKeyboard {
		CurStateWrapper curKeys;
		//TKeyCode disable_up = 0;

		// todo: unite
		CHotKey possible_hk_up;
		HotKeyType possible_hktype_up;

		// "Дважды" из одних модификаторов ("Shift #double", "Ctrl #double") срабатывает, когда второе нажатие
		// отпущено и до этого не было других клавиш и щелчков мыши. Срабатывание на само второе нажатие
		// путало быстрый набор: случайно коснулся Shift и сразу Shift+7 ("?") - последнее слово уходило
		// в другую раскладку посреди текста.
		CHotKey pending_double;
		HotKeyType pending_double_hk{};
		TKeyCode pending_double_vk{};
		TimePoint pending_double_time;

		public: TimePoint last_mouse_click_time;

		LRESULT CALLBACK LowLevelKeyboardProc(
			_In_  int nCode,
			_In_  WPARAM wParam,
			_In_  LPARAM lParam
		);
	};

private: inline static HookerKeyboard hookerKeyb;

	// Последний вызов перехвата клавиатуры и мыши (GetTickCount) - по ним видно, что Windows его отключила (Watch).
	inline static DWORD lastKeyTick = 0, lastMouseTick = 0;
	inline static DWORD lastRehookKey = 0, lastRehookMouse = 0;
	// Клавиши, которые Windows отдала программам (Raw Input, окно потока перехвата: OnRawKey): когда последняя и в каком
	// окне. Мышь их не обновляет - отключённый перехват клавиатуры виден, даже если мышью работают.
	inline static DWORD lastRawKeyTick = 0;
	inline static HWND rawKeyWindow = nullptr;
	// Сколько нажатий и отпусканий подряд дошло до программ, а наш перехват их не видел (больше секунды ничего): одно - ещё
	// не признак (его мог съесть перехват другой программы: AutoHotkey, PowerToys, запись сочетания), четыре - признак.
	inline static int rawUnseen = 0;
	inline static bool rawSeen = false; // Raw Input приходит (не пришло ни одного - смотрим по-старому, GetLastInputInfo)

	static LRESULT CALLBACK LowLevelKeyboardProc(
		_In_  int nCode,
		_In_  WPARAM wParam,
		_In_  LPARAM lParam
	) {
		lastKeyTick = GetTickCount();
		if (nCode == HC_ACTION) Late("key", ((KBDLLHOOKSTRUCT*)lParam)->time);
		return hookerKeyb.LowLevelKeyboardProc(nCode, wParam, lParam);
	}

	// Нажатие или движение мыши пришло к перехвату позже, чем случилось (время события - от Windows): его держал
	// кто-то до нас (перехваты, поставленные позже, зовутся раньше) или занятый поток перехвата. Только в журнал
	// отладки, не чаще раза в секунду: по нему видно, кто тормозит ввод.
	static void Late(const char* what, DWORD eventTime) {
		const DWORD now = GetTickCount(), late = now - eventTime;
		static DWORD lastReport = 0;
		if (late < 200 || late > 60000 || now - lastReport < 1000 || GetLogLevel() < LOG_LEVEL_2) return;
		lastReport = now;
		LOG_WARN("hook: a {} event came {} ms late", what, late);
	}

	//static void CALLBACK WinEventProc_SwitchDesk(
	//	HWINEVENTHOOK hWinEventHook,
	//	DWORD event,
	//	HWND hwnd,
	//	LONG idObject,
	//	LONG idChild,
	//	DWORD dwEventThread,
	//	DWORD dwmsEventTime
	//) {
	//	LOG_ANY("WinEventProc_SwitchDesk");
	//	Worker()->PostMsg(Message_ClearWorlds{});
	//}


	static void CALLBACK WinEventProc(HWINEVENTHOOK hWinEventHook, DWORD event, HWND hwnd, LONG idObject, LONG idChild, DWORD dwEventThread, DWORD dwmsEventTime) {
		KeyHold::current = 0;   // другое окно: исправлять придержанное нельзя,
		KeyHold::caretMoves++; // а печатающееся исправление ушло бы туда
		KeyHold::ResetWord();  // и слово там начинается заново
		Worker()->PostMsg(Message_ChangeForeg{ hwnd });
	}

	// Фокус перешёл в другое поле (в том же окне или в новом): слово там начинается заново. Сочетание, которым туда
	// попали (Ctrl+Shift+R - ответ на письмо, Ctrl+L, Ctrl+F), - не часть нового слова: иначе KeyHold::Track считал
	// его испорченным (сочетание внутри слова) и не проверял до пробела (Дмитрий 06.10: eM Client, ответ по
	// Ctrl+Shift+R - первое слово не переключалось, по кнопке "Ответить" - переключалось: щелчок слово сбрасывает).
	static void CALLBACK FocusProc(HWINEVENTHOOK, DWORD, HWND, LONG, LONG, DWORD, DWORD) {
		KeyHold::FocusIn(); // не посреди набора: там это список подсказок (KeyHold::FocusIn)
	}

	static LRESULT CALLBACK LowLevelMouseProc(
		_In_  int nCode,
		_In_  WPARAM wParam,
		_In_  LPARAM lParam
	) {
		lastMouseTick = GetTickCount();
		if (nCode == HC_ACTION) {
			Late("mouse", ((MSLLHOOKSTRUCT*)lParam)->time);
			if (wParam == WM_MOUSEMOVE) {
				// nothing
			}
			else {
				hookerKeyb.last_mouse_click_time.SetToNow();
				KeyHold::ResetWord(); // курсор мог переехать: слово уже не то
				// Щелчок, пока движок решает: курсор уже в другом месте - исправлять придержанное слово нельзя (стёрлось бы
				// не то); нажатия всё равно уйдут, когда движок ответит.
				if (wParam != WM_MOUSEWHEEL && wParam != WM_MOUSEHWHEEL) {
					KeyHold::current = 0;
					KeyHold::caretMoves++; // и печатающееся исправление - бросить
				}
				// И после прокрутки (буфер очищен, а каретка там же): дописанное дальше - кусок слова, как после щелчка.
				Worker()->PostMsg(Message_ClearWorlds{ .click = true });
				// Щелчок или прокрутка двигают каретку. Отпустили левую - курсор могли поставить в поле: в режиме
				// "ненадолго" флажок показывается и от этого (браузеры о фокусе внутри страницы Windows не сообщают).
				CaretFlagPoke(80, wParam == WM_LBUTTONUP);
			}
		}

		return CallNextHookEx(NULL, nCode, wParam, lParam);
	}

public:

	TStatus StartHook() {

		LOG_ANY(L"HookGlobal...");

		hookerKeyb = {}; // очистка.

		hHookKeyGlobal = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc, 0, 0);
		IFW_RET(hHookKeyGlobal.IsValid());
		lastKeyTick = lastMouseTick = GetTickCount();

		if (!Utils::IsDebug()) {
			hHookMouseGlobal = SetWindowsHookEx(WH_MOUSE_LL, LowLevelMouseProc, 0, 0);
			IFW_RET(hHookMouseGlobal.IsValid());
		}

		hHookEventFocus = SetWinEventHook(EVENT_OBJECT_FOCUS, EVENT_OBJECT_FOCUS, NULL, FocusProc, 0, 0,
			WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
		IFW_LOG(hHookEventFocus.IsValid());

		hHookEventGlobal = SetWinEventHook(
			EVENT_SYSTEM_FOREGROUND,
			EVENT_SYSTEM_FOREGROUND,
			NULL,
			WinEventProc,
			0, 0,
			WINEVENT_OUTOFCONTEXT 
			//| WINEVENT_SKIPOWNPROCESS
		);
		IFW_LOG(hHookEventGlobal.IsValid());

		RETURN_SUCCESS;
	}

	// Raw Input клавиатуры - окну потока перехвата (HookerThread.h), даже когда впереди другие: по нему видно, что
	// перехват клавиатуры отключён (Watch).
	void WatchRawInput(HWND window) {
		RAWINPUTDEVICE device{ .usUsagePage = 0x01, .usUsage = 0x06, .dwFlags = RIDEV_INPUTSINK, .hwndTarget = window };
		IFW_LOG(RegisterRawInputDevices(&device, 1, sizeof(device)));
	}
	// WM_INPUT в окне потока перехвата: клавиша дошла до программ (time - когда пришло сообщение: поток мог быть занят).
	static void OnRawKey(DWORD time) {
		lastRawKeyTick = time;
		rawKeyWindow = GetForegroundWindow();
		rawUnseen = (LONG)(time - lastKeyTick) >= 1000 ? rawUnseen + 1 : 0;
		if (!rawSeen) {
			rawSeen = true;
			LOG_ANY("hook: raw input comes, the keyboard hook is watched by it");
		}
	}

	// Windows молча отключает перехват, который не ответил вовремя (LowLevelHooksTimeout): программа перестаёт видеть
	// клавиши, а пока не отключила - каждое нажатие и движение мыши ждёт его. Таймер потока перехвата (HookerThread.h),
	// раз в 2 с; каждый перехват подключаем заново не чаще раза в 30 с.
	//   - Клавиатура: нажатия дошли до программ (Raw Input), а наш перехват больше секунды не видел ни одного (rawUnseen),
	//     в том же окне, что впереди сейчас, - перехват клавиатуры отключён. Подключить заново его: каждый новый перехват
	//     встаёт первым, перед перехватами других программ (AutoHotkey, FluentClipper), - их порядок без нужды не трогаем.
	//     Состояние клавиш - заново: отпускания, пока его не было, мы не видели (иначе клавиша "нажата" до 10 с).
	//     Перехват мыши после этого не звали ни разу - его Windows отключила тогда же (поток стоял, а мышь двигали) или
	//     мышь не трогали: подключить заново и его, сразу. Иначе щелчок в другом месте текста остался бы незамеченным
	//     ещё 30 с, и автопереключение стёрло бы не те буквы.
	//   - Мышь (и клавиатура, пока Raw Input не пришёл ни разу): ввод был (GetLastInputInfo), а перехваты не видели
	//     ничего больше 1,5 с. Когда Raw Input есть, клавиатуру по этому признаку не трогаем: ввод мимо перехвата бывает и
	//     у сенсорной панели, экрана и пера.
	void Watch() {
		const DWORD now = GetTickCount();
		const bool keyboard = rawSeen && rawUnseen >= 4 && (LONG)(lastRawKeyTick - lastKeyTick) >= 1000 &&
			now - lastRawKeyTick < 4000 && rawKeyWindow == GetForegroundWindow() && now - lastRehookKey >= 30000;
		LASTINPUTINFO li{ sizeof(li) };
		const bool unseen = !keyboard && GetLastInputInfo(&li) && (LONG)(li.dwTime - lastMouseTick) >= 1500 &&
			(LONG)(li.dwTime - lastKeyTick) >= 1500 && (!rawSeen || (LONG)(li.dwTime - lastRawKeyTick) >= 1500);
		const bool keyboardUnseen = unseen && !rawSeen && now - lastRehookKey >= 30000;
		const bool mouse = ((keyboard && (LONG)(lastMouseTick - lastKeyTick) <= 0) || unseen) && now - lastRehookMouse >= 30000;
		if (!keyboard && !keyboardUnseen && !mouse) return;
		// Впереди окно от администратора, а мы нет: его ввод Windows нам и не показывает. Или удалённый рабочий стол: его
		// клиент на весь экран ловит клавиатуру своим перехватом, и новый наш встал бы перед ним.
		if (!KeyHold::CanHold()) return;
		if (keyboard || keyboardUnseen) {
			if (keyboard) {
				LOG_WARN("hook: {} keys reached the programs {} ms after the keyboard hook last saw any, hooking it again",
				         rawUnseen, lastRawKeyTick - lastKeyTick);
			}
			else {
				LOG_WARN("hook: input {} ms after the hooks last saw any (no raw input yet), hooking the keyboard again",
				         li.dwTime - lastKeyTick);
			}
			lastRehookKey = now;
			rawUnseen = 0;
			hHookKeyGlobal.Cleanup();
			hHookKeyGlobal = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc, 0, 0);
			IFW_LOG(hHookKeyGlobal.IsValid());
			lastKeyTick = now;
			ClearAllKeys();
			KeyHold::ResetWord();
			if (KeyHold::replaying > 0) { // отправленное, пока перехвата не было, уже не вернётся - не ждать его
				KeyHold::replaying = 0;
				if (!KeyHold::active && !KeyHold::held.empty()) KeyHold::Next();
			}
		}
		if (mouse && !Utils::IsDebug()) {
			LOG_WARN("hook: the mouse hook saw nothing since {} ms ago, hooking the mouse again", now - lastMouseTick);
			lastRehookMouse = now;
			hHookMouseGlobal.Cleanup();
			hHookMouseGlobal = SetWindowsHookEx(WH_MOUSE_LL, LowLevelMouseProc, 0, 0);
			IFW_LOG(hHookMouseGlobal.IsValid());
			lastMouseTick = now;
		}
	}
};
