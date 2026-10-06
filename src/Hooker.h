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

	// Последний вызов перехвата клавиатуры или мыши (GetTickCount) - по нему видно, что Windows его отключила (Watch).
	inline static DWORD lastHookTick = 0;
	inline static DWORD lastRehook = 0;

	static LRESULT CALLBACK LowLevelKeyboardProc(
		_In_  int nCode,
		_In_  WPARAM wParam,
		_In_  LPARAM lParam
	) {
		lastHookTick = GetTickCount();
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
		KeyHold::ResetWord();
	}

	static LRESULT CALLBACK LowLevelMouseProc(
		_In_  int nCode,
		_In_  WPARAM wParam,
		_In_  LPARAM lParam
	) {
		lastHookTick = GetTickCount();
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
				Worker()->PostMsg(Message_ClearWorlds{ .click = wParam != WM_MOUSEWHEEL && wParam != WM_MOUSEHWHEEL });
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
		lastHookTick = GetTickCount();

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

	// Windows молча отключает перехват, который не ответил вовремя (LowLevelHooksTimeout): программа перестаёт видеть
	// клавиши, а пока не отключила - каждое нажатие и движение мыши ждёт его. Ввод был, а перехват не видел ничего
	// больше 1,5 с - подключиться заново (не чаще раза в 30 с: ввод мимо перехвата бывает у сенсорного экрана и пера).
	// Таймер потока перехвата (HookerThread.h), раз в 2 с.
	void Watch() {
		LASTINPUTINFO li{ sizeof(li) };
		if (!GetLastInputInfo(&li)) return;
		const DWORD now = GetTickCount();
		if ((LONG)(li.dwTime - lastHookTick) < 1500 || now - lastRehook < 30000) return;
		if (!KeyHold::CanHold()) return; // впереди окно от администратора, а мы нет: его ввод Windows нам и не показывает
		LOG_WARN("hook: input {} ms after the hooks last saw any, hooking again", li.dwTime - lastHookTick);
		lastRehook = now;
		hHookKeyGlobal.Cleanup();
		hHookKeyGlobal = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc, 0, 0);
		IFW_LOG(hHookKeyGlobal.IsValid());
		if (!Utils::IsDebug()) {
			hHookMouseGlobal.Cleanup();
			hHookMouseGlobal = SetWindowsHookEx(WH_MOUSE_LL, LowLevelMouseProc, 0, 0);
			IFW_LOG(hHookMouseGlobal.IsValid());
		}
		lastHookTick = now;
	}
};
