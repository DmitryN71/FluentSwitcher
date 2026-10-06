#pragma once

#include "FluentMenu.h"

class WinTray {

public:struct TrayItem {
		string name;
		std::function<void()> callback;
		bool is_separator = false;
		bool is_checkbox = false;
		bool edit_val = false;
		wchar_t icon = 0; // значок пункта в меню Windows 11 (знак шрифта Segoe Fluent Icons)
		std::function<bool()> state; // флажок после callback - включён ли (меню Windows 11 не закрывается)
	};
private:

	static const int WM_TRAYICON = WM_USER + 1;
	static const int ID_TRAY_EXIT = 1002;

	inline static WinTray* Inst = 0;

	std::function<void()> double_click;
	std::function<void()> left_click;
	std::function<void()> r_click;
	std::function<void()> balloon_click;
	std::function<void()> timer_func;
	bool after_double = false; // отпускание после двойного щелчка - не новый одиночный
	static const UINT_PTR ID_CLICK_TIMER = 1;
	std::function<std::vector<TrayItem>()> createMenu;
	std::vector<TrayItem> last_menu;

	NOTIFYICONDATA nid {};
	HWND hwnd = 0;

	// Меню у флага - как меню Windows 11 (и FluentClipper): тёмное при тёмной панели задач. Своё меню
	// обычной программы Windows всегда рисует светлым; тёмное включают недокументированные функции
	// uxtheme (порядковые номера 135 SetPreferredAppMode и 136 FlushMenuThemes, Windows 10 1903+),
	// как делают Проводник, wxWidgets, Notepad++. Проверяется при каждом открытии: тема могла смениться.
	static void MenuThemeLikeTaskbar() {
		static DWORD build = [] {
			using RtlGetVersion_t = LONG(WINAPI*)(OSVERSIONINFOW*);
			auto fn = (RtlGetVersion_t)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion");
			OSVERSIONINFOW v{ sizeof(v) };
			return fn && fn(&v) == 0 ? v.dwBuildNumber : 0;
		}();
		if (build < 18362) return;
		static HMODULE uxtheme = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
		if (!uxtheme) return;
		using SetPreferredAppMode_t = int(WINAPI*)(int);
		using FlushMenuThemes_t = void(WINAPI*)();
		static auto setMode = (SetPreferredAppMode_t)GetProcAddress(uxtheme, MAKEINTRESOURCEA(135));
		static auto flush = (FlushMenuThemes_t)GetProcAddress(uxtheme, MAKEINTRESOURCEA(136));
		if (!setMode || !flush) return;
		DWORD light = 1, size = sizeof(light);
		RegGetValueW(HKEY_CURRENT_USER, LR"(Software\Microsoft\Windows\CurrentVersion\Themes\Personalize)",
			L"SystemUsesLightTheme", RRF_RT_REG_DWORD, nullptr, &light, &size);
		setMode(light ? 3 : 2); // ForceLight : ForceDark
		flush();
	}

	static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
		switch (uMsg) {
		case WM_TRAYICON: {
			if (lParam == WM_RBUTTONUP) {
				if (Inst->r_click) {
					Inst->r_click();
				}
			}
			else if (lParam == WM_RBUTTONDOWN) {
				Inst->ShowMenu();
			}
			else if (lParam == WM_LBUTTONUP) {
				if (Inst->after_double) {
					Inst->after_double = false;
				}
				else if (Inst->left_click) {
					Inst->left_click();
				}
			}
			else if (lParam == WM_LBUTTONDBLCLK) {
				Inst->after_double = true;
				if (Inst->double_click) {
					Inst->double_click();
				}
			}
			else if (lParam == NIN_BALLOONUSERCLICK) {
				if (Inst->balloon_click) {
					Inst->balloon_click();
				}
			}
			break;
		}


		case WM_TIMER: {
			if (wParam == ID_CLICK_TIMER) {
				KillTimer(hwnd, ID_CLICK_TIMER);
				auto func = std::move(Inst->timer_func);
				Inst->timer_func = nullptr;
				if (func) func();
			}
			break;
		}

		case WM_COMMAND: {
			for (int i = 0; auto & it : Inst->last_menu) {
				i++;
				if (i == LOWORD(wParam)) {
					if(it.callback)
						it.callback();
					break;
				}
			}
			break;
		}

		default:
			return DefWindowProc(hwnd, uMsg, wParam, lParam);
		}
		return 0;
	}
public:
	HWND GetHandler() { return hwnd; }
	void OnCreateMenu(auto&& func) {
		createMenu = func;
	}
	void OnDouble(auto&& func) {
		double_click = std::move(func);
	}
	// Одиночный щелчок левой (на отпускание; отпускание после двойного щелчка - не он).
	void OnLeftClick(auto&& func) {
		left_click = std::move(func);
	}
	// Отложенное действие на окне значка: одиночный щелчок ждёт, не будет ли второго.
	void StartTimer(UINT ms, std::function<void()> func) {
		timer_func = std::move(func);
		SetTimer(hwnd, ID_CLICK_TIMER, ms, nullptr);
	}
	void StopTimer() {
		KillTimer(hwnd, ID_CLICK_TIMER);
		timer_func = nullptr;
	}
	// Где открыть меню: у самого значка - над панелью задач (под ней, если она сверху; сбоку - рядом), а не у курсора.
	// По щелчку меню открывается с задержкой (ждёт, не будет ли второго щелчка), и мышь уже на пути к пункту - меню
	// уезжало за ней; у значка пункты всегда на одном месте, в них попадают по памяти. Значок не нашёлся - у курсора.
	POINT MenuPoint() const {
		POINT p{};
		GetCursorPos(&p);
		NOTIFYICONIDENTIFIER id{ sizeof(id) };
		id.hWnd = nid.hWnd;
		id.uID = nid.uID;
		RECT r{};
		if (FAILED(Shell_NotifyIconGetRect(&id, &r)) || r.right <= r.left) return p;
		MONITORINFO mi{ sizeof(mi) };
		GetMonitorInfoW(MonitorFromRect(&r, MONITOR_DEFAULTTONEAREST), &mi);
		const RECT& work = mi.rcWork;
		if (r.bottom <= work.top + 1) return { r.left, r.bottom }; // панель задач сверху
		if (r.right <= work.left + 1) return { r.right, r.bottom }; // слева
		if (r.left >= work.right - 1) return { r.left, r.bottom };  // справа
		return { r.left, r.top }; // снизу; или значок среди скрытых - над ним
	}
	// Меню у флага (правый щелчок; по настройке - и левый).
	void ShowMenu() {
		if (!createMenu) return;
		last_menu = createMenu();
		POINT cursorPos = MenuPoint();

		// Меню в стиле Windows 11 (FluentMenu.h); не вышло - обычное меню Windows, как раньше.
		std::vector<FluentMenu::Item> items;
		for (auto& it : last_menu) {
			items.push_back({ .text = StrUtils::Convert(it.name), .icon = it.icon, .separator = it.is_separator,
			                  .toggle = it.is_checkbox, .on = it.edit_val, .action = it.callback, .state = it.state });
		}
		std::wstring title = L"FluentSwitcher ";
		for (const char* p = details::SW_VERSION; *p; ++p) title += (wchar_t)*p;
		if (FluentMenu::Show(std::move(title), std::move(items), cursorPos)) return;

		HMENU hMenu = CreatePopupMenu();
		for (int i = 0; auto & it : last_menu) {
			i++;
			if (it.is_separator) {
				AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
				continue;
			}
			if (it.is_checkbox) {
				AppendMenu(hMenu, MF_STRING | (it.edit_val ? MF_CHECKED : MF_UNCHECKED), i, StrUtils::Convert(it.name).c_str());
				continue;
			}
			AppendMenu(hMenu, MF_STRING, i, StrUtils::Convert(it.name).c_str());
		}

		MenuThemeLikeTaskbar();
		SetForegroundWindow(hwnd);
		TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN, cursorPos.x, cursorPos.y, 0, hwnd, NULL);
		DestroyMenu(hMenu);
	}
	void OnRight(auto&& func) {
		r_click = std::move(func);
	}
	// Щелчок по уведомлению (ShowBalloon).
	void OnBalloonClick(auto&& func) {
		balloon_click = std::move(func);
	}

	WinTray() {
		Inst = this;
		hwnd = WinUtils::CreateMsgWin(L"SimpleSwitcher_Tray_001", WindowProc);
		//hwnd = CreateWindowEx(0, CLASS_NAME, NULL, 0, 0, 0, 0, 0, 0, 0, 0, 0);
		//UpdateWindow(hwnd);
		nid.cbSize = sizeof(NOTIFYICONDATA);
		nid.hWnd = hwnd;
		//nid.uID = ID_TRAY_APP_ICON;
		nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
		nid.uCallbackMessage = WM_TRAYICON;
		swprintf_s(nid.szTip, L"FluentSwitcher %S", details::SW_VERSION);


	}
	~WinTray() {
		DeleteIcon();
	}

	void SetIcon(HICON icon) {
		//nid.hIcon = LoadIcon(NULL, IDI_APPLICATION); // Load your icon here
		nid.hIcon = icon; // Load your icon here
		if (!Shell_NotifyIcon(NIM_MODIFY, &nid)) {
			if (!Shell_NotifyIcon(NIM_ADD, &nid)) {
				LOG_WARN("Can't modify or add icon");
				// todo - by timer
			}
		}
	}
	// Уведомление у значка (в Windows 10/11 - всплывающее сообщение и Центр уведомлений). Значка нет - нет и его.
	bool ShowBalloon(const std::wstring& title, const std::wstring& text) {
		NOTIFYICONDATA balloon = nid;
		balloon.uFlags = NIF_INFO;
		wcsncpy_s(balloon.szInfoTitle, title.c_str(), _TRUNCATE);
		wcsncpy_s(balloon.szInfo, text.c_str(), _TRUNCATE);
		balloon.dwInfoFlags = NIIF_INFO | NIIF_RESPECT_QUIET_TIME;
		return Shell_NotifyIcon(NIM_MODIFY, &balloon) != FALSE;
	}
	void DeleteIcon() {
		Shell_NotifyIcon(NIM_DELETE, &nid); // Remove icon from tray
	}

};
